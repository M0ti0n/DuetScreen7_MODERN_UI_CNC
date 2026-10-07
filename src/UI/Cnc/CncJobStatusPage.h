/*
 * CncJobStatusPage.h
 *
 * JOB > JOB STATUS, ported from the PanelDue CNC UI (CncJobPage.cpp, JOB STATUS part), under the
 * compact DRO:
 *
 *   [ bracket_v2.nc                    ][  89% ]     progress tile: green running, amber tool
 *   [ ELAPSED  00:12:31 ][ LEFT  00:17:05 ]           change, cyan paused; idle DONE / ABORTED
 *   FEED WORK  T3                 ACTUAL     STEP
 *   [-][ 100% ][+][    1180 ][1%][5%][10%]
 *   SPINDLE  T3                   ACTUAL     STEP
 *   [-][  90% ][+][    7210 ][1%][5%][10%]
 *   [ COOLANT ][ CUSTOM 2 ][ CUSTOM 3 ]
 *   [ Insert T5 and press OK        [ OK ] ]          tool change row (only when needed)
 *   [    PAUSE    ]            [ ABORT ]
 *
 * Same behaviour as on PanelDue:
 *  - Overrides are per tool and only work while a job is in progress; all go back to 100 % at the
 *    end of the job. FEED WORK: M220, sent again on every tool change with the new tool's value.
 *    SPINDLE: RRF has no spindle override, so it is emulated: the programmed speed is tracked
 *    (spindles[0].active) and M3/M4 S<programmed x override> is sent; the value the panel sent
 *    comes back as 'active' and is recognised as an echo, not a new programmed speed.
 *    Tapping the percentage opens the numpad (feed 10-200 %, spindle 50-150 %).
 *  - Tool change row: M291 prompts whose title starts with TOOL show here during a job (blinking
 *    amber outline, OK = M292 P0). Without a prompt, "TOOL CHANGE T3 >>> T5" shows while an
 *    automatic change runs (state.nextTool).
 *  - PAUSE / RESUME / ABORT ask first ("ALERT !"). ABORT while running: M25, then M0 once paused
 *    (given up after 60 s).
 *  - A job that runs out shows "JOB DONE" with the time; JOB STATUS opens when a job starts and
 *    when a tool change prompt arrives. An error reply during a job is shown as ALERT !.
 */
#pragma once

#include "CncLayout.h"
#include "CncWidgets.h"
#include "ObjectModel/PrinterStatus.h"
#include "lvgl/lvgl.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace UI::Cnc
{
	class JobStatusPage
	{
	  public:
		static constexpr size_t NumRows = 2;		// 0 = FEED WORK, 1 = SPINDLE
		static constexpr size_t NumSteps = 3;		// 1 %, 5 %, 10 %
		// Limits: SETTINGS > OVERRIDES (global.cncOverrides, default feed 10-200 %, spindle 50-150 %)
		static int MinPct(size_t row);
		static int MaxPct(size_t row);
		static constexpr uint32_t EchoTime = 3000;				// ms a sent value may take to come back
		static constexpr uint32_t AbortPauseTimeout = 60000;	// ms: ABORT waits this long for the pause
		static constexpr uint32_t BlinkTime = 500;				// ms: tool change prompt outline

		JobStatusPage() = default;
		JobStatusPage(const JobStatusPage&) = delete;
		JobStatusPage& operator=(const JobStatusPage&) = delete;
		~JobStatusPage();

		// parentTop = screen y of the parent's top edge (the page content area)
		void create(lv_obj_t* parent, int32_t parentTop);
		// Show JOB STATUS (a job started, a tool change prompt arrived)
		void onShowRequest(std::function<void()> h) { m_showRequest = std::move(h); }

		// Machine data. statusChanged after State::StatusChanged (which keeps the job flag).
		void statusChanged(OM::PrinterStatus status);
		void setFileName(const std::string& path);
		void setProgress(unsigned int percent);
		void setDuration(uint32_t seconds);
		void setTimeLeft(uint32_t seconds);			// file based estimate
		void setRequestedSpeed(int32_t mmPerMin);	// FEED WORK actual
		void setSpeedFactor(int percent);			// move.speedFactor (M220)
		void setSpindleLimits(int32_t min, int32_t max);	// 0 = not reported
		void setSpindleActive(int32_t rpm);			// set speed (may send M3 / M4)
		void setSpindleState(bool running, bool reverse);
		void setSpindleCurrent(int32_t rpm);		// SPINDLE actual
		void setTool(int tool);						// -1 = none
		void setNextTool(int tool);					// state.nextTool, -1 = none
		void emergencyStop();						// STOP: the job ends as ABORTED

		// Tool change prompt (M291 titled TOOL..., during a job), and errors during a job
		bool showToolPrompt(const std::string& text, uint32_t seq);	// false: no job, use the popup
		void closeToolPrompt();						// answered elsewhere
		bool toolPromptOpen() const { return m_promptOpen; }
		bool error(const std::string& text);		// true: shown (ALERT !)

		// Touches (also used by the tests)
		void minusPressed(size_t row);
		void plusPressed(size_t row);
		void valuePressed(size_t row);
		void stepPressed(size_t row, size_t step);
		void auxPressed();
		void customPressed(size_t slot);			// 1 = CUSTOM 2, 2 = CUSTOM 3
		void toolOkPressed();
		void pausePressed();
		void abortPressed();
		void tick();								// blink, abort timeout (every 100 ms)

		// State, for tests
		bool inProgress() const { return m_inProgress; }
		int pct(size_t row) const;
		size_t stepIndex(size_t row) const { return m_stepIndex[row]; }
		std::string nameText() const { return m_name.line1(); }
		std::string resultText() const { return m_result.line1(); }
		std::string elapsedText() const;
		std::string leftText() const;
		std::string rowLabel(size_t row) const;
		std::string valueText(size_t row) const { return m_value[row].text(); }
		std::string actualText(size_t row) const;
		bool toolRowShown() const { return m_tc.visible(); }
		std::string toolRowText() const;			// both lines, space separated
		bool toolOkShown() const { return m_tcOk.visible(); }
		std::string pauseText() const { return m_pause.text(); }
		std::string abortText() const { return m_abort.text(); }
		bool customShown(size_t slot) const { return m_custom[slot - 1].visible(); }
		std::string auxText() const { return m_aux.text(); }

	  private:
		enum class Result : uint8_t
		{
			None,
			Done,
			Aborted
		};
		static constexpr int MaxToolNumber = 99;
		static constexpr size_t NoToolIndex = MaxToolNumber + 1;	// overrides while no tool is loaded
		static constexpr unsigned int MaxSpindleRetries = 2;		// the same speed is not sent more often
		static constexpr size_t NumRecentSent = 4;

		static size_t toolIndex(int tool);
		int currentPct(size_t row) const;
		void resetOverrides();
		bool spindleFrozen() const;
		bool isOwnEcho(int32_t rpm) const;
		bool canShowAutoPopup() const;

		void jobStarted();
		void jobEnded(OM::PrinterStatus oldStatus, OM::PrinterStatus newStatus);
		void sendFeed(int pct);
		void applySpindle();
		void setPct(size_t row, int pct);
		void doPause();
		void doResume();
		void doAbort();
		void wrapPrompt(const std::string& text);

		void updateAll();
		void updateNameAndResult();
		void updateTimes();
		void updateOverrideRow(size_t row);
		void updatePauseAbort();
		void updateToolRow();
		void updateAux();
		void updateCustom();

		lv_obj_t* m_root = nullptr;
		int32_t m_top = 0;
		lv_timer_t* m_timer = nullptr;
		std::function<void()> m_showRequest;

		// Widgets
		TextTile m_name, m_result, m_tc;
		lv_obj_t* m_elapsed = nullptr;
		lv_obj_t* m_left = nullptr;
		lv_obj_t* m_rowLabel[NumRows] = {};
		lv_obj_t* m_actual[NumRows] = {};
		TextButton m_minus[NumRows], m_value[NumRows], m_plus[NumRows];
		TextButton m_steps[NumRows][NumSteps];
		TextButton m_aux, m_custom[2], m_tcOk, m_pause, m_abort;

		// Job state
		Result m_resultState = Result::None;
		OM::PrinterStatus m_status = OM::PrinterStatus::connecting;
		bool m_inProgress = false;
		bool m_cancelSeen = false;
		bool m_panelAbort = false;
		bool m_abortPending = false;
		uint32_t m_abortStart = 0;
		uint32_t m_elapsedSeconds = 0;
		bool m_haveElapsed = false;
		unsigned int m_progress = 0;
		std::string m_fileName;

		// Tools
		int m_currentTool = -1;
		int m_nextTool = -1;

		// Per-tool overrides (percent), step choice per row
		uint16_t m_overrides[NumRows][NoToolIndex + 1] = {};
		size_t m_stepIndex[NumRows] = {1, 1};		// 5 %

		// Feed override bookkeeping
		int m_speedFactor = -1;						// last reported M220 value
		int m_feedSent = -1;
		uint32_t m_feedSentTime = 0;

		// Spindle override emulation. A G-code S equal to the value the panel applied cannot be told
		// apart from it: RRF reports the same set speed either way.
		int32_t m_spindleActive = 0;				// last reported set speed (rpm, >= 0)
		int32_t m_spindleProgrammed = 0;			// speed programmed by the job
		int32_t m_spindleSent = 0;					// last speed the panel sent
		uint32_t m_spindleSentTime = 0;
		unsigned int m_spindleRetries = 0;
		bool m_spindleReverse = false;
		bool m_spindleRunning = false;				// RRF keeps 'active' after M5
		int32_t m_spindleMin = 0, m_spindleMax = 0;	// 0 = unknown
		int32_t m_recentSent[NumRecentSent] = {};	// speeds the panel sent lately: their echoes are not
		uint32_t m_recentSentTime[NumRecentSent] = {};	// new programmed speeds (two taps within one poll)
		size_t m_recentSentNext = 0;

		// Tool change prompt
		bool m_promptOpen = false;
		uint32_t m_promptSeq = 0;
		std::string m_tcLine1, m_tcLine2;
		bool m_blinkOn = true;
		uint32_t m_blinkTime = 0;
	};
}
