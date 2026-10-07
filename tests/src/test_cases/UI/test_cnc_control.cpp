/*
 * test_cnc_control.cpp
 *
 * CONTROL page of the CNC UI: behaviour (the G-code it sends, what it refuses) and looks
 * (screenshots, compare with the PanelDue CNC UI references x 1.25).
 */

#include "Debug.h"
#include "UI/Cnc/CncHomeView.h"
#include "UI/Cnc/CncLayout.h"
#include "UI/Cnc/CncPopup.h"
#include "UI/Cnc/CncSettings.h"
#include "UI/Cnc/CncState.h"
#include "test_utils/UiTestSuite.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace UI::Cnc;

class TestCncControl : public UiTestSuite
{
  protected:
	TestCncControl()
	{
		ZoneScoped;
		lv_display_set_resolution(lv_display_get_default(), UI::CncLayout::ScreenW, UI::CncLayout::ScreenH);
		lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
		State::Reset();
		State::SetSendHook([this](std::string_view g) { sent.emplace_back(g); });
	}

	~TestCncControl() override { Popup::Close(); }

	void loadMachine(bool fourAxes = false)
	{
		load_model_data_from_file(fourAxes ? "tests/object_model/cnc/model_move_4axis.json"
										   : "tests/object_model/cnc/model_move_3axis.json");
		load_model_data_from_file("tests/object_model/cnc/model_spindles.json");
		load_model_data_from_file("tests/object_model/cnc/model_tools.json");
		load_model_data_from_file("tests/object_model/cnc/model_state_idle.json");
	}

	// The frame with machine data, idle
	std::unique_ptr<UI::CncHomeView> makeView(bool fourAxes = false)
	{
		loadMachine(fourAxes);
		auto v = std::make_unique<UI::CncHomeView>();
		v->show();
		State::StatusChanged(OM::PrinterStatus::idle);
		return v;
	}

	std::string last() const { return sent.empty() ? std::string() : sent.back(); }

	std::vector<std::string> sent;
};

// ---- Looks ------------------------------------------------------------------------------------

TEST_F(TestCncControl, Idle3Axis)
{
	auto view = makeView();
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/control_idle_3axis.png"));
}

TEST_F(TestCncControl, Idle4Axis)
{
	auto view = makeView(true);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/control_idle_4axis.png"));
}

TEST_F(TestCncControl, AxisStepSlowSelected)
{
	auto view = makeView();
	auto& page = view->getControlPage();
	page.axisPressed(1);
	page.stepPressed(3);
	page.feedModePressed();
	EXPECT_TRUE(page.axisChosen());
	EXPECT_TRUE(page.slowMode());
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/control_y_10_slow.png"));
}

TEST_F(TestCncControl, NotHomedColours)
{
	auto view = makeView();
	State::SetHomed(2, false); // Z not homed: HOME Z and ALL dark orange
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/control_z_not_homed.png"));
}

TEST_F(TestCncControl, JobRunningLocked)
{
	auto view = makeView();
	State::StatusChanged(OM::PrinterStatus::processing);
	EXPECT_TRUE(State::MachineBusy());
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/control_job_running.png"));

	view->getControlPage().axisPressed(0);
	EXPECT_FALSE(view->getControlPage().axisChosen());
	EXPECT_EQ(Popup::Title(), "ALERT !");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/control_job_running_refused.png"));
}

TEST_F(TestCncControl, CustomAndAuxOn)
{
	auto view = makeView();
	State::SetCustomMacro(0, "VACUUM", "0:/macros/vacuum.g");
	State::SetAuxLabel(false);
	view->getControlPage().auxPressed(); // COOLANT on: accent outline
	EXPECT_EQ(last(), "M98 P\"0:/macros/aux_on.g\"\n");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/control_custom_coolant_on.png"));
}

TEST_F(TestCncControl, HomeConfirmPopup)
{
	auto view = makeView();
	view->getControlPage().homePressed(1); // HOME Z
	EXPECT_EQ(Popup::Title(), "HOME Z");
	EXPECT_TRUE(sent.empty()); // nothing before the check mark
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/control_home_z_confirm.png"));
	Popup::PressOk();
	EXPECT_EQ(last(), "G28 Z\n");
	EXPECT_FALSE(Popup::IsOpen());
}

// ---- Behaviour --------------------------------------------------------------------------------

TEST_F(TestCncControl, MoveNeedsAxis)
{
	auto view = makeView();
	view->getControlPage().movePressed(1);
	EXPECT_TRUE(sent.empty());
	ASSERT_TRUE(Popup::IsOpen());
	EXPECT_EQ(Popup::Lines().at(0), "Select an axis first.");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/control_select_axis_alert.png"));
}

TEST_F(TestCncControl, JogRapidAndSlow)
{
	auto view = makeView();
	auto& page = view->getControlPage();
	page.axisPressed(0);
	page.movePressed(1);
	EXPECT_EQ(last(), "G91\nG0 X0.100\nG90\n");
	page.stepPressed(0);
	page.movePressed(-1);
	EXPECT_EQ(last(), "G91\nG0 X-0.010\nG90\n");
	page.feedModePressed(); // SLOW: G1 at the jog feed
	page.setJogFeed(1200);
	page.axisPressed(2);
	page.stepPressed(2);
	page.movePressed(-1);
	EXPECT_EQ(last(), "G91\nG1 Z-1.000 F1200\nG90\n");
}

TEST_F(TestCncControl, LargeStepNotQueued)
{
	auto view = makeView();
	auto& page = view->getControlPage();
	lv_tick_inc(1300); // the machine data just arrived counts as a move: let it settle (1.2 s)
	page.axisPressed(0);
	page.stepPressed(4); // 100
	page.movePressed(1);
	EXPECT_EQ(sent.size(), 1u);
	page.movePressed(1); // still moving: refused
	EXPECT_EQ(sent.size(), 1u);
	EXPECT_EQ(Popup::Lines().at(0), "Wait until the last move has finished.");
}

TEST_F(TestCncControl, IdleTimeoutClearsSelection)
{
	auto view = makeView();
	auto& page = view->getControlPage();
	page.axisPressed(1);
	page.stepPressed(3);
	lv_tick_inc(5000 + 100); // SETTINGS > IDLE TIMEOUT default
	page.tick();
	EXPECT_FALSE(page.axisChosen());
	EXPECT_EQ(page.currentStep(), ControlPage::DefaultStep);
}

TEST_F(TestCncControl, SafeZNeedsHoming)
{
	auto view = makeView();
	State::SetHomed(0, false);
	view->getControlPage().safeZPressed();
	EXPECT_TRUE(sent.empty());
	EXPECT_EQ(Popup::Lines().at(0), "Home X, Y and Z first.");
	Popup::Close();
	State::SetHomed(0, true);
	view->getControlPage().safeZPressed();
	EXPECT_EQ(last(), "M98 P\"0:/macros/safe_z.g\"\n");
	view->getControlPage().xy0Pressed();
	EXPECT_EQ(last(), "M98 P\"0:/macros/goto_xy0.g\"\n");
}

TEST_F(TestCncControl, PausedAllowsJogButNotHome)
{
	auto view = makeView();
	auto& page = view->getControlPage();
	State::StatusChanged(OM::PrinterStatus::processing);
	State::StatusChanged(OM::PrinterStatus::paused);
	page.axisPressed(2);
	page.movePressed(1);
	EXPECT_EQ(last(), "G91\nG0 Z0.100\nG90\n");
	page.homePressed(0);
	EXPECT_EQ(Popup::Lines().at(0), "HOME is locked until the job ends.");
}

TEST_F(TestCncControl, ConfirmMacroWithBang)
{
	auto view = makeView();
	State::SetCustomMacro(0, "PARK", "0:/macros/!park.g");
	view->getControlPage().customPressed();
	EXPECT_TRUE(sent.empty());
	EXPECT_EQ(Popup::Title(), "PARK");
	Popup::PressOk();
	EXPECT_EQ(last(), "M98 P\"0:/macros/!park.g\"\n");
}

TEST_F(TestCncControl, ZeroLockedDuringJob)
{
	auto view = makeView();
	State::StatusChanged(OM::PrinterStatus::paused); // a paused job counts
	State::StatusChanged(OM::PrinterStatus::processing);
	EXPECT_TRUE(State::JobActive());
}

// ---- DRO tiles: spindle numpad, spindle on / standby, SLOW jog feed ----------------------------

TEST_F(TestCncControl, SpindleNumpadLooks)
{
	auto view = makeView();
	view->getControlPage().spindleTilePressed();
	ASSERT_TRUE(Popup::IsNumpadOpen());
	EXPECT_EQ(Popup::NumpadTag(), "S");
	EXPECT_EQ(Popup::NumpadValue(), "12000"); // the set speed
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/control_numpad_spindle.png"));
}

TEST_F(TestCncControl, SpindleNumpadNewSpeedWhileRunning)
{
	auto view = makeView();
	view->getControlPage().spindleTilePressed();
	for (char c : std::string("8000"))
	{
		Popup::NumpadKey(c); // the first key replaces the shown value
	}
	EXPECT_EQ(Popup::NumpadValue(), "8000");
	Popup::PressOk();
	EXPECT_FALSE(Popup::IsOpen());
	EXPECT_EQ(last(), "M3 S8000\n");
	EXPECT_EQ(State::SpindleRpm(), 8000);
}

TEST_F(TestCncControl, SpindleNumpadWhileStoppedOnlySetsSpeed)
{
	auto view = makeView();
	State::SetSpindleFromMachine(0, false, false, 24000); // standby: the set speed stays
	EXPECT_EQ(State::SpindleRpm(), 12000);
	view->getControlPage().spindleTilePressed();
	for (char c : std::string("9500"))
	{
		Popup::NumpadKey(c);
	}
	Popup::PressOk();
	EXPECT_TRUE(sent.empty());
	EXPECT_EQ(State::SpindleRpm(), 9500);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/control_spindle_standby_9500.png"));
}

TEST_F(TestCncControl, NumpadKeyRules)
{
	auto view = makeView();
	view->getControlPage().spindleTilePressed();
	Popup::NumpadKey('0');
	Popup::NumpadKey('0'); // no leading zeros
	Popup::NumpadKey('7');
	EXPECT_EQ(Popup::NumpadValue(), "7");
	Popup::NumpadKey('.'); // rpm: no decimal point, no minus
	Popup::NumpadKey('-');
	EXPECT_EQ(Popup::NumpadValue(), "7");
	for (char c : std::string("1234567890"))
	{
		Popup::NumpadKey(c); // up to 9 characters
	}
	EXPECT_EQ(Popup::NumpadValue(), "712345678");
	Popup::NumpadKey('\b');
	Popup::NumpadKey('\b');
	EXPECT_EQ(Popup::NumpadValue(), "7123456");
}

TEST_F(TestCncControl, NumpadOutOfRangeAlertBackToNumpad)
{
	auto view = makeView();
	view->getControlPage().spindleTilePressed();
	for (char c : std::string("30000"))
	{
		Popup::NumpadKey(c);
	}
	Popup::PressOk(); // above the spindle's 24000 rpm
	EXPECT_EQ(Popup::Title(), "ALERT !");
	EXPECT_EQ(Popup::Lines(), std::vector<std::string>({"Enter a value from 1 to 24000."}));
	EXPECT_TRUE(sent.empty());
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/control_numpad_range_alert.png"));

	Popup::PressCancel(); // X: the numpad again, typed value kept
	ASSERT_TRUE(Popup::IsNumpadOpen());
	EXPECT_EQ(Popup::NumpadValue(), "30000");
	Popup::NumpadKey('\b');
	Popup::PressOk();
	EXPECT_EQ(last(), "M3 S3000\n");

	// X on the numpad: nothing sent
	sent.clear();
	view->getControlPage().spindleTilePressed();
	Popup::NumpadKey('5');
	Popup::PressCancel();
	EXPECT_FALSE(Popup::IsOpen());
	EXPECT_TRUE(sent.empty());
}

TEST_F(TestCncControl, ToolTileSpindleOnStandby)
{
	auto view = makeView();
	auto& page = view->getControlPage();
	page.toolTilePressed(); // running: standby
	EXPECT_EQ(last(), "M5\n");

	State::SetSpindleFromMachine(0, false, false, 24000);
	page.toolTilePressed(); // stopped: on again at the set speed
	EXPECT_EQ(last(), "M3 S12000\n");

	State::SetSpindleFromMachine(0, false, true, 24000); // last ran in reverse
	page.toolTilePressed();
	EXPECT_EQ(last(), "M4 S12000\n");
}

TEST_F(TestCncControl, ToolTileAsksForSpeedFirst)
{
	auto view = makeView();
	State::Reset(); // no speed set yet, spindle stopped
	State::SetSendHook([this](std::string_view g) { sent.emplace_back(g); });
	auto& page = view->getControlPage();
	page.toolTilePressed();
	ASSERT_TRUE(Popup::IsNumpadOpen());
	EXPECT_EQ(Popup::NumpadValue(), "0");
	Popup::PressOk();																		  // 0 is below the range
	EXPECT_EQ(Popup::Lines(), std::vector<std::string>({"Enter a value from 1 to 100000."})); // max not reported
	Popup::PressCancel();
	for (char c : std::string("5000"))
	{
		Popup::NumpadKey(c);
	}
	Popup::PressOk();
	EXPECT_EQ(last(), "M3 S5000\n");
}

TEST_F(TestCncControl, SpindleTilesLockedDuringJob)
{
	auto view = makeView();
	State::StatusChanged(OM::PrinterStatus::processing);
	auto& page = view->getControlPage();
	page.toolTilePressed();
	EXPECT_EQ(Popup::Lines(), std::vector<std::string>({"Locked while a job is running."}));
	page.spindleTilePressed();
	EXPECT_EQ(Popup::Lines(),
			  std::vector<std::string>({"Locked while a job is running.", "Use the spindle override on JOB."}));
	EXPECT_TRUE(sent.empty());

	page.feedTilePressed(); // the jog feed is a setting: not locked
	EXPECT_TRUE(Popup::IsChooseOpen());
}

TEST_F(TestCncControl, FeedTileJogFeedPresets)
{
	auto view = makeView();
	auto& page = view->getControlPage();
	// The jog feed is a machine setting: the machine's settings have arrived (global.cnc*)
	load_model_data(R"({"key":"global","flags":"v","result":{"cncJogFeed":300}})");
	lv_tick_inc(Settings::QuietTime + 10);
	Settings::Tick();
	page.feedTilePressed();
	ASSERT_TRUE(Popup::IsChooseOpen());
	EXPECT_EQ(Popup::Title(), "SLOW JOG FEEDRATE");
	EXPECT_EQ(Popup::ChoiceSelected(), 1u); // 300 mm/min
	Popup::PressChoice(4);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/control_jog_feed_choose.png"));
	Popup::PressOk();
	EXPECT_EQ(State::JogFeed(), 1000u);
	EXPECT_TRUE(page.slowMode()); // the feed is for SLOW moves

	page.axisPressed(0);
	page.movePressed(1);
	EXPECT_EQ(last(), "G91\nG1 X0.100 F1000\nG90\n");

	// X: nothing changes
	page.feedTilePressed();
	EXPECT_EQ(Popup::ChoiceSelected(), 4u);
	Popup::PressChoice(0);
	Popup::PressCancel();
	EXPECT_EQ(State::JogFeed(), 1000u);
}
