/*
 * CncPanel.h
 *
 * The screen's own settings of the CNC UI (SYSTEM > SETTINGS page 1), kept by DuetScreen (StorageHelper,
 * /etc/duetscreen.json), and its sound. PanelDue kept these in its flash (nvData).
 *
 *   ACCENT COLOR     8 colours                 cnc:accent        applied by restarting the UI (as PanelDue)
 *   BRIGHTNESS       10..100 %                 sys:brightness    DuetScreen's backlight setting
 *   INFO TIMEOUT     0 / 3 / 6 / 10 s          cnc:info_timeout  machine reply popups
 *   SCREEN DIMMING   OFF / 1 / 5 / 10 min      ui:screensaver_timeout (DuetScreen's screensaver)
 *   SCREEN ROTATION  90 / 270 degrees          cnc:rotation      the portrait UI either way up
 *   SOUND            OFF / BUZZER / SPEAKER 1-5  cnc:sound       on-board piezo (beep) or an 8 ohm speaker
 *
 * Sound, as on PanelDue: a short click on every touch, and the machine's M300 beeps (state.beep).
 * The buzzer is played with `beep`, the speaker with `aplay` (a generated tone) after `amixer` sets
 * its volume; a missing program just stays quiet.
 */
#pragma once

#include "utils/DisplayHelper.h"
#include <cstdint>
#include <functional>
#include <string>

namespace UI::Cnc::Panel
{
	constexpr unsigned int NumAccents = 8;

	void Load();								// at start-up, before the CNC UI is built (accent colour)
	void Init();								// touch click and the machine's beeps (once the display exists)

	unsigned int Accent();
	void SetAccent(unsigned int index, bool restart = true); // stored; the UI restarts to show it
	void RestartUi();										 // the UI starts again (the simulator: the program again)
	unsigned int Brightness();					// %
	void SetBrightness(unsigned int percent);
	unsigned int InfoTimeout();					// s, 0 = replies are not shown (errors are)
	void SetInfoTimeout(unsigned int seconds);
	unsigned int DimTimeout();					// s, 0 = never
	void SetDimTimeout(unsigned int seconds);
	bool RotationFlipped();						// true: 270 degrees
	DisplayRotation Rotation();					// what main.cpp sets at start-up
	void SetRotationFlipped(bool flipped);

	enum class Sound : uint8_t
	{
		Off = 0,
		Buzzer = 1,
		Speaker1 = 2,							// .. Speaker5 = 6
	};
	unsigned int SoundSetting();				// 0 off, 1 buzzer, 2..6 speaker volume 1..5
	void SetSoundSetting(unsigned int value);
	void Click();								// a touch
	void Beep(unsigned int frequency, unsigned int ms);

	// CONNECTION: how DuetScreen talks to the Duet (DuetScreen's own setting)
	enum class Link : uint8_t
	{
		Uart,
		Usb,
		Network
	};
	Link Connection();
	std::string DuetIp();						// the address used for NETWORK
	void SetConnection(Link link, const std::string& ip = std::string());
	// The machine's password (M551) for downloads (PREVIEW); DuetScreen's own setting, RRF's default
	// "reprap" when not set (any password is accepted when the machine has none)
	std::string DuetPassword();
	void SetDuetPassword(const std::string& password);	// PREVIEW: typed after a rejected download
	// SETTINGS > SPLIT OPERATIONS (PREVIEW): false AUTO (names, else guesses), true TOOL CHANGES only
	bool PreviewToolChangesOnly();
	void SetPreviewToolChangesOnly(bool on);
	// SETTINGS > PREVIEW IN JOB: what PREVIEW may show while a job runs
	enum class PreviewInJob : uint8_t
	{
		AnyFile,	// the running job, or another file opened from JOB LIST (downloaded when opened)
		RunningJob, // the running job only
		Off			// no downloads while a job runs: only a file PREVIEW already holds
	};
	PreviewInJob PreviewDuringJob();
	void SetPreviewDuringJob(PreviewInJob mode);

	// Tests: shell commands and restarts go to the hook instead, nothing is stored on disk
	void SetTestHook(std::function<void(const std::string&)> hook);
	void Reset();
}
