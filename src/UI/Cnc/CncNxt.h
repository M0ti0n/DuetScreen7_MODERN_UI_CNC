/*
 * CncNxt.h
 *
 * MillenniumOS NeXT (mos-nxt) support: detection from the machine's globals, the probing cycles
 * (G6500 ... G6520) with their parameters, and the calibration macros. The screen only builds the
 * G-code; NeXT does the probing, asks its own questions (M291) and writes the work offset.
 *
 * Detection: global.nxtLoaded is true once NeXT has booted. Also read: nxtProbeToolID (the touch probe's
 * tool, which every cycle needs selected), nxtFeatureTouchProbe, nxtFeatureToolSetter,
 * nxtFeatureFourthAxis (the MOS Fourth Axis macros M4910 / M4912 / M4807 for the rotary).
 *
 * SETTINGS > PROBE MODE: NXT (cncProbeMode = 3) can only be chosen when NeXT is detected. WCS > PROBE
 * then shows NxtProbePage instead of the standard page. Targets NeXT v0.6 (cycle words from the macros'
 * USAGE lines). Results go straight to a work offset with U<1..5> (NeXT keeps 5 result rows, so
 * G54 ... G58).
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UI::Cnc::Nxt
{
	void Init();										// the subscribers (once)
	void Reset();										// tests

	bool Detected();									// global.nxtLoaded
	bool Active();										// detected and PROBE MODE = NXT
	int ProbeToolId();									// -1: not known
	bool TouchProbe();
	bool ToolSetter();
	bool FourthAxis();									// the rotary macros are installed (and an A axis is visible)
	void SetGlobal(const std::string& name, const std::string& value);	// the subscriber (tests call it)

	// ---- Cycles ----
	enum class Cycle : uint8_t
	{
		Bore,
		Boss,
		RectPocket,
		RectBlock,
		Web,
		Pocket,
		OutCorner,
		InCorner,
		Surface,
		ViseCorner,
		Rotation,
		RotaryCentre,
		Count
	};
	constexpr size_t NumCycles = (size_t)Cycle::Count;

	enum class Param : uint8_t
	{
		Diameter,		// D
		Width,			// W (rect: X size; web / pocket: the size along N)
		Height,			// H
		Depth,			// L
		Clearance,		// C
		Overtravel,		// O
		Samples,		// R (0 = NeXT's default)
		SaveTo,			// U 1..5
		Rotation,		// Q 0 ask / 1 apply / 2 skip
		Axis,			// N 0 X / 1 Y
		Side,			// G6506 D: 1 from the + side, 0 from the - side
		Spacing,		// S
		Corner,			// TL TR BL BR -> X / Y targets
		Distance,		// corner: how far to probe from the start
		SurfaceAxis,	// X / Y / Z
		SurfaceDistance, // signed distance to probe along that axis
		Count
	};
	constexpr size_t NumParams = (size_t)Param::Count;
	constexpr size_t MaxCycleParams = 8;
	constexpr size_t NumSaveTo = 5;

	struct Values
	{
		float diameter = 25.0f, width = 50.0f, height = 30.0f, depth = 5.0f;
		float clearance = 5.0f, overtravel = 2.0f, spacing = 20.0f;
		float cornerDistance = 10.0f, surfaceDistance = -10.0f;
		unsigned int samples = 0;
		unsigned int saveTo = 0;			// 0 = G54 (U1)
		unsigned int rotation = 0;			// Q
		unsigned int axis = 0;				// N
		unsigned int side = 1;
		unsigned int corner = 0;			// 0 TL, 1 TR, 2 BL, 3 BR
		unsigned int surfaceAxis = 2;		// Z
	};
	Values& Get();

	const char* CycleName(Cycle c);					// "BORE"
	const char* CycleHint(Cycle c);					// "hole centre"
	const char* CycleCode(Cycle c);					// "G6500"
	bool CycleAvailable(Cycle c);					// ROTARY CENTRE: only with the rotary macros
	std::vector<Param> CycleParams(Cycle c);
	const char* ParamLabel(Param p, Cycle c);		// "DIAMETER", "WIDTH" ...
	std::string ParamText(Param p);					// the value as shown
	bool ParamIsChoice(Param p);					// a choice popup instead of the numpad
	std::vector<std::string> ParamChoices(Param p, Cycle c);
	size_t ParamChoice(Param p);					// current choice index
	void SetParamChoice(Param p, size_t index);
	float ParamValue(Param p);
	void SetParamValue(Param p, float value);
	void ParamRange(Param p, float& min, float& max, bool& minus, int& decimals);

	// The G-code of a cycle with the current values (lines end in "\n")
	std::string Command(Cycle c);

	// ---- Calibration ----
	struct CalibrationItem
	{
		const char* label;
		std::string gcode;
		const char* question;		// shown before it runs
	};
	std::vector<CalibrationItem> CalibrationItems();
}
