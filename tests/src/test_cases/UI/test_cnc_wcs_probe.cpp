/*
 * test_cnc_wcs_probe.cpp
 *
 * WCS > PROBE of the CNC UI and the machine messages (M291): the probe macros it calls, the auto
 * probe progress popup, the jog prompt of semi-automatic / manual probing, the message popups, and
 * screenshots (compare with the PanelDue CNC UI references x 1.25).
 */

#include "Debug.h"
#include "UI/Cnc/CncHomeView.h"
#include "UI/Cnc/CncLayout.h"
#include "UI/Cnc/CncMessages.h"
#include "UI/Cnc/CncPopup.h"
#include "UI/Cnc/CncState.h"
#include "test_utils/UiTestSuite.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace UI::Cnc;
using Lines = std::vector<std::string>;
using P = UI::Cnc::WcsProbePage;
using M = Messages::Mode;

class TestCncWcsProbe : public UiTestSuite
{
  protected:
	TestCncWcsProbe()
	{
		ZoneScoped;
		lv_display_set_resolution(lv_display_get_default(), UI::CncLayout::ScreenW, UI::CncLayout::ScreenH);
		lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
		State::Reset();
		Messages::Reset();
		State::SetSendHook([this](std::string_view g) { sent.emplace_back(g); });
	}

	~TestCncWcsProbe() override
	{
		Popup::Close();
		Messages::Reset();
	}

	// The frame on WCS > PROBE with machine data, idle
	std::unique_ptr<UI::CncHomeView> makeView(State::ProbeMode mode = State::ProbeMode::Auto)
	{
		load_model_data_from_file("tests/object_model/cnc/model_move_3axis.json");
		load_model_data_from_file("tests/object_model/cnc/model_spindles.json");
		load_model_data_from_file("tests/object_model/cnc/model_tools.json");
		load_model_data_from_file("tests/object_model/cnc/model_state_idle.json");
		auto v = std::make_unique<UI::CncHomeView>();
		v->show();
		v->showPage(UI::CncHomeView::PageWcs);
		v->showSubTab(2);
		State::StatusChanged(OM::PrinterStatus::idle);
		State::SetProbeMode(mode);
		return v;
	}

	static void message(M mode, uint32_t seq, const std::string& title, const std::string& text, uint32_t controls = 0)
	{
		Messages::Message m;
		m.mode = mode;
		m.seq = seq;
		m.title = title;
		m.text = text;
		m.controls = controls;
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

	std::string last() const { return sent.empty() ? std::string() : sent.back(); }

	std::vector<std::string> sent;
};

// ---- Looks of the three modes ------------------------------------------------------------------

TEST_F(TestCncWcsProbe, SemiAutoLooks)
{
	auto view = makeView(State::ProbeMode::Semi);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_probe_semi.png")); // PanelDue p04_wcs_probe
}

TEST_F(TestCncWcsProbe, ManualLooks)
{
	auto view = makeView(State::ProbeMode::Manual);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_probe_manual.png")); // PanelDue m08_probe_manual
}

TEST_F(TestCncWcsProbe, ProbeTileShowsTheSignal)
{
	auto view = makeView();
	State::SetProbeValue(0);
	State::SetProbeValue(1000);
	EXPECT_TRUE(State::ProbeClosed());
	view->getWcsProbePage().originPressed(10); // BOSS: outline style
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_probe_closed_boss.png"));
}

// ---- Parameters --------------------------------------------------------------------------------

TEST_F(TestCncWcsProbe, StockAskedXThenY)
{
	auto view = makeView();
	auto& page = view->getWcsProbePage();
	page.paramPressed(P::PStock);
	EXPECT_EQ(Popup::NumpadTag(), "STOCK X");
	for (char c : std::string("150"))
	{
		Popup::NumpadKey(c);
	}
	Popup::PressOk();
	EXPECT_EQ(Popup::NumpadTag(), "STOCK Y");
	for (char c : std::string("80.5"))
	{
		Popup::NumpadKey(c);
	}
	Popup::PressOk();
	EXPECT_EQ(page.tileText(P::PStock), "150x80.5");

	page.paramPressed(P::PTip); // 0.1 .. 20
	for (char c : std::string("25"))
	{
		Popup::NumpadKey(c);
	}
	Popup::PressOk();
	EXPECT_EQ(Popup::Lines(), Lines({"Enter a value from 0.100 to 20.000."}));
}

// ---- RUN: AUTO and its progress popup ----------------------------------------------------------

TEST_F(TestCncWcsProbe, RunAutoAndProgress)
{
	auto view = makeView();
	auto& page = view->getWcsProbePage();
	page.originPressed(0); // top left corner
	page.paramPressed(P::PStoreTo);
	Popup::PressChoice(1);
	Popup::PressOk(); // G55
	page.runPressed();
	EXPECT_EQ(last(), "M98 P\"0:/sys/probe/probe_auto.g\" O\"TL\" D2.000 C5.000 S10.000 Z3.000 L100.000 B60.000 W2\n");
	ASSERT_TRUE(page.autoRunning());
	EXPECT_EQ(page.autoHead(), "Top left corner");
	EXPECT_EQ(page.autoStatus(), "Probing ...");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_probe_auto_start.png"));

	// The macro's messages fill in the popup
	message(M::Info, 3, "PROBE:L 1/2", "Probing toward +X ...");
	EXPECT_EQ(page.autoHead(), "Side 1 of 2");
	EXPECT_EQ(page.autoStatus(), "Probing toward +X ...");
	message(M::Info, 4, "PROBE=X", "212.415");
	EXPECT_EQ(page.autoRowValue(0), "212.415");
	EXPECT_EQ(page.autoRowValue(1), "-");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_probe_auto_progress.png"));

	// Busy, then idle: the macro ended
	State::StatusChanged(OM::PrinterStatus::busy);
	EXPECT_TRUE(page.autoRunning());
	State::StatusChanged(OM::PrinterStatus::idle);
	EXPECT_FALSE(page.autoRunning());
}

// ---- OUTSIDE | INSIDE ----------------------------------------------------------------------------

TEST_F(TestCncWcsProbe, InsideAddsI1ForCornersAndEdges)
{
	auto view = makeView();
	auto& page = view->getWcsProbePage();
	page.originPressed(0);
	EXPECT_FALSE(page.inside());
	EXPECT_FALSE(page.sideLocked());
	page.runPressed();
	EXPECT_EQ(last(), "M98 P\"0:/sys/probe/probe_auto.g\" O\"TL\" D2.000 C5.000 S10.000 Z3.000 L100.000 B60.000 W1\n");
	State::StatusChanged(OM::PrinterStatus::busy);
	State::StatusChanged(OM::PrinterStatus::idle);

	page.sidePressed();
	EXPECT_TRUE(page.inside());
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_probe_inside_corner.png"));
	page.runPressed();
	EXPECT_EQ(last(), "M98 P\"0:/sys/probe/probe_auto.g\" O\"TL\" D2.000 C5.000 S10.000 Z3.000 L100.000 B60.000 W1 I1\n");
	State::StatusChanged(OM::PrinterStatus::busy);
	State::StatusChanged(OM::PrinterStatus::idle);
}

TEST_F(TestCncWcsProbe, SideSwitchPicksTheTouchedHalf)
{
	auto view = makeView();
	auto& page = view->getWcsProbePage();
	page.originPressed(0);
	lv_test_mouse_click_at(202, 785); // INSIDE
	EXPECT_TRUE(page.inside());
	lv_test_mouse_click_at(202, 785); // INSIDE again: stays
	EXPECT_TRUE(page.inside());
	lv_test_mouse_click_at(73, 785); // OUTSIDE
	EXPECT_FALSE(page.inside());
	lv_test_mouse_click_at(73, 785);
	EXPECT_FALSE(page.inside());
}

TEST_F(TestCncWcsProbe, BoreAndBossForceTheSideAndRememberTheChoice)
{
	auto view = makeView();
	auto& page = view->getWcsProbePage();
	page.originPressed(0);
	page.sidePressed(); // INSIDE

	page.originPressed(10); // BOSS: always outside
	EXPECT_TRUE(page.sideLocked());
	EXPECT_FALSE(page.inside());
	page.sidePressed(); // ignored
	EXPECT_FALSE(page.inside());

	page.originPressed(9); // BORE: always inside, but the macro knows from the origin
	EXPECT_TRUE(page.sideLocked());
	EXPECT_TRUE(page.inside());
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_probe_bore_locked.png"));
	page.runPressed();
	EXPECT_EQ(last().find(" I1"), std::string::npos);
	State::StatusChanged(OM::PrinterStatus::busy);
	State::StatusChanged(OM::PrinterStatus::idle);

	page.originPressed(11); // Z TOP
	EXPECT_TRUE(page.sideLocked());
	page.originPressed(1); // back to an edge: the earlier choice returns
	EXPECT_FALSE(page.sideLocked());
	EXPECT_TRUE(page.inside());
}

TEST_F(TestCncWcsProbe, AutoPopupClosesWhenTheMacroNeverStarted)
{
	auto view = makeView();
	auto& page = view->getWcsProbePage();
	page.runPressed();
	page.tick();
	EXPECT_TRUE(page.autoRunning());
	lv_tick_inc(P::AutoStartTimeout + 100);
	page.tick();
	EXPECT_FALSE(page.autoRunning());
}

TEST_F(TestCncWcsProbe, AutoErrorEndsThePopup)
{
	auto view = makeView();
	auto& page = view->getWcsProbePage();
	page.runPressed();
	Messages::Response("Error: G38.2: probe not triggered", true);
	EXPECT_FALSE(page.autoRunning());
	EXPECT_EQ(Popup::Title(), "ALERT !");
	EXPECT_EQ(text(), "Error: G38.2: probe not triggered");
}

TEST_F(TestCncWcsProbe, RunRefusals)
{
	auto view = makeView();
	auto& page = view->getWcsProbePage();
	State::SetProbeValue(1000); // closed
	page.runPressed();
	EXPECT_EQ(Popup::Lines(), Lines({"Probe is closed (triggered).", "Check the probe and its wiring."}));
	State::SetProbeMode(State::ProbeMode::Manual); // the cutter is the probe: no signal to check
	sent.clear();
	page.runPressed();
	EXPECT_EQ(last(), "M98 P\"0:/sys/probe/probe_manual.g\" O\"TL\" D6.000 W1\n");

	State::StatusChanged(OM::PrinterStatus::busy);
	page.runPressed();
	EXPECT_EQ(text(), "Wait until the machine is idle.");
	State::StatusChanged(OM::PrinterStatus::idle);
	State::SetHomed(0, false);
	page.runPressed();
	EXPECT_EQ(text(), "Home X, Y and Z first.");
	State::StatusChanged(OM::PrinterStatus::paused);
	page.runPressed();
	EXPECT_EQ(text(), "Locked while a job is running or paused.");
}

// ---- SEMI-AUTO / MANUAL: the jog prompt --------------------------------------------------------

TEST_F(TestCncWcsProbe, SemiAutoJogPrompt)
{
	auto view = makeView(State::ProbeMode::Semi);
	auto& page = view->getWcsProbePage();
	page.originPressed(11); // Z TOP
	page.runPressed();
	EXPECT_EQ(last(), "M98 P\"0:/sys/probe/probe_semi.g\" O\"ZTOP\" D2.000 C2.000 S5.000 W1\n");
	EXPECT_FALSE(page.autoRunning());

	message(M::ConfirmCancel, 5, "PROBE:Z", "Move near top of stock, press PROBE", 1u << 2);
	ASSERT_TRUE(page.jogPromptOpen());
	EXPECT_EQ(page.jogTitle(), "PROBE");
	EXPECT_EQ(page.jogOkLabel(), "PROBE");
	EXPECT_EQ(page.jogAxisCount(), 1u);
	EXPECT_EQ(page.jogInstruction(), "Move near [Z] top of stock, press PROBE");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/msg_jog_probe_z.png")); // PanelDue m04_m291_jog_probe_z

	sent.clear();
	lv_tick_inc(1300); // the machine data just arrived counts as a move: let it settle (1.2 s)
	page.jogMovePressed(1);
	EXPECT_EQ(last(), "G91\nG1 Z0.100 F300\nG90\n");
	page.jogStepPressed(3);
	page.jogMovePressed(-1);
	EXPECT_EQ(last(), "G91\nG1 Z-10.000 F300\nG90\n");
	page.jogMovePressed(-1); // 10: the last one may still be moving, the tap is dropped
	EXPECT_EQ(sent.size(), 2u);
	page.jogOkPressed();
	EXPECT_EQ(last(), "M292 P0 S5\n");
	EXPECT_FALSE(page.jogPromptOpen());
}

TEST_F(TestCncWcsProbe, ManualReadPrompt)
{
	auto view = makeView(State::ProbeMode::Manual);
	auto& page = view->getWcsProbePage();
	message(M::ConfirmCancel, 7, "READ:L", "Touch left side of stock and press READ", 0x3);
	ASSERT_TRUE(page.jogPromptOpen());
	EXPECT_EQ(page.jogOkLabel(), "READ");
	EXPECT_EQ(page.jogAxisCount(), 2u);
	EXPECT_EQ(page.jogInstruction(), "Touch [L] side of stock and press READ");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/msg_jog_read_left.png")); // PanelDue m05_m291_jog_read_left
	page.jogAxisPressed(1);
	page.jogMovePressed(1);
	EXPECT_EQ(last(), "G91\nG1 Y0.100 F300\nG90\n");
	page.jogCancelPressed();
	EXPECT_EQ(last(), "M292 P1 S7\n");

	// Cleared elsewhere (DWC): the prompt closes
	message(M::InfoConfirm, 8, "READ:B", "Touch bottom side", 0x3);
	EXPECT_TRUE(page.jogPromptOpen());
	message(M::None, 8, "", "");
	EXPECT_FALSE(page.jogPromptOpen());
}

TEST_F(TestCncWcsProbe, OtherPromptWithAxisControls)
{
	auto view = makeView();
	auto& page = view->getWcsProbePage();
	message(M::InfoConfirm, 9, "Align", "Jog the spindle over the fixture pin and press OK to continue", 0x7);
	ASSERT_TRUE(page.jogPromptOpen());
	EXPECT_EQ(page.jogTitle(), "Align");
	EXPECT_EQ(page.jogOkLabel(), "OK");
	EXPECT_EQ(page.jogInstruction(), "Jog the spindle over the fixture\npin and press OK to continue");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/msg_jog_other.png"));
}

// ---- Machine messages without axis controls ----------------------------------------------------

TEST_F(TestCncWcsProbe, MessagePopups)
{
	auto view = makeView();
	message(M::Info, 1, "", "Spindle warming up, please wait");
	EXPECT_EQ(Popup::Title(), "MESSAGE");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/msg_message.png")); // PanelDue m01_message
	Popup::PressOk();
	EXPECT_TRUE(sent.empty()); // S0: nothing to answer

	message(M::InfoClose, 2, "Info", "Done");
	Popup::PressOk();
	EXPECT_EQ(last(), "M292 P0 S2\n");

	message(M::ConfirmCancel, 3, "Confirm", "Continue with the program?");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/msg_confirm.png")); // PanelDue m03_m291_confirm
	Popup::PressCancel();
	EXPECT_EQ(last(), "M292 P1 S3\n");
	message(M::InfoConfirm, 4, "Confirm", "OK?");
	Popup::PressOk();
	EXPECT_EQ(last(), "M292 P0 S4\n");

	message(M::Choices, 5, "Choose", "Pick one");
	EXPECT_EQ(Popup::Lines(), Lines({"Answer this prompt in DWC.", "Pick one"}));
	message(M::None, 5, "", "");
	EXPECT_FALSE(Popup::IsOpen());
}

TEST_F(TestCncWcsProbe, InfoMessageNotOverAPopupInUse)
{
	auto view = makeView();
	view->getWcsProbePage().paramPressed(P::PTip);
	Popup::NumpadKey('3');
	message(M::Info, 1, "", "Hello");
	EXPECT_TRUE(Popup::IsNumpadOpen()); // the value being typed is not thrown away
	EXPECT_EQ(Popup::NumpadValue(), "3");
	// A message that needs an answer does replace it
	message(M::InfoConfirm, 2, "Confirm", "OK?");
	EXPECT_TRUE(Popup::IsMessageOpen());
}
