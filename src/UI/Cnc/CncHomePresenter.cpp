/*
 * CncHomePresenter.cpp
 */

#include "CncHomePresenter.h"
#include "CncHomeView.h"
#include "CncMessages.h"
#include "CncNxt.h"
#include "CncPreviewSource.h"
#include "CncState.h"
#include "Debug.h"
#include "Hardware/Duet.h"
#include "ObjectModel/Axis.h"
#include "ObjectModel/Job.h"
#include "ObjectModel/PrinterStatus.h"
#include "ObjectModel/Sensor.h"
#include "ObjectModel/Spindle.h"
#include "ObjectModel/Tool.h"
#include "Subscribers/ResponseSubscribers.h"
#include <cmath>

namespace UI
{
	void CncHomePresenter::onInit()
	{
		ZoneScoped;
		registerEventListener<EventType::AxesData>(this, &CncHomePresenter::newAxesData);
		registerEventListener<EventType::WorkplaceNumber>(this, &CncHomePresenter::newWorkplaceNumber);
		registerEventListener<EventType::CurrentTool>(this, &CncHomePresenter::newCurrentTool);
		registerEventListener<EventType::ToolData>(this, &CncHomePresenter::newToolData);
		registerEventListener<EventType::Alert>(this, &CncHomePresenter::newAlert);
		registerEventListener<EventType::Response>(this, &CncHomePresenter::newResponse);
		registerEventListener<EventType::ProbeData>(this, &CncHomePresenter::newProbeData);
		registerEventListener<EventType::SpindleData>(this, &CncHomePresenter::newSpindleData);
		registerEventListener<EventType::CurrentMoveRequestedSpeed>(this, &CncHomePresenter::newRequestedSpeed);
		registerEventListener<EventType::Status>(this, &CncHomePresenter::newStatus);
		registerEventListener<EventType::NextTool>(this, &CncHomePresenter::newNextTool);
		registerEventListener<EventType::JobFileName>(this, &CncHomePresenter::newJobFileName);
		registerEventListener<EventType::JobFileProgress>(this, &CncHomePresenter::newJobProgress);
		registerEventListener<EventType::JobDuration>(this, &CncHomePresenter::newJobDuration);
		registerEventListener<EventType::JobTimeLeft>(this, &CncHomePresenter::newJobTimeLeft);
		registerEventListener<EventType::SpeedFactor>(this, &CncHomePresenter::newSpeedFactor);
		registerEventListener<EventType::IpAddress>(this, &CncHomePresenter::newIpAddress);
	}

	void CncHomePresenter::onActivate()
	{
		ZoneScoped;
		newAxesData();
		newWorkplaceNumber();
		newCurrentTool();
		newToolData();
		newProbeData();
		newSpindleData();
		newRequestedSpeed();
		newSpeedFactor();
		newJobFileName(OM::GetJobName());
		newStatus(OM::GetStatus());
		newNextTool();
		newJobProgress();
		newJobDuration();
		newJobTimeLeft();
	}

	// Job state for every page (PanelDue ChangeStatus): the shared flag first, then JOB STATUS
	// (DONE / ABORTED, opens JOB STATUS when a job starts)
	void CncHomePresenter::newStatus(OM::PrinterStatus status)
	{
		ZoneScoped;
		Cnc::State::StatusChanged(status);
		m_view->getJobStatusPage().statusChanged(status);
	}

	// ---- JOB STATUS data ----

	void CncHomePresenter::newNextTool()
	{
		ZoneScoped;
		m_view->getJobStatusPage().setNextTool((int)OM::GetNextTool());
	}

	void CncHomePresenter::newJobFileName(const std::string& name)
	{
		ZoneScoped;
		m_view->getJobStatusPage().setFileName(name);
		m_view->getJobPreviewPage().setJobFile(name);
		m_view->getJobListPage().setJobFile(name);
	}

	void CncHomePresenter::newJobProgress()
	{
		ZoneScoped;
		const uint32_t size = OM::GetFileSize();
		m_view->getJobPreviewPage().setJobPosition(OM::GetFilePosition()); // PREVIEW: the running operation
		if (size != 0)
		{
			m_view->getJobStatusPage().setProgress(
				(unsigned int)(((uint64_t)OM::GetFilePosition() * 100 + size / 2) / size));
		}
	}

	void CncHomePresenter::newJobDuration()
	{
		ZoneScoped;
		m_view->getJobStatusPage().setDuration(OM::GetPrintDuration());
	}

	// SYSTEM > SETTINGS: the Duet's address
	void CncHomePresenter::newIpAddress(const std::string& ip)
	{
		ZoneScoped;
		m_view->getSettingsPage().setIp(ip);
		Cnc::PreviewSource::SetMachineIp(ip); // PREVIEW downloads from here (USB / UART connection)
	}

	// File based estimate (as PanelDue)
	void CncHomePresenter::newJobTimeLeft()
	{
		ZoneScoped;
		m_view->getJobStatusPage().setTimeLeft(OM::GetPrintRemaining(OM::RemainingTimeType::FILE));
	}

	void CncHomePresenter::newSpeedFactor()
	{
		ZoneScoped;
		m_view->getJobStatusPage().setSpeedFactor((int)std::lround(OM::Move::GetSpeedFactor() * 100.0f));
	}

	// DRO rows: the visible axes in slot order, up to X Y Z + one more (PanelDue shows 3 or 4 rows)
	void CncHomePresenter::newAxesData()
	{
		ZoneScoped;
		MODEL_LOCK();
		size_t row = 0;
		const size_t count = OM::Move::GetAxisCount();
		for (size_t slot = 0; slot < count && row < MaxDroAxes; ++slot)
		{
			auto axis = OM::Move::GetAxisBySlot(slot);
			if (axis == nullptr || !axis->visible)
			{
				continue;
			}
			m_rowLetters[row] = axis->letter[0];
			m_view->setAxisRow(row, axis->letter[0], axis->userPosition, axis->machinePosition, axis->homed);
			Cnc::State::SetHomed(row, axis->homed);
			Cnc::State::SetMachinePosition(row, axis->machinePosition);
			Cnc::State::SetAxisLetter(row, axis->letter[0]);
			Cnc::State::SetWorkPosition(row, axis->userPosition);
			for (size_t w = 0; w < Cnc::State::NumWcs; ++w)
			{
				Cnc::State::SetWorkplaceOffset(row, w, axis->workplaceOffsets[w]);
			}
			if (axis->letter[0] == 'Z')
			{
				m_zAxisIndex = axis->index;
				Cnc::State::SetBabystepZ(axis->babystep); // the OFFSET column total (WCS > OFFSETS)
			}
			++row;
		}
		m_rowCount = (row > 3) ? 4 : 3;
		m_view->setAxisRowCount(m_rowCount);
		Cnc::State::SetAxisCount(m_rowCount);
		Cnc::State::CommitOffsets();
		newToolData(); // tool offsets arrive as axes data
	}

	void CncHomePresenter::newWorkplaceNumber()
	{
		ZoneScoped;
		m_view->setWorkplace(OM::Move::GetCurrentWorkplaceNumber());
		Cnc::State::SetActiveWorkplace(OM::Move::GetCurrentWorkplaceNumber());
		Cnc::State::CommitOffsets();
	}

	void CncHomePresenter::newCurrentTool()
	{
		ZoneScoped;
		MODEL_LOCK();
		auto tool = OM::GetCurrentTool();
		m_view->setTool(tool == nullptr ? -1 : (int)tool->index);
		Cnc::State::SetActiveTool(tool == nullptr ? -1 : (int)tool->index);
		m_view->getJobStatusPage().setTool(tool == nullptr ? -1 : (int)tool->index);
	}

	// The tool table of WCS > TOOLS: number, name, Z offset
	void CncHomePresenter::newToolData()
	{
		ZoneScoped;
		MODEL_LOCK();
		std::vector<Cnc::State::ToolEntry> tools;
		OM::IterateToolsWhile(
			[&](OM::ToolPtr tool, size_t)
			{
				const float z = (m_zAxisIndex < MAX_TOTAL_AXES) ? tool->offsets[m_zAxisIndex] : 0.0f;
				tools.push_back({(int)tool->index, std::string(tool->name.c_str()), z});
				return true;
			});
		Cnc::State::SetTools(std::move(tools));
	}

	void CncHomePresenter::newSpindleData()
	{
		ZoneScoped;
		MODEL_LOCK();
		auto spindle = OM::GetSpindle(0);
		auto& job = m_view->getJobStatusPage();
		if (spindle == nullptr)
		{
			Cnc::State::SetSpindleFromMachine(0, false, false, 0);
		}
		else
		{
			const bool running = spindle->state != OM::SpindleState::stopped;
			const bool reverse = spindle->state == OM::SpindleState::reverse;
			Cnc::State::SetSpindleFromMachine(spindle->active, running, reverse, spindle->max);
			// JOB STATUS: the spindle override emulation (may send M3 / M4), in object model order
			job.setSpindleLimits((int32_t)spindle->min, (int32_t)spindle->max);
			job.setSpindleActive((int32_t)spindle->active);
			job.setSpindleState(running, reverse);
			job.setSpindleCurrent((int32_t)spindle->current);
		}
		// The S tiles show the set speed: kept in standby, also set from the S numpad
		m_view->setSpindle(Cnc::State::SpindleRpm(), Cnc::State::SpindleRunning());
	}

	// RRF reports mm/s, the DRO shows mm/min (as PanelDue)
	void CncHomePresenter::newRequestedSpeed()
	{
		ZoneScoped;
		const int32_t mmPerMin = (int32_t)(OM::Move::GetCurrentMoveRequestedSpeed() * 60.0f + 0.5f);
		m_view->setFeed(mmPerMin);
		m_view->getJobStatusPage().setRequestedSpeed(mmPerMin);
	}

	// ZERO: the current position becomes 0 in the active work offset (same G-code as PanelDue)
	void CncHomePresenter::zeroAxis(size_t row)
	{
		ZoneScoped;
		if (row >= m_rowCount)
		{
			return;
		}
		Comm::DUET.SendGcodef("G10 L20 P{:d} {:c}0\n", Cnc::State::ActiveWorkplace() + 1, m_rowLetters[row]);	// RRF 3.7: move.workplaceNumber is obsolete
	}

	void CncHomePresenter::showPage(unsigned int page)
	{
		ZoneScoped;
		m_view->showPage(page);
	}

	void CncHomePresenter::emergencyStop()
	{
		ZoneScoped;
		LOG_WARN("CNC STOP pressed");
		Comm::DUET.Estop();
		m_view->getJobStatusPage().emergencyStop(); // the job ends as ABORTED
	}

	// Messages from the machine (M291): popups, jog prompt, auto probe progress (Cnc::Messages)
	void CncHomePresenter::newAlert(const OM::Alert& alert)
	{
		ZoneScoped;
		Cnc::Messages::Message msg;
		msg.mode = static_cast<Cnc::Messages::Mode>(static_cast<int>(alert.mode));
		msg.seq = alert.seq;
		msg.controls = alert.controls;
		msg.title = alert.title.c_str();
		msg.text = alert.text.c_str();
		msg.cancelButton = alert.cancelButton;
		for (size_t i = 0; i < alert.choices_count && i < std::size(alert.choices); ++i)
		{
			msg.choices.emplace_back(alert.choices[i].c_str());
		}
		msg.intMin = alert.limits.numberInt.min;
		msg.intMax = alert.limits.numberInt.max;
		msg.intDefault = alert.limits.numberInt.valueDefault;
		msg.floatMin = alert.limits.numberFloat.min;
		msg.floatMax = alert.limits.numberFloat.max;
		msg.floatDefault = alert.limits.numberFloat.valueDefault;
		msg.textMin = alert.limits.text.min;
		msg.textMax = alert.limits.text.max;
		msg.textDefault = alert.limits.text.valueDefault.c_str();
		Cnc::Messages::Process(msg);
	}

	void CncHomePresenter::newResponse(ResponseType type, const std::string& text)
	{
		ZoneScoped;
		m_view->getConsolePage().response(text); // SYSTEM > CONSOLE log (first: is it a typed command's reply?)
		m_view->getAlertsPage().collect(text);	 // SYSTEM > ALERT history
		Cnc::Messages::Response(text,
								type == ResponseType::ERROR || text.find("Error:") != std::string::npos,
								m_view->getConsolePage().lastReplyWasConsole(),
								m_view->getPage() == CncHomeView::PageSystem);
	}

	// The probe of WCS > PROBE (OPEN / CLOSED): K0, or NeXT's touch probe in NXT mode
	void CncHomePresenter::newProbeData()
	{
		ZoneScoped;
		int32_t value = 0;
		if (OM::GetProbeValue((size_t)Cnc::Nxt::UiProbeIndex(), value))
		{
			Cnc::State::SetProbeValue(value);
		}
	}
} // namespace UI
