/*
 * CncWifi.h
 *
 * SYSTEM > SETTINGS > WIFI: the screen's own Wi-Fi (not the machine's). New with DuetScreen, no PanelDue
 * page. A window over the page (the stock DuetScreen has this in its printer Settings, which the CNC UI
 * replaces):
 *
 *   [        WIFI        ]
 *   SCREEN IP                192.168.1.77
 *   Connected to Workshop                      <- status of the last action
 *   [ Workshop        ||||   CONNECTED ]
 *   [ Office          |||        SAVED ]
 *   [ Guest           ||               ]
 *   ...
 *   [ ^ ][ v ][   SCAN   ][ X ]
 *
 * Tap a network: unknown -> the keyboard asks for the password (shown as *), then connect; saved ->
 * CONNECT / FORGET; connected -> DISCONNECT / FORGET. The networks are scanned when the window opens and
 * with SCAN. Scanning and connecting block (wpa_supplicant), so they run on a worker thread and the
 * result comes back on the UI thread.
 *
 * The built-in Wi-Fi is on the USB-C port's switch: SETTINGS > USB-C PORT = INTERNAL WIFI. With HOST
 * the window says so (an external RTL8188FU stick on USB-A may still work). With INTERNAL WIFI a USB
 * cable to the machine must use the USB-A port.
 */
#pragma once

#include "utils/NetworkHelper.h"
#include <functional>
#include <string>
#include <vector>

namespace UI::Cnc::Wifi
{
	constexpr int WindowId = 11;						// Popup::WindowId while open
	constexpr int KeyboardOwner = 12;
	constexpr unsigned int NumRows = 4;					// networks per page (two-line tiles)

	void Open();										// the window (scans)
	bool IsOpen();

	// SETTINGS > USB-C PORT
	bool InternalWifi();								// the USB-C port is switched to the built-in Wi-Fi
	void ChooseUsbPort();								// HOST / INTERNAL WIFI question

	// Bars 0..4 from a scan level (dBm from wpa_supplicant; the simulator gives 0..100)
	unsigned int Bars(int32_t level);

	// Tests: replace the system calls (and run them at once instead of on a thread)
	struct Backend
	{
		std::function<std::vector<WiFiNetwork>()> scan;
		std::function<NetworkHelper::ConnectResult(const std::string& ssid, const std::string* password)> connect;
		std::function<void()> disconnect;
		std::function<void(const std::string& ssid)> forget;
		std::function<std::string()> ip;
		std::function<bool()> internalWifi;
		std::function<void(bool internal)> setInternalWifi;
		bool synchronous = false;
	};
	void SetBackend(Backend backend);					// empty: the real one
	void Reset();

	// State, for tests
	size_t NetworkCount();
	std::string RowText(unsigned int row);				// "Workshop 4 CONNECTED", "" = hidden
	std::string Status();
	std::string Ip();
	bool Busy();										// a scan / connect is running
	void PressRow(unsigned int row);
	void PressScan();
	void PressPage(int direction);
	void PressClose();
	void ResultsArrived(std::vector<WiFiNetwork> networks);	// what a scan found
}
