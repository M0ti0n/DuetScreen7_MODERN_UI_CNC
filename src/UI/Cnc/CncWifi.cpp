/*
 * CncWifi.cpp
 *
 * See CncWifi.h.
 */

#include "CncWifi.h"
#include "CncKeyboard.h"
#include "CncLayout.h"
#include "CncPopup.h"
#include "CncState.h"
#include "CncTheme.h"
#include "CncWidgets.h"
#include "Comm/Usb.h"
#include "Debug.h"
#include "UI/Components/LVGL/LvObj.h"
#include <algorithm>
#include <fmt/format.h>
#include <memory>
#include <optional>
#include <thread>

namespace UI::Cnc::Wifi
{
	using namespace UI::CncLayout;

	namespace
	{
		// Window in screen pixels, over the page content (the nav bar stays)
		constexpr int32_t WinX = 8, WinY = 150, WinW = ScreenW - 16, WinH = Nav::Y - 16 - WinY;
		constexpr int32_t In = 12;										// inner margin
		constexpr int32_t InnerW = WinW - 2 * CardBorder - 2 * In;
		constexpr int32_t TitleW = 300, TitleH = 56, TitleY = 12;
		constexpr int32_t IpY = TitleY + TitleH + 14, LineH = 32;
		constexpr int32_t StatusY = IpY + LineH;
		constexpr int32_t HintY = StatusY + LineH;
		constexpr int32_t RowsY = HintY + LineH + 6, RowH = 114, RowPitch = 120;	// two rows tall: name, then CONNECTED / SAVED
		constexpr int32_t BarY = WinH - 2 * CardBorder - 12 - 64, BarH = 64;

		Backend s_backend;
		bool s_custom = false;
		std::vector<WiFiNetwork> s_networks;
		std::string s_status;
		std::string s_ip;
		size_t s_scroll = 0;
		bool s_busy = false;
		bool s_open = false;
		uint32_t s_generation = 0;		// late worker results after Reset() are dropped

		lv_obj_t* s_box = nullptr;
		lv_obj_t* s_ipLabel = nullptr;
		lv_obj_t* s_statusLabel = nullptr;
		lv_obj_t* s_hintLabel = nullptr;
		lv_obj_t* s_rows[NumRows] = {};
		lv_obj_t* s_rowName[NumRows] = {};
		lv_obj_t* s_rowTag[NumRows] = {};
		lv_obj_t* s_rowBars[NumRows][4] = {};
		std::unique_ptr<ArrowButton> s_up, s_down;
		std::unique_ptr<TextButton> s_scan;
		std::string s_rowTexts[NumRows];

		// The real system calls
		std::vector<WiFiNetwork> realScan()
		{
			return NetworkHelper::scanWiFiNetworks();
		}

		const Backend& backend()
		{
			static Backend real;
			if (!real.scan)
			{
				real.scan = realScan;
				real.connect = [](const std::string& ssid, const std::string* pw)
				{ return pw ? NetworkHelper::connect(ssid, *pw) : NetworkHelper::connect(ssid); };
				real.disconnect = [] { NetworkHelper::disconnect(); };
				real.forget = [](const std::string& ssid) { NetworkHelper::forgetNetwork(ssid); };
				real.ip = [] { return NetworkHelper::getIpAddress(); };
				real.internalWifi = [] { return Comm::getUsbMode() == Comm::UsbMode::InternalWiFi; };
				real.setInternalWifi = [](bool internal)
				{ Comm::setUsbMode(internal ? Comm::UsbMode::InternalWiFi : Comm::UsbMode::Host); };
			}
			return s_custom ? s_backend : real;
		}

		// Blocking work off the UI thread; the result runs on it
		void runAsync(std::function<void()> work, std::function<void()> done)
		{
			if (backend().synchronous)
			{
				work();
				done();
				return;
			}
			const uint32_t gen = s_generation;
			std::thread(
				[work = std::move(work), done = std::move(done), gen]() mutable
				{
					work();
					State::RunOnUi(
						[done = std::move(done), gen]
						{
							if (gen == s_generation)
							{
								done();
							}
						});
				})
				.detach();
		}

		void show(lv_obj_t* o, bool visible)
		{
			if (o == nullptr)
			{
				return;
			}
			if (visible)
			{
				lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
			}
			else
			{
				lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
			}
		}

		void refresh()
		{
			if (!IsOpen())
			{
				return;
			}
			lv_label_set_text(s_ipLabel, s_ip.empty() ? "-" : s_ip.c_str());
			lv_label_set_text(s_statusLabel, s_status.c_str());
			const bool internal = backend().internalWifi();
			lv_label_set_text(s_hintLabel, internal ? "" : "Built-in WiFi is off: SETTINGS > USB-C PORT");
			for (unsigned int i = 0; i < NumRows; ++i)
			{
				const size_t ni = s_scroll + i;
				const bool vis = ni < s_networks.size();
				show(s_rows[i], vis);
				s_rowTexts[i].clear();
				if (!vis)
				{
					continue;
				}
				const WiFiNetwork& n = s_networks[ni];
				const char* tag = n.connected ? "CONNECTED" : n.isKnown() ? "SAVED" : "";
				lv_label_set_text(s_rowName[i], n.ssid.c_str());
				lv_label_set_text(s_rowTag[i], tag);
				lv_obj_set_style_text_color(s_rowTag[i], n.connected ? Colour::Accent() : Colour::Muted(), 0);
				lv_obj_set_style_border_color(s_rows[i], n.connected ? Colour::Accent() : Colour::Border(), 0);
				const unsigned int bars = Bars(n.signal_level);
				for (unsigned int b = 0; b < 4; ++b)
				{
					lv_obj_set_style_bg_color(s_rowBars[i][b], b < bars ? Colour::Text() : Colour::Border(), 0);
				}
				s_rowTexts[i] = fmt::format("{:s} {:d}{:s}{:s}", n.ssid, bars, *tag ? " " : "", tag);
			}
			s_up->setDisabled(s_scroll == 0);
			s_down->setDisabled(s_scroll + NumRows >= s_networks.size());
			s_scan->setLocked(s_busy);
		}

		void build()
		{
			s_box = Popup::OpenWindowPx(
				WindowId,
				WinX,
				WinY,
				WinW,
				WinH,
				true,
				[]
				{
					s_open = false;
					s_box = nullptr;
					s_up.reset();
					s_down.reset();
					s_scan.reset();
					if (Keyboard::IsOpenFor(KeyboardOwner))
					{
						return;
					}
				});
			s_open = true;

			// Title tile
			const int32_t tx = (WinW - 2 * CardBorder - TitleW) / 2;
			lv_obj_t* title = CreateBare(s_box, tx, TitleY, TitleW, TitleH);
			StyleCard(title, Colour::Tile(), Colour::Accent());
			CreateLabel(title, 0, 0, TitleW - 2 * CardBorder, TitleH - 2 * CardBorder, Font::Large(), Colour::Text(), LV_TEXT_ALIGN_CENTER, "WIFI");

			CreateLabel(s_box, In, IpY, 200, LineH, Font::Small(), Colour::Muted(), LV_TEXT_ALIGN_LEFT, "SCREEN IP");
			s_ipLabel = CreateLabel(s_box, In + 200, IpY, InnerW - 200, LineH, Font::Small(), Colour::Text(), LV_TEXT_ALIGN_RIGHT, "");
			s_statusLabel = CreateLabel(s_box, In, StatusY, InnerW, LineH, Font::Small(), Colour::Amber(), LV_TEXT_ALIGN_LEFT, "");
			s_hintLabel = CreateLabel(s_box, In, HintY, InnerW, LineH, Font::Small(), Colour::Muted(), LV_TEXT_ALIGN_LEFT, "");

			for (unsigned int i = 0; i < NumRows; ++i)
			{
				lv_obj_t* r = CreateBare(s_box, In, RowsY + (int32_t)i * RowPitch, InnerW, RowH);
				StyleCard(r, Colour::Tile(), Colour::Border());
				lv_obj_add_flag(r, LV_OBJ_FLAG_CLICKABLE);
				lv_obj_set_style_bg_color(r, Colour::Pressed(), LV_STATE_PRESSED);
				lv_obj_add_event_cb(
					r,
					[](lv_event_t* e) { PressRow((unsigned int)(uintptr_t)lv_event_get_user_data(e)); },
					LV_EVENT_CLICKED,
					(void*)(uintptr_t)i);
				// Name on the top line, CONNECTED / SAVED under it, the signal at the right edge
				constexpr int32_t barsW = 4 * 14 - 4, lineH = 46;
				const int32_t innerRowW = InnerW - 2 * CardBorder, innerRowH = RowH - 2 * CardBorder;
				const int32_t textW = innerRowW - 12 - barsW - 28;
				s_rowName[i] = CreateLabel(r, 12, 8, textW, lineH, Font::Small(), Colour::Text(), LV_TEXT_ALIGN_LEFT, "");
				s_rowTag[i] = CreateLabel(r, 12, innerRowH - 8 - lineH, textW, lineH, Font::Small(), Colour::Muted(), LV_TEXT_ALIGN_LEFT, "");
				// Signal: 4 bars of growing height, bottoms aligned, centred on the tile's height
				const int32_t bx = innerRowW - 16 - barsW, bottom = innerRowH / 2 + 26;
				for (unsigned int b = 0; b < 4; ++b)
				{
					const int32_t h = 14 + (int32_t)b * 12;
					lv_obj_t* bar = CreateBare(r, bx + (int32_t)b * 14, bottom - h, 10, h);
					lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
					s_rowBars[i][b] = bar;
				}
				s_rows[i] = r;
			}

			// Bottom row: page up / down, SCAN, X
			constexpr int32_t aw = 80, xw = 110;
			s_up = std::make_unique<ArrowButton>();
			s_up->create(s_box, In, BarY, aw, BarH, true);
			s_up->onPress([] { PressPage(-1); });
			s_down = std::make_unique<ArrowButton>();
			s_down->create(s_box, In + aw + 8, BarY, aw, BarH, false);
			s_down->onPress([] { PressPage(1); });
			s_scan = std::make_unique<TextButton>();
			const int32_t sx = In + 2 * (aw + 8);
			s_scan->create(s_box, sx, BarY, InnerW - (sx - In) - xw - 8, BarH, "SCAN", Font::Small());
			s_scan->onPress([] { PressScan(); });
			lv_obj_t* close = CreateBare(s_box, In + InnerW - xw, BarY, xw, BarH);
			StyleCard(close, Colour::StopRed(), Colour::Border(), false);
			lv_obj_add_flag(close, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_event_cb(
				close,
				[](lv_event_t* e)
				{
					lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
					lv_area_t a;
					lv_obj_get_coords(obj, &a);
					Pen pen(lv_event_get_layer(e), a.x1 + lv_area_get_width(&a) / 2, a.y1 + lv_area_get_height(&a) / 2);
					DrawCancelIcon(pen, Colour::ButtonText()); // as the popups' X
				},
				LV_EVENT_DRAW_MAIN_END,
				nullptr);
			lv_obj_add_event_cb(close, [](lv_event_t*) { PressClose(); }, LV_EVENT_CLICKED, nullptr);
		}

		std::string resultText(const std::string& ssid, NetworkHelper::ConnectResult r)
		{
			switch (r)
			{
			case NetworkHelper::ConnectResult::Success:
				return "Connected to " + ssid;
			case NetworkHelper::ConnectResult::AuthFailure:
				return "Wrong password for " + ssid;
			case NetworkHelper::ConnectResult::Timeout:
				return ssid + " did not answer";
			case NetworkHelper::ConnectResult::NetworkNotFound:
				return ssid + " was not found";
			default:
				return "Could not connect to " + ssid;
			}
		}

		void startScan()
		{
			if (s_busy)
			{
				return;
			}
			s_busy = true;
			auto found = std::make_shared<std::vector<WiFiNetwork>>();
			auto ip = std::make_shared<std::string>();
			runAsync(
				[found, ip]
				{
					*found = backend().scan();
					*ip = backend().ip();
				},
				[found, ip]
				{
					s_busy = false;
					s_ip = *ip;
					ResultsArrived(std::move(*found));
				});
			refresh();
		}

		void connect(const std::string& ssid, std::optional<std::string> password)
		{
			if (s_busy)
			{
				return;
			}
			s_busy = true;
			s_status = "Connecting to " + ssid + " ...";
			auto result = std::make_shared<NetworkHelper::ConnectResult>(NetworkHelper::ConnectResult::Error);
			runAsync(
				[ssid, password, result]
				{ *result = backend().connect(ssid, password ? &*password : nullptr); },
				[ssid, result]
				{
					s_busy = false;
					s_status = resultText(ssid, *result);
					startScan();
					refresh();
				});
			refresh();
		}

		void askPassword(const std::string& ssid)
		{
			Keyboard::Client client;
			client.promptTitle = "WIFI PASSWORD";
			client.promptText = ssid;
			client.secret = true;
			client.maxLength = 63; // WPA2 pass phrase
			client.enter = [ssid](const std::string& pw)
			{
				Open();
				connect(ssid, pw);
			};
			client.cancel = [] { Open(); };
			Keyboard::Open("", client, KeyboardOwner);
		}
	} // namespace

	unsigned int Bars(int32_t level)
	{
		if (level > 0)
		{
			return (unsigned int)std::min<int32_t>(4, (level + 24) / 25); // the simulator: 0..100
		}
		return (level >= -55) ? 4 : (level >= -67) ? 3 : (level >= -78) ? 2 : (level != 0) ? 1 : 0;
	}

	void Open()
	{
		UI_LOCK();
		build();
		refresh();
		startScan();
	}

	bool IsOpen()
	{
		return s_open && s_box != nullptr && Popup::WindowId() == WindowId;
	}

	bool InternalWifi()
	{
		return backend().internalWifi();
	}

	void ChooseUsbPort()
	{
		Popup::Choose("USB-C PORT",
					  {"HOST", "INTERNAL WIFI"},
					  InternalWifi() ? 1 : 0,
					  [](size_t c)
					  {
						  const bool internal = (c == 1);
						  if (internal == InternalWifi())
						  {
							  return;
						  }
						  backend().setInternalWifi(internal);
						  Popup::Info("USB-C PORT",
									  internal ? "Built-in WiFi on. A USB cable to the machine\nmust now use the USB-A port."
											   : "Built-in WiFi off. The USB-C port is\na USB host again.");
					  });
	}

	void SetBackend(Backend b)
	{
		s_backend = std::move(b);
		s_custom = static_cast<bool>(s_backend.scan);
	}

	void Reset()
	{
		++s_generation;
		s_networks.clear();
		s_status.clear();
		s_ip.clear();
		s_scroll = 0;
		s_busy = false;
		s_open = false;
		s_box = nullptr;
		s_up.reset();
		s_down.reset();
		s_scan.reset();
		s_custom = false;
		s_backend = Backend();
	}

	void ResultsArrived(std::vector<WiFiNetwork> networks)
	{
		// One row per name (the strongest), the connected one first, then the saved ones, then by signal
		std::vector<WiFiNetwork> out;
		for (WiFiNetwork& n : networks)
		{
			auto same = std::find_if(out.begin(), out.end(), [&](const WiFiNetwork& o) { return o.ssid == n.ssid; });
			if (same == out.end())
			{
				out.push_back(std::move(n));
			}
			else
			{
				same->connected = same->connected || n.connected;
				same->id = same->isKnown() ? same->id : n.id;
				if ((n.signal_level > 0 ? n.signal_level : 100 + n.signal_level) >
					(same->signal_level > 0 ? same->signal_level : 100 + same->signal_level))
				{
					same->signal_level = n.signal_level;
				}
			}
		}
		std::stable_sort(out.begin(),
						 out.end(),
						 [](const WiFiNetwork& a, const WiFiNetwork& b)
						 {
							 if (a.connected != b.connected)
							 {
								 return a.connected;
							 }
							 if (a.isKnown() != b.isKnown())
							 {
								 return a.isKnown();
							 }
							 return Bars(a.signal_level) > Bars(b.signal_level);
						 });
		s_networks = std::move(out);
		if (s_scroll >= s_networks.size())
		{
			s_scroll = 0;
		}
		refresh();
	}

	size_t NetworkCount()
	{
		return s_networks.size();
	}

	std::string RowText(unsigned int row)
	{
		return (row < NumRows && IsOpen()) ? s_rowTexts[row] : std::string();
	}

	std::string Status()
	{
		return s_status;
	}

	std::string Ip()
	{
		return s_ip;
	}

	bool Busy()
	{
		return s_busy;
	}

	void PressRow(unsigned int row)
	{
		const size_t ni = s_scroll + row;
		if (row >= NumRows || ni >= s_networks.size() || s_busy)
		{
			return;
		}
		const WiFiNetwork n = s_networks[ni];
		if (!n.isKnown() && !n.connected)
		{
			askPassword(n.ssid);
			return;
		}
		const std::string ssid = n.ssid;
		Popup::Choose(ssid,
					  {n.connected ? "DISCONNECT" : "CONNECT", "FORGET"},
					  0,
					  [ssid, connected = n.connected](size_t c)
					  {
						  Open();
						  if (c == 1)
						  {
							  backend().forget(ssid);
							  s_status = "Forgot " + ssid;
							  startScan();
						  }
						  else if (connected)
						  {
							  backend().disconnect();
							  s_status = "Disconnected";
							  startScan();
						  }
						  else
						  {
							  connect(ssid, std::nullopt);
						  }
						  refresh();
					  });
	}

	void PressScan()
	{
		startScan();
	}

	void PressPage(int direction)
	{
		if (direction < 0)
		{
			s_scroll = (s_scroll > NumRows) ? s_scroll - NumRows : 0;
		}
		else if (s_scroll + NumRows < s_networks.size())
		{
			s_scroll += NumRows;
		}
		refresh();
	}

	void PressClose()
	{
		if (IsOpen())
		{
			Popup::Close();
		}
	}
} // namespace UI::Cnc::Wifi
