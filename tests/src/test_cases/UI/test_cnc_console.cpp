/*
 * test_cnc_console.cpp
 *
 * SYSTEM > CONSOLE of the CNC UI: the log (colours, wrapping, newest message first, ages), sending
 * with the keyboard, command history, the kept draft, and screenshots (compare with the PanelDue
 * CNC UI references x 1.25).
 */

#include "Debug.h"
#include "UI/Cnc/CncHomeView.h"
#include "UI/Cnc/CncKeyboard.h"
#include "UI/Cnc/CncLayout.h"
#include "UI/Cnc/CncPopup.h"
#include "UI/Cnc/CncState.h"
#include "test_utils/UiTestSuite.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace UI::Cnc;
using V = UI::CncHomeView;
using K = ConsolePage::Kind;

class TestCncConsole : public UiTestSuite
{
  protected:
	TestCncConsole()
	{
		ZoneScoped;
		lv_display_set_resolution(lv_display_get_default(), UI::CncLayout::ScreenW, UI::CncLayout::ScreenH);
		lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
		State::Reset();
		State::SetSendHook([this](std::string_view g) { sent.emplace_back(g); });
	}

	~TestCncConsole() override { Popup::Close(); }

	std::unique_ptr<V> makeView()
	{
		load_model_data_from_file("tests/object_model/cnc/model_move_3axis.json");
		load_model_data_from_file("tests/object_model/cnc/model_spindles.json");
		load_model_data_from_file("tests/object_model/cnc/model_tools.json");
		load_model_data_from_file("tests/object_model/cnc/model_state_idle.json");
		auto v = std::make_unique<V>();
		v->show();
		v->showPage(V::PageSystem);
		v->showSubTab(V::SystemConsoleTab);
		return v;
	}

	static void reply(V& v, const std::string& text) { v.getPresenter()->newResponse(ResponseType::INFO, text); }

	static void type(const std::string& s)
	{
		for (char c : s)
		{
			Keyboard::PressKey(c);
		}
	}

	// The PanelDue simulator scenario: G28, ok, M114, position, a warning and an error
	void scenario(V& v)
	{
		auto& page = v.getConsolePage();
		page.inputPressed();
		type("G28");
		Keyboard::PressEnter();
		reply(v, "ok");
		type("M114");
		Keyboard::PressEnter();
		Keyboard::PressCancel();
		reply(v, "X:112.345 Y:94.500 Z:-21.800");
		reply(v, "Warning: Spindle speed limited to 24000 rpm");
		reply(v, "Error: G0/G1: target position outside machine limits");
	}

	std::vector<std::string> sent;
};

TEST_F(TestCncConsole, LogNewestMessageFirst)
{
	auto view = makeView();
	auto& page = view->getConsolePage();
	scenario(*view);
	EXPECT_EQ(sent, std::vector<std::string>({"G28\n", "M114\n"}));
	EXPECT_EQ(page.lineText(0), "Error: G0/G1: target position outside");
	EXPECT_EQ(page.lineText(1), "machine limits"); // wrapped, the message's lines in reading order
	EXPECT_EQ(page.lineKind(0), K::Error);
	EXPECT_EQ(page.lineKind(1), K::Error);
	EXPECT_EQ(page.lineAge(0), "0m00");
	EXPECT_EQ(page.lineAge(1), "");												// the age on the first line only
	EXPECT_EQ(page.lineText(2), "Warning: Spindle speed limited to 24000 rpm"); // one line here (PanelDue: two)
	EXPECT_EQ(page.lineKind(2), K::Warning);
	EXPECT_EQ(page.lineText(3), "X:112.345 Y:94.500 Z:-21.800");
	EXPECT_EQ(page.lineKind(3), K::Reply);
	EXPECT_EQ(page.lineText(4), "M114");
	EXPECT_EQ(page.lineKind(4), K::Command);
	EXPECT_EQ(page.lineText(6), "G28");
	EXPECT_EQ(page.lineText(7), "");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/console_log.png")); // PanelDue p09_system_console
}

TEST_F(TestCncConsole, KeyboardKeepsSevenLines)
{
	auto view = makeView();
	auto& page = view->getConsolePage();
	scenario(*view);
	reply(*view, "Warning: Driver 2 warning: phase A may be disconnected"); // two more rows
	page.inputPressed();
	EXPECT_TRUE(page.keyboardOpen());
	EXPECT_EQ(page.lineText(6), "M114");
	EXPECT_EQ(page.lineText(7), "");									 // under the sheet
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/console_keyboard.png")); // PanelDue k08_keyboard_console
	Keyboard::PressCancel();
	EXPECT_EQ(page.lineText(7), "ok");
}

TEST_F(TestCncConsole, EnterSendsAndStaysOpen)
{
	auto view = makeView();
	auto& page = view->getConsolePage();
	page.inputPressed();
	Keyboard::PressEnter(); // nothing typed: nothing sent
	EXPECT_TRUE(sent.empty());
	type(" G0 X10");
	Keyboard::PressEnter();
	EXPECT_EQ(sent.back(), "G0 X10\n");
	EXPECT_TRUE(Keyboard::IsOpen());
	EXPECT_EQ(Keyboard::Text(), "");
	EXPECT_EQ(page.lineText(0), "G0 X10");
}

TEST_F(TestCncConsole, History)
{
	auto view = makeView();
	auto& page = view->getConsolePage();
	page.inputPressed();
	for (const char* c : {"G28", "M114", "M114", "G0 X1", "G0 X2", "G0 X3", "G0 X4"})
	{
		type(c);
		Keyboard::PressEnter();
	}
	EXPECT_EQ(page.history(), std::vector<std::string>({"G0 X4", "G0 X3", "G0 X2", "G0 X1", "M114"}));
	type("M5"); // the draft
	Keyboard::PressArrow(-1);
	EXPECT_EQ(Keyboard::Text(), "G0 X4");
	Keyboard::PressArrow(-1);
	EXPECT_EQ(Keyboard::Text(), "G0 X3");
	Keyboard::PressArrow(1);
	Keyboard::PressArrow(1);
	EXPECT_EQ(Keyboard::Text(), "M5"); // back to what was typed
	for (int i = 0; i < 7; ++i)
	{
		Keyboard::PressArrow(-1);
	}
	EXPECT_EQ(Keyboard::Text(), "M114"); // the oldest kept
}

TEST_F(TestCncConsole, DraftKeptOverClosing)
{
	auto view = makeView();
	auto& page = view->getConsolePage();
	page.inputPressed();
	type("G1 Z");
	Keyboard::PressCancel();
	EXPECT_EQ(page.draft(), "G1 Z");
	page.inputPressed();
	EXPECT_EQ(Keyboard::Text(), "G1 Z");
	view->showPage(V::PageControl); // another page closes it, the draft stays
	EXPECT_FALSE(Keyboard::IsOpen());
	EXPECT_FALSE(page.keyboardOpen());
	EXPECT_EQ(page.draft(), "G1 Z");
}

TEST_F(TestCncConsole, ReplyBelongsToTypedCommand)
{
	auto view = makeView();
	auto& page = view->getConsolePage();
	page.inputPressed();
	type("M115");
	Keyboard::PressEnter();
	reply(*view, "FIRMWARE_NAME: RepRapFirmware");
	EXPECT_TRUE(page.lastReplyWasConsole());
	reply(*view, "a later reply");
	EXPECT_FALSE(page.lastReplyWasConsole());
	type("M114");
	Keyboard::PressEnter();
	lv_tick_inc(ConsolePage::ConsoleReplyTime + 100);
	reply(*view, "too late");
	EXPECT_FALSE(page.lastReplyWasConsole());
}

TEST_F(TestCncConsole, LongReplyFillsTheLogWithDots)
{
	auto view = makeView();
	auto& page = view->getConsolePage();
	std::string longText;
	for (int i = 0; i < 40; ++i)
	{
		longText += "line" + std::to_string(i) + "\n";
	}
	reply(*view, longText);
	EXPECT_EQ(page.lineText(0), "line0");
	EXPECT_EQ(page.lineText(13), "line13");
	EXPECT_EQ(page.lineText(14), "line14..");
	lv_tick_inc(65000);
	page.tick();
	EXPECT_EQ(page.lineAge(0), "1m05");
}
