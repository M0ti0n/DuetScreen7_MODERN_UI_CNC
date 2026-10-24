/*
 * CncNxt.cpp
 *
 * See CncNxt.h.
 */

#include "CncNxt.h"
#include "CncSettings.h"
#include "CncState.h"
#include "Subscribers/Subscribers.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fmt/format.h>

namespace UI::Cnc::Nxt
{
	namespace
	{
		bool s_inited = false;
		bool s_loaded = false, s_touchProbe = false, s_toolSetter = false, s_fourthAxis = false;
		int s_probeTool = -1;
		int s_touchProbeIndex = 0;		// global.nxtTouchProbeID: the K number of the touch probe
		Values s_values;

		const char* const CycleNames[NumCycles] = {"BORE",
												   "BOSS",
												   "RECT POCKET",
												   "RECT BLOCK",
												   "WEB",
												   "POCKET",
												   "OUT CORNER",
												   "IN CORNER",
												   "SURFACE",
												   "VISE CORNER",
												   "ROTATION",
												   "ROTARY CENTRE"};
		const char* const CycleHints[NumCycles] = {"hole centre",
												   "post centre",
												   "width x height",
												   "width x height",
												   "one axis, outside",
												   "one axis, inside",
												   "outside corner",
												   "inside corner",
												   "single face, Z top",
												   "vise jaw corner",
												   "skew of the stock",
												   "A axis centre line"};
		const char* const CycleCodes[NumCycles] = {
			"G6500", "G6501", "G6502", "G6503", "G6504", "G6505", "G6508", "G6509", "G6510", "G6520", "G6506", "M4910"};

		const char* const CornerNames[] = {"TOP LEFT", "TOP RIGHT", "BOTTOM LEFT", "BOTTOM RIGHT"};

		bool parseBool(const std::string& s)
		{
			std::string l = s;
			std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return (char)std::tolower(c); });
			return l == "true" || l == "1";
		}

		template <bool* Flag>
		bool onBool(Comm::JsonDecoder*, const char* data, const size_t[])
		{
			const std::string v = data != nullptr ? data : "";
			State::RunOnUi([v] { *Flag = parseBool(v); });
			return true;
		}

		bool onProbeTool(Comm::JsonDecoder*, const char* data, const size_t[])
		{
			const std::string v = data != nullptr ? data : "";
			State::RunOnUi([v] { s_probeTool = v.empty() ? -1 : std::atoi(v.c_str()); });
			return true;
		}

		bool onTouchProbeIndex(Comm::JsonDecoder*, const char* data, const size_t[])
		{
			const std::string v = data != nullptr ? data : "";
			State::RunOnUi([v] { s_touchProbeIndex = std::max(0, std::atoi(v.c_str())); });
			return true;
		}

		std::string num(float v)
		{
			return fmt::format("{:.3f}", v);
		}
	} // namespace

	void Init()
	{
		if (s_inited)
		{
			return;
		}
		s_inited = true;
		SubscriberMap::addSubscriber("global:nxtLoaded", &onBool<&s_loaded>);
		SubscriberMap::addSubscriber("global:nxtFeatureTouchProbe", &onBool<&s_touchProbe>);
		SubscriberMap::addSubscriber("global:nxtFeatureToolSetter", &onBool<&s_toolSetter>);
		SubscriberMap::addSubscriber("global:nxtFeatureFourthAxis", &onBool<&s_fourthAxis>);
		SubscriberMap::addSubscriber("global:nxtProbeToolID", &onProbeTool);
		SubscriberMap::addSubscriber("global:nxtTouchProbeID", &onTouchProbeIndex);
	}

	void Reset()
	{
		s_loaded = s_touchProbe = s_toolSetter = s_fourthAxis = false;
		s_probeTool = -1;
		s_touchProbeIndex = 0;
		s_values = Values();
	}

	bool Detected()
	{
		return s_loaded;
	}

	bool Active()
	{
		return s_loaded && Settings::Get().probeMode == 3;
	}

	int ProbeToolId()
	{
		return s_probeTool;
	}

	int UiProbeIndex()
	{
		return Active() ? s_touchProbeIndex : 0;
	}

	bool TouchProbe()
	{
		return s_touchProbe;
	}

	bool ToolSetter()
	{
		return s_toolSetter;
	}

	bool FourthAxis()
	{
		if (!s_fourthAxis)
		{
			return false;
		}
		for (size_t i = 0; i < State::AxisCount(); ++i)
		{
			if (State::AxisLetter(i) == 'A')
			{
				return true;
			}
		}
		return false;
	}

	void SetGlobal(const std::string& name, const std::string& value)
	{
		if (name == "nxtLoaded")
		{
			s_loaded = parseBool(value);
		}
		else if (name == "nxtFeatureTouchProbe")
		{
			s_touchProbe = parseBool(value);
		}
		else if (name == "nxtFeatureToolSetter")
		{
			s_toolSetter = parseBool(value);
		}
		else if (name == "nxtFeatureFourthAxis")
		{
			s_fourthAxis = parseBool(value);
		}
		else if (name == "nxtProbeToolID")
		{
			s_probeTool = value.empty() ? -1 : std::atoi(value.c_str());
		}
		else if (name == "nxtTouchProbeID")
		{
			s_touchProbeIndex = std::max(0, std::atoi(value.c_str()));
		}
	}

	Values& Get()
	{
		return s_values;
	}

	// ---- Cycles ----

	const char* CycleName(Cycle c)
	{
		return CycleNames[(size_t)c];
	}

	const char* CycleHint(Cycle c)
	{
		return CycleHints[(size_t)c];
	}

	const char* CycleCode(Cycle c)
	{
		return CycleCodes[(size_t)c];
	}

	bool CycleAvailable(Cycle c)
	{
		return c != Cycle::RotaryCentre || FourthAxis();
	}

	std::vector<Param> CycleParams(Cycle c)
	{
		using P = Param;
		switch (c)
		{
		case Cycle::Bore:
			return {P::Diameter, P::Depth, P::Overtravel, P::Samples, P::SaveTo, P::Rotation};
		case Cycle::Boss:
			return {P::Diameter, P::Depth, P::Clearance, P::Overtravel, P::Samples, P::SaveTo, P::Rotation};
		case Cycle::RectPocket:
		case Cycle::RectBlock:
			return {P::Width, P::Height, P::Depth, P::Clearance, P::Overtravel, P::Samples, P::SaveTo, P::Rotation};
		case Cycle::Web:
		case Cycle::Pocket:
			return {P::Axis, P::Width, P::Depth, P::Clearance, P::Overtravel, P::Samples, P::SaveTo};
		case Cycle::OutCorner:
			return {P::Corner, P::Distance, P::Depth, P::Clearance, P::Samples, P::SaveTo};
		case Cycle::InCorner:
			return {P::Corner, P::Distance, P::Depth, P::Clearance, P::Overtravel, P::Samples, P::SaveTo};
		case Cycle::Surface:
			return {P::SurfaceAxis, P::SurfaceDistance, P::Samples, P::SaveTo};
		case Cycle::ViseCorner:
			return {P::Depth, P::Clearance, P::Overtravel, P::Samples, P::SaveTo};
		case Cycle::Rotation:
			return {P::Axis, P::Side, P::Spacing, P::Depth, P::Overtravel, P::Samples, P::SaveTo, P::Rotation};
		case Cycle::RotaryCentre:
			return {P::SaveTo};
		default:
			return {};
		}
	}

	const char* ParamLabel(Param p, Cycle c)
	{
		switch (p)
		{
		case Param::Diameter:
			return "DIAMETER";
		case Param::Width:
			return (c == Cycle::Web || c == Cycle::Pocket) ? "SIZE" : "WIDTH X";
		case Param::Height:
			return "HEIGHT Y";
		case Param::Depth:
			return "DEPTH";
		case Param::Clearance:
			return "CLEARANCE";
		case Param::Overtravel:
			return "OVERTRAVEL";
		case Param::Samples:
			return "SAMPLES";
		case Param::SaveTo:
			return "SAVE TO";
		case Param::Rotation:
			return "ROTATION";
		case Param::Axis:
			return (c == Cycle::Rotation) ? "EDGE ALONG" : "AXIS";
		case Param::Side:
			return "FROM SIDE";
		case Param::Spacing:
			return "SPACING";
		case Param::Corner:
			return "CORNER";
		case Param::Distance:
			return "TO CORNER";
		case Param::SurfaceDistance:
			return "DISTANCE";
		case Param::SurfaceAxis:
			return "AXIS";
		default:
			return "";
		}
	}

	bool ParamIsChoice(Param p)
	{
		return p == Param::SaveTo || p == Param::Rotation || p == Param::Axis || p == Param::Side ||
			   p == Param::Corner || p == Param::SurfaceAxis;
	}

	std::vector<std::string> ParamChoices(Param p, Cycle c)
	{
		switch (p)
		{
		case Param::SaveTo:
			return {"G54", "G55", "G56", "G57", "G58"};
		case Param::Rotation:
			return {"ASK", "APPLY", "SKIP"};
		case Param::Axis:
			return {"X", "Y"};
		case Param::Side:
			return {"+", "-"};
		case Param::Corner:
			return {CornerNames[0], CornerNames[1], CornerNames[2], CornerNames[3]};
		case Param::SurfaceAxis:
			return {"X", "Y", "Z"};
		default:
			(void)c;
			return {};
		}
	}

	size_t ParamChoice(Param p)
	{
		const Values& v = s_values;
		switch (p)
		{
		case Param::SaveTo:
			return v.saveTo;
		case Param::Rotation:
			return v.rotation;
		case Param::Axis:
			return v.axis;
		case Param::Side:
			return v.side == 1 ? 0 : 1;
		case Param::Corner:
			return v.corner;
		case Param::SurfaceAxis:
			return v.surfaceAxis;
		default:
			return 0;
		}
	}

	void SetParamChoice(Param p, size_t index)
	{
		Values& v = s_values;
		const auto i = (unsigned int)index;
		switch (p)
		{
		case Param::SaveTo:
			v.saveTo = std::min(i, (unsigned int)NumSaveTo - 1);
			break;
		case Param::Rotation:
			v.rotation = std::min(i, 2u);
			break;
		case Param::Axis:
			v.axis = std::min(i, 1u);
			break;
		case Param::Side:
			v.side = (i == 0) ? 1 : 0;
			break;
		case Param::Corner:
			v.corner = std::min(i, 3u);
			break;
		case Param::SurfaceAxis:
			v.surfaceAxis = std::min(i, 2u);
			break;
		default:
			break;
		}
	}

	float ParamValue(Param p)
	{
		const Values& v = s_values;
		switch (p)
		{
		case Param::Diameter:
			return v.diameter;
		case Param::Width:
			return v.width;
		case Param::Height:
			return v.height;
		case Param::Depth:
			return v.depth;
		case Param::Clearance:
			return v.clearance;
		case Param::Overtravel:
			return v.overtravel;
		case Param::Samples:
			return (float)v.samples;
		case Param::Spacing:
			return v.spacing;
		case Param::Distance:
			return v.cornerDistance;
		case Param::SurfaceDistance:
			return v.surfaceDistance;
		default:
			return 0.0f;
		}
	}

	void SetParamValue(Param p, float value)
	{
		Values& v = s_values;
		switch (p)
		{
		case Param::Diameter:
			v.diameter = value;
			break;
		case Param::Width:
			v.width = value;
			break;
		case Param::Height:
			v.height = value;
			break;
		case Param::Depth:
			v.depth = value;
			break;
		case Param::Clearance:
			v.clearance = value;
			break;
		case Param::Overtravel:
			v.overtravel = value;
			break;
		case Param::Samples:
			v.samples = (unsigned int)std::max(0.0f, value);
			break;
		case Param::Spacing:
			v.spacing = value;
			break;
		case Param::Distance:
			v.cornerDistance = value;
			break;
		case Param::SurfaceDistance:
			v.surfaceDistance = value;
			break;
		default:
			break;
		}
	}

	void ParamRange(Param p, float& min, float& max, bool& minus, int& decimals)
	{
		minus = false;
		decimals = 3;
		switch (p)
		{
		case Param::Diameter:
		case Param::Width:
		case Param::Height:
			min = 0.5f;
			max = 1000.0f;
			break;
		case Param::Depth:
			min = 0.1f;
			max = 100.0f;
			break;
		case Param::Clearance:
		case Param::Overtravel:
			min = 0.1f;
			max = 50.0f;
			break;
		case Param::Samples:
			min = 0.0f;
			max = 10.0f;
			decimals = 0;
			break;
		case Param::Spacing:
			min = 1.0f;
			max = 1000.0f;
			break;
		case Param::Distance:
			min = 0.5f;
			max = 500.0f;
			break;
		case Param::SurfaceDistance:
			min = -500.0f;
			max = 500.0f;
			minus = true;
			break;
		default:
			min = 0.0f;
			max = 1000.0f;
			break;
		}
	}

	std::string ParamText(Param p)
	{
		if (ParamIsChoice(p))
		{
			const auto choices = ParamChoices(p, Cycle::Bore);
			const size_t i = ParamChoice(p);
			return i < choices.size() ? choices[i] : std::string();
		}
		if (p == Param::Samples)
		{
			return s_values.samples == 0 ? "AUTO" : fmt::format("{:d}", s_values.samples);
		}
		return num(ParamValue(p));
	}

	std::string Command(Cycle c)
	{
		const Values& v = s_values;
		const unsigned int u = v.saveTo + 1;
		const std::string r = v.samples ? fmt::format(" R{:d}", v.samples) : std::string();
		const std::string q = fmt::format(" Q{:d}", v.rotation);
		switch (c)
		{
		case Cycle::Bore:
			return fmt::format("G6500 U{:d} D{:s} L{:s} O{:s}{:s}{:s}\n", u, num(v.diameter), num(v.depth), num(v.overtravel), r, q);
		case Cycle::Boss:
			return fmt::format("G6501 U{:d} D{:s} L{:s} C{:s} O{:s}{:s}{:s}\n",
							   u,
							   num(v.diameter),
							   num(v.depth),
							   num(v.clearance),
							   num(v.overtravel),
							   r,
							   q);
		case Cycle::RectPocket:
		case Cycle::RectBlock:
			return fmt::format("{:s} U{:d} W{:s} H{:s} L{:s} C{:s} O{:s}{:s}{:s}\n",
							   CycleCode(c),
							   u,
							   num(v.width),
							   num(v.height),
							   num(v.depth),
							   num(v.clearance),
							   num(v.overtravel),
							   r,
							   q);
		case Cycle::Web:
		case Cycle::Pocket:
			return fmt::format("{:s} U{:d} N{:d} W{:s} L{:s} C{:s} O{:s}{:s}\n",
							   CycleCode(c),
							   u,
							   v.axis,
							   num(v.width),
							   num(v.depth),
							   num(v.clearance),
							   num(v.overtravel),
							   r);
		case Cycle::OutCorner:
		case Cycle::InCorner:
		{
			// The corner sets where the probe goes from the start: outside, toward the stock (away from the
			// corner's sides); inside a pocket, toward the corner. TL = -X, +Y.
			const int sx = (v.corner == 0 || v.corner == 2) ? -1 : 1;
			const int sy = (v.corner == 0 || v.corner == 1) ? 1 : -1;
			const int dir = (c == Cycle::OutCorner) ? -1 : 1;
			const float d = v.cornerDistance;
			const std::string targets = fmt::format(" X{{global.nxtAbsPos[0]{:+.3f}}} Y{{global.nxtAbsPos[1]{:+.3f}}}",
													(float)(dir * sx) * d,
													(float)(dir * sy) * d);
			const std::string tail = (c == Cycle::OutCorner)
										 ? fmt::format(" C{:s}{:s}", num(v.clearance), r)
										 : fmt::format(" C{:s} O{:s}{:s}", num(v.clearance), num(v.overtravel), r);
			// M5000 first: the targets are worked out from the position it stores
			return fmt::format("M5000\n{:s} U{:d} L{:s}{:s}{:s}\n", CycleCode(c), u, num(v.depth), targets, tail);
		}
		case Cycle::Surface:
		{
			const char letter = "XYZ"[v.surfaceAxis];
			return fmt::format("M5000 P1 I{:d}\nG6510 U{:d} {:c}{{global.nxtAbsPos{:+.3f}}}{:s}\n",
							   v.surfaceAxis,
							   u,
							   letter,
							   v.surfaceDistance,
							   r);
		}
		case Cycle::ViseCorner:
			return fmt::format("G6520 U{:d} L{:s} C{:s} O{:s}{:s}\n", u, num(v.depth), num(v.clearance), num(v.overtravel), r);
		case Cycle::Rotation:
			return fmt::format("G6506 U{:d} N{:d} D{:d} S{:s} L{:s} O{:s}{:s}{:s}\n",
							   u,
							   v.axis,
							   v.side,
							   num(v.spacing),
							   num(v.depth),
							   num(v.overtravel),
							   r,
							   q);
		case Cycle::RotaryCentre:
			// MOS Fourth Axis: the centre line (Y), then Y0 into the chosen work offset
			return fmt::format("M4910\nM4807 W{:d}\n", 54 + v.saveTo);
		default:
			return "";
		}
	}

	// ---- Calibration ----

	std::vector<CalibrationItem> CalibrationItems()
	{
		std::vector<CalibrationItem> items;
		items.push_back({"DATUM SETUP", "M5016\n", "Toolsetter and probe reference (M5016)"});
		if (s_touchProbe)
		{
			items.push_back({"PROBE REPEAT", "M6523 B0 C10\n", "Touch probe: 10 probes, min / max / range (M6523)"});
		}
		if (s_toolSetter)
		{
			items.push_back({"SETTER REPEAT", "M6523 B1 C10\n", "Toolsetter: 10 probes, min / max / range (M6523)"});
		}
		if (FourthAxis())
		{
			items.push_back({"ROTARY FLAT", "M4912\n", "Rotary: flatness along Y (M4912)"});
			items.push_back({"ROTARY CENTRE", "M4910\n", "Rotary: centre line Y (M4910)"});
			items.push_back({"ROTARY Y0", fmt::format("M4807 W{:d}\n", 54 + s_values.saveTo), "Rotary centre as Y0 of the SAVE TO offset (M4807)"});
		}
		return items;
	}
} // namespace UI::Cnc::Nxt
