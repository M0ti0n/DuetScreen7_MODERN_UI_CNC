/*
 * test_cnc_wifi.cpp
 *
 * SYSTEM > SETTINGS > WIFI and USB-C PORT of the CNC UI (CncWifi): the network list, connecting with a
 * password, saved / connected networks, the USB-C switch. The system calls are replaced (Backend), run
 * at once.
 */

#include "Debug.h"
#include "UI/Cnc/CncHomeView.h"
#include "UI/Cnc/CncKeyboard.h"
#include "UI/Cnc/CncLayout.h"
#include "UI/Cnc/CncMessages.h"
#include "UI/Cnc/CncPanel.h"
#include "UI/Cnc/CncPopup.h"
#include "UI/Cnc/CncSettingsPage.h"
#include "UI/Cnc/CncState.h"
#include "UI/Cnc/CncWifi.h"
#include "test_utils/UiTestSuite.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace UI::Cnc;
using SP = SettingsPage;
using CR = NetworkHelper::ConnectResult;

class TestCncWifi : public UiTestSuite
{
  protected:
	TestCncWifi()
	{
		ZoneScoped;
		lv_display_set_resolution(lv_display_get_default(), UI::CncLayout::ScreenW, UI::CncLayout::ScreenH);
		lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
		State::Reset();
		Messages::Reset();
		Panel::Reset();
		Wifi::Reset();
		Wifi::Backend b;
		b.synchronous = true;
		b.scan = [this] { return networks; };
		b.connect = [this](const std::string& ssid, const std::string* pw)
		{
			connects.push_back(ssid + (pw ? "/" + *pw : std::string()));
			if (pw && *pw != "secret99")
			{
				return CR::AuthFailure;
			}
			for (auto& n : networks)
			{
				n.connected = (n.ssid == ssid);
				if (n.connected)
				{
					n.id = 9;
				}
			}
			return CR::Success;
		};
		b.disconnect = [this]
		{
			for (auto& n : networks)
			{
				n.connected = false;
			}
		};
		b.forget = [this](const std::string& ssid)
		{
			for (auto& n : networks)
			{
				if (n.ssid == ssid)
				{
					n.id = -1;
					n.connected = false;
				}
			}
		};
		b.ip = [] { return std::string("192.168.1.77"); };
		b.internalWifi = [this] { return internal; };
		b.setInternalWifi = [this](bool i) { internal = i; };
		Wifi::SetBackend(b);
	}

	~TestCncWifi() override
	{
		Popup::Close();
		view.reset();
		Wifi::Reset();
		Messages::Reset();
	}

	void makeView()
	{
		load_model_data_from_file("tests/object_model/cnc/model_move_3axis.json");
		load_model_data_from_file("tests/object_model/cnc/model_spindles.json");
		load_model_data_from_file("tests/object_model/cnc/model_tools.json");
		load_model_data_from_file("tests/object_model/cnc/model_state_idle.json");
		view = std::make_unique<UI::CncHomeView>();
		view->show();
		State::StatusChanged(OM::PrinterStatus::idle);
		view->showPage(UI::CncHomeView::PageSystem);
		view->showSubTab(UI::CncHomeView::SystemSettingsTab);
	}

	SP& page() { return view->getSettingsPage(); }

	std::unique_ptr<UI::CncHomeView> view;
	std::vector<WiFiNetwork> networks = {{.ssid = "Office", .signal_level = -70, .id = 2, .connected = false},
										 {.ssid = "Workshop", .signal_level = -50, .id = 1, .connected = true},
										 {.ssid = "Guest", .signal_level = -80, .id = -1, .connected = false},
										 {.ssid = "Guest", .signal_level = -60, .id = -1, .connected = false},
										 {.ssid = "Neighbour", .signal_level = -90, .id = -1, .connected = false}};
	std::vector<std::string> connects;
	bool internal = true;
};

TEST_F(TestCncWifi, Bars)
{
	EXPECT_EQ(Wifi::Bars(-50), 4u);
	EXPECT_EQ(Wifi::Bars(-60), 3u);
	EXPECT_EQ(Wifi::Bars(-75), 2u);
	EXPECT_EQ(Wifi::Bars(-90), 1u);
	EXPECT_EQ(Wifi::Bars(100), 4u); // the simulator's 0..100
	EXPECT_EQ(Wifi::Bars(25), 1u);
}

TEST_F(TestCncWifi, ListSortedAndSettingsTiles)
{
	makeView();
	EXPECT_EQ(page().tileText(0, SP::PUsbPort), "USB-C PORT");
	EXPECT_EQ(page().tileText(0, SP::PWifi), "WIFI");
	page().panelTilePressed(SP::PWifi);
	ASSERT_TRUE(Wifi::IsOpen());
	EXPECT_EQ(Wifi::Ip(), "192.168.1.77");
	EXPECT_EQ(Wifi::NetworkCount(), 4u); // Guest once (the stronger)
	EXPECT_EQ(Wifi::RowText(0), "Workshop 4 CONNECTED");
	EXPECT_EQ(Wifi::RowText(1), "Office 2 SAVED");
	EXPECT_EQ(Wifi::RowText(2), "Guest 3");
	EXPECT_EQ(Wifi::RowText(3), "Neighbour 1");
	EXPECT_EQ(Wifi::RowText(4), "");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wifi_list.png"));
	Wifi::PressClose();
	EXPECT_FALSE(Wifi::IsOpen());
}

TEST_F(TestCncWifi, ConnectWithPassword)
{
	makeView();
	Wifi::Open();
	Wifi::PressRow(2); // Guest: not saved, the password
	ASSERT_TRUE(Keyboard::IsOpenFor(Wifi::KeyboardOwner));
	for (char c : std::string("WRONG"))
	{
		Keyboard::PressKey(c);
	}
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wifi_password.png")); // shown as *
	Keyboard::PressEnter(); // 5 characters: too short for WPA2, not sent
	EXPECT_TRUE(connects.empty());
	EXPECT_EQ(Popup::Title(), "ALERT !");
	Popup::PressOk(); // the keyboard again
	ASSERT_TRUE(Keyboard::IsOpenFor(Wifi::KeyboardOwner));
	for (char c : std::string("WRONGPASS"))
	{
		Keyboard::PressKey(c);
	}
	Keyboard::PressEnter();
	ASSERT_TRUE(Wifi::IsOpen()); // back to the list
	EXPECT_EQ(connects.back(), "Guest/WRONGPASS");
	EXPECT_EQ(Wifi::Status(), "Wrong password for Guest");

	Wifi::PressRow(2);
	Keyboard::PressShift(); // lower case
	for (char c : std::string("secret99"))
	{
		Keyboard::PressKey(c);
	}
	Keyboard::PressEnter();
	EXPECT_EQ(connects.back(), "Guest/secret99");
	EXPECT_EQ(Wifi::Status(), "Connected to Guest");
	EXPECT_EQ(Wifi::RowText(0), "Guest 3 CONNECTED");
}

TEST_F(TestCncWifi, SavedAndConnectedNetworks)
{
	makeView();
	Wifi::Open();
	Wifi::PressRow(1); // Office: saved
	EXPECT_EQ(Popup::Title(), "Office");
	Popup::PressChoice(0); // CONNECT
	Popup::PressOk();
	EXPECT_EQ(connects.back(), "Office"); // no password
	EXPECT_EQ(Wifi::RowText(0), "Office 2 CONNECTED");

	Wifi::PressRow(0); // connected: DISCONNECT / FORGET
	Popup::PressChoice(1); // FORGET
	Popup::PressOk();
	EXPECT_EQ(Wifi::Status(), "Forgot Office");
	EXPECT_EQ(Wifi::RowText(0), "Workshop 4 SAVED");
}

TEST_F(TestCncWifi, UsbPortSwitch)
{
	makeView();
	internal = false;
	Wifi::Open();
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wifi_usb_off.png")); // the hint line
	Wifi::PressClose();
	page().panelTilePressed(SP::PUsbPort);
	EXPECT_EQ(Popup::Title(), "USB-C PORT");
	Popup::PressChoice(1);
	Popup::PressOk();
	EXPECT_FALSE(internal); // asked first: the USB-C port drops what is connected to it
	EXPECT_EQ(Popup::Lines().front(), "Turn the built-in WiFi on?");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wifi_usb_confirm.png"));
	Popup::PressOk();
	EXPECT_TRUE(internal);
	EXPECT_EQ(Popup::Title(), "USB-C PORT"); // the USB-A note
	Popup::Close();

	// Not during a job: the machine's cable may be on the USB-C port
	State::StatusChanged(OM::PrinterStatus::processing);
	page().panelTilePressed(SP::PUsbPort);
	EXPECT_EQ(Popup::Title(), "ALERT !");
	EXPECT_TRUE(internal);
}
