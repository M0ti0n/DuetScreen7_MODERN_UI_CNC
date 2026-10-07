/*
 * test_cnc_messages.cpp
 *
 * Machine popups of the CNC UI: replies (ALERT ! until X / closing after INFO TIMEOUT, where they are
 * not shown), display messages (M117) and the M291 prompts that need a choice, a number or a text
 * (S4..S7), with screenshots. The texts are examples of what a macro might ask.
 */

#include "Debug.h"
#include "UI/Cnc/CncHomeView.h"
#include "UI/Cnc/CncKeyboard.h"
#include "UI/Cnc/CncLayout.h"
#include "UI/Cnc/CncMessages.h"
#include "UI/Cnc/CncPanel.h"
#include "UI/Cnc/CncPopup.h"
#include "UI/Cnc/CncState.h"
#include "test_utils/UiTestSuite.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace UI::Cnc;
using V = UI::CncHomeView;
using M = Messages::Mode;

class TestCncMessages : public UiTestSuite
{
  protected:
	TestCncMessages()
	{
		ZoneScoped;
		lv_display_set_resolution(lv_display_get_default(), UI::CncLayout::ScreenW, UI::CncLayout::ScreenH);
		lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
		State::Reset();
		Messages::Reset();
		Panel::Reset();
		Panel::SetTestHook([](const std::string&) {});
		State::SetSendHook([this](std::string_view g) { sent.emplace_back(g); });
	}

	~TestCncMessages() override
	{
		Keyboard::Close();
		Popup::Close();
		view.reset();
		Panel::SetTestHook(nullptr);
		Messages::Reset();
	}

	void makeView()
	{
		load_model_data_from_file("tests/object_model/cnc/model_move_3axis.json");
		load_model_data_from_file("tests/object_model/cnc/model_spindles.json");
		load_model_data_from_file("tests/object_model/cnc/model_tools.json");
		load_model_data_from_file("tests/object_model/cnc/model_state_idle.json");
		view = std::make_unique<V>();
		view->show();
		view->getPresenter()->newStatus(OM::PrinterStatus::idle);
		view->showPage(V::PageControl);
	}

	void reply(const std::string& text)
	{
		const ResponseType type = (text.rfind("Error: ", 0) == 0)	  ? ResponseType::ERROR
								  : (text.rfind("Warning: ", 0) == 0) ? ResponseType::WARNING
																	  : ResponseType::INFO;
		view->getPresenter()->newResponse(type, text);
	}

	static void wait(uint32_t ms)
	{
		lv_tick_inc(ms);
		lv_timer_handler();
	}

	static Messages::Message prompt(M mode, uint32_t seq, const std::string& title, const std::string& text)
	{
		Messages::Message m;
		m.mode = mode;
		m.seq = seq;
		m.title = title;
		m.text = text;
		return m;
	}

	std::string last() const { return sent.empty() ? std::string() : sent.back(); }

	std::unique_ptr<V> view;
	std::vector<std::string> sent;
};

// ---- Replies ----

TEST_F(TestCncMessages, ReplyClosesAfterInfoTimeout)
{
	makeView();
	reply("X:112.345 Y:94.500 Z:-21.800 A:0.000");
	ASSERT_TRUE(Popup::IsResponseOpen());
	EXPECT_EQ(Popup::Title(), ""); // ordinary replies have no title tile
	EXPECT_FALSE(Popup::HasOk());
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/popup_reply.png"));

	wait(Panel::InfoTimeout() * 1000 - 200);
	EXPECT_TRUE(Popup::IsResponseOpen());
	wait(400);
	EXPECT_FALSE(Popup::IsOpen());
}

TEST_F(TestCncMessages, ErrorReplyStaysUntilX)
{
	makeView();
	reply("Error: G0/G1: target position outside machine limits");
	ASSERT_TRUE(Popup::IsResponseOpen());
	EXPECT_EQ(Popup::Title(), "ALERT !");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/popup_reply_error.png"));
	wait(30000);
	EXPECT_TRUE(Popup::IsResponseOpen());
	Popup::PressCancel();
	EXPECT_FALSE(Popup::IsOpen());
}

TEST_F(TestCncMessages, NewestReplyReplacesAndWarningsCloseByThemselves)
{
	makeView();
	reply("Error: first");
	reply("Warning: Spindle speed limited to 24000 rpm");
	ASSERT_TRUE(Popup::IsResponseOpen());
	EXPECT_EQ(Popup::Title(), ""); // a warning is not an "Error" reply
	ASSERT_FALSE(Popup::Lines().empty());
	EXPECT_EQ(Popup::Lines()[0].rfind("Warning:", 0), 0u);
	wait(Panel::InfoTimeout() * 1000 + 100);
	EXPECT_FALSE(Popup::IsOpen());
}

TEST_F(TestCncMessages, ReplyNotOverPopupInUse)
{
	makeView();
	Popup::Confirm("HOME ALL", {"Home all axes?"}, nullptr);
	reply("Error: something");
	EXPECT_EQ(Popup::Title(), "HOME ALL");

	// A message that needs no answer is replaced, one the machine waits for is not
	Popup::Close();
	Messages::Process(prompt(M::Info, 1, "INFO", "Spindle warming up"));
	reply("ok then");
	EXPECT_TRUE(Popup::IsResponseOpen());
	Popup::Close();
	Messages::Process(prompt(M::InfoConfirm, 2, "CHECK", "Is the clamp tight?"));
	reply("Error: not now");
	EXPECT_EQ(Popup::Title(), "CHECK");
}

TEST_F(TestCncMessages, ReplyNotShownOnSystemOrForConsoleOrWithTimeoutZero)
{
	makeView();
	view->showPage(V::PageSystem);
	reply("Error: on system");
	EXPECT_FALSE(Popup::IsOpen()); // SYSTEM > ALERT shows it

	view->showPage(V::PageControl);
	Panel::SetInfoTimeout(0);
	reply("X:1 Y:2 Z:3");
	EXPECT_FALSE(Popup::IsOpen()); // INFO TIMEOUT 0: ordinary replies not shown
	reply("Error: still shown");
	EXPECT_TRUE(Popup::IsResponseOpen());
	Popup::Close();

	// The reply to a command typed in CONSOLE stays there
	Panel::SetInfoTimeout(6);
	view->showPage(V::PageSystem);
	view->showSubTab(V::SystemConsoleTab);
	view->getConsolePage().inputPressed();
	Keyboard::PressKey('M');
	Keyboard::PressKey('1');
	Keyboard::PressKey('1');
	Keyboard::PressKey('5');
	Keyboard::PressEnter();
	Keyboard::Close();
	view->showPage(V::PageControl);
	reply("FIRMWARE_NAME: RepRapFirmware");
	EXPECT_FALSE(Popup::IsOpen());
}

TEST_F(TestCncMessages, ToolPromptClosesReply)
{
	makeView();
	reply("X:1 Y:2 Z:3");
	ASSERT_TRUE(Popup::IsResponseOpen());
	Messages::JobHooks hooks;
	hooks.showToolPrompt = [](const std::string&, uint32_t) { return true; };
	Messages::SetJobHooks(hooks);
	Messages::Process(prompt(M::InfoConfirm, 3, "TOOL CHANGE", "Insert T2"));
	EXPECT_FALSE(Popup::IsOpen());
	Messages::SetJobHooks({});
}

// ---- Display messages (M117) ----

TEST_F(TestCncMessages, DisplayMessagePopup)
{
	makeView();
	Messages::DisplayMessage("Roughing done", true);
	ASSERT_TRUE(Popup::IsMessageOpen());
	EXPECT_EQ(Popup::Title(), "MESSAGE");
	EXPECT_FALSE(Popup::IsBlockingMessageOpen());
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/popup_display_message.png"));
	Popup::PressOk();
	EXPECT_FALSE(Popup::IsOpen());

	// The same text from state.displayMessage right after: not again
	Messages::DisplayMessage("old", false); // the first one is what was there when connecting
	EXPECT_FALSE(Popup::IsOpen());
	Messages::DisplayMessage("Roughing done", false);
	EXPECT_FALSE(Popup::IsOpen());
	Messages::DisplayMessage("Roughing done", false);
	EXPECT_FALSE(Popup::IsOpen());

	// A new text through the state only (USB / network)
	Messages::DisplayMessage("Finishing done", false);
	ASSERT_TRUE(Popup::IsMessageOpen());
	ASSERT_FALSE(Popup::Lines().empty());
	EXPECT_EQ(Popup::Lines()[0], "Finishing done");

	// Never over a popup in use
	Popup::Confirm("HOME ALL", {"Home all axes?"}, nullptr);
	Messages::DisplayMessage("Another", true);
	EXPECT_EQ(Popup::Title(), "HOME ALL");
}

// ---- M291 S4: choices ----

TEST_F(TestCncMessages, ChoicePrompt)
{
	makeView();
	auto m = prompt(M::Choices, 7, "PROBE CORNER", "Which corner of the stock to probe?");
	m.choices = {"FRONT LEFT", "FRONT RIGHT", "BACK LEFT", "BACK RIGHT"};
	m.intDefault = 1;
	m.cancelButton = true;
	Messages::Process(m);
	ASSERT_TRUE(Popup::IsChooseOpen());
	EXPECT_TRUE(Popup::IsMachinePromptOpen());
	EXPECT_EQ(Popup::Title(), "PROBE CORNER");
	EXPECT_EQ(Popup::ChoiceSelected(), 1u);
	EXPECT_EQ(Popup::ChoicePages(), 1u);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/popup_prompt_choices.png"));

	// A reply does not replace it
	reply("Error: something else");
	EXPECT_TRUE(Popup::IsChooseOpen());

	Popup::PressChoice(2);
	Popup::PressOk();
	EXPECT_EQ(last(), "M292 R{2} S7\n");
	EXPECT_FALSE(Popup::IsOpen());

	Messages::Process(m);
	Popup::PressCancel();
	EXPECT_EQ(last(), "M292 P1 S7\n");
}

TEST_F(TestCncMessages, ChoicePromptPagesAndNoCancel)
{
	makeView();
	auto m = prompt(M::Choices, 8, "MEASURE TOOL", "Which tool to measure on the tool setter?");
	for (int i = 1; i <= 10; ++i)
	{
		m.choices.push_back("T" + std::to_string(i) + " " + std::to_string(i + 2) + " MM FLAT");
	}
	Messages::Process(m);
	ASSERT_TRUE(Popup::IsChooseOpen());
	EXPECT_EQ(Popup::ChoicePages(), 2u); // 2 columns x 4 rows
	EXPECT_EQ(Popup::ChoicePage(), 0u);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/popup_prompt_choices_pages.png"));

	Popup::PressCancel(); // no cancel button: the machine waits for a choice
	EXPECT_TRUE(Popup::IsChooseOpen());
	Popup::PressChoicePage(-1);
	EXPECT_EQ(Popup::ChoicePage(), 0u);
	Popup::PressChoicePage(1);
	EXPECT_EQ(Popup::ChoicePage(), 1u);
	Popup::PressChoicePage(1);
	EXPECT_EQ(Popup::ChoicePage(), 1u);
	Popup::PressChoice(9);
	Popup::PressOk();
	EXPECT_EQ(last(), "M292 R{9} S8\n");

	// Opens on the page of the default choice
	m.intDefault = 8;
	Messages::Process(m);
	EXPECT_EQ(Popup::ChoicePage(), 1u);
	EXPECT_EQ(Popup::ChoiceSelected(), 8u);
}

// ---- M291 S5 / S6: numbers ----

TEST_F(TestCncMessages, IntegerPrompt)
{
	makeView();
	auto m = prompt(M::NumberInt, 9, "WARM-UP", "Spindle warm-up time in minutes");
	m.intMin = 1;
	m.intMax = 20;
	m.intDefault = 3;
	m.cancelButton = true;
	Messages::Process(m);
	ASSERT_TRUE(Popup::IsNumpadOpen());
	EXPECT_EQ(Popup::Title(), "WARM-UP");
	EXPECT_EQ(Popup::NumpadValue(), "3");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/popup_prompt_number.png"));

	Popup::NumpadKey('.'); // inactive for an integer
	Popup::NumpadKey('-'); // and for a minimum >= 0
	Popup::NumpadKey('2');
	Popup::NumpadKey('5');
	EXPECT_EQ(Popup::NumpadValue(), "25");
	Popup::PressOk(); // outside 1..20: ALERT, its X brings the numpad back
	EXPECT_EQ(Popup::Title(), "ALERT !");
	EXPECT_TRUE(Popup::IsMachinePromptOpen());
	reply("Error: not over the range ALERT");
	EXPECT_EQ(Popup::Title(), "ALERT !");
	Popup::PressCancel();
	ASSERT_TRUE(Popup::IsNumpadOpen());
	Popup::NumpadKey('\b');
	Popup::PressOk();
	EXPECT_EQ(last(), "M292 R{2} S9\n");

	Messages::Process(m);
	Popup::PressCancel();
	EXPECT_EQ(last(), "M292 P1 S9\n");
}

TEST_F(TestCncMessages, FloatPromptWithoutCancel)
{
	makeView();
	auto m = prompt(M::NumberFloat, 10, "DEPTH", "Depth of cut in mm");
	m.floatMin = -5.0f;
	m.floatMax = 0.0f;
	m.floatDefault = -1.5f;
	Messages::Process(m);
	ASSERT_TRUE(Popup::IsNumpadOpen());
	EXPECT_EQ(Popup::NumpadValue(), "-1.5");
	Popup::PressCancel(); // no cancel button
	ASSERT_TRUE(Popup::IsNumpadOpen());
	Popup::NumpadKey('-');
	Popup::NumpadKey('0');
	Popup::NumpadKey('.');
	Popup::NumpadKey('7');
	Popup::NumpadKey('5');
	Popup::PressOk();
	EXPECT_EQ(last(), "M292 R{-0.75} S10\n");
}

// ---- M291 S7: text ----

TEST_F(TestCncMessages, TextPrompt)
{
	makeView();
	auto m = prompt(M::Text, 11, "NOTE", "Short note for the job log");
	m.textMin = 3;
	m.textMax = 6;
	m.textDefault = "AB";
	m.cancelButton = true;
	Messages::Process(m);
	ASSERT_TRUE(Keyboard::IsOpenFor(Messages::KeyboardOwner));
	EXPECT_TRUE(Popup::IsMachinePromptOpen());
	EXPECT_EQ(Popup::Title(), "NOTE");
	EXPECT_EQ(Keyboard::Text(), "AB");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/popup_prompt_text.png"));

	// Too short: ALERT, its check mark brings the keyboard back with the text
	Keyboard::PressEnter();
	EXPECT_EQ(Popup::Title(), "ALERT !");
	ASSERT_FALSE(Popup::Lines().empty());
	EXPECT_EQ(Popup::Lines()[0], "Enter 3 to 6 characters.");
	Popup::PressOk();
	ASSERT_TRUE(Keyboard::IsOpenFor(Messages::KeyboardOwner));
	EXPECT_EQ(Keyboard::Text(), "AB");
	Keyboard::PressKey('C');
	Keyboard::PressMode();
	Keyboard::PressKey('"');
	Keyboard::PressMode();
	for (char c : std::string("DEFG"))
	{
		Keyboard::PressKey(c); // F and G: the text is at its maximum, the keys do nothing
	}
	EXPECT_EQ(Keyboard::Text(), "ABC\"DE");
	Keyboard::PressEnter();
	EXPECT_EQ(last(), "M292 R{\"ABC\"\"DE\"} S11\n"); // a quote is doubled
	EXPECT_FALSE(Keyboard::IsOpen());

	Messages::Process(m);
	Keyboard::PressCancel();
	EXPECT_EQ(last(), "M292 P1 S11\n");
}

TEST_F(TestCncMessages, PromptClosedWhenAnsweredElsewhere)
{
	makeView();
	auto m = prompt(M::Text, 12, "NOTE", "Type a note");
	Messages::Process(m);
	ASSERT_TRUE(Keyboard::IsOpen());
	Keyboard::PressCancel(); // no cancel button
	EXPECT_TRUE(Keyboard::IsOpen());
	Messages::Process(prompt(M::None, 12, "", ""));
	EXPECT_FALSE(Keyboard::IsOpen());
	EXPECT_FALSE(Popup::IsOpen());

	auto c = prompt(M::Choices, 13, "PICK", "Pick one");
	c.choices = {"A", "B"};
	Messages::Process(c);
	ASSERT_TRUE(Popup::IsChooseOpen());
	Messages::Process(prompt(M::None, 13, "", ""));
	EXPECT_FALSE(Popup::IsOpen());
}
