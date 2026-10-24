/*
 * test_cnc_macros.cpp
 *
 * MACROS page of the CNC UI: listing (display names), folders, list / grid, RUN MACRO with its
 * refusal, search, the SETTINGS pick, and screenshots (compare with the PanelDue CNC UI references
 * x 1.25).
 *
 * Listing (tests/object_model/cnc/filelist_macros*.json): Probing/ and six numbered macros, as in
 * the PanelDue simulator scenarios.
 */

#include "Debug.h"
#include "UI/Cnc/CncHomeView.h"
#include "UI/Cnc/CncKeyboard.h"
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

class TestCncMacros : public UiTestSuite
{
  protected:
	TestCncMacros()
	{
		ZoneScoped;
		lv_display_set_resolution(lv_display_get_default(), UI::CncLayout::ScreenW, UI::CncLayout::ScreenH);
		lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
		State::Reset();
		State::SetSendHook([this](std::string_view g) { sent.emplace_back(g); });
	}

	~TestCncMacros() override { Popup::Close(); }

	std::unique_ptr<UI::CncHomeView> makeView(bool withListing = true)
	{
		load_model_data_from_file("tests/object_model/cnc/model_move_3axis.json");
		load_model_data_from_file("tests/object_model/cnc/model_spindles.json");
		load_model_data_from_file("tests/object_model/cnc/model_tools.json");
		load_model_data_from_file("tests/object_model/cnc/model_state_idle.json");
		auto v = std::make_unique<UI::CncHomeView>();
		v->show();
		State::StatusChanged(OM::PrinterStatus::idle);
		v->showPage(UI::CncHomeView::PageMacros); // requests the listing
		if (withListing)
		{
			load_model_data_from_file("tests/object_model/cnc/filelist_macros.json");
		}
		return v;
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

TEST_F(TestCncMacros, DisplayNames)
{
	EXPECT_EQ(MacrosPage::DisplayName("01_Park.g"), "Park");
	EXPECT_EQ(MacrosPage::DisplayName("!12_Safe_Z.G"), "Safe_Z");
	EXPECT_EQ(MacrosPage::DisplayName("!probe.g"), "probe");
	EXPECT_EQ(MacrosPage::DisplayName("2024plan.g"), "2024plan"); // digits without '_' stay
	EXPECT_EQ(MacrosPage::DisplayName("readme"), "readme");
}

TEST_F(TestCncMacros, ListFoldersFirst)
{
	auto view = makeView();
	auto& page = view->getMacrosPage();
	EXPECT_EQ(page.labelLeft(), "MACROS");
	EXPECT_EQ(page.labelRight(), "0:/macros");
	EXPECT_EQ(page.cellText(0), "sys"); // 0:/sys/, then the folders and macros
	EXPECT_EQ(page.cellTag(0), "SYSTEM");
	EXPECT_EQ(page.cellText(1), "Probing");
	EXPECT_EQ(page.cellTag(1), "FOLDER");
	EXPECT_EQ(page.cellText(2), "Park");
	EXPECT_EQ(page.cellText(7), "Laser_pointer");
	EXPECT_EQ(page.cellText(8), "");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/macros_list.png")); // PanelDue p07_macros_list
}

TEST_F(TestCncMacros, LoadingThenError)
{
	auto view = makeView(false);
	auto& page = view->getMacrosPage();
	EXPECT_EQ(page.message(), "Loading...");
	lv_tick_inc(MacrosPage::ListTimeout + 100);
	page.tick();
	EXPECT_EQ(page.message(), "Cannot read this folder.");
	EXPECT_EQ(page.cellText(0), "..");
	EXPECT_EQ(page.cellTag(0), "BACK");
}

TEST_F(TestCncMacros, GridView)
{
	auto view = makeView();
	auto& page = view->getMacrosPage();
	page.togglePressed();
	EXPECT_TRUE(page.gridMode());
	EXPECT_EQ(page.cellText(7), "Laser_pointer");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/macros_grid.png")); // PanelDue k06_macros_grid
	page.togglePressed();
	EXPECT_FALSE(page.gridMode());
}

TEST_F(TestCncMacros, PagingListAndGrid)
{
	auto view = makeView();
	auto& page = view->getMacrosPage();
	std::vector<MacrosPage::Entry> many;
	for (int i = 0; i < 25; ++i)
	{
		many.push_back({fmt::format("{:02d}_m{:02d}.g", i, i), false});
	}
	page.listingArrived(page.fullDir(), many);
	page.pagePressed(1);
	EXPECT_EQ(page.scroll(), 11u);
	EXPECT_EQ(page.cellText(0), "m10"); // the sys row comes first
	page.togglePressed(); // grid: 10 per page, scroll back on the grid of pages
	EXPECT_EQ(page.scroll(), 10u);
	EXPECT_EQ(page.cellText(0), "m09");
	page.pagePressed(1);
	EXPECT_EQ(page.scroll(), 20u);
	EXPECT_EQ(page.cellText(5), "m24");
	EXPECT_EQ(page.cellText(6), "");
	page.pagePressed(1); // the end
	EXPECT_EQ(page.scroll(), 20u);
}

TEST_F(TestCncMacros, FolderAndBack)
{
	auto view = makeView();
	auto& page = view->getMacrosPage();
	page.cellPressed(1);
	EXPECT_EQ(page.folder(), "Probing/");
	load_model_data_from_file("tests/object_model/cnc/filelist_macros_probing.json");
	EXPECT_EQ(page.labelRight(), "0:/macros/Probing");
	EXPECT_EQ(page.cellText(0), "..");
	EXPECT_EQ(page.cellTag(0), "UP");
	EXPECT_EQ(page.cellText(1), "probe_z_top"); // "!" sorts first, shown without it
	EXPECT_EQ(page.cellText(2), "probe_corner");
	page.cellPressed(0);
	EXPECT_EQ(page.folder(), "");
}

TEST_F(TestCncMacros, RunMacro)
{
	auto view = makeView();
	auto& page = view->getMacrosPage();
	page.cellPressed(2);
	EXPECT_EQ(Popup::Title(), "RUN MACRO");
	EXPECT_EQ(Popup::Lines(), Lines({"Do you want to run this macro?", "Park"}));
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/macros_run_confirm.png")); // PanelDue k07_confirm_run_macro
	Popup::PressOk();
	EXPECT_EQ(last(), "M98 P\"0:/macros/01_Park.g\"\n");
}

TEST_F(TestCncMacros, RunRefusedWhileJobRuns)
{
	auto view = makeView();
	auto& page = view->getMacrosPage();
	State::StatusChanged(OM::PrinterStatus::processing);
	page.cellPressed(2);
	EXPECT_EQ(text(), "Locked while a job is running.");
	State::StatusChanged(OM::PrinterStatus::paused); // allowed while paused
	page.cellPressed(2);
	EXPECT_EQ(Popup::Title(), "RUN MACRO");
	State::StatusChanged(OM::PrinterStatus::resuming); // resumed while the question was open
	Popup::PressOk();
	EXPECT_EQ(text(), "Locked while a job is running.");
	EXPECT_TRUE(sent.empty());
}

TEST_F(TestCncMacros, Search)
{
	auto view = makeView();
	auto& page = view->getMacrosPage();
	page.searchPressed();
	ASSERT_TRUE(Keyboard::IsOpen());
	EXPECT_EQ(page.labelLeft(), "SEARCH");
	EXPECT_EQ(page.labelRight(), "6 of 6");
	EXPECT_EQ(page.cellText(2), "Spindle_warmup"); // 3 rows above the keyboard
	EXPECT_EQ(page.cellText(3), "");
	Keyboard::PressKey('V');
	Keyboard::PressKey('A');
	EXPECT_EQ(page.cellText(0), "Vacuum_on"); // display names, folders left out
	EXPECT_EQ(page.cellText(1), "Vacuum_off");
	EXPECT_EQ(page.labelRight(), "2 of 6");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/macros_search.png"));
	Keyboard::PressKey('X');
	EXPECT_EQ(page.message(), "No macros match");
	Keyboard::PressBackspace();
	Keyboard::PressEnter();
	EXPECT_TRUE(page.searching());
	page.cellPressed(1);
	EXPECT_EQ(Popup::Lines(), Lines({"Do you want to run this macro?", "Vacuum_off"}));
	Popup::Close();

	// Grid while typing: 2 tiles above the keyboard
	page.togglePressed();
	page.searchPressed();
	EXPECT_EQ(page.cellText(1), "Vacuum_off");
	EXPECT_EQ(page.cellText(2), "");
	Keyboard::PressCancel();
	EXPECT_FALSE(page.searching());
	EXPECT_EQ(page.cellText(0), "sys");
	EXPECT_EQ(page.cellText(1), "Probing");
}

TEST_F(TestCncMacros, LeavingClosesKeyboardKeepsFilter)
{
	auto view = makeView();
	auto& page = view->getMacrosPage();
	page.searchPressed();
	Keyboard::PressKey('P');
	view->showPage(UI::CncHomeView::PageControl);
	EXPECT_FALSE(Keyboard::IsOpen());
	EXPECT_TRUE(page.searching());
	EXPECT_EQ(page.query(), "P");
}

TEST_F(TestCncMacros, PickForSettings)
{
	auto view = makeView();
	auto& page = view->getMacrosPage();
	std::string path, name;
	page.startPick(
		[&](const std::string& p, const std::string& n)
		{
			path = p;
			name = n;
		});
	EXPECT_EQ(page.labelLeft(), "SELECT");
	page.cellPressed(3);
	EXPECT_FALSE(Popup::IsOpen()); // handed over, not run
	EXPECT_EQ(path, "0:/macros/02_Spindle_warmup.g");
	EXPECT_EQ(name, "Spindle_warmup");
	EXPECT_FALSE(page.picking());
	EXPECT_EQ(page.labelLeft(), "MACROS");

	page.startPick([](const std::string&, const std::string&) {});
	view->showPage(UI::CncHomeView::PageControl); // leaving cancels it
	EXPECT_FALSE(page.picking());
}

TEST_F(TestCncMacros, SystemFolder)
{
	auto view = makeView();
	auto& page = view->getMacrosPage();
	page.cellPressed(0); // sys
	EXPECT_TRUE(page.inSystem());
	EXPECT_EQ(page.fullDir(), "0:/sys/");
	EXPECT_EQ(page.labelRight(), "0:/sys");
	page.listingArrived(page.fullDir(),
						{{"config.g", false},
						 {"config-override.g", false},
						 {"daemon.g", false},
						 {"homeall.g", false},
						 {"runonce.g", false},
						 {"probe", true},
						 {"tpre0.g", false}});
	EXPECT_EQ(page.cellText(0), "..");
	EXPECT_EQ(page.cellTag(0), "UP");
	EXPECT_EQ(page.cellText(1), "probe"); // folders first
	EXPECT_EQ(page.cellText(2), "config-override.g");
	EXPECT_EQ(page.cellTag(2), "CONFIG"); // edit only
	EXPECT_EQ(page.cellText(3), "config.g");
	EXPECT_EQ(page.cellTag(3), "CONFIG");
	EXPECT_EQ(page.cellTag(4), "CONFIG"); // daemon.g
	EXPECT_EQ(page.cellText(5), "homeall");
	EXPECT_EQ(page.cellTag(5), "");
	EXPECT_EQ(page.cellTag(6), "CONFIG"); // runonce.g
	EXPECT_EQ(page.cellText(7), "tpre0");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/macros_sys.png"));

	page.cellPressed(5);
	EXPECT_EQ(Popup::Lines(), Lines({"Do you want to run this macro?", "homeall", "0:/sys/homeall.g"}));
	Popup::PressOk();
	EXPECT_EQ(last(), "M98 P\"0:/sys/homeall.g\"\n");

	sent.clear();
	page.cellPressed(3); // config.g: never run
	EXPECT_EQ(text(), "Config files are not run from the screen.");
	EXPECT_TRUE(sent.empty());
	Popup::Close();

	page.cellPressed(1); // a folder inside 0:/sys/
	EXPECT_EQ(page.fullDir(), "0:/sys/probe/");
	page.listingArrived(page.fullDir(), {{"config.g", false}});
	EXPECT_EQ(page.cellText(1), "config"); // CONFIG only in the root of 0:/sys/
	page.cellPressed(0);
	EXPECT_EQ(page.fullDir(), "0:/sys/");
	page.cellPressed(0); // root of 0:/sys/: back to the macros
	EXPECT_FALSE(page.inSystem());
	EXPECT_EQ(page.fullDir(), "0:/macros/");
}
