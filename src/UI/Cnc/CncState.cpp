/*
 * CncState.cpp
 */

#include "CncState.h"
#include "CncPopup.h"
#include "CncSettings.h"
#include "Debug.h"
#include "Hardware/Duet.h"
#include "lvgl/lvgl.h"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <vector>

namespace UI::Cnc::State
{
	namespace
	{
		std::function<void(std::string_view)> s_sendHook;

		OM::PrinterStatus s_status = OM::PrinterStatus::connecting;
		bool s_jobInProgress = false;

		constexpr size_t MaxAxes = 4;
		uint8_t s_homedBits = 0;
		float s_machinePos[MaxAxes] = {};
		uint32_t s_lastPositionChange = 0;

		char s_axisLetters[MaxAxes] = {'X', 'Y', 'Z', 'A'};
		size_t s_axisCount = 3;
		float s_workPos[MaxAxes] = {};
		float s_offsets[MaxAxes][NumWcs] = {};
		size_t s_activeWorkplace = 0;
		float s_babystepZ = 0.0f;
		bool s_offsetsDirty = false;
		bool s_unsaved = false;
		bool s_offsetsLoaded = false; // the first offsets have arrived
		uint32_t s_firstOffsetTime = 0;
		constexpr uint32_t InitialLoadTime = 8000; // ms: offsets arriving after connecting are not edits

		ProbeMode s_probeMode = ProbeMode::Auto;
		bool s_probeReported = false;
		int32_t s_probeValue = 0;

		std::vector<ToolEntry> s_tools;
		int s_activeTool = -1;
		bool s_toolSetter = true;

		int32_t s_spindleRpm = 0;
		bool s_spindleRunning = false, s_spindleReverse = false;
		int32_t s_spindleMax = 0;
		unsigned int s_jogFeed = DefaultJogFeed;

		bool s_auxOn = false;
		bool s_auxVacuum = false;

		std::string s_customLabels[NumCustomMacros];
		std::string s_customMacros[NumCustomMacros];

		std::vector<Listener> s_auxListeners, s_customListeners, s_jobListeners, s_homedListeners, s_spindleListeners,
			s_offsetsListeners, s_toolsListeners, s_probeListeners;

		void Notify(const std::vector<Listener>& list)
		{
			for (const auto& l : list)
			{
				l();
			}
		}

		// A job is running (not paused); tool changes / macros / lost connection inside a job count
		bool IsRunning(OM::PrinterStatus status)
		{
			switch (status)
			{
			case OM::PrinterStatus::changingTool:
			case OM::PrinterStatus::busy:
			case OM::PrinterStatus::connecting:
			case OM::PrinterStatus::disconnected:
				return s_jobInProgress;
			case OM::PrinterStatus::processing:
			case OM::PrinterStatus::pausing:
			case OM::PrinterStatus::resuming:
			case OM::PrinterStatus::simulating:
			case OM::PrinterStatus::cancelling:
			case OM::PrinterStatus::starting:
				return true;
			default:
				return false;
			}
		}

		// File name part of a macro path ("0:/macros/!park.g" -> "!park.g")
		std::string_view MacroFileName(size_t slot)
		{
			const std::string& path = s_customMacros[slot];
			const size_t cut = path.find_last_of("/:");
			return (cut == std::string::npos) ? std::string_view(path) : std::string_view(path).substr(cut + 1);
		}

		void SendCustom(int slot)
		{
			if (slot == 0 && MachineBusy())
			{
				Popup::Refuse("Locked while a job is running."); // CUSTOM 1: a job started while the question was open
				return;
			}
			Send(fmt::format("M98 P\"{:s}\"\n", s_customMacros[slot]));
		}
	} // namespace

	// ---- Commands ----

	void Send(std::string_view gcode)
	{
		if (s_sendHook)
		{
			s_sendHook(gcode);
			return;
		}
		Comm::DUET.SendGcode(gcode);
	}

	void SetSendHook(std::function<void(std::string_view)> hook)
	{
		s_sendHook = std::move(hook);
	}

	uint32_t Now()
	{
		return lv_tick_get();
	}

	// ---- Job state ----

	bool StatusChanged(OM::PrinterStatus status)
	{
		bool flag = s_jobInProgress;
		switch (status)
		{
		case OM::PrinterStatus::processing:
		case OM::PrinterStatus::simulating:
		case OM::PrinterStatus::pausing:
		case OM::PrinterStatus::paused:
		case OM::PrinterStatus::resuming:
		case OM::PrinterStatus::cancelling:
		case OM::PrinterStatus::starting:
			flag = true;
			break;
		case OM::PrinterStatus::changingTool:
		case OM::PrinterStatus::busy:
		case OM::PrinterStatus::connecting:
		case OM::PrinterStatus::disconnected:
			break; // part of the job if one is in progress
		default:
			flag = false;
			break;
		}
		const bool started = flag && !s_jobInProgress;
		const bool changed = (status != s_status) || (flag != s_jobInProgress);
		s_status = status;
		s_jobInProgress = flag;
		if (changed)
		{
			Notify(s_jobListeners);
		}
		return started;
	}

	OM::PrinterStatus Status()
	{
		return s_status;
	}
	bool JobInProgress()
	{
		return s_jobInProgress;
	}
	bool MachineBusy()
	{
		return IsRunning(s_status);
	}
	bool JobActive()
	{
		return IsRunning(s_status) || s_status == OM::PrinterStatus::paused;
	}

	// ---- Axes ----

	void SetHomed(size_t axis, bool homed)
	{
		if (axis >= MaxAxes)
		{
			return;
		}
		const uint8_t bit = (uint8_t)(1u << axis);
		const uint8_t bits = homed ? (s_homedBits | bit) : (s_homedBits & ~bit);
		if (bits != s_homedBits)
		{
			s_homedBits = bits;
			Notify(s_homedListeners);
		}
	}

	bool Homed(size_t axis)
	{
		return axis < MaxAxes && (s_homedBits & (1u << axis)) != 0;
	}
	bool XyzHomed()
	{
		return (s_homedBits & 0x07) == 0x07;
	}

	void SetMachinePosition(size_t axis, float value)
	{
		if (axis >= MaxAxes)
		{
			return;
		}
		if (std::fabs(s_machinePos[axis] - value) > 0.0005f)
		{
			s_lastPositionChange = Now() + 1; // the machine is moving (0 = no change seen; stored + 1)
		}
		s_machinePos[axis] = value;
	}

	float MachinePosition(size_t axis)
	{
		return (axis < MaxAxes) ? s_machinePos[axis] : 0.0f;
	}

	bool GuardedJogAllowed(uint32_t& lastSent, bool ignoreBusy)
	{
		constexpr uint32_t GuardTime = 700;	  // ms after sending: covers the status poll delay
		constexpr uint32_t SettleTime = 1200; // ms without a position change: the move is over
		// lastSent and s_lastPositionChange hold the tick + 1, so 0 means "never" even at tick 0
		const uint32_t now = Now();
		if ((lastSent != 0 && now - (lastSent - 1) < GuardTime) ||
			(!ignoreBusy && s_status == OM::PrinterStatus::busy) ||
			(s_lastPositionChange != 0 && now - (s_lastPositionChange - 1) < SettleTime))
		{
			return false;
		}
		lastSent = now + 1;
		return true;
	}

	// ---- Work offsets ----

	void SetAxisLetter(size_t axis, char letter)
	{
		if (axis < MaxAxes && s_axisLetters[axis] != letter)
		{
			s_axisLetters[axis] = letter;
			s_offsetsDirty = true;
		}
	}
	char AxisLetter(size_t axis)
	{
		return (axis < MaxAxes) ? s_axisLetters[axis] : '?';
	}

	void SetAxisCount(size_t count)
	{
		count = (count == 4) ? 4 : 3;
		if (count != s_axisCount)
		{
			s_axisCount = count;
			s_offsetsDirty = true;
		}
	}
	size_t AxisCount()
	{
		return s_axisCount;
	}

	void SetWorkPosition(size_t axis, float value)
	{
		if (axis < MaxAxes)
		{
			s_workPos[axis] = value;
		}
	}
	float WorkPosition(size_t axis)
	{
		return (axis < MaxAxes) ? s_workPos[axis] : 0.0f;
	}

	void SetWorkplaceOffset(size_t axis, size_t wcs, float value)
	{
		if (axis >= MaxAxes || wcs >= NumWcs)
		{
			return;
		}
		const uint32_t now = Now();
		if (!s_offsetsLoaded)
		{
			s_offsetsLoaded = true;
			s_firstOffsetTime = now;
		}
		if (s_offsets[axis][wcs] != value)
		{
			s_offsets[axis][wcs] = value;
			s_offsetsDirty = true;
			if (now - s_firstOffsetTime > InitialLoadTime)
			{
				s_unsaved = true; // changed after the initial load (ZERO, probe, DWC, ...)
			}
		}
	}
	float WorkplaceOffset(size_t axis, size_t wcs)
	{
		return (axis < MaxAxes && wcs < NumWcs) ? s_offsets[axis][wcs] : 0.0f;
	}

	void SetActiveWorkplace(size_t workplace)
	{
		if (workplace != s_activeWorkplace)
		{
			s_activeWorkplace = workplace;
			s_offsetsDirty = true;
		}
	}
	size_t ActiveWorkplace()
	{
		return s_activeWorkplace;
	}

	void SetBabystepZ(float value)
	{
		if (value != s_babystepZ)
		{
			s_babystepZ = value;
			s_offsetsDirty = true;
		}
	}
	float BabystepZ()
	{
		return s_babystepZ;
	}

	void CommitOffsets()
	{
		if (s_offsetsDirty)
		{
			s_offsetsDirty = false;
			Notify(s_offsetsListeners);
		}
	}

	bool OffsetsUnsaved()
	{
		return s_unsaved;
	}
	void MarkOffsetsUnsaved()
	{
		if (!s_unsaved)
		{
			s_unsaved = true;
			Notify(s_offsetsListeners);
		}
	}
	void MarkOffsetsSaved()
	{
		if (s_unsaved)
		{
			s_unsaved = false;
			Notify(s_offsetsListeners);
		}
	}

	// ---- Probing ----

	ProbeMode GetProbeMode()
	{
		return s_probeMode;
	}

	void SetProbeMode(ProbeMode mode)
	{
		if (mode != s_probeMode)
		{
			s_probeMode = mode;
			Notify(s_probeListeners);
		}
	}

	void SetProbeValue(int32_t value)
	{
		const bool changed = !s_probeReported || (value >= 500) != (s_probeValue >= 500);
		s_probeReported = true;
		s_probeValue = value;
		if (changed)
		{
			Notify(s_probeListeners);
		}
	}

	bool ProbeReported()
	{
		return s_probeReported;
	}

	bool ProbeClosed()
	{
		return s_probeReported && s_probeValue >= 500;
	}

	// ---- Tools ----

	void SetTools(std::vector<ToolEntry> tools)
	{
		std::vector<ToolEntry> list;
		for (ToolEntry& t : tools)
		{
			if (t.number >= 0 && t.number <= MaxToolNumber && list.size() < MaxTools)
			{
				list.push_back(std::move(t)); // higher numbers or a 33rd tool are not listed (PanelDue)
			}
		}
		std::sort(list.begin(), list.end(), [](const ToolEntry& a, const ToolEntry& b) { return a.number < b.number; });
		const bool changed = list.size() != s_tools.size() ||
							 !std::equal(list.begin(),
										 list.end(),
										 s_tools.begin(),
										 [](const ToolEntry& a, const ToolEntry& b)
										 { return a.number == b.number && a.name == b.name && a.z == b.z; });
		if (changed)
		{
			s_tools = std::move(list);
			Notify(s_toolsListeners);
		}
	}

	const std::vector<ToolEntry>& Tools()
	{
		return s_tools;
	}

	const ToolEntry* FindTool(int number)
	{
		for (const ToolEntry& t : s_tools)
		{
			if (t.number == number && number >= 0)
			{
				return &t;
			}
		}
		return nullptr;
	}

	void SetActiveTool(int tool)
	{
		if (tool != s_activeTool)
		{
			s_activeTool = tool;
			Notify(s_toolsListeners);
		}
	}

	int ActiveTool()
	{
		return s_activeTool;
	}

	bool ToolSetter()
	{
		return s_toolSetter;
	}
	void SetToolSetter(bool on)
	{
		s_toolSetter = on;
	}

	// ---- Spindle ----

	// RRF keeps 'active' after M5; 0 = standby: keep the set speed (PanelDue SetSpindleActive / SetSpindleState)
	void SetSpindleFromMachine(uint32_t activeRpm, bool running, bool reverse, uint32_t maxRpm)
	{
		const int32_t rpm = (activeRpm > 0) ? (int32_t)activeRpm : s_spindleRpm;
		const bool changed = rpm != s_spindleRpm || running != s_spindleRunning;
		s_spindleRpm = rpm;
		s_spindleRunning = running;
		s_spindleReverse = reverse;
		s_spindleMax = (int32_t)maxRpm;
		if (changed)
		{
			Notify(s_spindleListeners);
		}
	}

	int32_t SpindleRpm()
	{
		return s_spindleRpm;
	}

	void SetSpindleRpm(int32_t rpm)
	{
		if (rpm != s_spindleRpm)
		{
			s_spindleRpm = rpm;
			Notify(s_spindleListeners);
		}
	}

	bool SpindleRunning()
	{
		return s_spindleRunning;
	}
	bool SpindleReverse()
	{
		return s_spindleReverse;
	}
	int32_t SpindleMaxRpm()
	{
		return s_spindleMax;
	}

	void SendSpindleOn()
	{
		Send(fmt::format("{:s} S{:d}\n", s_spindleReverse ? "M4" : "M3", s_spindleRpm));
	}

	// ---- SLOW jog feed ----

	unsigned int JogFeed()
	{
		return s_jogFeed;
	}

	void SetJogFeed(unsigned int mmPerMin)
	{
		if (mmPerMin >= 1 && mmPerMin <= MaxJogFeed)
		{
			s_jogFeed = mmPerMin;
		}
	}

	// ---- COOLANT / VACUUM ----

	bool AuxOn()
	{
		return s_auxOn;
	}

	void ToggleAux()
	{
		s_auxOn = !s_auxOn;
		Send(s_auxOn ? "M98 P\"0:/macros/aux_on.g\"\n" : "M98 P\"0:/macros/aux_off.g\"\n");
		Notify(s_auxListeners);
	}

	const char* AuxLabel()
	{
		return s_auxVacuum ? "VACUUM" : "COOLANT";
	}

	void SetAuxLabel(bool vacuum)
	{
		s_auxVacuum = vacuum;
		Notify(s_auxListeners);
	}

	// ---- CUSTOM macros ----

	void SetCustomMacro(size_t slot, const char* label, const char* macro)
	{
		if (slot >= NumCustomMacros)
		{
			return;
		}
		const bool used = label != nullptr && label[0] != 0 && macro != nullptr && macro[0] != 0;
		s_customLabels[slot] = used ? label : "";
		s_customMacros[slot] = used ? macro : "";
		Notify(s_customListeners);
	}

	bool CustomUsed(size_t slot)
	{
		return slot < NumCustomMacros && !s_customMacros[slot].empty();
	}
	const char* CustomLabel(size_t slot)
	{
		return slot < NumCustomMacros ? s_customLabels[slot].c_str() : "";
	}
	const char* CustomMacro(size_t slot)
	{
		return slot < NumCustomMacros ? s_customMacros[slot].c_str() : "";
	}

	void RunCustomMacro(size_t slot)
	{
		if (!CustomUsed(slot))
		{
			return;
		}
		const std::string_view file = MacroFileName(slot);
		if (!file.empty() && file[0] == '!')
		{
			Popup::Confirm(s_customLabels[slot], {fmt::format("Run {:s}?", file)}, [slot] { SendCustom((int)slot); });
		}
		else
		{
			SendCustom((int)slot);
		}
	}

	// ---- Listeners ----

	void AddAuxListener(Listener l)
	{
		s_auxListeners.push_back(std::move(l));
	}
	void AddCustomListener(Listener l)
	{
		s_customListeners.push_back(std::move(l));
	}
	void AddJobListener(Listener l)
	{
		s_jobListeners.push_back(std::move(l));
	}
	void AddHomedListener(Listener l)
	{
		s_homedListeners.push_back(std::move(l));
	}
	void AddSpindleListener(Listener l)
	{
		s_spindleListeners.push_back(std::move(l));
	}
	void AddOffsetsListener(Listener l)
	{
		s_offsetsListeners.push_back(std::move(l));
	}
	void AddToolsListener(Listener l)
	{
		s_toolsListeners.push_back(std::move(l));
	}
	void AddProbeListener(Listener l)
	{
		s_probeListeners.push_back(std::move(l));
	}

	void Reset()
	{
		s_status = OM::PrinterStatus::connecting;
		s_jobInProgress = false;
		s_homedBits = 0;
		for (float& p : s_machinePos)
		{
			p = 0.0f;
		}
		s_lastPositionChange = 0;
		const char letters[MaxAxes] = {'X', 'Y', 'Z', 'A'};
		for (size_t a = 0; a < MaxAxes; ++a)
		{
			s_axisLetters[a] = letters[a];
			s_workPos[a] = 0.0f;
			for (float& o : s_offsets[a])
			{
				o = 0.0f;
			}
		}
		s_axisCount = 3;
		s_activeWorkplace = 0;
		s_babystepZ = 0.0f;
		s_offsetsDirty = false;
		s_unsaved = false;
		s_offsetsLoaded = false;
		s_firstOffsetTime = 0;
		s_probeMode = ProbeMode::Auto;
		s_probeReported = false;
		s_probeValue = 0;
		s_tools.clear();
		s_activeTool = -1;
		s_toolSetter = true;
		s_spindleRpm = 0;
		s_spindleRunning = s_spindleReverse = false;
		s_spindleMax = 0;
		s_jogFeed = DefaultJogFeed;
		s_auxOn = false;
		s_auxVacuum = false;
		for (size_t i = 0; i < NumCustomMacros; ++i)
		{
			s_customLabels[i].clear();
			s_customMacros[i].clear();
		}
		s_auxListeners.clear();
		s_customListeners.clear();
		s_jobListeners.clear();
		s_homedListeners.clear();
		s_spindleListeners.clear();
		s_offsetsListeners.clear();
		s_toolsListeners.clear();
		s_probeListeners.clear();
		s_sendHook = nullptr;
		Settings::Reset();
	}
} // namespace UI::Cnc::State
