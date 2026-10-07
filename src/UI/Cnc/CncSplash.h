/*
 * CncSplash.h
 *
 * The CNC UI's start-up splash: an end mill cutting a pocket, "CNC" / "MODERN UI" in the accent colour,
 * the firmware version, and up to three logos at the bottom.
 *
 * Shown over the home screen for a fixed 3 s; a touch closes it earlier. It is not shown when the UI
 * restarted itself (a new accent colour), as on PanelDue.
 *
 * Logos: the panel owner's own image files, loaded when present (PNG, transparent background, up to
 * 170 x 100 px; larger ones are scaled down to fit). A missing file leaves its slot empty.
 *   assets/cnc/logo_duet3d.png      left
 *   assets/cnc/logo_rrf.png         middle
 *   assets/cnc/logo_millennium.png  right
 */
#pragma once

#include "lvgl/lvgl.h"
#include <string>

namespace UI::Cnc::Splash
{
	constexpr uint32_t ShowMs = 3000;
	constexpr int32_t LogoW = 170, LogoH = 100;
	constexpr int NumLogos = 3;
	extern const char* const LogoFiles[NumLogos]; // left to right, file names in the logo folder

	// Shows the splash unless the UI is coming back from its own restart. Returns true if shown.
	// 'logoDir' ends with '/'; 'version' is what the splash shows (default: FIRMWARE_VERSION).
	bool Show(const std::string& logoDir = "", const std::string& version = "");
	void Close();
	bool IsOpen();
	lv_obj_t* Object(); // the full-screen splash (touch target), nullptr when closed
	int LogosShown();	// how many logo files were found

	// Called just before the UI restarts itself: the next start skips the splash
	void SkipNextStart();
	// Tests: where the skip marker lives (default: the temp folder, cleared on a reboot)
	void SetMarkerPath(const std::string& path);
} // namespace UI::Cnc::Splash
