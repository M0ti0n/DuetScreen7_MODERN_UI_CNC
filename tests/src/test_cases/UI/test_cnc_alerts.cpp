/*
 * test_cnc_alerts.cpp
 *
 * SYSTEM > ALERT of the CNC UI: Error: / Warning: lines from machine replies, newest first, repeats,
 * 5 pages of 10, ages, unread dot, the ERROR / WARNING popup, and screenshots (compare with the
 * PanelDue CNC UI references x 1.25).
 */

#include "Debug.h"
#include "UI/Cnc/CncHomeView.h"
#include "UI/Cnc/CncLayout.h"
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

class TestCncAlerts : public UiTestSuite
{
  protected:
	TestCncAlerts()
	{
		ZoneScoped;
		lv_display_set_resolution(lv_display_get_default(), UI::CncLayout::ScreenW, UI::CncLayout::ScreenH);
		lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
		State::Reset();
		State::SetSendHook([](std::string_view) {});
	}

	~TestCncAlerts() override { Popup::Close(); }

	std::unique_ptr<V> makeView()
	{
		load_model_data_from_file("tests/object_model/cnc/model_move_3axis.json");
		load_model_data_from_file("tests/object_model/cnc/model_spindles.json");
		load_model_data_from_file("tests/object_model/cnc/model_tools.json");
		load_model_data_from_file("tests/object_model/cnc/model_state_idle.json");
		auto v = std::make_unique<V>();
		v->show();
		v->showPage(V::PageSystem);
		v->showSubTab(V::SystemAlertTab);
		return v;
	}

	// A reply as the presenter gets it
	static void reply(V& v, const std::string& text) { v.getPresenter()->newResponse(ResponseType::INFO, text); }
};

TEST_F(TestCncAlerts, Empty)
{
	auto view = makeView();
	auto& page = view->getAlertsPage();
	EXPECT_EQ(page.count(), 0u);
	EXPECT_EQ(page.labelRight(), "0 / 50   NEWEST FIRST");
	EXPECT_EQ(page.pageText(), "PAGE 1 / 1");
}

TEST_F(TestCncAlerts, ErrorsAndWarningsNewestFirst)
{
	auto view = makeView();
	auto& page = view->getAlertsPage();
	reply(*view, "Warning: Spindle speed limited to 24000 rpm");
	lv_tick_inc(12000);
	reply(*view, "ok\nError: G0/G1: target position outside machine limits\n");
	reply(*view, "M115 says hello"); // neither: not kept
	EXPECT_EQ(page.count(), 2u);
	EXPECT_EQ(page.rowText(0), "G0/G1: target position outside machine limits");
	EXPECT_TRUE(page.entry(0).error);
	EXPECT_EQ(page.rowText(1), "Spindle speed limited to 24000 rpm");
	EXPECT_FALSE(page.entry(1).error);
	EXPECT_EQ(page.rowAge(1), "0m12");
	EXPECT_TRUE(page.rowUnread(0));
	EXPECT_EQ(page.labelRight(), "2 / 50   NEWEST FIRST");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/alerts_two.png")); // PanelDue p08_system_alert
}

TEST_F(TestCncAlerts, RepeatOnlyRefreshes)
{
	auto view = makeView();
	auto& page = view->getAlertsPage();
	reply(*view, "Error: Probe already triggered");
	lv_tick_inc(70000);
	reply(*view, "Error:   Probe already triggered\t");
	EXPECT_EQ(page.count(), 1u);
	EXPECT_EQ(page.rowAge(0), "0m00");
	reply(*view, "Warning: Probe already triggered"); // another kind: a new one
	EXPECT_EQ(page.count(), 2u);
	reply(*view, "Error:");
	EXPECT_EQ(page.rowText(0), "(no text)");
}

TEST_F(TestCncAlerts, AgesFormat)
{
	EXPECT_EQ(AlertsPage::FormatAge(0), "0m00");
	EXPECT_EQ(AlertsPage::FormatAge(247000), "4m07");
	EXPECT_EQ(AlertsPage::FormatAge(8100000), "2h15");
	EXPECT_EQ(AlertsPage::FormatAge(273600000), "3d04");
}

TEST_F(TestCncAlerts, FivePagesOfTen)
{
	auto view = makeView();
	auto& page = view->getAlertsPage();
	for (int i = 0; i < 57; ++i)
	{
		reply(*view, fmt::format("Error: alert {:d}", i));
	}
	EXPECT_EQ(page.count(), 50u); // the oldest 7 fell out
	EXPECT_EQ(page.pageCount(), 5u);
	EXPECT_EQ(page.rowText(0), "alert 56");
	page.pagePressed(-1); // at the start
	EXPECT_EQ(page.page(), 0u);
	page.pagePressed(1);
	EXPECT_EQ(page.pageText(), "PAGE 2 / 5");
	EXPECT_EQ(page.rowText(0), "alert 46");
	for (int i = 0; i < 5; ++i)
	{
		page.pagePressed(1);
	}
	EXPECT_EQ(page.page(), 4u);
	EXPECT_EQ(page.rowText(9), "alert 7");
	reply(*view, "Warning: new one"); // the page you look at stays
	EXPECT_EQ(page.page(), 4u);
	EXPECT_EQ(page.rowText(9), "alert 8");
}

TEST_F(TestCncAlerts, PagesLook)
{
	auto view = makeView();
	auto& page = view->getAlertsPage();
	const char* texts[] = {"Workplace offsets not saved",
						   "G10: Bad tool number",
						   "Motor 3 stalled",
						   "M98: Cannot open file 0:/macros/park.g",
						   "Low voltage on VIN: 22.1V",
						   "Macro file probe_bore.g not found",
						   "Heater 0 fault ignored (no heater configured)",
						   "Tool 7 not found",
						   "Driver 2 warning: phase A may be disconnected",
						   "Probe already triggered at start of probing move",
						   "Spindle speed limited to 24000 rpm",
						   "G0/G1: target position outside machine limits"};
	for (int i = 0; i < 12; ++i)
	{
		reply(*view, fmt::format("{:s} {:s}", (i % 2 == 0) ? "Warning:" : "Error:", texts[i]));
		lv_tick_inc(1000);
	}
	page.rowPressed(5);
	page.rowPressed(9);
	Popup::Close();
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/alerts_page1.png"));
	page.pagePressed(1);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/alerts_page2.png"));
}

TEST_F(TestCncAlerts, TapShowsWholeTextAndMarksRead)
{
	auto view = makeView();
	auto& page = view->getAlertsPage();
	reply(*view, "Error: G0/G1: target position outside machine limits");
	page.rowPressed(0);
	EXPECT_EQ(Popup::Title(), "ERROR");
	EXPECT_EQ(Popup::Lines(), Lines({"G0/G1: target position outside machine limits"}));
	EXPECT_FALSE(page.rowUnread(0));
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/alerts_popup.png")); // PanelDue m02_error_alert
	Popup::Close();
	reply(*view, "Warning: x");
	page.rowPressed(0);
	EXPECT_EQ(Popup::Title(), "WARNING");
}

TEST_F(TestCncAlerts, LongTextCut)
{
	auto view = makeView();
	auto& page = view->getAlertsPage();
	reply(*view, "Error: " + std::string(150, 'x'));
	EXPECT_EQ(page.entry(0).text.size(), AlertsPage::MaxTextLen);
}
