/*
 * test_cnc_job_preview.cpp
 *
 * JOB > PREVIEW: notices (no file, no network, failure and retry), the loaded job (operation strip,
 * paging, info strip, stock), the 3D view (ISO / TOP / FRONT, drag, double tap, G0) and the running
 * job's operation, with screenshots. The file comes from a test source (no network).
 */

#include "Debug.h"
#include "UI/Cnc/CncHomeView.h"
#include "UI/Cnc/CncKeyboard.h"
#include "UI/Cnc/CncLayout.h"
#include "UI/Cnc/CncPanel.h"
#include "UI/Cnc/CncPopup.h"
#include "UI/Cnc/CncPreviewSource.h"
#include "UI/Cnc/CncState.h"
#include "test_utils/UiTestSuite.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace UI::Cnc;
using V = UI::CncHomeView;

namespace
{
	std::string readFile(const std::string& path)
	{
		std::ifstream f(path, std::ios::binary);
		std::stringstream ss;
		ss << f.rdbuf();
		return ss.str();
	}

	const char* const Fusion = "0:/gcodes/bracket_fusion.nc";
	const char* const FreeCad = "0:/gcodes/plate_freecad.nc";
} // namespace

class TestCncJobPreview : public UiTestSuite
{
  protected:
	TestCncJobPreview()
	{
		ZoneScoped;
		lv_display_set_resolution(lv_display_get_default(), UI::CncLayout::ScreenW, UI::CncLayout::ScreenH);
		lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
		State::Reset();
		Panel::Reset();
		Panel::SetTestHook([](const std::string&) {});
		State::SetSendHook([this](std::string_view g) { sent.emplace_back(g); });
		PreviewSource::Reset();
		JobPreviewPage::SetPartDefault(false); // the toolpath views; PART tests turn it on
		PreviewSource::SetTestSource(
			[this](const std::string& path, std::string& contents)
			{
				++downloads;
				if (failDownload)
				{
					return false;
				}
				contents = readFile("tests/object_model/cnc/gcode/" + path.substr(path.find_last_of('/') + 1));
				return true;
			});
	}

	~TestCncJobPreview() override
	{
		Popup::Close();
		view.reset();
		PreviewSource::SetTestSource(nullptr);
		PreviewSource::Reset();
		Panel::SetTestHook(nullptr);
		State::SetSendHook(nullptr);
		JobPreviewPage::SetPartDefault(true);
	}

	void makeView(const std::string& listPath)
	{
		load_model_data_from_file("tests/object_model/cnc/model_move_3axis.json");
		load_model_data_from_file("tests/object_model/cnc/model_spindles.json");
		load_model_data_from_file("tests/object_model/cnc/model_tools.json");
		load_model_data_from_file("tests/object_model/cnc/model_state_idle.json");
		view = std::make_unique<V>();
		view->show();
		view->getPresenter()->newStatus(OM::PrinterStatus::idle);
		// G54 so the sample job fits the test machine's X / Y travel (-117 .. 63)
		State::SetWorkplaceOffset(0, 0, -87.0f);
		State::SetWorkplaceOffset(1, 0, -100.0f);
		path = listPath;
		page().setListPath([this] { return path; });
		view->showPage(V::PageJob);
		view->showSubTab(V::JobPreviewTab);
		page().tick();
	}

	JobPreviewPage& page() { return view->getJobPreviewPage(); }

	std::unique_ptr<V> view;
	std::string path;
	int downloads = 0;
	bool failDownload = false;
	std::vector<std::string> sent;
};

// ---- Notices ----

TEST_F(TestCncJobPreview, NothingSelected)
{
	JobPreviewPage::SetPartDefault(true); // the page as it starts (the same screenshot as TestCncHome's)
	makeView("");
	EXPECT_EQ(page().message(), "Select a job in JOB LIST");
	EXPECT_EQ(page().opButtonText(0), "");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/page_job_preview_3axis.png"));
}

TEST_F(TestCncJobPreview, NeedsNetwork)
{
	PreviewSource::SetTestSource(nullptr); // the real download: USB / UART and no address of the machine
	makeView(Fusion);
	EXPECT_EQ(page().message(), "PREVIEW needs a network connection to the machine (WiFi or Ethernet)");
	EXPECT_EQ(page().fileText(), "bracket_fusion.nc");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_needs_network.png"));
}

// ---- RUN tile ----

TEST_F(TestCncJobPreview, RunTile)
{
	makeView(FreeCad);
	EXPECT_TRUE(page().runTileShown());
	EXPECT_TRUE(page().runAvailable()); // idle, a file from JOB LIST: accent outline, play triangle
	EXPECT_EQ(page().fileText(), "plate_freecad.nc");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_run_tile.png"));
	// Operation 3 uses T5, which the machine does not have: the question names it, then asks again
	page().pressRun();
	EXPECT_EQ(Popup::Title(), "WARNING");
	EXPECT_TRUE(Popup::WarningLook());
	EXPECT_EQ(
		Popup::Lines(),
		std::vector<std::string>({"Do you want to RUN", "this job?", "plate_freecad.nc", "", "Warning: operation 3"}));
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_run_warnings.png"));
	Popup::PressOk();
	EXPECT_EQ(Popup::Title(), "WARNING");
	EXPECT_EQ(Popup::Lines(), std::vector<std::string>({"Op 3: T5 not in the tool table", "", "RUN anyway?"}));
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_run_anyway.png"));
	Popup::PressCancel(); // X: nothing started
	EXPECT_TRUE(sent.empty());

	page().pressRun();
	Popup::PressOk();
	Popup::PressOk();
	ASSERT_FALSE(sent.empty());
	EXPECT_EQ(sent.back(), "M32 \"0:/gcodes/plate_freecad.nc\"\n");
}

// No warnings: the plain question. A tool the file creates itself (M563) is not missing.
TEST_F(TestCncJobPreview, RunWithoutWarningsAndOwnTools)
{
	makeView("0:/gcodes/own_tools.nc");
	ASSERT_EQ(page().opButtonText(1).substr(0, 2), "2|");
	EXPECT_TRUE(page().warnings(1).empty()); // T5: M563 P5 in the file
	page().pressRun();
	EXPECT_EQ(Popup::Title(), "ALERT !");
	EXPECT_FALSE(Popup::WarningLook());
	EXPECT_EQ(Popup::Lines(), std::vector<std::string>({"Do you want to RUN", "this job?", "own_tools.nc"}));
	Popup::PressOk();
	ASSERT_FALSE(sent.empty());
	EXPECT_EQ(sent.back(), "M32 \"0:/gcodes/own_tools.nc\"\n");
}

// Warnings that are only information (below the stock bottom, outside the travel): asked once
TEST_F(TestCncJobPreview, RunWithInformationalWarningsAsksOnce)
{
	makeView("0:/gcodes/below_stock.nc");
	ASSERT_EQ(page().warnings(0).size(), 1u);
	EXPECT_EQ(page().warnings(0)[0].text, "Cuts 1 mm below the stock bottom");
	page().pressRun();
	EXPECT_EQ(Popup::Title(), "WARNING");
	EXPECT_EQ(
		Popup::Lines(),
		std::vector<std::string>({"Do you want to RUN", "this job?", "below_stock.nc", "", "Warning: operation 1"}));
	Popup::PressOk();
	EXPECT_FALSE(Popup::IsOpen()); // nothing serious: started
	ASSERT_FALSE(sent.empty());
	EXPECT_EQ(sent.back(), "M32 \"0:/gcodes/below_stock.nc\"\n");
}

TEST_F(TestCncJobPreview, RunTileGreyWhenItCannotRun)
{
	makeView(FreeCad);
	view->getPresenter()->newStatus(OM::PrinterStatus::busy);
	page().tick();
	EXPECT_TRUE(page().runTileShown());
	EXPECT_FALSE(page().runAvailable()); // grey, no triangle
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_run_tile_grey.png"));
	page().pressRun();
	std::string why;
	for (const std::string& l : Popup::Lines())
	{
		why += (why.empty() ? "" : " ") + l;
	}
	EXPECT_EQ(why, "Wait until the machine is idle.");
	Popup::Close();
	view->getPresenter()->newStatus(OM::PrinterStatus::idle);
	page().tick();
	EXPECT_TRUE(page().runAvailable());
	path = "";
	page().tick();
	EXPECT_FALSE(page().runTileShown()); // nothing selected: no tile
	EXPECT_TRUE(sent.empty());
}

TEST_F(TestCncJobPreview, RunWithoutNetwork)
{
	PreviewSource::SetTestSource(nullptr); // no drawing possible (USB only), the job can still be started
	makeView(Fusion);
	EXPECT_TRUE(page().runAvailable());
	EXPECT_EQ(page().fileText(), "bracket_fusion.nc");
	page().pressRun();
	Popup::PressOk();
	ASSERT_FALSE(sent.empty());
	EXPECT_EQ(sent.back(), "M32 \"0:/gcodes/bracket_fusion.nc\"\n");
}

TEST_F(TestCncJobPreview, FailureAndRetry)
{
	failDownload = true;
	makeView(Fusion);
	EXPECT_EQ(page().message(), "Cannot read bracket_fusion.nc: download failed. Tap to try again.");
	EXPECT_EQ(downloads, 1);
	failDownload = false;
	page().pressMessage();
	page().tick();
	EXPECT_EQ(downloads, 2);
	EXPECT_EQ(page().message(), "");
	EXPECT_EQ(page().opButtonText(0), "1|FACE||T1");
}

// ---- A loaded job ----

TEST_F(TestCncJobPreview, OperationsAndInfo)
{
	makeView(Fusion);
	EXPECT_EQ(page().message(), "");
	EXPECT_EQ(page().fileText(), "bracket_fusion.nc");
	EXPECT_EQ(page().stockText(), "STOCK 120 × 80 × 20");
	EXPECT_EQ(page().opButtonText(0), "1|FACE||T1");
	EXPECT_EQ(page().opButtonText(1), "2|ADAPT|CLEAR|T2");
	EXPECT_EQ(page().opButtonText(2), "3|2D|CONTOUR|T2|!"); // 0.5 mm below the stock
	EXPECT_EQ(page().selected(), 0);

	page().pressOp(1);
	EXPECT_EQ(page().selected(), 1);
	const std::string info = page().infoText();
	EXPECT_EQ(info.rfind("2  2D ADAPTIVE CLEARING1 | ", 0), 0u) << info;
	EXPECT_NE(info.find("| T2  Ø10 FLAT |"), std::string::npos) << info;
	EXPECT_NE(info.find("| Z -2 → -8 |"), std::string::npos) << info;
	EXPECT_NE(info.find("| 18000 | 1200 | COOLANT | WCS G54"), std::string::npos) << info;
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_iso.png"));
}

TEST_F(TestCncJobPreview, PagingThroughOperations)
{
	makeView(Fusion);
	page().pressUp(); // already at the top
	EXPECT_EQ(page().firstShown(), 0);
	page().pressDown();
	EXPECT_EQ(page().firstShown(), 2); // 5 operations: the last 3
	EXPECT_EQ(page().opButtonText(0), "3|2D|CONTOUR|T2|!");
	EXPECT_EQ(page().opButtonText(2), "5|DRILL||T4|!"); // T4 is not in the tool table
	page().pressDown();
	EXPECT_EQ(page().firstShown(), 2);
	page().pressOp(2);
	EXPECT_EQ(page().selected(), 4);
	const std::string info = page().infoText();
	EXPECT_NE(info.find("| T4  Ø6 DRILL |"), std::string::npos) << info;
	EXPECT_NE(info.find("| Z 2 → -22 |"), std::string::npos) << info; // a drill: from its approach height
	EXPECT_NE(info.find("| COOL OFF |"), std::string::npos) << info;
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_last_ops.png"));
	page().pressUp();
	EXPECT_EQ(page().firstShown(), 0);
}

TEST_F(TestCncJobPreview, RapidsOfTheSelectedOperation)
{
	makeView(Fusion);
	const uint32_t without = page().view().drawnSegments();
	page().pressRapids();
	EXPECT_TRUE(page().view().showRapids());
	EXPECT_GT(page().view().drawnSegments(), without);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_rapids.png"));
	// another operation: its own rapids only, the toggle stays on
	page().pressOp(3);
	EXPECT_TRUE(page().view().showRapids());
	page().pressRapids();
	EXPECT_FALSE(page().view().showRapids());
}

TEST_F(TestCncJobPreview, ViewPresetsDragAndDoubleTap)
{
	makeView(Fusion);
	EXPECT_EQ(page().view().preset(), PreviewView::Preset::Iso);
	page().pressPreset();
	EXPECT_EQ(page().view().preset(), PreviewView::Preset::Top);
	EXPECT_FLOAT_EQ(page().view().pitch(), 90.0f);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_top.png"));
	page().pressPreset();
	EXPECT_EQ(page().view().preset(), PreviewView::Preset::Front);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_front.png"));
	page().pressPreset();
	EXPECT_EQ(page().view().preset(), PreviewView::Preset::Side);
	EXPECT_EQ(page().presetText(), "SIDE");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_side.png"));
	page().pressPreset();
	EXPECT_EQ(page().view().preset(), PreviewView::Preset::Iso);

	page().view().rotate(30.0f, 20.0f);
	page().view().endRotate();
	EXPECT_EQ(page().view().preset(), PreviewView::Preset::Free);
	EXPECT_FLOAT_EQ(page().view().yaw(), -5.0f);
	EXPECT_FLOAT_EQ(page().view().pitch(), 52.0f);
	page().view().rotate(0.0f, 80.0f);
	EXPECT_FLOAT_EQ(page().view().pitch(), 90.0f); // no further than straight down
	page().doubleTap();
	EXPECT_EQ(page().view().preset(), PreviewView::Preset::Iso);
	EXPECT_FLOAT_EQ(page().view().yaw(), -35.0f);
}

// ---- PART: the simulated part, the cut point and the sections ----

TEST_F(TestCncJobPreview, PartViews)
{
	JobPreviewPage::SetPartDefault(true);
	makeView(Fusion);
	EXPECT_TRUE(page().partOn());
	page().pressOp(1); // 2D ADAPTIVE CLEARING: the part after the facing and the pocket
	EXPECT_EQ(page().view().stockMap().lastOp, 1);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_part_iso.png"));
	page().pressPreset();
	EXPECT_EQ(page().presetText(), "TOP");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_part_top.png"));
	page().pressPreset();
	EXPECT_EQ(page().presetText(), "FRONT"); // no cut point: a drawing, not a section
	EXPECT_EQ(page().cutText(), "");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_part_front.png"));
	page().pressPreset();
	EXPECT_EQ(page().presetText(), "SIDE");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_part_side.png"));
	page().pressPart(); // off: the toolpath view again
	EXPECT_FALSE(page().partOn());
	page().pressPart();
	EXPECT_TRUE(page().partOn());
}

TEST_F(TestCncJobPreview, TapPlacesTheCutAndSelectsTheOperation)
{
	JobPreviewPage::SetPartDefault(true);
	makeView(Fusion);
	page().pressPreset(); // TOP: the hole at X105 Y15 is a point
	const lv_point_t p = page().view().canvasPoint(105.0f, 15.0f, -10.0f);
	page().view().tapAt(p.x + 3, p.y - 2); // a finger is not exact
	EXPECT_TRUE(page().view().hasCutPoint());
	EXPECT_NEAR(page().view().cutX(), 105.0f, 0.01f);
	EXPECT_NEAR(page().view().cutY(), 15.0f, 0.01f);
	EXPECT_EQ(page().selected(), 4); // DRILL, the tapped move's operation
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_cut_point_top.png"));

	page().pressPreset(); // FRONT is now a section along X through the point
	EXPECT_EQ(page().presetText(), "CUT X");
	EXPECT_EQ(page().cutText(), "CUT X  ·  Y 15");
	EXPECT_TRUE(page().view().sectionShown());
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_cut_x.png"));
	page().pressPreset();
	EXPECT_EQ(page().presetText(), "CUT Y");
	EXPECT_EQ(page().cutText(), "CUT Y  ·  X 105");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_cut_y.png"));

	page().pressPreset(); // ISO: a crosshair on the hole
	EXPECT_EQ(page().presetText(), "ISO");
	EXPECT_EQ(page().cutText(), "");
	page().view().tapAt(6, 6); // away from every move: no cut point
	EXPECT_FALSE(page().view().hasCutPoint());
	EXPECT_EQ(page().selected(), 4); // the operation stays
	page().pressPreset();
	page().pressPreset();
	EXPECT_EQ(page().presetText(), "FRONT");
}

TEST_F(TestCncJobPreview, SectionsAlsoWithoutPart)
{
	makeView(Fusion); // PART off
	page().pressDown();
	page().pressOp(2); // DRILL selected: its hole wins over the facing pass on top of it
	page().pressPreset();
	const lv_point_t p = page().view().canvasPoint(15.0f, 65.0f, -10.0f);
	page().view().tapAt(p.x, p.y);
	EXPECT_TRUE(page().view().hasCutPoint());
	page().pressPreset();
	EXPECT_TRUE(page().view().sectionShown()); // a section always shows the simulated part
	EXPECT_EQ(page().cutText(), "CUT X  ·  Y 65");
	EXPECT_EQ(page().view().stockMap().lastOp, 4);
}

TEST_F(TestCncJobPreview, ANewFileClearsTheCutPoint)
{
	JobPreviewPage::SetPartDefault(true);
	makeView(Fusion);
	page().pressPreset();
	const lv_point_t p = page().view().canvasPoint(105.0f, 65.0f, -10.0f);
	page().view().tapAt(p.x, p.y);
	EXPECT_TRUE(page().view().hasCutPoint());
	path = FreeCad;
	page().tick();
	EXPECT_EQ(page().fileText(), "plate_freecad.nc");
	EXPECT_FALSE(page().view().hasCutPoint());
}

TEST_F(TestCncJobPreview, ToolpathBoxWithoutStockLine)
{
	makeView(FreeCad);
	EXPECT_EQ(page().stockText(), "TOOLPATH 56 × 36 × 10");
	EXPECT_EQ(page().opButtonText(0), "1|PROFILE||T2");
	EXPECT_EQ(page().opButtonText(2), "3|DRILL||T5|!"); // "Drilling" shortened; T5 not in the tool table
}

// ---- A running job ----

TEST_F(TestCncJobPreview, RunningOperationIsMarkedAndFollowed)
{
	makeView(Fusion);
	const std::string text = readFile("tests/object_model/cnc/gcode/bracket_fusion.nc");
	view->getPresenter()->newStatus(OM::PrinterStatus::processing);
	page().setJobFile(Fusion);
	view->showSubTab(V::JobPreviewTab);
	page().tick();
	page().setJobPosition((uint32_t)text.find("(Pocket Finishing3)") + 30);
	EXPECT_EQ(page().runningOp(), 3);
	EXPECT_EQ(page().selected(), 3);
	EXPECT_EQ(page().opButtonText(2), "4|POCKET|FINISH|T3|RUN");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_running.png"));

	// A pick by hand stays while the job goes on to the next operation
	page().pressUp();
	page().pressOp(0);
	page().setJobPosition((uint32_t)text.find("(Drill4)") + 30);
	EXPECT_EQ(page().runningOp(), 4);
	EXPECT_EQ(page().selected(), 0);
	lv_tick_inc(JobPreviewPage::ManualSelectHoldMs + 100);
	page().setJobPosition((uint32_t)text.find("(Drill4)") + 60);
	EXPECT_EQ(page().selected(), 0); // same operation: nothing new to follow
	// The job ends: no running operation any more, without another position update
	view->getPresenter()->newStatus(OM::PrinterStatus::idle);
	page().tick();
	EXPECT_EQ(page().runningOp(), -1);
	EXPECT_EQ(page().opButtonText(2).find("RUN"), std::string::npos);
}

// A running job: the moves already run in the done colour (only the new ones drawn as it goes on); PART as
// the finished operations left the part, the running one added when it is finished
TEST_F(TestCncJobPreview, ProgressOfTheRunningJob)
{
	JobPreviewPage::SetPartDefault(true);
	makeView(Fusion);
	const std::string text = readFile("tests/object_model/cnc/gcode/bracket_fusion.nc");
	view->getPresenter()->newStatus(OM::PrinterStatus::processing);
	page().setJobFile(Fusion);
	view->showSubTab(V::JobPreviewTab);
	page().tick();
	const auto program = PreviewSource::Current().program;
	ASSERT_TRUE(program != nullptr);
	const uint32_t opStart = program->ops[1].fileStart;
	const uint32_t opEnd = program->ops[2].fileStart;
	const uint32_t half = opStart + (opEnd - opStart) / 2;
	page().setJobPosition(half); // halfway through the adaptive clearing
	EXPECT_EQ(page().runningOp(), 1);
	EXPECT_EQ(page().view().done(), Gcode::PointsBefore(*program, half));
	EXPECT_GT(page().view().done(), program->ops[1].firstPoint);
	EXPECT_EQ(page().view().partOp(), 0); // the facing pass only: the running operation is not finished
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_progress.png"));
	page().setJobPosition(opEnd + 10); // the next operation
	EXPECT_EQ(page().runningOp(), 2);
	EXPECT_EQ(page().view().partOp(), 1); // the adaptive clearing is in the part now
	page().pressOp(0);					  // another operation by hand: the part as usual
	EXPECT_EQ(page().view().partOp(), page().selected());
	view->getPresenter()->newStatus(OM::PrinterStatus::idle);
	page().tick();
	EXPECT_EQ(page().view().done(), 0u); // the job ended
	JobPreviewPage::SetPartDefault(false);
}

// Drawing only the newly run moves gives the same picture as drawing it all again
TEST_F(TestCncJobPreview, ProgressDrawnStepByStepMatchesAFullDraw)
{
	makeView(Fusion);
	view->getPresenter()->newStatus(OM::PrinterStatus::processing);
	page().setJobFile(Fusion);
	view->showSubTab(V::JobPreviewTab);
	page().tick();
	const auto program = PreviewSource::Current().program;
	ASSERT_TRUE(program != nullptr);
	const uint32_t a = program->ops[1].fileStart, b = program->ops[2].fileStart;
	for (int i = 1; i <= 8; ++i)
	{
		page().setJobPosition(a + (b - a) * (uint32_t)i / 9); // step by step
	}
	const std::vector<uint32_t> stepped = page().view().pixels();
	page().view().redraw();
	const std::vector<uint32_t>& full = page().view().pixels();
	ASSERT_EQ(stepped.size(), full.size());
	size_t differ = 0;
	for (size_t i = 0; i < full.size(); ++i)
	{
		differ += (stepped[i] != full[i]) ? 1 : 0;
	}
	std::printf("[progress] %zu of %zu pixels differ\n", differ, full.size());
	EXPECT_EQ(differ, 0u); // the done moves on top in both
}

TEST_F(TestCncJobPreview, RunningJobFileWinsOverTheList)
{
	makeView(FreeCad);
	EXPECT_EQ(page().fileText(), "plate_freecad.nc");
	page().setJobFile(Fusion);
	view->getPresenter()->newStatus(OM::PrinterStatus::processing);
	EXPECT_EQ(view->getSubTab(V::PageJob), (unsigned int)V::JobStatusTab); // a job start shows JOB STATUS
	view->showSubTab(V::JobPreviewTab);									   // back to PREVIEW
	page().tick();
	EXPECT_EQ(page().fileText(), "bracket_fusion.nc");
}

// SETTINGS > PREVIEW IN JOB: ANY FILE: another file during a job, RUNNING goes back; the file shown
// before is kept, so going back and forth downloads nothing
TEST_F(TestCncJobPreview, NextJobDuringARunningJob)
{
	makeView(FreeCad);
	EXPECT_EQ(downloads, 1);
	page().setJobFile(Fusion);
	view->getPresenter()->newStatus(OM::PrinterStatus::processing);
	view->showSubTab(V::JobPreviewTab);
	page().tick();
	EXPECT_EQ(page().fileText(), "bracket_fusion.nc");
	EXPECT_FALSE(page().runningShown());
	EXPECT_EQ(downloads, 2);

	page().openListFile(); // the JOB popup's check on plate_freecad.nc
	EXPECT_EQ(page().fileText(), "plate_freecad.nc");
	EXPECT_EQ(page().opButtonText(0).substr(0, 2), "1|");
	EXPECT_TRUE(page().runningShown());
	EXPECT_FALSE(page().runAvailable()); // grey: a job runs
	EXPECT_EQ(page().runningOp(), -1);	 // not the running file
	EXPECT_EQ(downloads, 2);			 // kept
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_next_job.png"));
	page().pressRun();
	EXPECT_EQ(Popup::Title(), "ALERT !");
	Popup::Close();

	page().pressRunning();
	EXPECT_EQ(page().fileText(), "bracket_fusion.nc");
	EXPECT_FALSE(page().runningShown());
	EXPECT_EQ(downloads, 2);
	page().openListFile();
	EXPECT_EQ(page().fileText(), "plate_freecad.nc");
	EXPECT_EQ(downloads, 2);

	// A new job (or the end of this one): PREVIEW follows the job again
	view->getPresenter()->newStatus(OM::PrinterStatus::idle);
	page().tick();
	EXPECT_EQ(page().fileText(), "plate_freecad.nc"); // JOB LIST's file
	EXPECT_FALSE(page().runningShown());
	view->getPresenter()->newStatus(OM::PrinterStatus::processing);
	view->showSubTab(V::JobPreviewTab);
	page().tick();
	EXPECT_EQ(page().fileText(), "bracket_fusion.nc");
}

TEST_F(TestCncJobPreview, PreviewInJobRunningJobOnly)
{
	Panel::SetPreviewDuringJob(Panel::PreviewInJob::RunningJob);
	makeView(FreeCad);
	page().setJobFile(Fusion);
	view->getPresenter()->newStatus(OM::PrinterStatus::processing);
	view->showSubTab(V::JobPreviewTab);
	page().openListFile();
	EXPECT_EQ(page().fileText(), "bracket_fusion.nc");
	EXPECT_FALSE(page().runningShown());
}

// OFF: nothing is downloaded while a job runs; a file PREVIEW already holds is still shown
TEST_F(TestCncJobPreview, PreviewInJobOff)
{
	Panel::SetPreviewDuringJob(Panel::PreviewInJob::Off);
	makeView(FreeCad);
	EXPECT_EQ(downloads, 1);
	page().setJobFile(Fusion);
	view->getPresenter()->newStatus(OM::PrinterStatus::processing);
	view->showSubTab(V::JobPreviewTab);
	page().tick();
	EXPECT_EQ(page().message(), "PREVIEW is off while a job runs\n(SETTINGS > PREVIEW IN JOB)");
	EXPECT_EQ(page().fileText(), "bracket_fusion.nc");
	EXPECT_EQ(page().opButtonText(0), "");
	EXPECT_EQ(downloads, 1);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_off_in_job.png"));
	page().openListFile(); // not with OFF
	EXPECT_EQ(page().fileText(), "bracket_fusion.nc");
	EXPECT_EQ(downloads, 1);

	// The job ends: downloads again
	view->getPresenter()->newStatus(OM::PrinterStatus::idle);
	page().tick();
	EXPECT_EQ(page().message(), "");
	EXPECT_EQ(page().fileText(), "plate_freecad.nc");

	// The job's file opened before RUN: shown, nothing downloaded
	path = Fusion;
	page().tick();
	EXPECT_EQ(downloads, 2);
	view->getPresenter()->newStatus(OM::PrinterStatus::processing);
	view->showSubTab(V::JobPreviewTab);
	page().tick();
	EXPECT_EQ(page().message(), "");
	EXPECT_EQ(page().fileText(), "bracket_fusion.nc");
	EXPECT_EQ(downloads, 2);
}

// A file uploaded again under the same name (JOB LIST shows another size / date): read again when it is
// opened again from JOB LIST; the same file is not
TEST_F(TestCncJobPreview, ChangedFileIsReadAgainWhenOpenedAgain)
{
	PreviewSource::Stamp stamp{12345, "2026-10-08T10:00:00"};
	makeView("");
	page().setStampOf([&stamp](const std::string&) { return stamp; });
	path = Fusion;
	page().tick();
	EXPECT_EQ(downloads, 1);
	page().openListFile(); // the JOB popup's check again: unchanged
	EXPECT_EQ(downloads, 1);
	stamp.date = "2026-10-08T11:30:00"; // uploaded again
	page().tick();
	EXPECT_EQ(downloads, 1); // not by itself
	page().openListFile();
	EXPECT_EQ(downloads, 2);
	EXPECT_EQ(page().fileText(), "bracket_fusion.nc");
	EXPECT_EQ(page().message(), "");
}

// Over the size limit (from JOB LIST): refused before any download
TEST_F(TestCncJobPreview, TooLargeIsRefusedBeforeTheDownload)
{
	makeView("");
	page().setStampOf([](const std::string&) { return PreviewSource::Stamp{40u * 1024u * 1024u, "x"}; });
	path = Fusion;
	page().tick();
	EXPECT_EQ(downloads, 0);
	EXPECT_EQ(page().message(), "bracket_fusion.nc is too large for PREVIEW (over 32 MB)");
	EXPECT_TRUE(page().runTileShown()); // it can still be started
}

// SPLIT OPERATIONS changed while a job runs with PREVIEW IN JOB: OFF: read again only after the job
TEST_F(TestCncJobPreview, SplitChangeWaitsForTheJobWithPreviewOff)
{
	Panel::SetPreviewDuringJob(Panel::PreviewInJob::Off);
	makeView(Fusion);
	page().setJobFile(Fusion);
	view->getPresenter()->newStatus(OM::PrinterStatus::processing);
	view->showSubTab(V::JobPreviewTab);
	page().tick();
	EXPECT_EQ(downloads, 1);
	Panel::SetPreviewToolChangesOnly(true);
	page().tick();
	EXPECT_EQ(downloads, 1);										 // not during the job
	EXPECT_EQ(page().opButtonText(2).substr(0, 12), "3|2D|CONTOUR"); // still split as it was read
	view->getPresenter()->newStatus(OM::PrinterStatus::idle);
	view->showSubTab(V::JobPreviewTab);
	page().tick();
	EXPECT_EQ(downloads, 2); // now with the new setting
	EXPECT_EQ(page().opButtonText(2), "3|POCKET|FINISH|T3");
	Panel::SetPreviewToolChangesOnly(false);
}

// The machine's tool over the drawing: shown in the selected operation's work offset, follows the position
TEST_F(TestCncJobPreview, ToolMarker)
{
	makeView(Fusion);
	page().tick();
	EXPECT_TRUE(page().view().toolShown()); // G54 active, the job in G54
	const lv_point_t before = page().view().toolTip();
	EXPECT_EQ(before.x, page().view().canvasPoint(12.345f, -5.5f, 3.2f).x);
	State::SetWorkPosition(0, 60.0f); // X moves
	page().tick();
	const lv_point_t after = page().view().toolTip();
	EXPECT_EQ(after.x, page().view().canvasPoint(60.0f, -5.5f, 3.2f).x);
	EXPECT_NE(after.x, before.x);
	State::SetActiveWorkplace(1); // G55: not the drawing's coordinates
	page().tick();
	EXPECT_FALSE(page().view().toolShown());
	State::SetActiveWorkplace(0);
	page().tick();
	EXPECT_TRUE(page().view().toolShown());
}

// The real download (rr_connect, rr_download with the session key, rr_disconnect) against a web server
// that answers like RRF. Manual: CNC_TEST_HTTP=127.0.0.1:8765 with such a server running, then
// --gtest_also_run_disabled_tests. Skipped without it.
TEST(TestCncPreviewSource, DISABLED_RealHttpDownload)
{
	const char* host = std::getenv("CNC_TEST_HTTP");
	if (host == nullptr)
	{
		GTEST_SKIP() << "CNC_TEST_HTTP not set";
	}
	Panel::SetTestHook([](const std::string&) {}); // USB / UART connection: the machine's reported address
	PreviewSource::Reset();
	PreviewSource::SetTestSource(nullptr);
	PreviewSource::SetMachineIp(host);
	PreviewSource::Request(Fusion);
	EXPECT_EQ(PreviewSource::Current().status, PreviewSource::Status::Loading);
	for (int i = 0; i < 100 && PreviewSource::Current().status == PreviewSource::Status::Loading; ++i)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	const PreviewSource::Result r = PreviewSource::Current();
	EXPECT_EQ(r.status, PreviewSource::Status::Ready) << r.message;
	ASSERT_TRUE(r.program != nullptr);
	EXPECT_EQ(r.program->ops.size(), 5u);

	PreviewSource::Request("0:/gcodes/missing.nc");
	for (int i = 0; i < 100 && PreviewSource::Current().status == PreviewSource::Status::Loading; ++i)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	EXPECT_EQ(PreviewSource::Current().status, PreviewSource::Status::Failed);
	EXPECT_EQ(PreviewSource::Current().message, "file not found");
	PreviewSource::Reset();
	Panel::SetTestHook(nullptr);
}

// ---- Warnings, details, password ----

TEST_F(TestCncJobPreview, WarningsAndInfoPopups)
{
	makeView(Fusion);
	EXPECT_TRUE(page().warnings(0).empty());
	EXPECT_TRUE(page().warnings(1).empty());
	ASSERT_EQ(page().warnings(2).size(), 1u);
	EXPECT_EQ(page().warnings(2)[0].text, "Cuts 0.5 mm below the stock bottom");
	EXPECT_EQ(page().warnings(2)[0].detail, "(Z -20.5, stock 20 mm)");
	ASSERT_EQ(page().warnings(4).size(), 2u); // the drill goes through: Z -22 in 20 mm stock
	EXPECT_EQ(page().warnings(4)[0].text, "Cuts 2 mm below the stock bottom");
	EXPECT_EQ(page().warnings(4)[1].text, "T4 is not in the tool table");

	// No warnings: no warning button; the name button opens the info popup (facts only)
	page().pressOp(1);
	EXPECT_FALSE(page().warningButtonShown());
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_no_warning.png"));
	page().pressInfo();
	EXPECT_EQ(Popup::Title(), "OPERATION 2");
	std::vector<std::string> lines = Popup::Lines();
	ASSERT_EQ(lines.size(), 4u);
	EXPECT_EQ(lines[0], "X 35 → 85   Y 25 → 55");
	EXPECT_EQ(lines[1], "Z -2 → -8");
	EXPECT_EQ(lines[2].rfind("CUT ", 0), 0u) << lines[2];
	EXPECT_EQ(lines[3], "T2  FLAT END MILL");
	Popup::PressCancel();
	page().pressWarnings(); // nothing to show
	EXPECT_FALSE(Popup::IsOpen());

	// The drill: info without warnings, the warning button for them
	page().pressDown();
	page().pressOp(2);
	EXPECT_TRUE(page().warningButtonShown());
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_warning_button.png"));
	page().pressInfo();
	EXPECT_EQ(Popup::Title(), "OPERATION 5");
	ASSERT_FALSE(Popup::Lines().empty());
	EXPECT_EQ(Popup::Lines()[0], "X 15 → 105   Y 15 → 65");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_info.png"));
	Popup::PressCancel();
	page().pressWarnings();
	EXPECT_EQ(Popup::Title(), "WARNINGS");
	lines = Popup::Lines();
	ASSERT_EQ(lines.size(), 5u);
	EXPECT_EQ(lines[0], "Cuts 2 mm below the stock bottom");
	EXPECT_EQ(lines[1], "(Z -22, stock 20 mm)");
	EXPECT_EQ(lines[2], "");
	EXPECT_EQ(lines[3], "T4 is not in the tool table");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_warnings.png"));
	Popup::PressCancel();

	// One warning: WARNING
	page().pressOp(0);
	page().pressWarnings();
	EXPECT_EQ(Popup::Title(), "WARNING");
	Popup::PressCancel();

	// The machine's own G54 (X / Y 100) puts the job outside the travel
	State::SetWorkplaceOffset(0, 0, 100.0f);
	State::SetWorkplaceOffset(1, 0, 100.0f);
	ASSERT_FALSE(page().warnings(0).empty());
	EXPECT_EQ(page().warnings(0).back().text, "Outside the X and Y travel");
	EXPECT_EQ(page().warnings(0).back().detail, "(machine X -117 → 63, Y -117 → 63)");
}

TEST_F(TestCncJobPreview, PasswordRejected)
{
	PreviewSource::SetTestSource(
		[this](const std::string& path, std::string& contents)
		{
			++downloads;
			if (Panel::DuetPassword() != "SECRET")
			{
				contents = PreviewSource::WrongPassword;
				return false;
			}
			contents = readFile("tests/object_model/cnc/gcode/" + path.substr(path.find_last_of('/') + 1));
			return true;
		});
	makeView(Fusion);
	EXPECT_EQ(page().message(), "The machine rejected the password. Tap to enter it.");
	page().pressMessage();
	ASSERT_TRUE(Keyboard::IsOpenFor(JobPreviewPage::KeyboardOwner));
	EXPECT_EQ(Popup::Title(), "MACHINE PASSWORD");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/preview_password.png"));
	for (char c : std::string("SECRET"))
	{
		Keyboard::PressKey(c);
	}
	Keyboard::PressEnter();
	page().tick();
	EXPECT_EQ(Panel::DuetPassword(), "SECRET");
	EXPECT_EQ(downloads, 2);
	EXPECT_EQ(page().message(), "");
	EXPECT_EQ(page().opButtonText(0), "1|FACE||T1");
}

TEST_F(TestCncJobPreview, SplitOperationsSetting)
{
	makeView(Fusion);
	EXPECT_EQ(page().opButtonText(2), "3|2D|CONTOUR|T2|!");
	Panel::SetPreviewToolChangesOnly(true);
	PreviewSource::Reload(); // as SETTINGS does
	page().tick();
	EXPECT_EQ(downloads, 2);
	EXPECT_EQ(page().opButtonText(1), "2|ADAPT|CLEAR|T2|!"); // with the contour, below the stock
	EXPECT_EQ(page().opButtonText(2), "3|POCKET|FINISH|T3");
	Panel::SetPreviewToolChangesOnly(false);
}

// How long the simulation and one PART frame take (printed; the panel is roughly 10x slower)
TEST_F(TestCncJobPreview, PartTiming)
{
	JobPreviewPage::SetPartDefault(true);
	makeView(Fusion);
	page().pressDown();
	page().pressOp(2); // all 5 operations simulated
	const auto t0 = std::chrono::steady_clock::now();
	for (int k = 0; k < 10; ++k)
	{
		page().view().redraw();
	}
	const auto t1 = std::chrono::steady_clock::now();
	const double frameMs = std::chrono::duration<double, std::milli>(t1 - t0).count() / 10.0;
	std::printf("[timing] simulation %u ms, ISO PART frame %.1f ms, cells %d x %d\n",
				page().view().simMs(),
				frameMs,
				page().view().stockMap().nx,
				page().view().stockMap().ny);
	EXPECT_LT(frameMs, 500.0);
}
