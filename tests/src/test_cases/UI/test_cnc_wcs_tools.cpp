/*
 * test_cnc_wcs_tools.cpp
 *
 * WCS > TOOLS of the CNC UI: selection, paging, LOAD / MEASURE / SET Z with their popups and
 * refusals, and screenshots (compare with the PanelDue CNC UI references x 1.25).
 *
 * Machine: T0 6mm Endmill 0, T1 3mm Ballnose -1.25 (loaded), T2 Probe 0.5, T3 V-Bit 60 2.1.
 */

#include "Debug.h"
#include "UI/Cnc/CncHomeView.h"
#include "UI/Cnc/CncLayout.h"
#include "UI/Cnc/CncPopup.h"
#include "UI/Cnc/CncState.h"
#include "test_utils/UiTestSuite.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace UI::Cnc;
using Lines = std::vector<std::string>;

class TestCncWcsTools : public UiTestSuite
{
  protected:
	TestCncWcsTools()
	{
		ZoneScoped;
		lv_display_set_resolution(lv_display_get_default(), UI::CncLayout::ScreenW, UI::CncLayout::ScreenH);
		lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
		State::Reset();
		State::SetSendHook([this](std::string_view g) { sent.emplace_back(g); });
	}

	~TestCncWcsTools() override { Popup::Close(); }

	// The frame on WCS > TOOLS with machine data, idle
	std::unique_ptr<UI::CncHomeView> makeView()
	{
		load_model_data_from_file("tests/object_model/cnc/model_move_3axis.json");
		load_model_data_from_file("tests/object_model/cnc/model_spindles.json");
		load_model_data_from_file("tests/object_model/cnc/model_tools.json");
		load_model_data_from_file("tests/object_model/cnc/model_state_idle.json");
		auto v = std::make_unique<UI::CncHomeView>();
		v->show();
		v->showPage(UI::CncHomeView::PageWcs);
		v->showSubTab(1);
		State::StatusChanged(OM::PrinterStatus::idle);
		return v;
	}

	// Popup lines joined with spaces (long ALERT texts wrap)
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

// ---- Looks, selection, paging ------------------------------------------------------------------

TEST_F(TestCncWcsTools, ListFromTheMachine)
{
	auto view = makeView();
	auto& page = view->getWcsToolsPage();
	EXPECT_EQ(State::Tools().size(), 4u);
	EXPECT_EQ(State::ActiveTool(), 1);
	EXPECT_EQ(page.selectedTool(), 1); // the loaded tool
	EXPECT_EQ(page.rowTool(3), 3);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_tools_3axis.png"));
}

TEST_F(TestCncWcsTools, SelectRow)
{
	auto view = makeView();
	auto& page = view->getWcsToolsPage();
	page.rowPressed(0);
	EXPECT_EQ(page.selectedTool(), 0);
	EXPECT_TRUE(sent.empty());
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_tools_t0_selected.png")); // as the PanelDue reference
}

TEST_F(TestCncWcsTools, PagingAndSelectionFollowsToolChange)
{
	auto view = makeView();
	auto& page = view->getWcsToolsPage();
	State::SetTools({{0, "A", 0.0f}, {1, "B", -1.25f}, {2, "C", 0.5f}, {3, "D", 2.1f}, {5, "E", 0.0f}, {7, "F", 1.0f}});
	EXPECT_EQ(page.firstRow(), 0u);
	page.pagePressed(-1); // already at the top: greyed
	EXPECT_EQ(page.firstRow(), 0u);
	page.pagePressed(1);
	EXPECT_EQ(page.firstRow(), 4u);
	EXPECT_EQ(page.rowTool(0), 5);
	EXPECT_EQ(page.rowTool(1), 7);
	EXPECT_EQ(page.rowTool(2), -1);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_tools_page2.png"));
	page.pagePressed(1); // the end
	EXPECT_EQ(page.firstRow(), 4u);
	page.pagePressed(-1);
	EXPECT_EQ(page.firstRow(), 0u);

	// The selection was the loaded tool: a tool change moves it along, and the list pages to it
	State::SetActiveTool(7);
	EXPECT_EQ(page.selectedTool(), 7);
	EXPECT_EQ(page.firstRow(), 4u);
	// A selection of another tool stays where it is
	page.pagePressed(-1);
	page.rowPressed(0);
	State::SetActiveTool(5);
	EXPECT_EQ(page.selectedTool(), 0);
	EXPECT_EQ(page.firstRow(), 0u);
}

// ---- LOAD --------------------------------------------------------------------------------------

TEST_F(TestCncWcsTools, LoadToolChangeAsksFirst)
{
	auto view = makeView();
	auto& page = view->getWcsToolsPage();
	page.rowPressed(2);
	page.loadPressed();
	EXPECT_EQ(Popup::Title(), "LOAD T2");
	EXPECT_EQ(Popup::Lines(), Lines({"Change T1 -> T2?", "Probe", "Runs the tool change."}));
	EXPECT_TRUE(sent.empty());
	Popup::PressOk();
	EXPECT_EQ(last(), "T2\n");
}

TEST_F(TestCncWcsTools, LoadWithEmptySpindleAsksWhatIsIn)
{
	auto view = makeView();
	auto& page = view->getWcsToolsPage();
	State::SetActiveTool(-1); // T-
	page.rowPressed(3);
	page.loadPressed();
	ASSERT_TRUE(Popup::IsFormOpen());
	EXPECT_EQ(Popup::Title(), "LOAD T3");
	EXPECT_EQ(Popup::Lines(), Lines({"IS T3 IN THE SPINDLE?"}));
	EXPECT_FALSE(Popup::FormOkEnabled()); // no answer yet
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_tools_load_question.png"));
	Popup::PressFormItem(0, 0); // YES: only noted, nothing moves
	Popup::PressOk();
	EXPECT_EQ(sent, Lines({"T3 P0\n", "echo >\"0:/sys/cnc-lasttool.g\" \"T3 P0\"\n"}));

	sent.clear();
	page.loadPressed();
	Popup::PressFormItem(0, 1); // NO: tool change
	Popup::PressOk();
	EXPECT_EQ(sent, Lines({"T3\n"}));
}

TEST_F(TestCncWcsTools, LoadRefusals)
{
	auto view = makeView();
	auto& page = view->getWcsToolsPage();
	page.loadPressed();
	EXPECT_EQ(text(), "T1 is already loaded.");
	State::SetHomed(2, false);
	page.rowPressed(0);
	page.loadPressed();
	EXPECT_EQ(text(), "Home X, Y and Z first.");
	EXPECT_TRUE(sent.empty());
}

// ---- MEASURE -----------------------------------------------------------------------------------

TEST_F(TestCncWcsTools, MeasureTheLoadedTool)
{
	auto view = makeView();
	auto& page = view->getWcsToolsPage();
	page.measurePressed();
	EXPECT_EQ(Popup::Title(), "MEASURE T1");
	EXPECT_EQ(Popup::Lines(), Lines({"Measure T1 length", "on the tool setter?"}));
	Popup::PressOk();
	EXPECT_EQ(last(), "M98 P\"0:/sys/toolchange/measure_tool.g\" K1\n");

	page.rowPressed(0);
	page.measurePressed();
	EXPECT_EQ(text(), "Only the loaded tool (T1) can be measured.");
	page.rowPressed(1);
	State::SetToolSetter(false);
	page.measurePressed();
	EXPECT_EQ(Popup::Lines(), Lines({"No tool setter.", "TOOL SETTER is off in SETTINGS."}));
	State::SetActiveTool(-1);
	page.measurePressed();
	EXPECT_EQ(Popup::Lines(), Lines({"No tool is loaded.", "LOAD a tool first."}));
}

// ---- SET Z -------------------------------------------------------------------------------------

TEST_F(TestCncWcsTools, ZTileOpensSetZ)
{
	auto view = makeView();
	auto& page = view->getWcsToolsPage();
	State::MarkOffsetsUnsaved();
	page.zPressed(2);
	EXPECT_EQ(page.selectedTool(), 2);
	ASSERT_TRUE(Popup::IsNumpadOpen());
	EXPECT_EQ(Popup::NumpadTag(), "T2 Z");
	EXPECT_EQ(Popup::NumpadValue(), "0.500");
	for (char c : std::string("-3.25"))
	{
		Popup::NumpadKey(c);
	}
	Popup::PressOk();
	EXPECT_EQ(last(), "G10 P2 Z-3.250\nM500 P10\n");
	EXPECT_FALSE(State::OffsetsUnsaved()); // M500 saved the work offsets too
}

// ---- During a job ------------------------------------------------------------------------------

TEST_F(TestCncWcsTools, LockedDuringJob)
{
	auto view = makeView();
	auto& page = view->getWcsToolsPage();
	State::StatusChanged(OM::PrinterStatus::paused);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_tools_job.png"));
	const std::string locked = "Locked while a job is running or paused.";
	page.loadPressed();
	EXPECT_EQ(text(), locked);
	page.measurePressed();
	EXPECT_EQ(text(), locked);
	page.setZPressed();
	EXPECT_EQ(text(), locked);
	page.zPressed(0); // selects, but no editing
	EXPECT_EQ(page.selectedTool(), 0);
	EXPECT_EQ(text(), locked);
	EXPECT_TRUE(sent.empty());
}
