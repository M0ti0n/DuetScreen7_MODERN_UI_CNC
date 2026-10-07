/*
 * CncSettings.h
 *
 * The machine settings of the CNC UI (SYSTEM > SETTINGS pages 2 and 3, and the saved PROBE values),
 * ported from the PanelDue CNC UI (CncSettingsPage.cpp). They are globals on the machine, kept in
 * 0:/sys/cnc-settings.g, which config.g runs at power-on:
 *
 *   global.cncProbeMode            0 / 1 / 2                 AUTO / SEMI-AUTO / MANUAL
 *   global.cncMeasureAfterChange   true / false              ER COLLET / TOOL HOLDER
 *   global.cncRememberTool         true / false
 *   global.cncToolSetter           true / false
 *   global.cncAuxVacuum            true / false              COOLANT / VACUUM
 *   global.cncJogFeed              mm/min                    SLOW jog feed
 *   global.cncCustom<n>Label/Macro strings                   CUSTOM 1 (CONTROL), 2 and 3 (JOB STATUS)
 * New with DuetScreen:
 *   global.cncIdleTimeout          s, 0 = never              CONTROL: axis and step back to default
 *   global.cncOverrides            "10,200,50,150"           JOB STATUS: feed min,max, spindle min,max (%)
 *   global.cncJogPresets           "200,300,500,700,1000,1200"   SLOW JOG FEEDRATE choices (mm/min)
 *   global.cncProbeValues          "2,5,10,3,100,60,2,5,6,0" WCS > PROBE: tip, auto clearance, auto search,
 *                                                            Z depth, stock X, stock Y, semi clearance,
 *                                                            semi search, tool dia, STORE TO (0 = G54)
 *   global.cncJogIcons             "XH+YD+ZV+AR+"            JOG DIRECTION: per axis letter the icon pair
 *                                                            (H left/right, V down/up, D 225/45, R CCW/CW,
 *                                                            N none) and where + is (+ right/up, - left/down)
 *   global.cncJogDegree            true / false              "°" on the step buttons of a CCW/CW axis
 *
 * As on PanelDue: a change is set on the machine at once ("set global.x = v", or "global x = v" when the
 * machine does not have it yet), then the whole file is rewritten with echo, one line every 100 ms so
 * the machine's input never overflows, to a temporary file that is renamed over the old one when
 * complete (M471). Values from the machine are ignored while writing and for 3 s after; the next
 * values must then be the ones written, else an ALERT says the machine did not keep them.
 * Difference: each global is guarded in the file (if !exists(...) global ...), so config.g does not
 * have to declare them.
 */
#pragma once

#include "CncGlyphs.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UI::Cnc::Settings
{
	enum class Global : uint8_t
	{
		ProbeMode,
		MeasureAfterChange,
		RememberTool,
		ToolSetter,
		AuxVacuum,
		JogFeed,
		Custom1Label,
		Custom1Macro,
		Custom2Label,
		Custom2Macro,
		Custom3Label,
		Custom3Macro,
		IdleTimeout,
		Overrides,
		JogPresets,
		ProbeValues,
		JogIcons,
		JogDegree,
		Count
	};
	constexpr size_t NumGlobals = (size_t)Global::Count;
	constexpr uint32_t Bit(Global g) { return 1u << (unsigned)g; }
	constexpr uint32_t SlotBits(size_t slot) { return 3u << ((unsigned)Global::Custom1Label + 2 * (unsigned)slot); }
	const char* GlobalName(Global g);

	constexpr size_t NumCustom = 3;
	constexpr size_t MaxLabel = 12;
	constexpr size_t MaxMacroPath = 63;
	constexpr size_t NumJogPresets = 6;
	constexpr unsigned int MaxJogFeed = 20000;

	enum class IconPair : uint8_t
	{
		None,
		LeftRight,
		DownUp,
		Diagonal,		// 225 / 45 degrees
		Rotary			// CCW / CW
	};
	struct AxisIcons
	{
		char letter;
		IconPair pair;
		bool plusRight;	// + on the right / up arrow (false: + gets the left / down arrow)
		bool operator==(const AxisIcons&) const = default;
	};

	struct Probe
	{
		float tip = 2.0f;
		float autoClear = 5.0f, autoSearch = 10.0f, zDepth = 3.0f;
		float stockX = 100.0f, stockY = 60.0f;
		float semiClear = 2.0f, semiSearch = 5.0f;
		float toolDia = 6.0f;
		uint8_t storeTo = 0;		// 0 = G54
		bool operator==(const Probe&) const = default;
	};

	struct Values
	{
		uint8_t probeMode = 0;
		bool measureAfterChange = true;
		bool rememberTool = false;
		bool toolSetter = true;
		bool auxVacuum = false;
		unsigned int jogFeed = 300;
		std::string labels[NumCustom], macros[NumCustom];
		unsigned int idleTimeout = 5;	// s, 0 = never
		int feedMin = 10, feedMax = 200, spindleMin = 50, spindleMax = 150;
		std::array<unsigned int, NumJogPresets> jogPresets{200, 300, 500, 700, 1000, 1200};
		Probe probe;
		std::vector<AxisIcons> jogIcons{{'X', IconPair::LeftRight, true},
										{'Y', IconPair::Diagonal, true},
										{'Z', IconPair::DownUp, true},
										{'A', IconPair::Rotary, true}};
		bool jogDegree = true;
		bool operator==(const Values&) const = default;
	};

	const Values& Get();
	bool Known();					// the machine's values have arrived since connecting
	bool Writing();					// the file is being written
	AxisIcons IconsFor(char letter);	// default for a letter not in the list (A/B/C: CCW/CW, else none)

	// The MOVE buttons of an axis (CONTROL, jog prompt): the left button has the left / down / 225 / CCW
	// arrow, the right one the other; + sits on the side its arrow moves to (as set on JOG DIRECTION).
	struct MoveLook
	{
		JogIcon left = JogIcon::None, right = JogIcon::None;
		int leftSign = -1;			// coordinate sign of the left button (+1 when + moves left / down)
		bool degree = false;		// "°" after the step (CCW / CW axis, JOG DIRECTION "°" on)
	};
	MoveLook MoveLookFor(char letter);
	// "- 10", "+ 0.1°": the text of a MOVE button
	std::string MoveText(int sign, const char* step, bool degree);

	// Change settings: fn edits a copy, 'changed' = the globals it touched. Refused (ALERT) when not
	// connected or the machine's values are not known yet; returns false then.
	bool Edit(const std::function<void(Values&)>& fn, uint32_t changed);
	// WCS > PROBE: saved like the others, but never refused (not saved until the values are known)
	void SetProbe(const Probe& probe);
	// FACTORY RESET: all machine settings back to the defaults (and saved)
	void FactoryReset();
	// SLOW JOG FEEDRATE (SETTINGS and the F tile on CONTROL): the presets, nearest one selected
	void ChooseJogFeed(std::function<void()> afterChoice);

	// The values written to the file / sent ("2", "true", "\"text\"")
	std::string FormatValue(Global g, const Values& v);
	std::vector<std::string> FileLines();	// the lines of cnc-settings.g (tests)

	// Machine data: a global response (subscribers registered by Init) and the writer, every 100 ms
	void Init();					// registers the object model subscribers (once)
	void Begin();					// "key":"global" arrived
	void Value(Global g, const std::string& data);
	void Tick();

	using Listener = std::function<void()>;
	void AddListener(Listener l);	// values changed (from the machine or edited)

	void Reset();					// tests
	constexpr uint32_t WriteInterval = 100;		// ms between file lines
	constexpr uint32_t IgnoreAfterWrite = 3000;	// ms after the rename: replies may still show older values
	constexpr uint32_t QuietTime = 300;			// ms without a new global: the response is complete
	constexpr const char* SettingsFile = "0:/sys/cnc-settings.g";
	constexpr const char* TempFile = "0:/sys/cnc-settings.tmp";
}
