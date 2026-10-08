/*
 * CncJobStatusPage.cpp
 *
 * Positions are PanelDue CncLayout::Job values through S() / SL(), relative to the page area.
 */

#include "CncJobStatusPage.h"
#include "CncMessages.h"
#include "CncPopup.h"
#include "CncSettings.h"
#include "CncState.h"
#include "CncTheme.h"
#include <algorithm>
#include <fmt/format.h>
#include <string>
#include <vector>

namespace UI::Cnc
{
	using namespace CncLayout;
	using namespace CncLayout::Job;

	namespace
	{
		constexpr const char* NoJob = "No job is running.";
		constexpr const char* OverridesOnlyInJob = "Overrides work only while a job is running.";
		const char* const StepNames[JobStatusPage::NumSteps] = {"1%", "5%", "10%"};
		constexpr int StepPct[JobStatusPage::NumSteps] = {1, 5, 10};

		std::string formatTime(uint32_t seconds)
		{
			return fmt::format("{:02d}:{:02d}:{:02d}", seconds / 3600, (seconds / 60) % 60, seconds % 60);
		}

		int32_t textWidth(const std::string& t)
		{
			lv_point_t size;
			lv_text_get_size(&size, t.c_str(), Font::Small(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
			return size.x;
		}

		bool isPausedState(OM::PrinterStatus s)
		{
			return s == OM::PrinterStatus::paused || s == OM::PrinterStatus::pausing ||
				   s == OM::PrinterStatus::resuming;
		}
	} // namespace

	int JobStatusPage::MinPct(size_t row)
	{
		return (row == 0) ? Settings::Get().feedMin : Settings::Get().spindleMin;
	}

	int JobStatusPage::MaxPct(size_t row)
	{
		return (row == 0) ? Settings::Get().feedMax : Settings::Get().spindleMax;
	}

	JobStatusPage::~JobStatusPage()
	{
		Messages::SetJobHooks({}); // the hooks point here
		if (m_timer != nullptr)
		{
			lv_timer_delete(m_timer);
		}
	}

	// ---- Building ------------------------------------------------------------------------------

	void JobStatusPage::create(lv_obj_t* parent, int32_t parentTop)
	{
		resetOverrides();
		m_root = CreateBare(parent, 0, 0, ScreenW, 1);
		lv_obj_set_size(m_root, LV_PCT(100), LV_PCT(100)); // the parent is not laid out yet
		m_top = parentTop;
		auto y = [this](int32_t pdY) { return S(pdY) - m_top; };
		const int32_t smallH = lv_font_get_line_height(Font::Small());

		// File name + progress / result
		m_name.create(m_root, S(NameX), y(NameY), SL(NameX, NameW), SL(NameY, NameH), false);
		m_result.create(m_root, S(ResultX), y(NameY), SL(ResultX, ResultW), SL(NameY, NameH), true);

		// ELAPSED / LEFT: muted label on the left, the time in the large font on the right
		auto timeCard = [&](int32_t x, const char* label) -> lv_obj_t*
		{
			lv_obj_t* card = CreateBare(m_root, S(x), y(TimesY), SL(x, TimeW), SL(TimesY, TimesH));
			StyleCard(card, Colour::Tile(), Colour::Border());
			const int32_t h = SL(TimesY, TimesH) - 2 * CardBorder;
			CreateLabel(card,
						S(x + TimeLabelX) - S(x) - CardBorder,
						0,
						SL(x + TimeLabelX, TimeLabelW),
						h,
						Font::Small(),
						Colour::Muted(),
						LV_TEXT_ALIGN_LEFT,
						label);
			const int32_t vx = S(x + TimeValueX) - S(x) - CardBorder;
			const int32_t vw = SL(x + TimeValueX, TimeW - TimeValueX - TimeValueInset);
			return CreateLabel(card, vx, 0, vw, h, Font::Large(), Colour::Text(), LV_TEXT_ALIGN_RIGHT, "-");
		};
		m_elapsed = timeCard(ElapsedX, "ELAPSED");
		m_left = timeCard(LeftX, "LEFT");

		// Override rows
		for (size_t r = 0; r < NumRows; ++r)
		{
			const int32_t ly = (r == 0) ? Label1Y : Label2Y;
			const int32_t oy = (r == 0) ? Ovr1Y : Ovr2Y;
			const int32_t top = y(oy), h = SL(oy, OvrH);
			auto label = [&](int32_t x, int32_t w, const char* text, lv_text_align_t align)
			{ return CreateLabel(m_root, S(x), y(ly), SL(x, w), smallH, Font::Small(), Colour::Muted(), align, text); };
			m_rowLabel[r] = label(MinusX + 2, 200, (r == 0) ? "FEED WORK" : "SPINDLE", LV_TEXT_ALIGN_LEFT);
			label(ActualX, ActualW, "ACTUAL", LV_TEXT_ALIGN_RIGHT);
			label(StepX, 474 - StepX, "STEP", LV_TEXT_ALIGN_RIGHT);

			m_minus[r].create(m_root, S(MinusX), top, SL(MinusX, PmW), h, "-", Font::Large());
			m_value[r].create(m_root, S(ValueX), top, SL(ValueX, ValueW), h, "100%", Font::Large());
			m_value[r].setPressedLook(false); // opens the numpad
			m_plus[r].create(m_root, S(PlusX), top, SL(PlusX, PmW), h, "+", Font::Large());
			m_minus[r].onPress([this, r] { minusPressed(r); });
			m_plus[r].onPress([this, r] { plusPressed(r); });
			m_value[r].onPress([this, r] { valuePressed(r); });

			lv_obj_t* card = CreateBare(m_root, S(ActualX), top, SL(ActualX, ActualW), h);
			StyleCard(card, Colour::Tile(), Colour::Border());
			m_actual[r] = CreateLabel(card,
									  S(ActualX + 6) - S(ActualX) - CardBorder,
									  0,
									  SL(ActualX + 6, ActualW - 18),
									  h - 2 * CardBorder,
									  Font::Large(),
									  Colour::Text(),
									  LV_TEXT_ALIGN_RIGHT,
									  "0");

			for (size_t s = 0; s < NumSteps; ++s)
			{
				const int32_t x = StepX + (int32_t)s * StepPitch;
				m_steps[r][s].create(m_root, S(x), top, SL(x, StepW), h, StepNames[s], Font::Small());
				m_steps[r][s].setPressedLook(false); // the selection shows
				m_steps[r][s].onPress([this, r, s] { stepPressed(r, s); });
			}
		}

		// COOLANT / VACUUM, CUSTOM 2, CUSTOM 3 (slots 1 and 2; CUSTOM 1 is on CONTROL)
		m_aux.create(m_root, S(BtnX(0)), y(BtnY), SL(BtnX(0), BtnW), SL(BtnY, BtnH), State::AuxLabel(), Font::Small());
		m_aux.setPressedLook(false); // toggle look
		m_aux.onPress([this] { auxPressed(); });
		for (size_t k = 0; k < 2; ++k)
		{
			const int32_t x = BtnX(1 + (unsigned int)k);
			const int32_t w = (k == 1) ? 474 - x : BtnW;
			m_custom[k].create(m_root, S(x), y(BtnY), SL(x, w), SL(BtnY, BtnH), "", Font::Small());
			m_custom[k].onPress([this, k] { customPressed(1 + k); });
		}

		// Tool change row, the OK button on the tile
		m_tc.create(m_root, S(TcX), y(TcY), SL(TcX, TcW), SL(TcY, TcH), false);
		m_tcOk.create(m_root, S(TcOkX), y(TcOkY), SL(TcOkX, TcOkW), SL(TcOkY, TcOkH), "OK", Font::Large());
		m_tcOk.setFilled(Colour::Green());
		m_tcOk.setPressedLook(false);
		m_tcOk.onPress([this] { toolOkPressed(); });
		m_tc.show(false);
		m_tcOk.show(false);

		// PAUSE / RESUME, ABORT
		m_pause.create(m_root, S(PauseX), y(PauseY), SL(PauseX, PauseW), SL(PauseY, PauseH), "PAUSE", Font::Small());
		m_abort.create(m_root, S(AbortX), y(PauseY), SL(AbortX, AbortW), SL(PauseY, PauseH), "ABORT", Font::Small());
		m_pause.setPressedLook(false); // they ask first
		m_abort.setPressedLook(false);
		m_pause.onPress([this] { pausePressed(); });
		m_abort.onPress([this] { abortPressed(); });

		State::AddAuxListener([this] { updateAux(); });
		State::AddCustomListener([this] { updateCustom(); });
		Messages::SetJobHooks({[this](const std::string& text, uint32_t seq) { return showToolPrompt(text, seq); },
							   [this] { closeToolPrompt(); },
							   [this](const std::string& text) { return error(text); }});

		m_timer = lv_timer_create(
			[](lv_timer_t* t) { static_cast<JobStatusPage*>(lv_timer_get_user_data(t))->tick(); }, 100, this);

		m_status = State::Status();
		updateAux();
		updateCustom();
		updateAll();
	}

	// ---- Helpers -------------------------------------------------------------------------------

	size_t JobStatusPage::toolIndex(int tool)
	{
		return (tool >= 0 && tool <= MaxToolNumber) ? (size_t)tool : NoToolIndex;
	}

	int JobStatusPage::currentPct(size_t row) const
	{
		return m_overrides[row][toolIndex(m_currentTool)];
	}

	int JobStatusPage::pct(size_t row) const
	{
		return (row < NumRows) ? currentPct(row) : 0;
	}

	void JobStatusPage::resetOverrides()
	{
		for (auto& row : m_overrides)
		{
			for (uint16_t& v : row)
			{
				v = 100;
			}
		}
	}

	// Paused (or on the way): pause.g / resume.g own the spindle (M5, M3 R1 restores the speed from
	// before the pause), so the emulation neither learns nor sends anything then.
	bool JobStatusPage::spindleFrozen() const
	{
		return isPausedState(m_status) || m_status == OM::PrinterStatus::cancelling ||
			   m_status == OM::PrinterStatus::connecting || m_status == OM::PrinterStatus::disconnected;
	}

	bool JobStatusPage::isOwnEcho(int32_t rpm) const
	{
		const uint32_t now = State::Now();
		for (size_t i = 0; i < NumRecentSent; ++i)
		{
			if (m_recentSent[i] == rpm && m_recentSent[i] != 0 && now - m_recentSentTime[i] < EchoTime)
			{
				return true;
			}
		}
		return false;
	}

	// Popups the panel opens by itself must not replace a prompt the machine waits for
	bool JobStatusPage::canShowAutoPopup() const
	{
		return !Popup::IsBlockingMessageOpen() && !Messages::ProbePopupOpen();
	}

	// ---- Look ----------------------------------------------------------------------------------

	void JobStatusPage::updateNameAndResult()
	{
		if (m_inProgress || m_resultState != Result::None)
		{
			m_name.setText(m_fileName.empty() ? std::string("-") : m_fileName);
			m_name.setTextColour(Colour::Text());
		}
		else
		{
			m_name.setText("No job yet");
			m_name.setTextColour(Colour::Muted());
		}

		if (m_inProgress)
		{
			lv_color_t c = Colour::Green();
			switch (m_status)
			{
			case OM::PrinterStatus::paused:
			case OM::PrinterStatus::pausing:
			case OM::PrinterStatus::resuming:
				c = Colour::Cyan();
				break;
			case OM::PrinterStatus::cancelling:
				c = Colour::StopRed();
				break;
			case OM::PrinterStatus::changingTool:
				c = Colour::Amber();
				break;
			default:
				if (m_promptOpen || (m_nextTool >= 0 && m_nextTool != m_currentTool))
				{
					c = Colour::Amber(); // RRF reports 'processing' during a tool change in a job
				}
				break;
			}
			m_result.setText(fmt::format("{:d}%", m_progress));
			m_result.setOutline(c, 3);
			m_result.setTextColour(Colour::Text());
		}
		else if (m_resultState == Result::Done)
		{
			m_result.setText("DONE");
			m_result.setOutline(Colour::Green(), 3);
			m_result.setTextColour(Colour::Text());
		}
		else if (m_resultState == Result::Aborted)
		{
			m_result.setText("ABORTED");
			m_result.setOutline(Colour::StopRed(), 3);
			m_result.setTextColour(Colour::Text());
		}
		else
		{
			m_result.setText("-");
			m_result.setOutline(Colour::Border(), 2);
			m_result.setTextColour(Colour::Muted());
		}
	}

	void JobStatusPage::updateTimes()
	{
		const bool show = m_haveElapsed && (m_inProgress || m_resultState != Result::None);
		lv_label_set_text(m_elapsed, show ? formatTime(m_elapsedSeconds).c_str() : "-");
		if (!m_inProgress)
		{
			lv_label_set_text(m_left, "-");
		}
	}

	void JobStatusPage::updateOverrideRow(size_t row)
	{
		const int p = currentPct(row);
		std::string label = (row == 0) ? "FEED WORK" : "SPINDLE";
		if (m_inProgress && m_currentTool >= 0)
		{
			label += fmt::format("  T{:d}", m_currentTool);
		}
		lv_label_set_text(m_rowLabel[row], label.c_str());

		m_value[row].setText(fmt::format("{:d}%", p).c_str());
		m_value[row].setColours(m_inProgress ? Colour::Text() : Colour::Muted(), Colour::Tile());
		m_minus[row].setLocked(!m_inProgress || p <= MinPct(row));
		m_plus[row].setLocked(!m_inProgress || p >= MaxPct(row));
		for (size_t s = 0; s < NumSteps; ++s)
		{
			m_steps[row][s].setLocked(!m_inProgress);
			m_steps[row][s].setSelected(m_inProgress && s == m_stepIndex[row]); // shown only during a job
		}
	}

	void JobStatusPage::updatePauseAbort()
	{
		const char* text = "PAUSE";
		lv_color_t fill = Colour::Cyan();
		bool enabled = m_inProgress;
		switch (m_status)
		{
		case OM::PrinterStatus::paused:
			text = "RESUME";
			fill = Colour::Green();
			break;
		case OM::PrinterStatus::pausing:
			text = "PAUSING";
			enabled = false;
			break;
		case OM::PrinterStatus::resuming:
			text = "RESUMING";
			enabled = false;
			break;
		case OM::PrinterStatus::cancelling:
		case OM::PrinterStatus::connecting:
		case OM::PrinterStatus::disconnected:
			enabled = false;
			break;
		default:
			break;
		}
		auto look = [](TextButton& b, lv_color_t f, bool on)
		{
			if (on)
			{
				b.setFilled(f);
			}
			else
			{
				b.resetColours();
			}
			b.setLocked(!on);
		};
		m_pause.setText(text);
		look(m_pause, fill, enabled);

		const bool aborting = m_abortPending || m_status == OM::PrinterStatus::cancelling;
		m_abort.setText(aborting ? "ABORTING" : "ABORT");
		look(m_abort, Colour::StopRed(), m_inProgress && !aborting);
	}

	void JobStatusPage::updateToolRow()
	{
		const bool atc = m_inProgress && m_nextTool >= 0 && m_nextTool != m_currentTool;
		const bool show = m_promptOpen || atc;
		if (m_promptOpen)
		{
			m_tc.setText(m_tcLine1, m_tcLine2);
			m_tc.setTextRight(S(TcOkW + 8));
			m_tc.setOutline(m_blinkOn ? Colour::Amber() : Colour::Border(), 3);
		}
		else if (atc)
		{
			const std::string from = (m_currentTool >= 0) ? fmt::format("T{:d}", m_currentTool) : std::string("T-");
			m_tc.setText(fmt::format("TOOL CHANGE  {:s} >>> T{:d}", from, m_nextTool));
			m_tc.setTextRight(0);
			m_tc.setOutline(Colour::Amber(), 3);
		}
		m_tc.show(show);
		m_tcOk.show(m_promptOpen);
	}

	void JobStatusPage::updateAux()
	{
		m_aux.setText(State::AuxLabel());
		m_aux.setToggle(State::AuxOn());
	}

	void JobStatusPage::updateCustom()
	{
		for (size_t k = 0; k < 2; ++k)
		{
			m_custom[k].setText(State::CustomLabel(1 + k));
			m_custom[k].show(State::CustomUsed(1 + k));
		}
	}

	void JobStatusPage::updateAll()
	{
		updateNameAndResult();
		updateTimes();
		for (size_t r = 0; r < NumRows; ++r)
		{
			updateOverrideRow(r);
		}
		updatePauseAbort();
		updateToolRow();
	}

	std::string JobStatusPage::elapsedText() const
	{
		return lv_label_get_text(m_elapsed);
	}

	std::string JobStatusPage::leftText() const
	{
		return lv_label_get_text(m_left);
	}

	std::string JobStatusPage::rowLabel(size_t row) const
	{
		return lv_label_get_text(m_rowLabel[row]);
	}

	std::string JobStatusPage::actualText(size_t row) const
	{
		return lv_label_get_text(m_actual[row]);
	}

	std::string JobStatusPage::toolRowText() const
	{
		return m_tc.line2().empty() ? m_tc.line1() : m_tc.line1() + " " + m_tc.line2();
	}

	// ---- Feed override (M220) ------------------------------------------------------------------

	void JobStatusPage::sendFeed(int p)
	{
		State::Send(fmt::format("M220 S{:d}\n", p));
		m_feedSent = p;
		m_feedSentTime = State::Now();
	}

	// ---- Spindle override (emulated) -----------------------------------------------------------

	void JobStatusPage::applySpindle()
	{
		if (!m_inProgress || spindleFrozen() || !m_spindleRunning || m_spindleActive == 0 || m_spindleProgrammed == 0)
		{
			return; // spindle off (M5 keeps 'active'): the new value applies when the job switches it on
		}
		const int p = currentPct(1);
		int32_t desired = (int32_t)(((int64_t)m_spindleProgrammed * p + 50) / 100);
		if (m_spindleMax > 0 && desired > m_spindleMax)
		{
			desired = m_spindleMax; // RRF would clamp it and the value would never come back
		}
		if (m_spindleMin > 0 && desired < m_spindleMin)
		{
			desired = m_spindleMin;
		}
		if (desired == m_spindleActive)
		{
			return;
		}
		const uint32_t now = State::Now();
		if (desired == m_spindleSent)
		{
			if (now - m_spindleSentTime < EchoTime || m_spindleRetries >= MaxSpindleRetries)
			{
				return; // on its way, or RRF does not take it: stop asking
			}
			++m_spindleRetries;
		}
		else
		{
			m_spindleRetries = 0;
		}
		State::Send(fmt::format("{:s} S{:d}\n", m_spindleReverse ? "M4" : "M3", desired));
		m_spindleSent = desired;
		m_spindleSentTime = now;
		m_recentSent[m_recentSentNext] = desired;
		m_recentSentTime[m_recentSentNext] = now;
		m_recentSentNext = (m_recentSentNext + 1) % NumRecentSent;
	}

	void JobStatusPage::setPct(size_t row, int p)
	{
		p = std::max(MinPct(row), std::min(MaxPct(row), p));
		m_overrides[row][toolIndex(m_currentTool)] = (uint16_t)p;
		if (row == 0)
		{
			sendFeed(p);
		}
		else
		{
			applySpindle();
		}
		updateOverrideRow(row);
	}

	// ---- Job start / end -----------------------------------------------------------------------

	void JobStatusPage::jobStarted()
	{
		m_resultState = Result::None;
		m_cancelSeen = false;
		m_panelAbort = false;
		m_abortPending = false;
		m_progress = 0;
		m_elapsedSeconds = 0;
		m_haveElapsed = true;
		lv_label_set_text(m_left, "-");
		resetOverrides();
		m_feedSent = -1;
		m_spindleSent = 0;
		m_spindleRetries = 0;
		m_promptOpen = false;
	}

	void JobStatusPage::jobEnded(OM::PrinterStatus oldStatus, OM::PrinterStatus newStatus)
	{
		// DONE only when the job runs out into idle. From paused it was cancelled (M0), even if
		// 'cancelling' was never seen; halted / off / a lost connection is not a finished job.
		const bool normalEnd =
			newStatus == OM::PrinterStatus::idle &&
			(oldStatus == OM::PrinterStatus::processing || oldStatus == OM::PrinterStatus::simulating ||
			 oldStatus == OM::PrinterStatus::busy || oldStatus == OM::PrinterStatus::changingTool);
		// ABORT still waiting for the pause (M0 not sent) when the job ran out: it finished (DONE)
		const bool aborted = m_cancelSeen || (m_panelAbort && !m_abortPending) || !normalEnd;
		m_resultState = aborted ? Result::Aborted : Result::Done;
		m_abortPending = false;
		m_promptOpen = false;
		m_nextTool = -1;
		resetOverrides();
		if (m_speedFactor >= 0 && m_speedFactor != 100)
		{
			sendFeed(100);
		}
		if (!aborted && canShowAutoPopup())
		{
			const std::string info = fmt::format("{:s}\nTime {:s}",
												 m_fileName.empty() ? std::string("Job finished.") : m_fileName,
												 formatTime(m_elapsedSeconds));
			Popup::Message("JOB DONE", info, false, nullptr);
		}
	}

	void JobStatusPage::statusChanged(OM::PrinterStatus status)
	{
		const OM::PrinterStatus oldStatus = m_status;
		const bool flag = State::JobInProgress();
		if (status == OM::PrinterStatus::cancelling)
		{
			m_cancelSeen = true;
		}
		const bool started = flag && !m_inProgress;
		const bool ended = !flag && m_inProgress;
		m_status = status;
		m_inProgress = flag;
		if (started)
		{
			jobStarted();
		}
		else if (ended)
		{
			jobEnded(oldStatus, status);
		}

		if (m_abortPending && status == OM::PrinterStatus::paused)
		{
			m_abortPending = false;
			State::Send("M0\n"); // paused now: cancel the job
		}
		else if (m_abortPending && !m_abortPauseSent &&
				 (status == OM::PrinterStatus::processing || status == OM::PrinterStatus::simulating))
		{
			// ABORT while it was resuming: resumed now, so pause it (M0 follows once paused)
			State::Send("M25\n");
			m_abortPauseSent = true;
			m_abortStart = State::Now();
		}
		if (m_inProgress && (status == OM::PrinterStatus::processing || status == OM::PrinterStatus::simulating))
		{
			applySpindle(); // resumed: overrides changed while paused apply now
		}
		updateAll();
		if (started && m_showRequest)
		{
			m_showRequest(); // a job started: show JOB STATUS
		}
	}

	// ---- Machine data --------------------------------------------------------------------------

	void JobStatusPage::setFileName(const std::string& path)
	{
		if (path.empty())
		{
			return; // keep the last name after the job
		}
		const size_t cut = path.find_last_of("/:");
		const std::string name = (cut == std::string::npos) ? path : path.substr(cut + 1);
		if (name != m_fileName)
		{
			m_fileName = name;
			updateNameAndResult();
		}
	}

	void JobStatusPage::setProgress(unsigned int percent)
	{
		percent = std::min(percent, 100u);
		if (m_inProgress && percent != m_progress)
		{
			m_progress = percent;
			updateNameAndResult();
		}
	}

	void JobStatusPage::setDuration(uint32_t seconds)
	{
		// 0 comes when the job ends (duration no longer reported): keep the last time
		if (m_inProgress && seconds != 0 && seconds != m_elapsedSeconds)
		{
			m_elapsedSeconds = seconds;
			m_haveElapsed = true;
			lv_label_set_text(m_elapsed, formatTime(seconds).c_str());
		}
	}

	void JobStatusPage::setTimeLeft(uint32_t seconds)
	{
		if (m_inProgress)
		{
			// 0: RRF has no estimate yet (job.timesLeft.file null)
			lv_label_set_text(m_left, (seconds == 0) ? "-" : formatTime(seconds).c_str());
		}
	}

	void JobStatusPage::setRequestedSpeed(int32_t mmPerMin)
	{
		lv_label_set_text(m_actual[0], fmt::format("{:d}", mmPerMin).c_str());
	}

	void JobStatusPage::setSpeedFactor(int percent)
	{
		m_speedFactor = percent;
		if (m_feedSent >= 0)
		{
			if (percent == m_feedSent)
			{
				m_feedSent = -1; // our M220 arrived
				return;
			}
			if (State::Now() - m_feedSentTime < EchoTime)
			{
				return; // old value, ours is on its way
			}
			m_feedSent = -1;
		}
		if (m_inProgress && percent != currentPct(0))
		{
			// changed elsewhere (DWC, console, G-code): becomes this tool's feed override
			m_overrides[0][toolIndex(m_currentTool)] = (uint16_t)std::max(1, percent);
			updateOverrideRow(0);
		}
	}

	void JobStatusPage::setSpindleLimits(int32_t min, int32_t max)
	{
		m_spindleMin = min;
		m_spindleMax = max;
	}

	void JobStatusPage::setSpindleActive(int32_t rpm)
	{
		if (rpm < 0)
		{
			rpm = -rpm; // old firmware: sign = direction (taken from the state instead)
		}
		m_spindleActive = rpm;
		if (rpm == 0)
		{
			return; // off / standby: keep the programmed speed
		}
		if (!m_inProgress)
		{
			m_spindleProgrammed = rpm;
			return;
		}
		if (spindleFrozen())
		{
			return; // pause.g / resume.g / changes while paused: not the job's speed
		}
		if (rpm == m_spindleSent)
		{
			m_spindleRetries = 0; // it arrived: a later M5 / M3 may be answered again
		}
		else if (rpm != m_spindleProgrammed && !isOwnEcho(rpm))
		{
			m_spindleProgrammed = rpm; // new speed from the job
		}
		applySpindle();
	}

	void JobStatusPage::setSpindleState(bool running, bool reverse)
	{
		m_spindleReverse = reverse;
		if (running != m_spindleRunning)
		{
			m_spindleRunning = running;
			if (running)
			{
				m_spindleRetries = 0; // switched on again (M3 after M5): apply the override afresh
				applySpindle();
			}
		}
	}

	void JobStatusPage::setSpindleCurrent(int32_t rpm)
	{
		lv_label_set_text(m_actual[1], fmt::format("{:d}", (rpm < 0) ? -rpm : rpm).c_str());
	}

	void JobStatusPage::setTool(int tool)
	{
		if (tool == m_currentTool || (tool < 0 && m_inProgress))
		{
			return; // RRF deselects the old tool during a change: keep its overrides
		}
		m_currentTool = tool;
		if (m_inProgress)
		{
			// the new tool's own overrides
			const int feed = currentPct(0);
			if (feed != m_speedFactor)
			{
				sendFeed(feed);
			}
			applySpindle();
		}
		for (size_t r = 0; r < NumRows; ++r)
		{
			updateOverrideRow(r);
		}
		updateToolRow();
		updateNameAndResult();
	}

	void JobStatusPage::setNextTool(int tool)
	{
		if (tool != m_nextTool)
		{
			m_nextTool = tool;
			updateToolRow();
			updateNameAndResult();
		}
	}

	void JobStatusPage::emergencyStop()
	{
		if (m_inProgress)
		{
			m_panelAbort = true;
		}
	}

	// ---- Tool change prompt, errors ------------------------------------------------------------

	// Two lines, word-wrapped by pixel width; the second is clipped at the tile edge if still too long
	void JobStatusPage::wrapPrompt(const std::string& text)
	{
		const int32_t maxW = S(TcW - 28 - (TcOkW + 8));
		size_t start = text.find_first_not_of(' ');
		if (start == std::string::npos)
		{
			start = text.size();
		}
		size_t end = start, fit = 0;
		while (end < text.size() && text[end] != '\n')
		{
			++end;
			if (textWidth(text.substr(start, end - start)) > maxW)
			{
				if (fit == 0)
				{
					fit = end - 1 - start;
				}
				break;
			}
			if (end == text.size() || text[end] == ' ' || text[end] == '\n')
			{
				fit = end - start;
			}
		}
		if (fit == 0)
		{
			fit = end - start;
		}
		m_tcLine1 = text.substr(start, fit);
		size_t rest = start + fit;
		while (rest < text.size() && (text[rest] == ' ' || text[rest] == '\n'))
		{
			++rest;
		}
		m_tcLine2 = text.substr(rest);
	}

	bool JobStatusPage::showToolPrompt(const std::string& text, uint32_t seq)
	{
		if (!m_inProgress)
		{
			return false;
		}
		wrapPrompt(text);
		m_promptSeq = seq;
		m_promptOpen = true;
		m_blinkOn = true;
		m_blinkTime = State::Now();
		updateToolRow();
		updateNameAndResult();
		if (m_showRequest)
		{
			m_showRequest(); // the OK is on JOB STATUS
		}
		return true;
	}

	void JobStatusPage::closeToolPrompt()
	{
		if (m_promptOpen)
		{
			m_promptOpen = false;
			updateToolRow();
			updateNameAndResult();
		}
	}

	bool JobStatusPage::error(const std::string& text)
	{
		if (m_inProgress && canShowAutoPopup())
		{
			Popup::Response(text, true, 0, true); // ALERT ! until X, also over other popups
			return true;
		}
		return false;
	}

	// ---- Touches -------------------------------------------------------------------------------

	void JobStatusPage::minusPressed(size_t row)
	{
		if (row >= NumRows)
		{
			return;
		}
		const int p = currentPct(row);
		if (!m_inProgress)
		{
			Popup::Refuse(OverridesOnlyInJob);
		}
		else if (p <= MinPct(row))
		{
			Popup::Refuse(fmt::format("{:s} override is at its limit ({:d} to {:d} %).",
									  (row == 0) ? "Feed" : "Spindle",
									  MinPct(row),
									  MaxPct(row)));
		}
		else
		{
			setPct(row, p - StepPct[m_stepIndex[row]]);
		}
	}

	void JobStatusPage::plusPressed(size_t row)
	{
		if (row >= NumRows)
		{
			return;
		}
		const int p = currentPct(row);
		if (!m_inProgress)
		{
			Popup::Refuse(OverridesOnlyInJob);
		}
		else if (p >= MaxPct(row))
		{
			Popup::Refuse(fmt::format("{:s} override is at its limit ({:d} to {:d} %).",
									  (row == 0) ? "Feed" : "Spindle",
									  MinPct(row),
									  MaxPct(row)));
		}
		else
		{
			setPct(row, p + StepPct[m_stepIndex[row]]);
		}
	}

	void JobStatusPage::valuePressed(size_t row)
	{
		if (row >= NumRows)
		{
			return;
		}
		if (!m_inProgress)
		{
			Popup::Refuse(OverridesOnlyInJob);
			return;
		}
		Popup::NumpadSpec pad;
		pad.tag = (row == 0) ? "FEED" : "SPINDLE";
		pad.unit = "%";
		pad.value = (float)currentPct(row);
		pad.decimals = 0;
		pad.allowDecimal = false;
		pad.allowMinus = false;
		pad.min = (float)MinPct(row);
		pad.max = (float)MaxPct(row);
		pad.onOk = [this, row](float value)
		{
			if (m_inProgress)
			{
				setPct(row, (int)(value + 0.5f));
			}
		};
		Popup::Numpad(pad);
	}

	void JobStatusPage::stepPressed(size_t row, size_t step)
	{
		if (!m_inProgress)
		{
			Popup::Refuse(OverridesOnlyInJob);
			return;
		}
		if (row < NumRows && step < NumSteps)
		{
			m_stepIndex[row] = step;
			updateOverrideRow(row);
		}
	}

	void JobStatusPage::auxPressed()
	{
		State::ToggleAux();
	}

	void JobStatusPage::customPressed(size_t slot)
	{
		State::RunCustomMacro(slot);
	}

	void JobStatusPage::toolOkPressed()
	{
		if (m_promptOpen)
		{
			State::Send(fmt::format("M292 P0 S{:d}\n", m_promptSeq));
			m_promptOpen = false;
			updateToolRow();
			updateNameAndResult();
		}
	}

	void JobStatusPage::doPause()
	{
		if (m_inProgress && m_status != OM::PrinterStatus::paused && m_status != OM::PrinterStatus::pausing)
		{
			State::Send("M25\n");
		}
	}

	void JobStatusPage::doResume()
	{
		if (m_inProgress && m_status == OM::PrinterStatus::paused)
		{
			State::Send("M24\n");
		}
	}

	void JobStatusPage::doAbort()
	{
		if (!m_inProgress)
		{
			return;
		}
		m_panelAbort = true;
		if (m_status == OM::PrinterStatus::paused)
		{
			State::Send("M0\n"); // cancels the paused job
		}
		else
		{
			// Pause first, M0 follows once paused. Already pausing: that pause. Resuming: an M25 now would land
			// in the resume, so it is sent once the job runs again (statusChanged).
			m_abortPauseSent = m_status != OM::PrinterStatus::resuming;
			if (m_status != OM::PrinterStatus::pausing && m_status != OM::PrinterStatus::resuming)
			{
				State::Send("M25\n");
			}
			m_abortPending = true;
			m_abortStart = State::Now();
		}
		updatePauseAbort();
	}

	void JobStatusPage::pausePressed()
	{
		if (!m_inProgress)
		{
			Popup::Refuse(NoJob);
		}
		else if (m_status == OM::PrinterStatus::paused)
		{
			Popup::Confirm("ALERT !", {"Do you want to RESUME", "this job?"}, [this] { doResume(); });
		}
		else if (m_status == OM::PrinterStatus::pausing || m_status == OM::PrinterStatus::resuming ||
				 m_status == OM::PrinterStatus::cancelling)
		{
			Popup::Refuse("Wait until the machine has finished pausing or resuming.");
		}
		else
		{
			Popup::Confirm("ALERT !", {"Do you want to PAUSE", "this job?"}, [this] { doPause(); });
		}
	}

	void JobStatusPage::abortPressed()
	{
		if (!m_inProgress)
		{
			Popup::Refuse(NoJob);
		}
		else if (m_abortPending || m_status == OM::PrinterStatus::cancelling)
		{
			Popup::Refuse("The job is already being aborted.");
		}
		else
		{
			Popup::Confirm("ALERT !", {"Do you want to ABORT", "this job?"}, [this] { doAbort(); });
		}
	}

	void JobStatusPage::tick()
	{
		const uint32_t now = State::Now();
		if (m_promptOpen && now - m_blinkTime >= BlinkTime)
		{
			m_blinkTime = now;
			m_blinkOn = !m_blinkOn;
			m_tc.setOutline(m_blinkOn ? Colour::Amber() : Colour::Border(), 3);
		}
		if (m_abortPending && now - m_abortStart >= AbortPauseTimeout)
		{
			m_abortPending = false;
			m_panelAbort = false;
			updatePauseAbort();
			if (canShowAutoPopup())
			{
				Popup::Refuse("The job did not pause within 60 s.\nIt was not cancelled.");
			}
		}
	}
} // namespace UI::Cnc
