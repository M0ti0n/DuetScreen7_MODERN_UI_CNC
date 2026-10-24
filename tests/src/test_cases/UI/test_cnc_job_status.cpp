/*
 * test_cnc_job_status.cpp
 *
 * JOB > JOB STATUS of the CNC UI: job start / end (DONE, ABORTED, JOB DONE notice), times and
 * progress from the object model, the per-tool FEED WORK (M220) and SPINDLE (emulated, M3 / M4)
 * overrides, PAUSE / RESUME / ABORT, the tool change row (M291 TOOL... prompt, automatic change),
 * errors during a job, and screenshots (compare with the PanelDue CNC UI references x 1.25).
 */

#include "Debug.h"
#include "ObjectModel/Job.h"
#include "UI/Cnc/CncHomeView.h"
#include "UI/Cnc/CncLayout.h"
#include "UI/Cnc/CncMessages.h"
#include "UI/Cnc/CncNxt.h"
#include "UI/Cnc/CncSettings.h"
#include "UI/Cnc/CncPopup.h"
#include "UI/Cnc/CncState.h"
#include "test_utils/UiTestSuite.h"
#include <fmt/format.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace UI::Cnc;
using Lines = std::vector<std::string>;
using V = UI::CncHomeView;
using M = Messages::Mode;

class TestCncJobStatus : public UiTestSuite
{
  protected:
	TestCncJobStatus()
	{
		ZoneScoped;
		lv_display_set_resolution(lv_display_get_default(), UI::CncLayout::ScreenW, UI::CncLayout::ScreenH);
		lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
		State::Reset();
		Messages::Reset();
		State::SetSendHook([this](std::string_view g) { sent.emplace_back(g); });
	}

	~TestCncJobStatus() override
	{
		Popup::Close();
		view.reset();
		// Leave the object model idle for the next tests
		load_model_data_from_file("tests/object_model/cnc/model_state_idle.json");
		load_model_data(R"({"key":"move","flags":"v","result":{"speedFactor":1.0}})");
		Messages::Reset();
	}

	// The frame with machine data, idle, on 'page'; CUSTOM 2 / 3 set as in the PanelDue simulator
	void makeView(unsigned int page = V::PageJob)
	{
		load_model_data_from_file("tests/object_model/cnc/model_move_3axis.json");
		load_model_data_from_file("tests/object_model/cnc/model_spindles.json");
		load_model_data_from_file("tests/object_model/cnc/model_tools.json");
		load_model_data_from_file("tests/object_model/cnc/model_state_idle.json");
		load_model_data(R"({"key":"state","flags":"v","result":{"nextTool":1}})");
		load_model_data(R"({"key":"move","flags":"v","result":{"speedFactor":1.0}})");
		State::SetCustomMacro(1, "LIGHT", "0:/macros/light.g");
		State::SetCustomMacro(2, "DUST", "0:/macros/dust.g");
		view = std::make_unique<V>();
		view->show();
		view->showPage(page);
		if (page == V::PageJob)
		{
			view->showSubTab(V::JobStatusTab);
		}
	}

	// The tests run no model event loop: after loading data, the presenter is told directly
	UI::CncHomePresenter& presenter() { return *view->getPresenter(); }

	void status(const char* s)
	{
		load_model_data(fmt::format(R"({{"key":"state","flags":"v","result":{{"status":"{:s}"}}}})", s));
		presenter().newStatus(OM::GetStatus());
	}

	void state(const std::string& json) // currentTool / nextTool
	{
		load_model_data(R"({"key":"state","flags":"v","result":)" + json + "}");
		presenter().newCurrentTool();
		presenter().newNextTool();
	}

	// A job running: bracket_v2.gcode at 37 %, 00:12:34 elapsed, 00:21:30 left, F 2400, S 18000 (j01)
	void startJob()
	{
		status("processing");
		load_model_data(
			R"({"key":"job","flags":"v","result":{"duration":754,"file":{"fileName":"0:/gcodes/bracket_v2.gcode","size":1000},"filePosition":370,"timesLeft":{"file":1290}}})");
		load_model_data(R"({"key":"move","flags":"v","result":{"currentMove":{"requestedSpeed":40}}})");
		presenter().newJobFileName(OM::GetJobName());
		presenter().newJobProgress();
		presenter().newJobDuration();
		presenter().newJobTimeLeft();
		presenter().newRequestedSpeed();
		spindle(18000, "forward");
		sent.clear();
	}

	void spindle(int active, const char* state, int current = -1)
	{
		load_model_data(fmt::format(
			R"({{"key":"spindles","flags":"v","result":[{{"active":{:d},"current":{:d},"max":24000,"min":0,"state":"{:s}"}}]}})",
			active,
			(current < 0) ? active : current,
			state));
		presenter().newSpindleData();
	}

	void speedFactor(float f)
	{
		load_model_data(fmt::format(R"({{"key":"move","flags":"v","result":{{"speedFactor":{:.2f}}}}})", f));
		presenter().newSpeedFactor();
	}

	static void message(M mode, uint32_t seq, const std::string& title, const std::string& text)
	{
		Messages::Message m;
		m.mode = mode;
		m.seq = seq;
		m.title = title;
		m.text = text;
		Messages::Process(m);
	}

	static std::string text()
	{
		std::string t;
		for (const std::string& l : Popup::Lines())
		{
			t += (t.empty() ? "" : " ") + l;
		}
		return t;
	}

	JobStatusPage& page() { return view->getJobStatusPage(); }
	std::string last() const { return sent.empty() ? std::string() : sent.back(); }

	std::unique_ptr<V> view;
	std::vector<std::string> sent;
};

// ---- Idle, job start -----------------------------------------------------------------------------

TEST_F(TestCncJobStatus, IdleLook)
{
	makeView();
	EXPECT_EQ(page().nameText(), "No job yet");
	EXPECT_EQ(page().resultText(), "-");
	EXPECT_EQ(page().elapsedText(), "-");
	EXPECT_EQ(page().leftText(), "-");
	EXPECT_EQ(page().rowLabel(0), "FEED WORK");
	EXPECT_EQ(page().valueText(1), "100%");
	EXPECT_EQ(page().actualText(1), "12000");
	EXPECT_FALSE(page().toolRowShown());
	EXPECT_TRUE(page().customShown(1));
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/job_status_idle.png")); // PanelDue p05_job_status
}

TEST_F(TestCncJobStatus, IdleRefusals)
{
	makeView();
	page().plusPressed(0);
	EXPECT_EQ(text(), "Overrides work only while a job is running.");
	page().valuePressed(1);
	EXPECT_EQ(text(), "Overrides work only while a job is running.");
	page().stepPressed(0, 2);
	EXPECT_EQ(text(), "Overrides work only while a job is running.");
	page().pausePressed();
	EXPECT_EQ(text(), "No job is running.");
	page().abortPressed();
	EXPECT_EQ(text(), "No job is running.");
	EXPECT_TRUE(sent.empty());
}

TEST_F(TestCncJobStatus, JobStartOpensJobStatus)
{
	makeView(V::PageControl);
	startJob();
	EXPECT_EQ(view->getPage(), V::PageJob);
	EXPECT_EQ(view->getSubTab(V::PageJob), (unsigned int)V::JobStatusTab);
	EXPECT_TRUE(page().inProgress());
	EXPECT_EQ(page().nameText(), "bracket_v2.gcode");
	EXPECT_EQ(page().resultText(), "37%");
	EXPECT_EQ(page().elapsedText(), "00:12:34");
	EXPECT_EQ(page().leftText(), "00:21:30");
	EXPECT_EQ(page().rowLabel(0), "FEED WORK  T1");
	EXPECT_EQ(page().rowLabel(1), "SPINDLE  T1");
	EXPECT_EQ(page().actualText(0), "2400");
	EXPECT_EQ(page().actualText(1), "18000");
	EXPECT_EQ(page().pauseText(), "PAUSE");
	EXPECT_TRUE(sent.empty());											   // overrides at 100 %: nothing sent
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/job_status_running.png")); // PanelDue j01_job_status_running
}

// ---- FEED WORK override (M220) --------------------------------------------------------------------

TEST_F(TestCncJobStatus, FeedOverride)
{
	makeView();
	startJob();
	EXPECT_EQ(page().stepIndex(0), 1u); // 5 %
	page().plusPressed(0);
	EXPECT_EQ(last(), "M220 S105\n");
	EXPECT_EQ(page().valueText(0), "105%");
	speedFactor(1.0f); // the old value, ours is on its way
	EXPECT_EQ(page().pct(0), 105);
	speedFactor(1.05f); // ours arrived
	page().stepPressed(0, 2);
	page().minusPressed(0);
	EXPECT_EQ(last(), "M220 S95\n");

	// Numpad, range 10 .. 200
	page().valuePressed(0);
	ASSERT_TRUE(Popup::IsNumpadOpen());
	EXPECT_EQ(Popup::NumpadTag(), "FEED");
	Popup::NumpadKey('2');
	Popup::NumpadKey('0');
	Popup::NumpadKey('0');
	Popup::PressOk();
	EXPECT_EQ(last(), "M220 S200\n");
	page().plusPressed(0);
	EXPECT_EQ(text(), "Feed override is at its limit (10 to 200 %).");

	// Changed elsewhere (DWC): becomes this tool's override
	speedFactor(2.0f);
	lv_tick_inc(JobStatusPage::EchoTime + 100);
	speedFactor(0.8f);
	EXPECT_EQ(page().pct(0), 80);
}

TEST_F(TestCncJobStatus, OverridesPerTool)
{
	makeView();
	startJob();
	page().plusPressed(0); // T1: 105 %
	speedFactor(1.05f);
	state(R"({"currentTool":2,"nextTool":2})");
	EXPECT_EQ(last(), "M220 S100\n"); // T2 has its own (100 %)
	EXPECT_EQ(page().rowLabel(0), "FEED WORK  T2");
	speedFactor(1.0f);
	state(R"({"currentTool":1,"nextTool":1})");
	EXPECT_EQ(last(), "M220 S105\n"); // back to T1's
}

// ---- SPINDLE override (emulated) ------------------------------------------------------------------

TEST_F(TestCncJobStatus, SpindleOverrideEmulation)
{
	makeView();
	startJob();
	page().plusPressed(1);
	EXPECT_EQ(last(), "M3 S18900\n"); // 18000 x 105 %
	spindle(18900, "forward");		  // the echo: not a new programmed speed
	EXPECT_EQ(sent.size(), 1u);
	spindle(12000, "forward"); // the job programs S12000
	EXPECT_EQ(last(), "M3 S12600\n");
	spindle(12600, "forward");
	spindle(12600, "reverse"); // M4 in the job, then a new speed
	spindle(10000, "reverse");
	EXPECT_EQ(last(), "M4 S10500\n");

	// Never above the spindle maximum
	page().valuePressed(1);
	Popup::NumpadKey('1');
	Popup::NumpadKey('5');
	Popup::NumpadKey('0');
	Popup::PressOk();
	EXPECT_EQ(last(), "M4 S15000\n");
	spindle(15000, "reverse");
	spindle(20000, "reverse");
	EXPECT_EQ(last(), "M4 S24000\n");

	// Paused: pause.g owns the spindle, nothing is learnt or sent
	spindle(24000, "reverse");
	sent.clear();
	status("paused");
	spindle(5000, "reverse");
	EXPECT_TRUE(sent.empty());
	lv_tick_inc(JobStatusPage::EchoTime + 100);
	status("processing"); // resumed (resume.g left 5000): back to the job's 20000 x 150 %
	EXPECT_EQ(last(), "M4 S24000\n");
}

// ---- PAUSE / RESUME / ABORT -----------------------------------------------------------------------

TEST_F(TestCncJobStatus, PauseResume)
{
	makeView();
	startJob();
	page().pausePressed();
	EXPECT_EQ(Popup::Lines(), Lines({"Do you want to PAUSE", "this job?"}));
	Popup::PressOk();
	EXPECT_EQ(last(), "M25\n");
	status("pausing");
	EXPECT_EQ(page().pauseText(), "PAUSING");
	page().pausePressed();
	EXPECT_EQ(text(), "Wait until the machine has finished pausing or resuming.");
	status("paused");
	EXPECT_EQ(page().pauseText(), "RESUME");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/job_status_paused.png")); // PanelDue j04_job_status_paused
	page().pausePressed();
	EXPECT_EQ(Popup::Lines(), Lines({"Do you want to RESUME", "this job?"}));
	Popup::PressOk();
	EXPECT_EQ(last(), "M24\n");
}

TEST_F(TestCncJobStatus, AbortWhilePaused)
{
	makeView();
	startJob();
	status("paused");
	page().abortPressed();
	EXPECT_EQ(Popup::Lines(), Lines({"Do you want to ABORT", "this job?"}));
	Popup::PressOk();
	EXPECT_EQ(last(), "M0\n");
	status("cancelling");
	EXPECT_EQ(page().abortText(), "ABORTING");
	page().abortPressed();
	EXPECT_EQ(text(), "The job is already being aborted.");
	Popup::Close();
	status("idle");
	EXPECT_FALSE(page().inProgress());
	EXPECT_EQ(page().resultText(), "ABORTED");
	EXPECT_EQ(page().nameText(), "bracket_v2.gcode");					   // kept after the job
	EXPECT_FALSE(Popup::IsOpen());										   // no JOB DONE
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/job_status_aborted.png")); // PanelDue j05_job_status_done
}

// ABORT while it resumes: no M25 into the resume; once it runs again it is paused, then cancelled.
// While it pauses: no second M25, M0 once paused.
TEST_F(TestCncJobStatus, AbortWhileResumingOrPausing)
{
	makeView();
	startJob();
	status("paused");
	status("resuming");
	sent.clear();
	page().abortPressed();
	Popup::PressOk();
	EXPECT_TRUE(sent.empty()); // nothing into the resume
	EXPECT_EQ(page().abortText(), "ABORTING");
	status("processing");
	EXPECT_EQ(last(), "M25\n"); // running again: pause it
	status("pausing");
	status("paused");
	EXPECT_EQ(last(), "M0\n");
	status("idle");
	EXPECT_EQ(page().resultText(), "ABORTED");

	startJob();
	status("pausing");
	sent.clear();
	page().abortPressed();
	Popup::PressOk();
	EXPECT_TRUE(sent.empty()); // the pause under way is enough
	status("paused");
	EXPECT_EQ(last(), "M0\n");
}

TEST_F(TestCncJobStatus, AbortWhileRunningPausesFirst)
{
	makeView();
	startJob();
	page().abortPressed();
	Popup::PressOk();
	EXPECT_EQ(last(), "M25\n");
	EXPECT_EQ(page().abortText(), "ABORTING");
	status("pausing");
	status("paused");
	EXPECT_EQ(last(), "M0\n"); // paused now: cancel
	status("idle");
	EXPECT_EQ(page().resultText(), "ABORTED");
}

TEST_F(TestCncJobStatus, AbortGivenUpWithoutPause)
{
	makeView();
	startJob();
	page().abortPressed();
	Popup::PressOk();
	lv_tick_inc(JobStatusPage::AbortPauseTimeout + 100);
	page().tick();
	EXPECT_EQ(text(), "The job did not pause within 60 s. It was not cancelled.");
	EXPECT_EQ(page().abortText(), "ABORT");
}

// ---- Job end -------------------------------------------------------------------------------------

TEST_F(TestCncJobStatus, JobDone)
{
	makeView();
	startJob();
	page().plusPressed(0);
	speedFactor(1.05f);
	sent.clear();
	status("idle");
	EXPECT_EQ(page().resultText(), "DONE");
	EXPECT_EQ(page().elapsedText(), "00:12:34");
	EXPECT_EQ(page().leftText(), "-");
	EXPECT_EQ(page().pct(0), 100);
	EXPECT_EQ(sent, std::vector<std::string>({"M220 S100\n"})); // overrides back to 100 %
	EXPECT_EQ(Popup::Title(), "JOB DONE");
	EXPECT_EQ(Popup::Lines(), Lines({"bracket_v2.gcode", "Time 00:12:34"}));
}

TEST_F(TestCncJobStatus, StopEndsAsAborted)
{
	makeView();
	startJob();
	presenter().emergencyStop();
	status("idle");
	EXPECT_EQ(page().resultText(), "ABORTED");
	EXPECT_FALSE(Popup::IsOpen());
}

// ---- Tool change row ------------------------------------------------------------------------------

TEST_F(TestCncJobStatus, ToolChangePrompt)
{
	makeView(V::PageControl);
	startJob();
	view->showPage(V::PageWcs);
	message(M::InfoConfirm, 7, "TOOL CHANGE", "Insert T2 Probe, then press OK");
	EXPECT_EQ(view->getPage(), V::PageJob); // JOB STATUS opens: the OK is there
	EXPECT_EQ(view->getSubTab(V::PageJob), (unsigned int)V::JobStatusTab);
	EXPECT_TRUE(page().toolRowShown());
	EXPECT_TRUE(page().toolOkShown());
	EXPECT_EQ(page().toolRowText(), "Insert T2 Probe, then press OK");
	EXPECT_FALSE(Popup::IsOpen());
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/job_status_tool_prompt.png")); // PanelDue m09_job_tool_change
	page().toolOkPressed();
	EXPECT_EQ(last(), "M292 P0 S7\n");
	EXPECT_FALSE(page().toolRowShown());

	// Answered elsewhere
	message(M::InfoConfirm, 8, "TOOL CHANGE", "Insert T3");
	message(M::None, 0, "", "");
	EXPECT_FALSE(page().toolRowShown());
}

TEST_F(TestCncJobStatus, ToolPromptWithoutJobIsAPopup)
{
	makeView();
	message(M::InfoConfirm, 4, "TOOL CHANGE", "Insert T2");
	EXPECT_FALSE(page().toolRowShown());
	EXPECT_EQ(Popup::Title(), "TOOL CHANGE");
	Popup::PressOk();
	EXPECT_EQ(last(), "M292 P0 S4\n");
}

TEST_F(TestCncJobStatus, LongPromptWrapsToTwoLines)
{
	makeView();
	startJob();
	message(M::ConfirmCancel, 9, "TOOL", "Insert the 6 mm two flute end mill T4 and tighten the collet, then press OK");
	EXPECT_TRUE(page().toolRowShown());
	EXPECT_EQ(page().toolRowText(), "Insert the 6 mm two flute end mill T4 and tighten the collet, then press OK");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/job_status_tool_prompt_long.png"));
}

TEST_F(TestCncJobStatus, AutomaticToolChangeRow)
{
	makeView();
	startJob();
	state(R"({"nextTool":3})");
	EXPECT_TRUE(page().toolRowShown());
	EXPECT_FALSE(page().toolOkShown());
	EXPECT_EQ(page().toolRowText(), "TOOL CHANGE  T1 >>> T3");
	state(R"({"currentTool":3})");
	EXPECT_FALSE(page().toolRowShown());
}

// ---- Errors during a job --------------------------------------------------------------------------

TEST_F(TestCncJobStatus, ErrorDuringJob)
{
	makeView();
	startJob();
	Messages::Response("Error: G1: target position outside machine limits", true);
	EXPECT_EQ(Popup::Title(), "ALERT !");
	EXPECT_EQ(text(), "Error: G1: target position outside machine limits");
}

TEST_F(TestCncJobStatus, NxtToolChangePrompts)
{
	makeView(V::PageControl);
	startJob();
	// Without NXT a title like "Install Tool" is an ordinary message
	message(M::ConfirmCancel, 20, "Install Tool", "Please install Tool 3 and confirm when ready.");
	EXPECT_FALSE(page().toolRowShown());
	EXPECT_TRUE(Popup::IsOpen());
	Popup::Close();

	load_model_data(R"({"key":"global","flags":"v","result":{"cncProbeMode":3,"nxtLoaded":true}})");
	lv_tick_inc(Settings::QuietTime + 10);
	Settings::Tick();
	ASSERT_TRUE(Nxt::Active());
	message(M::ConfirmCancel, 21, "Remove Tool", "Please remove Tool 1 and confirm when complete.");
	EXPECT_TRUE(page().toolRowShown());
	EXPECT_EQ(page().toolRowText(), "Please remove Tool 1 and confirm when complete.");
	page().toolOkPressed();
	EXPECT_EQ(last(), "M292 P0 S21\n");
	message(M::ConfirmCancel, 22, "Install Tool", "Please install Tool 3 and confirm when ready.");
	EXPECT_TRUE(page().toolRowShown());
	Nxt::Reset();
}
