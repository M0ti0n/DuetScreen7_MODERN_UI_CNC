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
		PreviewSource::Reset();
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
};

// ---- Notices ----

TEST_F(TestCncJobPreview, NothingSelected)
{
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
