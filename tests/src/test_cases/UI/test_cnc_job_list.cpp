/*
 * test_cnc_job_list.cpp
 *
 * JOB > JOB LIST of the CNC UI and the shared keyboard: listing, folders, selection, the JOB popup
 * (PREVIEW, delete), the RUN question with its refusals, search, and screenshots (compare with the PanelDue CNC UI references x 1.25).
 *
 * Listing (tests/object_model/cnc/filelist_*.json): Fixtures/ and six job files, as in the PanelDue
 * simulator scenarios.
 */

#include "Debug.h"
#include "UI/Cnc/CncHomeView.h"
#include "UI/Cnc/CncKeyboard.h"
#include "UI/Cnc/CncLayout.h"
#include "UI/Cnc/CncPanel.h"
#include "UI/Cnc/CncPopup.h"
#include "UI/Cnc/CncState.h"
#include "test_utils/UiTestSuite.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace UI::Cnc;
using Lines = std::vector<std::string>;

class TestCncJobList : public UiTestSuite
{
  protected:
	TestCncJobList()
	{
		ZoneScoped;
		lv_display_set_resolution(lv_display_get_default(), UI::CncLayout::ScreenW, UI::CncLayout::ScreenH);
		lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
		State::Reset();
		State::SetSendHook([this](std::string_view g) { sent.emplace_back(g); });
	}

	~TestCncJobList() override
	{
		Popup::Close();
		JobListPage::SetVolumeMounted(1, false); // the mounted cards are kept between views
	}

	// The frame on JOB > JOB LIST with machine data and the gcodes listing, idle
	std::unique_ptr<UI::CncHomeView> makeView(bool withListing = true)
	{
		load_model_data_from_file("tests/object_model/cnc/model_move_3axis.json");
		load_model_data_from_file("tests/object_model/cnc/model_spindles.json");
		load_model_data_from_file("tests/object_model/cnc/model_tools.json");
		load_model_data_from_file("tests/object_model/cnc/model_state_idle.json");
		auto v = std::make_unique<UI::CncHomeView>();
		v->show();
		State::StatusChanged(OM::PrinterStatus::idle);
		v->showPage(UI::CncHomeView::PageJob); // JOB LIST: requests the listing
		if (withListing)
		{
			load_model_data_from_file("tests/object_model/cnc/filelist_gcodes.json");
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

// ---- Listing and folders -----------------------------------------------------------------------

TEST_F(TestCncJobList, ListingFoldersFirst)
{
	auto view = makeView();
	auto& page = view->getJobListPage();
	EXPECT_EQ(page.labelLeft(), "FILES");
	EXPECT_EQ(page.labelRight(), "0:/gcodes");
	EXPECT_EQ(page.rowText(0), "Fixtures");
	EXPECT_EQ(page.rowText(1), "bracket_v2.gcode");
	EXPECT_EQ(page.rowText(6), "spoilboard_surface.gcode");
	EXPECT_EQ(page.rowText(7), "");
	EXPECT_EQ(page.message(), "");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/job_list_3axis.png")); // PanelDue p06_job_list
}

TEST_F(TestCncJobList, LoadingUntilTheListingArrives)
{
	auto view = makeView(false);
	auto& page = view->getJobListPage();
	EXPECT_EQ(page.message(), "Loading...");
	EXPECT_EQ(page.rowText(0), "");
}

TEST_F(TestCncJobList, OpenFolderAndBackUp)
{
	auto view = makeView();
	auto& page = view->getJobListPage();
	page.rowPressed(0); // Fixtures
	EXPECT_EQ(page.folder(), "Fixtures/");
	EXPECT_EQ(page.message(), "Loading...");
	load_model_data_from_file("tests/object_model/cnc/filelist_fixtures.json");
	EXPECT_EQ(page.labelRight(), "0:/gcodes/Fixtures");
	EXPECT_EQ(page.rowText(0), "..");
	EXPECT_EQ(page.rowText(1), "clamp_plate.nc");
	EXPECT_EQ(page.rowText(2), "vise_jaws.gcode");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/job_list_folder.png"));
	page.rowPressed(0); // .. UP
	EXPECT_EQ(page.folder(), "");
}

TEST_F(TestCncJobList, FolderThatCannotBeRead)
{
	auto view = makeView();
	auto& page = view->getJobListPage();
	page.rowPressed(0);
	lv_tick_inc(JobListPage::ListTimeout + 100);
	page.tick();
	EXPECT_EQ(page.message(), "Cannot read this folder.");
	EXPECT_EQ(page.rowText(0), ".."); // BACK
	page.rowPressed(0);
	EXPECT_EQ(page.folder(), "");
	EXPECT_EQ(page.rowText(0), "Fixtures"); // the last listing of the gcodes folder at once, refreshed after
}

TEST_F(TestCncJobList, PagingTenRows)
{
	auto view = makeView();
	auto& page = view->getJobListPage();
	std::vector<JobListPage::Entry> many;
	for (int i = 0; i < 14; ++i)
	{
		many.push_back({fmt::format("job_{:02d}.gcode", i), false});
	}
	page.listingArrived(page.fullDir(), many);
	EXPECT_EQ(page.rowText(9), "job_09.gcode");
	page.pagePressed(-1); // at the top already
	EXPECT_EQ(page.scroll(), 0u);
	page.pagePressed(1);
	EXPECT_EQ(page.scroll(), 10u);
	EXPECT_EQ(page.rowText(0), "job_10.gcode");
	EXPECT_EQ(page.rowText(4), "");
	page.pagePressed(1); // the end
	EXPECT_EQ(page.scroll(), 10u);
}

// ---- Select and RUN ----------------------------------------------------------------------------

TEST_F(TestCncJobList, TapSelectsSecondTapAsks)
{
	auto view = makeView();
	auto& page = view->getJobListPage();
	page.rowPressed(1);
	EXPECT_EQ(page.selectedName(), "bracket_v2.gcode");
	EXPECT_FALSE(Popup::IsOpen());
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/job_list_selected.png")); // PanelDue k04a_job_list_selected
	page.rowPressed(1);
	EXPECT_EQ(Popup::Title(), "JOB");
	EXPECT_EQ(Popup::Lines(), Lines({"bracket_v2.gcode", "", "Check: open in PREVIEW", "Trash: delete the file"}));
	EXPECT_TRUE(Popup::HasTrash());
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/job_list_job_popup.png")); // Modern UI START PRINT: trash / X / check
	Popup::PressOk(); // PREVIEW of the file, nothing started
	EXPECT_FALSE(Popup::IsOpen());
	EXPECT_TRUE(sent.empty());
	EXPECT_EQ(view->getSubTab(UI::CncHomeView::PageJob), (unsigned int)UI::CncHomeView::JobPreviewTab);
	EXPECT_EQ(page.selectedPath(), "0:/gcodes/bracket_v2.gcode");
}

// SETTINGS > PREVIEW IN JOB: while a job runs, the check opens another file only with ANY FILE
TEST_F(TestCncJobList, PreviewDuringAJobFollowsTheSetting)
{
	Panel::SetTestHook([](const std::string&) {});
	auto view = makeView();
	auto& page = view->getJobListPage();
	page.setJobFile("0:/gcodes/faceplate.nc"); // as the presenter does for both pages
	view->getJobPreviewPage().setJobFile("0:/gcodes/faceplate.nc");
	State::StatusChanged(OM::PrinterStatus::processing);
	view->showSubTab(UI::CncHomeView::JobListTab);

	// ANY FILE (default): another file opens in PREVIEW
	page.rowPressed(1);
	page.rowPressed(1);
	EXPECT_FALSE(Popup::OkGreyed());
	Popup::PressOk();
	EXPECT_EQ(view->getSubTab(UI::CncHomeView::PageJob), (unsigned int)UI::CncHomeView::JobPreviewTab);
	EXPECT_EQ(view->getJobPreviewPage().shownPath(), "0:/gcodes/bracket_v2.gcode");

	// RUNNING JOB: the check is grey and says why
	Panel::SetPreviewDuringJob(Panel::PreviewInJob::RunningJob);
	EXPECT_EQ(view->getJobPreviewPage().shownPath(), "0:/gcodes/faceplate.nc");
	view->showSubTab(UI::CncHomeView::JobListTab);
	page.rowPressed(1);
	EXPECT_EQ(Popup::Lines(),
			  Lines({"bracket_v2.gcode", "", "PREVIEW: not during this job", "Trash: delete the file"}));
	EXPECT_TRUE(Popup::OkGreyed());
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/job_list_job_popup_grey.png"));
	Popup::PressOk();
	EXPECT_EQ(Popup::Title(), "ALERT !");
	EXPECT_EQ(text(), "While a job runs, PREVIEW shows only that job. (SETTINGS > PREVIEW IN JOB)");
	Popup::Close();
	EXPECT_EQ(view->getSubTab(UI::CncHomeView::PageJob), (unsigned int)UI::CncHomeView::JobListTab);

	// OFF: the same, its own reason; the running job itself still opens
	Panel::SetPreviewDuringJob(Panel::PreviewInJob::Off);
	page.rowPressed(1);
	Popup::PressOk();
	EXPECT_EQ(text(), "PREVIEW is off while a job runs. (SETTINGS > PREVIEW IN JOB)");
	Popup::Close();
	page.rowPressed(2);
	EXPECT_EQ(page.selectedName(), "faceplate.nc");
	page.rowPressed(2);
	EXPECT_FALSE(Popup::OkGreyed());
	Popup::Close();

	// The job ends: every file again
	State::StatusChanged(OM::PrinterStatus::idle);
	page.rowPressed(1);
	page.rowPressed(1);
	EXPECT_FALSE(Popup::OkGreyed());
	Popup::Close();
	Panel::SetTestHook(nullptr);
}

// RRF: inside "..." a single ' makes the next letter lowercase; '' is one apostrophe
TEST_F(TestCncJobList, ApostropheInFileNames)
{
	auto view = makeView();
	AskRunJob("0:/gcodes/Bob's part.nc");
	Popup::PressOk();
	EXPECT_EQ(last(), "M32 \"0:/gcodes/Bob''s part.nc\"\n");
	EXPECT_EQ(State::Quoted("a\"b'c"), "a\"\"b''c");
}

TEST_F(TestCncJobList, DeleteAfterConfirm)
{
	auto view = makeView();
	auto& page = view->getJobListPage();
	page.rowPressed(1);
	page.rowPressed(1);
	Popup::PressTrash();
	EXPECT_EQ(Popup::Title(), "DELETE");
	EXPECT_EQ(Popup::Lines(), Lines({"Delete this file?", "bracket_v2.gcode"}));
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/job_list_delete_confirm.png"));
	Popup::PressCancel(); // X: nothing deleted
	EXPECT_TRUE(sent.empty());
	EXPECT_EQ(page.rowText(1), "bracket_v2.gcode");

	page.rowPressed(1);
	Popup::PressTrash();
	Popup::PressOk();
	EXPECT_EQ(last(), "M30 \"0:/gcodes/bracket_v2.gcode\"\n");
	EXPECT_EQ(page.selectedName(), "");
	EXPECT_EQ(page.rowText(1), "faceplate.nc"); // gone at once
	lv_tick_inc(JobListPage::RefreshAfterDelete + 100);
	page.tick(); // then the machine's listing again (no "Loading...": the rows stay until it arrives)
	EXPECT_EQ(page.message(), "");
	load_model_data_from_file("tests/object_model/cnc/filelist_gcodes.json"); // what the machine lists wins
	EXPECT_EQ(page.rowText(1), "bracket_v2.gcode");
}

TEST_F(TestCncJobList, DeleteRefusals)
{
	auto view = makeView();
	auto& page = view->getJobListPage();
	page.setJobFile("0:/gcodes/bracket_v2.gcode");
	State::StatusChanged(OM::PrinterStatus::processing);
	page.rowPressed(1);
	page.rowPressed(1); // the JOB popup also while a job runs
	Popup::PressTrash();
	EXPECT_EQ(text(), "This job is running. It cannot be deleted.");
	Popup::Close();
	page.rowPressed(2);
	page.rowPressed(2);
	Popup::PressTrash(); // another file: allowed while the job runs
	EXPECT_EQ(Popup::Title(), "DELETE");
	Popup::Close();
	State::StatusChanged(OM::PrinterStatus::idle);
	State::StatusChanged(OM::PrinterStatus::disconnected);
	page.rowPressed(2);
	Popup::PressTrash();
	EXPECT_EQ(text(), "No connection to the machine.");
	EXPECT_TRUE(sent.empty());
}

TEST_F(TestCncJobList, RunQuestionAndRefusals)
{
	auto view = makeView();
	AskRunJob("0:/gcodes/bracket_v2.gcode");
	EXPECT_EQ(Popup::Title(), "ALERT !");
	EXPECT_EQ(Popup::Lines(), Lines({"Do you want to RUN", "this job?", "bracket_v2.gcode"}));
	EXPECT_FALSE(Popup::HasTrash());
	Popup::PressOk();
	EXPECT_EQ(last(), "M32 \"0:/gcodes/bracket_v2.gcode\"\n");
	sent.clear();

	State::StatusChanged(OM::PrinterStatus::processing);
	AskRunJob("0:/gcodes/faceplate.nc");
	EXPECT_EQ(text(), "A job is already running.");
	State::StatusChanged(OM::PrinterStatus::idle);
	State::StatusChanged(OM::PrinterStatus::busy);
	AskRunJob("0:/gcodes/faceplate.nc");
	EXPECT_EQ(text(), "Wait until the machine is idle.");
	State::StatusChanged(OM::PrinterStatus::off);
	AskRunJob("0:/gcodes/faceplate.nc");
	EXPECT_EQ(text(), "The machine power is off.");
	State::StatusChanged(OM::PrinterStatus::connecting);
	AskRunJob("0:/gcodes/faceplate.nc");
	EXPECT_EQ(text(), "No connection to the machine.");

	// The machine got busy while the question was open
	State::StatusChanged(OM::PrinterStatus::idle);
	AskRunJob("0:/gcodes/faceplate.nc");
	State::StatusChanged(OM::PrinterStatus::busy);
	Popup::PressOk();
	EXPECT_EQ(text(), "Wait until the machine is idle.");
	EXPECT_TRUE(sent.empty());
}

// ---- SEARCH and the keyboard -------------------------------------------------------------------

TEST_F(TestCncJobList, SearchFiltersAsYouType)
{
	auto view = makeView();
	auto& page = view->getJobListPage();
	page.searchPressed();
	ASSERT_TRUE(Keyboard::IsOpen());
	EXPECT_EQ(page.labelLeft(), "SEARCH");
	EXPECT_EQ(page.labelRight(), "6 of 6 in 0:/gcodes");
	EXPECT_EQ(page.rowText(2), "faceplate.nc"); // 3 rows above the keyboard, folders included
	EXPECT_EQ(page.rowText(3), "");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/job_list_keyboard.png")); // PanelDue k05_keyboard_job_search

	Keyboard::PressKey('O');
	Keyboard::PressKey('A');
	EXPECT_EQ(page.query(), "OA");
	EXPECT_EQ(page.rowText(0), "sign_oak.gcode"); // case-insensitive, folders left out
	EXPECT_EQ(page.rowText(1), "spoilboard_surface.gcode");
	EXPECT_EQ(page.labelRight(), "2 of 6 in 0:/gcodes");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/job_list_search_typing.png"));

	Keyboard::PressEnter(); // keeps the filter
	EXPECT_FALSE(Keyboard::IsOpen());
	EXPECT_TRUE(page.searching());
	EXPECT_EQ(page.rowText(2), "");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/job_list_search_kept.png"));

	page.searchPressed(); // edit the kept search
	Keyboard::PressBackspace();
	EXPECT_EQ(page.query(), "O");
	Keyboard::PressCancel(); // X: back to the plain list
	EXPECT_FALSE(page.searching());
	EXPECT_EQ(page.rowText(0), "Fixtures");
}

TEST_F(TestCncJobList, TouchesWhileTyping)
{
	auto view = makeView();
	auto& page = view->getJobListPage();
	page.searchPressed();
	Keyboard::PressKey('G');
	page.rowPressed(0); // a row: the keyboard closes, the filter stays, the row is selected
	EXPECT_FALSE(Keyboard::IsOpen());
	EXPECT_TRUE(page.searching());
	EXPECT_EQ(page.selectedName(), "bracket_v2.gcode");

	page.searchPressed();
	page.searchPressed(); // SEARCH while typing: leaves the search
	EXPECT_FALSE(Keyboard::IsOpen());
	EXPECT_FALSE(page.searching());

	page.searchPressed();
	Keyboard::PressKey('N');
	view->showPage(UI::CncHomeView::PageControl); // another page: the keyboard closes, the filter stays
	EXPECT_FALSE(Keyboard::IsOpen());
	EXPECT_TRUE(page.searching());
	EXPECT_EQ(page.query(), "N");
}

TEST_F(TestCncJobList, KeyboardKeys)
{
	auto view = makeView();
	auto& page = view->getJobListPage();
	page.searchPressed();
	Keyboard::PressShift(); // lower case
	EXPECT_TRUE(Keyboard::LowerCase());
	Keyboard::PressMode(); // symbols: SHIFT types '~'
	EXPECT_TRUE(Keyboard::Symbols());
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/keyboard_symbols.png"));
	Keyboard::PressShift();
	EXPECT_EQ(page.query(), "~");
	// The keyboard arrows page the results (here: 6 files + folder, 3 rows)
	Keyboard::PressBackspace();
	Keyboard::PressArrow(1);
	EXPECT_EQ(page.scroll(), 3u);
}

// ---- SD: a second card ----

TEST_F(TestCncJobList, SecondCard)
{
	auto view = makeView();
	auto& page = view->getJobListPage();
	EXPECT_FALSE(page.sdShown()); // one card: no SD tile
	load_model_data_from_file("tests/object_model/cnc/model_volumes_2.json");
	page.tick();
	EXPECT_TRUE(page.sdShown());

	page.cardPressed();
	EXPECT_EQ(page.card(), 1u);
	EXPECT_EQ(page.message(), "Loading...");
	load_model_data_from_file("tests/object_model/cnc/filelist_card1.json");
	EXPECT_EQ(page.labelRight(), "1:");
	EXPECT_EQ(page.rowText(0), "old");
	EXPECT_EQ(page.rowText(1), "spare_part.nc");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/job_list_card1.png"));
	page.rowPressed(1);
	EXPECT_EQ(page.selectedPath(), "1:/spare_part.nc");
	page.rowPressed(1);
	Popup::PressTrash(); // a file on the second card is deleted with its own path
	Popup::PressOk();
	EXPECT_EQ(last(), "M30 \"1:/spare_part.nc\"\n");

	// Next: back to card 0, its gcodes folder
	page.cardPressed();
	EXPECT_EQ(page.card(), 0u);
	load_model_data_from_file("tests/object_model/cnc/filelist_gcodes.json");
	EXPECT_EQ(page.labelRight(), "0:/gcodes");
	EXPECT_TRUE(page.sdShown());

	// Card 1 removed while open: back to card 0, no SD tile
	page.cardPressed();
	EXPECT_EQ(page.card(), 1u);
	load_model_data_from_file("tests/object_model/cnc/model_volumes_1.json");
	page.tick();
	EXPECT_EQ(page.card(), 0u);
	EXPECT_FALSE(page.sdShown());
}
