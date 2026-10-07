/*
 * test_cnc_home.cpp
 *
 * Screenshot tests for the portrait CNC UI frame (src/UI/Cnc). On the machine the 1024 x 600 panel
 * is rotated by 90 degrees; LVGL's test display cannot rotate, so these tests use a 600 x 1024 test
 * display, which is what the UI sees after the rotation. The machine state comes from RRF object
 * model replies in tests/object_model/cnc.
 *
 * Reference images: tests/ref_imgs/cnc/. Compare them with the PanelDue CNC UI references
 * (PanelDue screens x 1.25).
 */

#include "Debug.h"
#include "UI/Cnc/CncHomeView.h"
#include "UI/Cnc/CncLayout.h"
#include "UI/Cnc/CncPopup.h"
#include "UI/Cnc/CncState.h"
#include "test_utils/UiTestSuite.h"
#include <gtest/gtest.h>

class TestCncHome : public UiTestSuite
{
  protected:
	TestCncHome()
	{
		ZoneScoped;
		// Make the test display portrait (it reallocates its buffer on a resolution change)
		lv_display_set_resolution(lv_display_get_default(), UI::CncLayout::ScreenW, UI::CncLayout::ScreenH);
		lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
		UI::Cnc::State::Reset();
	}

	~TestCncHome() override { UI::Cnc::Popup::Close(); }

	void loadMachine(bool fourAxes)
	{
		load_model_data_from_file(fourAxes ? "tests/object_model/cnc/model_move_4axis.json"
										   : "tests/object_model/cnc/model_move_3axis.json");
		load_model_data_from_file("tests/object_model/cnc/model_spindles.json");
		load_model_data_from_file("tests/object_model/cnc/model_tools.json");
		load_model_data_from_file("tests/object_model/cnc/model_state_idle.json");
	}
};

TEST_F(TestCncHome, PortraitResolution)
{
	EXPECT_EQ(LV_HOR_RES, UI::CncLayout::ScreenW);
	EXPECT_EQ(LV_VER_RES, UI::CncLayout::ScreenH);
}

TEST_F(TestCncHome, Frame3Axis)
{
	loadMachine(false);
	UI::CncHomeView view;
	view.show();
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/frame_3axis.png"));
}

TEST_F(TestCncHome, Frame4Axis)
{
	loadMachine(true);
	UI::CncHomeView view;
	view.show();
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/frame_4axis.png"));
}

TEST_F(TestCncHome, NavSwitchesPage)
{
	loadMachine(false);
	UI::CncHomeView view;
	view.show();
	view.showPage(1);
	EXPECT_EQ(view.getPage(), 1u);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/frame_wcs_selected.png"));
}

// ---- Navigation skeleton: every page and sub-tab, 3 and 4 axes ----------------------------------

struct PageShot
{
	unsigned int page, subTab;
	const char* name;
};

static const PageShot PageShots[] = {
	{UI::CncHomeView::PageWcs, 0, "wcs_offsets"},
	{UI::CncHomeView::PageWcs, 1, "wcs_tools"},
	{UI::CncHomeView::PageWcs, 2, "wcs_probe"},
	{UI::CncHomeView::PageJob, 0, "job_list"},
	{UI::CncHomeView::PageJob, 1, "job_preview"},
	{UI::CncHomeView::PageJob, 2, "job_status"},
	{UI::CncHomeView::PageMacros, 0, "macros"},
	{UI::CncHomeView::PageSystem, 0, "system_alert"},
	{UI::CncHomeView::PageSystem, 1, "system_console"},
	{UI::CncHomeView::PageSystem, 2, "system_settings"},
};

TEST_F(TestCncHome, Pages3Axis)
{
	loadMachine(false);
	UI::CncHomeView view;
	view.show();
	for (const PageShot& s : PageShots)
	{
		view.showPage(s.page);
		view.showSubTab(s.subTab);
		EXPECT_TRUE(lv_test_screenshot_compare(fmt::format("cnc/page_{:s}_3axis.png", s.name).c_str())) << s.name;
	}
}

TEST_F(TestCncHome, Pages4Axis)
{
	loadMachine(true);
	UI::CncHomeView view;
	view.show();
	for (const PageShot& s : PageShots)
	{
		view.showPage(s.page);
		view.showSubTab(s.subTab);
		EXPECT_TRUE(lv_test_screenshot_compare(fmt::format("cnc/page_{:s}_4axis.png", s.name).c_str())) << s.name;
	}
}

TEST_F(TestCncHome, SubTabsDefaultAndRemembered)
{
	loadMachine(false);
	UI::CncHomeView view;
	view.show();

	// PanelDue: every page opens on its first sub-tab
	for (unsigned int p = 0; p < UI::CncHomeView::NumPages; ++p)
	{
		EXPECT_EQ(view.getSubTab(p), 0u);
	}
	EXPECT_EQ(UI::CncHomeView::getSubTabCount(UI::CncHomeView::PageControl), 0u);
	EXPECT_EQ(UI::CncHomeView::getSubTabCount(UI::CncHomeView::PageWcs), 3u);
	EXPECT_EQ(UI::CncHomeView::getSubTabCount(UI::CncHomeView::PageJob), 3u);
	EXPECT_EQ(UI::CncHomeView::getSubTabCount(UI::CncHomeView::PageMacros), 0u);
	EXPECT_EQ(UI::CncHomeView::getSubTabCount(UI::CncHomeView::PageSystem), 3u);

	// A touch on a sub-tab switches the content; the page keeps it when left and shown again
	view.showPage(UI::CncHomeView::PageWcs);
	view.getSubTabStrip(UI::CncHomeView::PageWcs).press(2);
	EXPECT_EQ(view.getSubTab(UI::CncHomeView::PageWcs), 2u);
	EXPECT_FALSE(lv_obj_has_flag(view.getContent(UI::CncHomeView::PageWcs, 2), LV_OBJ_FLAG_HIDDEN));
	EXPECT_TRUE(lv_obj_has_flag(view.getContent(UI::CncHomeView::PageWcs, 0), LV_OBJ_FLAG_HIDDEN));

	view.showPage(UI::CncHomeView::PageSystem);
	EXPECT_EQ(view.getSubTab(UI::CncHomeView::PageSystem), 0u);
	view.showPage(UI::CncHomeView::PageWcs);
	EXPECT_EQ(view.getSubTab(UI::CncHomeView::PageWcs), 2u);
	EXPECT_EQ(view.getSubTabStrip(UI::CncHomeView::PageWcs).selected(), 2u);

	// Out of range is ignored; pages without sub-tabs have one content area
	view.showSubTab(3);
	EXPECT_EQ(view.getSubTab(UI::CncHomeView::PageWcs), 2u);
	EXPECT_NE(view.getContent(UI::CncHomeView::PageMacros, 0), nullptr);
	EXPECT_EQ(view.getContent(UI::CncHomeView::PageMacros, 1), nullptr);
}

TEST_F(TestCncHome, CompactDroOnJobMacrosSystem)
{
	EXPECT_FALSE(UI::CncHomeView::isCompactPage(UI::CncHomeView::PageControl));
	EXPECT_FALSE(UI::CncHomeView::isCompactPage(UI::CncHomeView::PageWcs));
	EXPECT_TRUE(UI::CncHomeView::isCompactPage(UI::CncHomeView::PageJob));
	EXPECT_TRUE(UI::CncHomeView::isCompactPage(UI::CncHomeView::PageMacros));
	EXPECT_TRUE(UI::CncHomeView::isCompactPage(UI::CncHomeView::PageSystem));
	// PanelDue: content from y 321 (full DRO) or 142 (compact), down to 657 (sub-tabs) or 710
	EXPECT_EQ(UI::CncHomeView::contentTop(UI::CncHomeView::PageWcs), UI::CncLayout::S(321));
	EXPECT_EQ(UI::CncHomeView::contentTop(UI::CncHomeView::PageJob), UI::CncLayout::S(142));
	EXPECT_EQ(UI::CncHomeView::contentBottom(UI::CncHomeView::PageJob), UI::CncLayout::SB(657));
	EXPECT_EQ(UI::CncHomeView::contentBottom(UI::CncHomeView::PageMacros), UI::CncLayout::SB(710));
}
