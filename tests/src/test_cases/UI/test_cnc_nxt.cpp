/*
 * test_cnc_nxt.cpp
 *
 * WCS > PROBE with MillenniumOS NeXT (CncNxt, CncNxtProbePage): detection from the machine's globals,
 * PROBE MODE = NXT, the cycle grid, each cycle's parameters and the G-code sent, the refusals, CALIBRATE.
 */

#include "Debug.h"
#include "UI/Cnc/CncHomeView.h"
#include "UI/Cnc/CncLayout.h"
#include "UI/Cnc/CncMessages.h"
#include "UI/Cnc/CncNxt.h"
#include "UI/Cnc/CncPanel.h"
#include "UI/Cnc/CncPopup.h"
#include "UI/Cnc/CncSettings.h"
#include "UI/Cnc/CncSettingsPage.h"
#include "UI/Cnc/CncState.h"
#include "test_utils/UiTestSuite.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace UI::Cnc;
using Lines = std::vector<std::string>;
using C = Nxt::Cycle;

class TestCncNxt : public UiTestSuite
{
  protected:
	TestCncNxt()
	{
		ZoneScoped;
		lv_display_set_resolution(lv_display_get_default(), UI::CncLayout::ScreenW, UI::CncLayout::ScreenH);
		lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
		State::Reset();
		Messages::Reset();
		Panel::Reset();
		Nxt::Reset();
		State::SetSendHook([this](std::string_view g) { sent.emplace_back(g); });
	}

	~TestCncNxt() override
	{
		Popup::Close();
		view.reset();
		Nxt::Reset();
		Messages::Reset();
	}

	// WCS > PROBE with machine data; the machine's globals with NeXT (or without)
	void makeView(bool nxt = true, int probeMode = 3, int probeTool = 1)
	{
		load_model_data_from_file("tests/object_model/cnc/model_move_3axis.json");
		load_model_data_from_file("tests/object_model/cnc/model_spindles.json");
		load_model_data_from_file("tests/object_model/cnc/model_tools.json");
		load_model_data_from_file("tests/object_model/cnc/model_state_idle.json");
		view = std::make_unique<UI::CncHomeView>();
		view->show();
		State::StatusChanged(OM::PrinterStatus::idle);
		view->showPage(UI::CncHomeView::PageWcs);
		view->showSubTab(2);
		load_model_data(R"({"key":"global","flags":"v","result":{"cncProbeMode":)" + std::to_string(probeMode) +
						R"(,"nxtLoaded":)" + (nxt ? "true" : "false") + R"(,"nxtProbeToolID":)" +
						std::to_string(probeTool) +
						R"(,"nxtFeatureTouchProbe":true,"nxtFeatureToolSetter":true,"nxtFeatureFourthAxis":false}})");
		lv_tick_inc(Settings::QuietTime + 10);
		Settings::Tick();
		page().tick();
	}

	NxtProbePage& page() { return view->getNxtProbePage(); }
	std::string last() const { return sent.empty() ? std::string() : sent.back(); }
	static std::string text()
	{
		std::string t;
		for (const std::string& l : Popup::Lines())
		{
			t += (t.empty() ? "" : " ") + l;
		}
		return t;
	}

	std::unique_ptr<UI::CncHomeView> view;
	std::vector<std::string> sent;
};

TEST_F(TestCncNxt, DetectedAndShownInsteadOfTheStandardPage)
{
	makeView();
	EXPECT_TRUE(Nxt::Detected());
	EXPECT_TRUE(Nxt::Active());
	EXPECT_TRUE(page().active());
	EXPECT_EQ(Nxt::ProbeToolId(), 1);
	EXPECT_EQ(page().gridText(0), "BORE");
	EXPECT_EQ(page().gridText(10), "ROTATION");
	EXPECT_EQ(page().gridText(11), "CALIBRATE");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/nxt_cycles.png"));
}

TEST_F(TestCncNxt, NotActiveWithoutNxt)
{
	makeView(false, 3);
	EXPECT_FALSE(Nxt::Active());
	EXPECT_FALSE(page().active()); // the standard page (falls back to AUTO)
	EXPECT_EQ(State::GetProbeMode(), State::ProbeMode::Auto);
}

TEST_F(TestCncNxt, NotActiveInSemiAuto)
{
	makeView(true, 1);
	EXPECT_TRUE(Nxt::Detected());
	EXPECT_FALSE(page().active()); // SEMI-AUTO: the standard page
}

TEST_F(TestCncNxt, ProbeModeChoiceNeedsNxt)
{
	makeView(false, 0);
	view->showPage(UI::CncHomeView::PageSystem);
	view->showSubTab(UI::CncHomeView::SystemSettingsTab);
	view->getSettingsPage().machineTilePressed(SettingsPage::MProbeMode);
	EXPECT_EQ(Popup::Title(), "PROBE MODE");
	Popup::PressChoice(3); // greyed: NeXT is not on the machine
	Popup::PressOk();
	EXPECT_NE(Settings::Get().probeMode, 3);
	Popup::Close();
	Nxt::SetGlobal("nxtLoaded", "true");
	view->getSettingsPage().machineTilePressed(SettingsPage::MProbeMode);
	Popup::PressChoice(3);
	Popup::PressOk();
	EXPECT_EQ(Settings::Get().probeMode, 3);
}

TEST_F(TestCncNxt, BoreParametersAndRun)
{
	makeView();
	page().gridPressed(0);
	ASSERT_TRUE(page().inCycle());
	EXPECT_EQ(page().paramText(0), "DIAMETER 25.000");
	EXPECT_EQ(page().paramText(3), "SAMPLES AUTO");
	EXPECT_EQ(page().paramText(4), "SAVE TO G54");
	EXPECT_EQ(page().paramText(5), "ROTATION ASK");
	EXPECT_EQ(page().paramText(6), "");
	EXPECT_EQ(page().runText(), "RUN BORE");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/nxt_bore.png"));

	page().paramPressed(0); // DIAMETER
	Popup::NumpadKey('C');
	for (char c : std::string("12.7"))
	{
		Popup::NumpadKey(c);
	}
	Popup::PressOk();
	page().paramPressed(4); // SAVE TO
	Popup::PressChoice(1);
	Popup::PressOk();
	page().paramPressed(5); // ROTATION
	Popup::PressChoice(1); // APPLY
	Popup::PressOk();
	page().runPressed();
	EXPECT_EQ(last(), "G6500 U2 D12.700 L5.000 O2.000 Q1\n");
	page().backPressed();
	EXPECT_FALSE(page().inCycle());
}

TEST_F(TestCncNxt, CommandsOfAllCycles)
{
	Nxt::Values& v = Nxt::Get();
	v = Nxt::Values();
	EXPECT_EQ(Nxt::Command(C::Boss), "G6501 U1 D25.000 L5.000 C5.000 O2.000 Q0\n");
	EXPECT_EQ(Nxt::Command(C::RectPocket), "G6502 U1 W50.000 H30.000 L5.000 C5.000 O2.000 Q0\n");
	EXPECT_EQ(Nxt::Command(C::RectBlock), "G6503 U1 W50.000 H30.000 L5.000 C5.000 O2.000 Q0\n");
	v.axis = 1;
	v.samples = 3;
	EXPECT_EQ(Nxt::Command(C::Web), "G6504 U1 N1 W50.000 L5.000 C5.000 O2.000 R3\n");
	EXPECT_EQ(Nxt::Command(C::Pocket), "G6505 U1 N1 W50.000 L5.000 C5.000 O2.000 R3\n");
	v.samples = 0;
	v.corner = 0; // TOP LEFT: outside the stock at -X +Y, probing toward +X -Y
	EXPECT_EQ(Nxt::Command(C::OutCorner),
			  "M5000\nG6508 U1 L5.000 X{global.nxtAbsPos[0]+10.000} Y{global.nxtAbsPos[1]-10.000} C5.000\n");
	v.corner = 3; // BOTTOM RIGHT inside a pocket: toward +X -Y
	EXPECT_EQ(Nxt::Command(C::InCorner),
			  "M5000\nG6509 U1 L5.000 X{global.nxtAbsPos[0]+10.000} Y{global.nxtAbsPos[1]-10.000} C5.000 O2.000\n");
	EXPECT_EQ(Nxt::Command(C::Surface), "M5000 P1 I2\nG6510 U1 Z{global.nxtAbsPos-10.000}\n");
	EXPECT_EQ(Nxt::Command(C::ViseCorner), "G6520 U1 L5.000 C5.000 O2.000\n");
	v.axis = 0;
	EXPECT_EQ(Nxt::Command(C::Rotation), "G6506 U1 N0 D1 S20.000 L5.000 O2.000 Q0\n");
	v.saveTo = 2;
	EXPECT_EQ(Nxt::Command(C::RotaryCentre), "M4910\nM4807 W56\n");
}

TEST_F(TestCncNxt, CornerAndSurfaceParameters)
{
	makeView();
	page().gridPressed(6); // OUT CORNER
	EXPECT_EQ(page().paramText(0), "CORNER TOP LEFT");
	EXPECT_EQ(page().paramText(1), "TO CORNER 10.000");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/nxt_corner.png"));
	page().backPressed();
	page().gridPressed(8); // SURFACE
	EXPECT_EQ(page().paramText(0), "AXIS Z");
	EXPECT_EQ(page().paramText(1), "DISTANCE -10.000");
	page().paramPressed(1);
	Popup::NumpadKey('C');
	Popup::NumpadKey('5');
	Popup::PressOk(); // positive: up
	page().runPressed();
	EXPECT_EQ(last(), "M5000 P1 I2\nG6510 U1 Z{global.nxtAbsPos+5.000}\n");
}

TEST_F(TestCncNxt, Refusals)
{
	makeView(true, 3, 49); // the probe is T49, T1 is loaded
	page().gridPressed(0);
	page().runPressed();
	EXPECT_EQ(text(), "Select the touch probe first (T49).");
	EXPECT_TRUE(sent.empty());
	Popup::Close();
	Nxt::SetGlobal("nxtProbeToolID", "1");
	State::SetProbeValue(1000); // triggered
	page().runPressed();
	EXPECT_EQ(text(), "Probe is closed (triggered). Check the probe and its wiring.");
	Popup::Close();
	State::SetProbeValue(0);
	State::StatusChanged(OM::PrinterStatus::busy);
	page().runPressed();
	EXPECT_EQ(text(), "Wait until the machine is idle.");
	EXPECT_TRUE(sent.empty());
}

TEST_F(TestCncNxt, Calibrate)
{
	makeView();
	page().gridPressed(11);
	EXPECT_EQ(Popup::Title(), "CALIBRATE");
	Popup::PressChoice(1); // PROBE REPEAT
	Popup::PressOk();
	EXPECT_EQ(Popup::Title(), "PROBE REPEAT");
	Popup::PressOk();
	EXPECT_EQ(last(), "M6523 B0 C10\n");

	// The rotary with the MOS Fourth Axis macros and an A axis
	auto items = Nxt::CalibrationItems();
	EXPECT_EQ(items.size(), 3u);
	Nxt::SetGlobal("nxtFeatureFourthAxis", "true");
	State::SetAxisCount(4);
	State::SetAxisLetter(3, 'A');
	items = Nxt::CalibrationItems();
	ASSERT_EQ(items.size(), 6u);
	EXPECT_EQ(items[4].gcode, "M4910\n");
	EXPECT_EQ(items[5].gcode, "M4807 W54\n");
}

TEST_F(TestCncNxt, SignalFollowsTheTouchProbe)
{
	makeView();
	// K0 open (toolsetter), K1 triggered (NeXT's touch probe)
	load_model_data(R"({"key":"sensors","flags":"v","result":{"probes":[{"value":[0]},{"value":[1000]}]}})");
	Nxt::SetGlobal("nxtTouchProbeID", "1");
	EXPECT_EQ(Nxt::UiProbeIndex(), 1);
	view->showPage(UI::CncHomeView::PageControl);
	Settings::Edit([](Settings::Values& x) { x.probeMode = 0; }, 0); // AUTO: K0
	page().tick();
	EXPECT_FALSE(State::ProbeClosed());
	Settings::Edit([](Settings::Values& x) { x.probeMode = 3; }, 0); // NXT: K1
	page().tick();
	EXPECT_TRUE(State::ProbeClosed());
}
