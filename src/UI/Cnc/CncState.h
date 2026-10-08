/*
 * CncState.h
 *
 * Machine and UI state shared by the CNC pages (port of the PanelDue CncCommon.cpp):
 * job state, homing, the large-jog guard, the COOLANT / VACUUM toggle, the CUSTOM macro slots,
 * and sending G-code. Plain C++ (no LVGL), so the rules can be unit-tested.
 */
#pragma once

#include "ObjectModel/PrinterStatus.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace UI::Cnc::State
{
	// ---- Commands ----
	void Send(std::string_view gcode);					// to the Duet (or the test hook)
	// A file name or text inside "..." of a G-code parameter: " and ' doubled (RRF: ' alone makes the next
	// letter lowercase), e.g. M32 "0:/gcodes/Bob''s part.nc"
	std::string Quoted(std::string_view text);

	// Object model subscribers run on the receive thread (USB decode / HTTP). What they do to the UI is queued
	// and run by the UI thread (RunQueued, every 20 ms), never under the UI lock on the receive thread: a USB
	// reconnect on the UI thread joins the receive thread while holding that lock.
	void SetUiThread();												// the thread that may touch the UI (the view's)
	void RunOnUi(std::function<void()> fn);							// on the UI thread: at once; else queued
	void RunQueued();												// the UI thread: what was queued, in order
	void SetSendHook(std::function<void(std::string_view)> hook);	// tests: capture instead of sending
	uint32_t Now();										// ms tick

	// ---- Job state (PanelDue CncJob::StatusChanged / CncCommon) ----
	// Call on every status change. Returns true when a job has just started.
	bool StatusChanged(OM::PrinterStatus status);
	OM::PrinterStatus Status();
	bool JobInProgress();								// from the start of a job until it ends (tool changes inside count)
	bool MachineBusy();									// a job runs (not paused): moving controls are locked
	bool JobActive();									// a job runs or is paused: setup controls are locked

	// ---- Axes ----
	void SetHomed(size_t axis, bool homed);				// axis 0..3 = X Y Z A (DRO rows)
	bool Homed(size_t axis);
	bool XyzHomed();
	void SetMachinePosition(size_t axis, float value);	// also tells when the machine has stopped moving
	float MachinePosition(size_t axis);
	// Large jogs (10 and 100): allowed only when the last one has finished, so they cannot queue up.
	// lastSent is the caller's tick of its last large jog. ignoreBusy: an M291 jog prompt is open.
	bool GuardedJogAllowed(uint32_t& lastSent, bool ignoreBusy = false);

	// ---- Work offsets (WCS > OFFSETS): G54..G59 = workplace 0..5, axis = DRO row (X Y Z A) ----
	constexpr size_t NumWcs = 6;
	void SetAxisLetter(size_t axis, char letter);
	char AxisLetter(size_t axis);
	void SetAxisCount(size_t count);					// 3 or 4 visible axes
	size_t AxisCount();
	void SetWorkPosition(size_t axis, float value);		// what the DRO shows
	float WorkPosition(size_t axis);
	// From the object model: changes after the first 8 s of offsets mark them unsaved (zero, probe, DWC)
	void SetWorkplaceOffset(size_t axis, size_t wcs, float value);
	float WorkplaceOffset(size_t axis, size_t wcs);
	void SetActiveWorkplace(size_t workplace);			// 0..8; 6..8 (G59.1..G59.3): none of the six
	size_t ActiveWorkplace();
	void SetBabystepZ(float value);						// RRF babystep: the OFFSET column total
	float BabystepZ();
	void CommitOffsets();								// after an object model update: tells the listeners
	bool OffsetsUnsaved();								// SAVE gets an accent outline
	void MarkOffsetsUnsaved();
	void MarkOffsetsSaved();

	// ---- Tools (WCS > TOOLS): whatever RRF has defined, ascending tool numbers T0..T99, up to 32 ----
	struct ToolEntry
	{
		int number;
		std::string name;
		float z;										// Z offset
	};
	constexpr size_t MaxTools = 32;
	constexpr int MaxToolNumber = 99;
	void SetTools(std::vector<ToolEntry> tools);		// sorted and limited here
	const std::vector<ToolEntry>& Tools();
	const ToolEntry* FindTool(int number);				// nullptr: not defined
	void SetActiveTool(int tool);						// -1 = none
	int ActiveTool();
	// SETTINGS > TOOL SETTER (global.cncToolSetter); on until SETTINGS reads it
	bool ToolSetter();
	void SetToolSetter(bool on);

	// ---- Probing (WCS > PROBE) ----
	enum class ProbeMode : uint8_t
	{
		Auto,											// global.cncProbeMode 0
		Semi,											// 1
		Manual											// 2
	};
	ProbeMode GetProbeMode();							// SETTINGS > PROBE MODE; AUTO until SETTINGS reads it
	void SetProbeMode(ProbeMode mode);
	void SetProbeValue(int32_t value);					// sensors.probes[0].value[0]
	bool ProbeReported();
	bool ProbeClosed();									// triggered: 500 and above (RRF)

	// ---- Spindle (PanelDue CncControl): the S tiles show the set speed, kept while in standby ----
	void SetSpindleFromMachine(uint32_t activeRpm, bool running, bool reverse, uint32_t maxRpm);
	int32_t SpindleRpm();								// set speed (0 = none yet)
	void SetSpindleRpm(int32_t rpm);					// entered on the S numpad
	bool SpindleRunning();
	bool SpindleReverse();
	int32_t SpindleMaxRpm();							// 0 = not reported
	void SendSpindleOn();								// M3 / M4 at the set speed

	// ---- SLOW jog feed (global.cncJogFeed on PanelDue), mm/min ----
	constexpr unsigned int DefaultJogFeed = 300, MaxJogFeed = 20000;
	unsigned int JogFeed();
	void SetJogFeed(unsigned int mmPerMin);			// 1 .. MaxJogFeed, else ignored

	// ---- COOLANT / VACUUM toggle (one button, label from SETTINGS); runs aux_on.g / aux_off.g ----
	bool AuxOn();
	void ToggleAux();
	const char* AuxLabel();
	void SetAuxLabel(bool vacuum);

	// ---- CUSTOM macros: slot 0 = CUSTOM 1 (CONTROL), 1 / 2 = CUSTOM 2 / 3 (JOB STATUS) ----
	constexpr size_t NumCustomMacros = 3;
	void SetCustomMacro(size_t slot, const char* label, const char* macro);
	bool CustomUsed(size_t slot);
	const char* CustomLabel(size_t slot);
	const char* CustomMacro(size_t slot);
	void RunCustomMacro(size_t slot);					// a file name starting with '!' asks first

	// ---- Change listeners (pages refresh their look) ----
	using Listener = std::function<void()>;
	void AddAuxListener(Listener l);
	void AddCustomListener(Listener l);
	void AddJobListener(Listener l);					// job state changed
	void AddHomedListener(Listener l);
	void AddSpindleListener(Listener l);				// set speed or running changed
	void AddOffsetsListener(Listener l);				// offsets, active workplace, babystep, axes, unsaved
	void AddToolsListener(Listener l);					// tool list or active tool changed
	void AddProbeListener(Listener l);					// probe mode or reading changed

	// Back to power-up state (tests)
	void Reset();
}
