/*
 * CncSettings.cpp
 */

#include "CncSettings.h"
#include "CncPopup.h"
#include "CncState.h"
#include "Subscribers/Subscribers.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fmt/format.h>
#include <sstream>
#include <utility>

namespace UI::Cnc::Settings
{
	namespace
	{
		const char* const Names[NumGlobals] = {"cncProbeMode",
											   "cncMeasureAfterChange",
											   "cncRememberTool",
											   "cncToolSetter",
											   "cncAuxVacuum",
											   "cncJogFeed",
											   "cncCustom1Label",
											   "cncCustom1Macro",
											   "cncCustom2Label",
											   "cncCustom2Macro",
											   "cncCustom3Label",
											   "cncCustom3Macro",
											   "cncIdleTimeout",
											   "cncOverrides",
											   "cncJogPresets",
											   "cncProbeValues",
											   "cncJogIcons",
											   "cncJogDegree"};
		// The object model keys ("global:cncProbeMode")
		std::array<std::string, NumGlobals> s_keys;

		const char* const HeaderLines[] = {
			"; CNC panel settings (SYSTEM > SETTINGS), loaded by config.g at power-on: M98 P\"0:/sys/cnc-settings.g\"",
			"; Written by the panel on every change.",
		};
		constexpr size_t NumHeaderLines = std::size(HeaderLines);
		constexpr size_t LinesPerGlobal = 3; // if !exists(...) / global ... / set global...

		Values s_values;
		Values s_incoming;
		uint32_t s_present = 0, s_incomingPresent = 0;
		bool s_receiving = false;
		uint32_t s_lastValueTime = 0;
		bool s_known = false;
		bool s_verify = false;
		bool s_writing = false;
		size_t s_writeLine = 0;
		uint32_t s_lastWriteTime = 0, s_writeDoneTime = 0;
		bool s_ignoring = false;
		bool s_probePending = false;
		std::vector<Listener> s_listeners;
		bool s_inited = false;

		bool connected()
		{
			const OM::PrinterStatus st = State::Status();
			return st != OM::PrinterStatus::connecting && st != OM::PrinterStatus::disconnected;
		}

		// The machine reads the panel's lines now (not busy with something its aux input waits for)
		bool machineListening()
		{
			const OM::PrinterStatus st = State::Status();
			return st == OM::PrinterStatus::idle || st == OM::PrinterStatus::processing ||
				   st == OM::PrinterStatus::paused || st == OM::PrinterStatus::off;
		}

		bool parseBool(const std::string& s, bool& b)
		{
			std::string l = s;
			std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			if (l == "true")
			{
				b = true;
				return true;
			}
			if (l == "false")
			{
				b = false;
				return true;
			}
			return false;
		}

		std::vector<float> parseNumbers(const std::string& s)
		{
			std::vector<float> out;
			std::stringstream ss(s);
			std::string item;
			while (std::getline(ss, item, ','))
			{
				char* end = nullptr;
				const float f = std::strtof(item.c_str(), &end);
				if (end == item.c_str())
				{
					return {}; // not a number: ignore the whole value
				}
				out.push_back(f);
			}
			return out;
		}

		std::string number(float f)
		{
			std::string s = fmt::format("{:.3f}", f);
			while (!s.empty() && s.back() == '0')
			{
				s.pop_back();
			}
			if (!s.empty() && s.back() == '.')
			{
				s.pop_back();
			}
			return s;
		}

		char pairCode(IconPair p)
		{
			switch (p)
			{
			case IconPair::LeftRight:
				return 'H';
			case IconPair::DownUp:
				return 'V';
			case IconPair::Diagonal:
				return 'D';
			case IconPair::Rotary:
				return 'R';
			default:
				return 'N';
			}
		}

		bool pairFromCode(char c, IconPair& p)
		{
			switch (std::toupper(static_cast<unsigned char>(c)))
			{
			case 'H':
				p = IconPair::LeftRight;
				return true;
			case 'V':
				p = IconPair::DownUp;
				return true;
			case 'D':
				p = IconPair::Diagonal;
				return true;
			case 'R':
				p = IconPair::Rotary;
				return true;
			case 'N':
				p = IconPair::None;
				return true;
			default:
				return false;
			}
		}

		void notify()
		{
			const auto listeners = s_listeners; // a listener may add another
			for (const Listener& l : listeners)
			{
				if (l)
				{
					l();
				}
			}
		}

		// Push the values to the shared state the pages use
		void apply()
		{
			State::SetProbeMode(static_cast<State::ProbeMode>(std::min<uint8_t>(s_values.probeMode, 2)));
			State::SetToolSetter(s_values.toolSetter);
			State::SetJogFeed(s_values.jogFeed);
			if (std::string(State::AuxLabel()) != (s_values.auxVacuum ? "VACUUM" : "COOLANT"))
			{
				State::SetAuxLabel(s_values.auxVacuum);
			}
			for (size_t i = 0; i < NumCustom; ++i)
			{
				const bool used = !s_values.labels[i].empty() && !s_values.macros[i].empty();
				if (used != State::CustomUsed(i) || (used && (s_values.labels[i] != State::CustomLabel(i) ||
															  s_values.macros[i] != State::CustomMacro(i))))
				{
					State::SetCustomMacro(
						i, used ? s_values.labels[i].c_str() : "", used ? s_values.macros[i].c_str() : "");
				}
			}
			notify();
		}

		// Set the changed globals on the machine now; a global it does not have yet is created
		void sendLive(uint32_t changed)
		{
			for (size_t g = 0; g < NumGlobals; ++g)
			{
				if ((changed & (1u << g)) == 0)
				{
					continue;
				}
				const std::string value = FormatValue((Global)g, s_values);
				if ((s_present & (1u << g)) != 0)
				{
					State::Send(fmt::format("set global.{:s} = {:s}\n", Names[g], value));
				}
				else
				{
					State::Send(fmt::format("global {:s} = {:s}\n", Names[g], value));
					s_present |= (1u << g); // so the next change sets it
				}
			}
		}

		void startWrite(uint32_t changed)
		{
			apply();
			sendLive(changed);
			s_verify = false; // checked again after this write
			s_writing = true;
			s_writeLine = 0;
		}

		std::string fileLine(size_t n)
		{
			if (n < NumHeaderLines)
			{
				return HeaderLines[n];
			}
			const size_t k = n - NumHeaderLines;
			const size_t g = k / LinesPerGlobal;
			const std::string value = FormatValue((Global)g, s_values);
			switch (k % LinesPerGlobal)
			{
			case 0:
				return fmt::format("if !exists(global.{:s})", Names[g]);
			case 1:
				return fmt::format("  global {:s} = {:s}", Names[g], value);
			default:
				return fmt::format("set global.{:s} = {:s}", Names[g], value);
			}
		}

		// echo >"file" "<line>" (quotes in the line doubled)
		void sendFileLine(size_t n)
		{
			std::string line;
			for (char c : fileLine(n))
			{
				if (c == '"')
				{
					line += '"';
				}
				line += c;
			}
			State::Send(fmt::format("echo {:s}\"{:s}\" \"{:s}\"\n", (n == 0) ? ">" : ">>", TempFile, line));
		}

		void finishIncoming()
		{
			s_receiving = false;
			s_known = true;
			if (s_writing || s_ignoring)
			{
				return; // ours are newer
			}
			if (s_verify)
			{
				s_verify = false;
				if (!(s_incoming == s_values))
				{
					// What we set is not what the machine has: it refused a "set global" (see SYSTEM > ALERT)
					Popup::Refuse("The machine did not keep\nthe setting. See SYSTEM > ALERT.");
				}
			}
			const bool probeKept = s_probePending;
			const Probe localProbe = s_values.probe;
			s_values = s_incoming;
			s_present = s_incomingPresent;
			apply();
			if (probeKept)
			{
				s_probePending = false;
				SetProbe(localProbe); // changed on WCS > PROBE before the values were known
			}
		}

		// Subscribers run on the receive thread; Tick() and the pages on the UI thread: handled there, in order
		template <size_t I>
		bool onGlobal(Comm::JsonDecoder*, const char* data, const size_t[])
		{
			State::RunOnUi([value = std::string(data != nullptr ? data : "")] { Value((Global)I, value); });
			return true;
		}

		template <size_t... I>
		void registerGlobals(std::index_sequence<I...>)
		{
			(SubscriberMap::addSubscriber(s_keys[I].c_str(), &onGlobal<I>), ...);
		}

		bool onKey(Comm::JsonDecoder*, const char* data, const size_t[])
		{
			State::RunOnUi(
				[key = std::string(data != nullptr ? data : "")]
				{
					if (key == "global")
					{
						Begin();
					}
					else if (s_receiving)
					{
						finishIncoming(); // another response: the global one is complete
					}
				});
			return true;
		}
	} // namespace

	const char* GlobalName(Global g)
	{
		return (g < Global::Count) ? Names[(size_t)g] : "";
	}

	const Values& Get()
	{
		return s_values;
	}

	bool Known()
	{
		return s_known;
	}

	bool Writing()
	{
		return s_writing;
	}

	AxisIcons IconsFor(char letter)
	{
		const char l = (char)std::toupper(static_cast<unsigned char>(letter));
		for (const AxisIcons& a : s_values.jogIcons)
		{
			if (a.letter == l)
			{
				return a;
			}
		}
		return {l, (l == 'A' || l == 'B' || l == 'C') ? IconPair::Rotary : IconPair::None, true};
	}

	MoveLook MoveLookFor(char letter)
	{
		const AxisIcons a = IconsFor(letter);
		MoveLook m;
		switch (a.pair)
		{
		case IconPair::LeftRight:
			m.left = JogIcon::Left;
			m.right = JogIcon::Right;
			break;
		case IconPair::DownUp:
			m.left = JogIcon::Down;
			m.right = JogIcon::Up;
			break;
		case IconPair::Diagonal:
			m.left = JogIcon::DownLeft;
			m.right = JogIcon::UpRight;
			break;
		case IconPair::Rotary:
			m.left = JogIcon::Ccw;
			m.right = JogIcon::Cw;
			break;
		default:
			break;
		}
		m.leftSign = a.plusRight ? -1 : 1;
		m.degree = (a.pair == IconPair::Rotary) && s_values.jogDegree;
		return m;
	}

	std::string MoveText(int sign, const char* step, bool degree)
	{
		return fmt::format("{:s} {:s}{:s}", (sign < 0) ? "-" : "+", step, degree ? "\xC2\xB0" : "");
	}

	std::string FormatValue(Global g, const Values& v)
	{
		auto quoted = [](const std::string& s)
		{
			std::string out = "\"";
			for (char c : s)
			{
				if (c == '"')
				{
					out += '"'; // a quote in a string is doubled
				}
				out += c;
			}
			return out + "\"";
		};
		auto boolean = [](bool b) { return std::string(b ? "true" : "false"); };
		switch (g)
		{
		case Global::ProbeMode:
			return fmt::format("{:d}", v.probeMode);
		case Global::MeasureAfterChange:
			return boolean(v.measureAfterChange);
		case Global::RememberTool:
			return boolean(v.rememberTool);
		case Global::ToolSetter:
			return boolean(v.toolSetter);
		case Global::AuxVacuum:
			return boolean(v.auxVacuum);
		case Global::JogFeed:
			return fmt::format("{:d}", v.jogFeed);
		case Global::IdleTimeout:
			return fmt::format("{:d}", v.idleTimeout);
		case Global::Overrides:
			return quoted(fmt::format("{:d},{:d},{:d},{:d}", v.feedMin, v.feedMax, v.spindleMin, v.spindleMax));
		case Global::JogPresets:
		{
			std::string s;
			for (unsigned int p : v.jogPresets)
			{
				s += (s.empty() ? "" : ",") + fmt::format("{:d}", p);
			}
			return quoted(s);
		}
		case Global::ProbeValues:
		{
			const Probe& p = v.probe;
			return quoted(fmt::format("{},{},{},{},{},{},{},{},{},{:d}",
									  number(p.tip),
									  number(p.autoClear),
									  number(p.autoSearch),
									  number(p.zDepth),
									  number(p.stockX),
									  number(p.stockY),
									  number(p.semiClear),
									  number(p.semiSearch),
									  number(p.toolDia),
									  p.storeTo));
		}
		case Global::JogIcons:
		{
			std::string s;
			for (const AxisIcons& a : v.jogIcons)
			{
				s += a.letter;
				s += pairCode(a.pair);
				s += a.plusRight ? '+' : '-';
			}
			return quoted(s);
		}
		case Global::JogDegree:
			return boolean(v.jogDegree);
		default:
		{
			const size_t k = (size_t)g - (size_t)Global::Custom1Label;
			const size_t slot = k / 2;
			return quoted((k % 2 == 0) ? v.labels[slot] : v.macros[slot]);
		}
		}
	}

	std::vector<std::string> FileLines()
	{
		std::vector<std::string> lines;
		for (size_t n = 0; n < NumHeaderLines + NumGlobals * LinesPerGlobal; ++n)
		{
			lines.push_back(fileLine(n));
		}
		return lines;
	}

	// ---- Changes -------------------------------------------------------------------------------

	bool Edit(const std::function<void(Values&)>& fn, uint32_t changed)
	{
		if (!connected())
		{
			Popup::Refuse("No connection to the machine.");
			return false;
		}
		if (!s_known)
		{
			Popup::Refuse("Waiting for the machine settings."); // a change now would overwrite them with defaults
			return false;
		}
		Values v = s_values;
		fn(v);
		s_values = v;
		startWrite(changed);
		return true;
	}

	void SetProbe(const Probe& probe)
	{
		if (probe == s_values.probe)
		{
			return;
		}
		s_values.probe = probe;
		if (!connected() || !s_known)
		{
			s_probePending = true; // saved once the machine's values are known
			return;
		}
		startWrite(Bit(Global::ProbeValues));
	}

	void FactoryReset()
	{
		Edit([](Values& v) { v = Values(); }, (1u << NumGlobals) - 1);
	}

	void ChooseJogFeed(std::function<void()> afterChoice)
	{
		const auto presets = s_values.jogPresets;
		std::vector<std::string> labels;
		size_t nearest = 0;
		const auto dist = [](unsigned int a, unsigned int b) { return (a > b) ? a - b : b - a; };
		for (size_t i = 0; i < presets.size(); ++i)
		{
			labels.push_back(fmt::format("{:d} mm/min", presets[i]));
			if (dist(presets[i], s_values.jogFeed) < dist(presets[nearest], s_values.jogFeed))
			{
				nearest = i;
			}
		}
		Popup::Choose(
			"SLOW JOG FEEDRATE",
			labels,
			nearest,
			[presets, afterChoice](size_t choice)
			{
				if (choice < presets.size() &&
					Edit([&](Values& v) { v.jogFeed = presets[choice]; }, Bit(Global::JogFeed)) && afterChoice)
				{
					afterChoice();
				}
			},
			2);
	}

	// ---- Machine data --------------------------------------------------------------------------

	void Init()
	{
		if (s_inited)
		{
			return;
		}
		s_inited = true;
		for (size_t g = 0; g < NumGlobals; ++g)
		{
			s_keys[g] = std::string("global:") + Names[g];
		}
		registerGlobals(std::make_index_sequence<NumGlobals>{});
		SubscriberMap::addSubscriber("key", &onKey);
	}

	void Begin()
	{
		s_incoming = Values(); // a global the machine does not have means its default
		s_incomingPresent = 0;
		s_receiving = true;
		s_lastValueTime = State::Now();
	}

	void Value(Global g, const std::string& data)
	{
		if (!s_receiving || g >= Global::Count)
		{
			return;
		}
		s_lastValueTime = State::Now();
		s_incomingPresent |= Bit(g);
		Values& v = s_incoming;
		bool b = false;
		switch (g)
		{
		case Global::ProbeMode:
		{
			const int m = std::atoi(data.c_str());
			if (m >= 0 && m <= 2)
			{
				v.probeMode = (uint8_t)m;
			}
			break;
		}
		case Global::MeasureAfterChange:
			if (parseBool(data, b))
			{
				v.measureAfterChange = b;
			}
			break;
		case Global::RememberTool:
			if (parseBool(data, b))
			{
				v.rememberTool = b;
			}
			break;
		case Global::ToolSetter:
			if (parseBool(data, b))
			{
				v.toolSetter = b;
			}
			break;
		case Global::AuxVacuum:
			if (parseBool(data, b))
			{
				v.auxVacuum = b;
			}
			break;
		case Global::JogFeed:
		{
			const long f = std::atol(data.c_str());
			if (f >= 1 && f <= (long)MaxJogFeed)
			{
				v.jogFeed = (unsigned int)f;
			}
			break;
		}
		case Global::IdleTimeout:
		{
			const long t = std::atol(data.c_str());
			if (t >= 0 && t <= 3600)
			{
				v.idleTimeout = (unsigned int)t;
			}
			break;
		}
		case Global::Overrides:
		{
			const auto n = parseNumbers(data);
			if (n.size() == 4 && n[0] >= 1 && n[0] <= 100 && n[1] >= 100 && n[1] <= 500 && n[2] >= 1 && n[2] <= 100 &&
				n[3] >= 100 && n[3] <= 300)
			{
				v.feedMin = (int)n[0];
				v.feedMax = (int)n[1];
				v.spindleMin = (int)n[2];
				v.spindleMax = (int)n[3];
			}
			break;
		}
		case Global::JogPresets:
		{
			const auto n = parseNumbers(data);
			if (n.size() == NumJogPresets &&
				std::all_of(n.begin(), n.end(), [](float f) { return f >= 1 && f <= (float)MaxJogFeed; }))
			{
				for (size_t i = 0; i < NumJogPresets; ++i)
				{
					v.jogPresets[i] = (unsigned int)n[i];
				}
			}
			break;
		}
		case Global::ProbeValues:
		{
			const auto n = parseNumbers(data);
			if (n.size() == 10 && std::all_of(n.begin(), n.end(), [](float f) { return f >= 0 && f < 10000; }) &&
				n[9] < 6)
			{
				v.probe = {n[0], n[1], n[2], n[3], n[4], n[5], n[6], n[7], n[8], (uint8_t)n[9]};
			}
			break;
		}
		case Global::JogIcons:
		{
			std::vector<AxisIcons> icons;
			bool ok = (data.size() % 3) == 0 && !data.empty();
			for (size_t i = 0; ok && i < data.size(); i += 3)
			{
				AxisIcons a{(char)std::toupper(static_cast<unsigned char>(data[i])), IconPair::None, true};
				ok = std::isalpha(static_cast<unsigned char>(data[i])) && pairFromCode(data[i + 1], a.pair) &&
					 (data[i + 2] == '+' || data[i + 2] == '-');
				a.plusRight = data[i + 2] == '+';
				icons.push_back(a);
			}
			if (ok)
			{
				v.jogIcons = icons;
			}
			break;
		}
		case Global::JogDegree:
			if (parseBool(data, b))
			{
				v.jogDegree = b;
			}
			break;
		default:
		{
			const size_t k = (size_t)g - (size_t)Global::Custom1Label;
			const size_t slot = k / 2;
			((k % 2 == 0) ? v.labels[slot] : v.macros[slot]) = data;
			break;
		}
		}
	}

	void Tick()
	{
		const uint32_t now = State::Now();
		if (!connected())
		{
			s_known = false; // ask again after (re)connecting: maybe another machine
			s_receiving = false;
		}
		if (s_receiving && now - s_lastValueTime >= QuietTime)
		{
			finishIncoming();
		}
		if (s_ignoring && !s_writing && now - s_writeDoneTime >= IgnoreAfterWrite)
		{
			s_ignoring = false;
			s_verify = true; // the machine must now report what we set
		}
		if (!s_writing || now - s_lastWriteTime < WriteInterval)
		{
			return;
		}
		if (!connected() || !s_known)
		{
			// Connection lost during the write: drop it. After reconnecting the machine's values are
			// shown again (they may have been changed in DWC, or it may be another machine).
			s_writing = false;
			s_verify = false;
			return;
		}
		if (!machineListening())
		{
			return; // its aux input would fill up: wait
		}
		s_lastWriteTime = now;
		if (s_writeLine < NumHeaderLines + NumGlobals * LinesPerGlobal)
		{
			sendFileLine(s_writeLine++);
		}
		else
		{
			// Complete: replace the real file (a write cut short never touches it). The file is not run:
			// the machine already has the values, and config.g runs the file at power-on.
			State::Send(fmt::format("M471 S\"{:s}\" T\"{:s}\" D1\n", TempFile, SettingsFile));
			s_writing = false;
			s_writeDoneTime = now;
			s_ignoring = true;
		}
	}

	void AddListener(Listener l)
	{
		s_listeners.push_back(std::move(l));
	}

	void Reset()
	{
		s_values = Values();
		s_incoming = Values();
		s_present = s_incomingPresent = 0;
		s_receiving = s_known = s_verify = s_writing = s_ignoring = s_probePending = false;
		s_writeLine = 0;
		s_lastValueTime = s_lastWriteTime = s_writeDoneTime = 0;
		s_listeners.clear();
	}
} // namespace UI::Cnc::Settings
