/*
 * test_cnc_splash.cpp
 *
 * The CNC UI start-up splash: look (with and without logo files), 3 s timeout, touch to close, and no
 * splash after the UI's own restart. The test logos are plain placeholders (tests/object_model/cnc/splash).
 */

#include "UI/Cnc/CncLayout.h"
#include "UI/Cnc/CncSplash.h"
#include "UI/Cnc/CncTheme.h"
#include "test_utils/UiTestSuite.h"
#include <filesystem>
#include <gtest/gtest.h>

using namespace UI::Cnc;

namespace
{
	const char* const Marker = "test_cnc_skip_splash";
	const char* const NoLogos = "tests/object_model/cnc/splash/none/";
	const char* const TestLogos = "tests/object_model/cnc/splash/";
	const char* const Version = "v1.0.0";
} // namespace

class TestCncSplash : public UiTestSuite
{
  protected:
	TestCncSplash()
	{
		lv_display_set_resolution(lv_display_get_default(), UI::CncLayout::ScreenW, UI::CncLayout::ScreenH);
		lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
		Splash::SetMarkerPath(Marker);
		std::filesystem::remove(Marker);
		Colour::SetAccent(0);
	}

	~TestCncSplash() override
	{
		Splash::Close();
		std::filesystem::remove(Marker);
		Splash::SetMarkerPath("");
		Colour::SetAccent(0);
	}

	static void wait(uint32_t ms)
	{
		lv_tick_inc(ms);
		lv_timer_handler();
	}
};

TEST_F(TestCncSplash, Look)
{
	ASSERT_TRUE(Splash::Show(NoLogos, Version));
	EXPECT_TRUE(Splash::IsOpen());
	EXPECT_EQ(Splash::LogosShown(), 0); // missing files: empty slots
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/splash.png"));
}

TEST_F(TestCncSplash, LookBlue)
{
	Colour::SetAccent(2); // Duet Blue
	ASSERT_TRUE(Splash::Show(NoLogos, Version));
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/splash_blue.png"));
}

TEST_F(TestCncSplash, Logos)
{
	// Left: small (centred), middle: missing (empty), right: larger than the slot (scaled down)
	ASSERT_TRUE(Splash::Show(TestLogos, Version));
	EXPECT_EQ(Splash::LogosShown(), 2);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/splash_logos.png"));
}

TEST_F(TestCncSplash, ClosesAfterThreeSeconds)
{
	ASSERT_TRUE(Splash::Show(NoLogos, Version));
	wait(Splash::ShowMs - 100);
	EXPECT_TRUE(Splash::IsOpen());
	wait(200);
	EXPECT_FALSE(Splash::IsOpen());
	EXPECT_EQ(Splash::Object(), nullptr);
}

TEST_F(TestCncSplash, TouchCloses)
{
	ASSERT_TRUE(Splash::Show(NoLogos, Version));
	wait(500);
	lv_obj_send_event(Splash::Object(), LV_EVENT_PRESSED, nullptr);
	wait(1);
	EXPECT_FALSE(Splash::IsOpen());
	wait(Splash::ShowMs); // the timer went with it
	EXPECT_FALSE(Splash::IsOpen());
}

TEST_F(TestCncSplash, SkippedAfterOwnRestart)
{
	Splash::SkipNextStart();
	EXPECT_TRUE(std::filesystem::exists(Marker));
	EXPECT_FALSE(Splash::Show(NoLogos, Version));
	EXPECT_FALSE(Splash::IsOpen());
	EXPECT_FALSE(std::filesystem::exists(Marker)); // used once
	EXPECT_TRUE(Splash::Show(NoLogos, Version));   // a normal start shows it again
}
