/*
 * CncPanel.cpp
 */

#include "CncPanel.h"
#include "CncSplash.h"
#include "CncState.h"
#include "CncTheme.h"
#include "Hardware/Duet.h"
#include "Hardware/Reset.h"
#include "Storage.h"
#include "Subscribers/Subscribers.h"
#include "lvgl/lvgl.h"
#include "utils/StorageHelper.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fmt/format.h>
#include <fstream>
#include <vector>

namespace UI::Cnc::Panel
{
	namespace
	{
		constexpr StorageKey<unsigned int> ID_CNC_ACCENT = {"cnc:accent", 0u};
		constexpr StorageKey<unsigned int> ID_CNC_INFO_TIMEOUT = {"cnc:info_timeout", 6u};
		constexpr StorageKey<unsigned int> ID_CNC_ROTATION = {"cnc:rotation", 90u};
		constexpr StorageKey<unsigned int> ID_CNC_SOUND = {"cnc:sound", 1u};
		constexpr StorageKey<unsigned int> ID_CNC_PREVIEW_SPLIT = {"cnc:preview_split", 0u};

		constexpr unsigned int ClickFrequency = 3000, ClickMs = 12;

		// Tests: no disk, no programs
		std::function<void(const std::string&)> s_hook;
		struct TestValues
		{
			unsigned int accent = 0, brightness = 100, infoTimeout = 6, dim = 0, rotation = 90, sound = 1, split = 0;
			Link link = Link::Uart;
			std::string ip = "192.168.1.50";
			std::string password;
		} s_test;
		bool s_inited = false;
		unsigned int s_speakerVolumeSet = 0; // volume last set with amixer (0 = not yet)
		unsigned int s_beepDuration = 0;	 // state.beep.duration of the beep being received
		uint32_t s_lastBeepTime = 0;
		unsigned int s_lastBeepFreq = 0, s_lastBeepMs = 0;

		void run(const std::string& cmd)
		{
			if (s_hook)
			{
				s_hook(cmd);
				return;
			}
			// in the background: the UI never waits for a sound
			const int rc = std::system((cmd + " >/dev/null 2>&1 &").c_str());
			(void)rc;
		}

		template <typename K>
		unsigned int get(const K& key, unsigned int TestValues::*field)
		{
			return s_hook ? s_test.*field : (unsigned int)StorageHelper::getData(key);
		}

		template <typename K>
		void set(const K& key, unsigned int TestValues::*field, unsigned int value)
		{
			if (s_hook)
			{
				s_test.*field = value;
				return;
			}
			StorageHelper::setData(key, value);
		}

		// A sine tone as a WAV file in /tmp (made once per tone)
		std::string toneFile(unsigned int frequency, unsigned int ms)
		{
			const std::string path = fmt::format("/tmp/cnc-tone-{:d}-{:d}.wav", frequency, ms);
			if (s_hook)
			{
				return path;
			}
			std::ifstream exists(path);
			if (exists.good())
			{
				return path;
			}
			constexpr uint32_t Rate = 22050;
			const uint32_t n = Rate * ms / 1000;
			std::vector<int16_t> samples(n);
			for (uint32_t i = 0; i < n; ++i)
			{
				const double env = std::min(1.0, std::min(i, n - i) / (Rate * 0.002)); // 2 ms fade: no click
				samples[i] = (int16_t)(12000.0 * env * std::sin(2.0 * 3.14159265 * frequency * i / Rate));
			}
			std::ofstream f(path, std::ios::binary);
			auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
			auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
			f.write("RIFF", 4);
			u32(36 + n * 2);
			f.write("WAVEfmt ", 8);
			u32(16);
			u16(1);
			u16(1);
			u32(Rate);
			u32(Rate * 2);
			u16(2);
			u16(16);
			f.write("data", 4);
			u32(n * 2);
			f.write(reinterpret_cast<const char*>(samples.data()), (std::streamsize)(n * 2));
			return path;
		}

		bool beepDuration(Comm::JsonDecoder*, const int32_t& val, const size_t[])
		{
			s_beepDuration = (val > 0) ? (unsigned int)val : 0;
			return true;
		}

		bool beepFrequency(Comm::JsonDecoder*, const int32_t& val, const size_t[])
		{
			if (val > 0 && s_beepDuration > 0)
			{
				Beep((unsigned int)val, s_beepDuration); // M300 on the machine
			}
			return true;
		}
	} // namespace

	void Load()
	{
		Colour::SetAccent(Accent());
	}

	void Init()
	{
		if (s_inited)
		{
			return;
		}
		s_inited = true;
		// A click on every touch, wherever it lands
		for (lv_indev_t* indev = lv_indev_get_next(nullptr); indev != nullptr; indev = lv_indev_get_next(indev))
		{
			if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER)
			{
				lv_indev_add_event_cb(indev, [](lv_event_t*) { Click(); }, LV_EVENT_PRESSED, nullptr);
			}
		}
		SubscriberMap::addSubscriber("state:beep:duration", &beepDuration);
		SubscriberMap::addSubscriber("state:beep:frequency", &beepFrequency);
	}

	// ---- Settings ----

	unsigned int Accent()
	{
		const unsigned int a = get(ID_CNC_ACCENT, &TestValues::accent);
		return (a < NumAccents) ? a : 0;
	}

	void SetAccent(unsigned int index)
	{
		if (index >= NumAccents)
		{
			return;
		}
		set(ID_CNC_ACCENT, &TestValues::accent, index);
		if (s_hook)
		{
			s_hook("restart");
			return;
		}
		Splash::SkipNextStart(); // no splash when coming back, as on PanelDue
		Restart();				 // the UI comes back with the new colour
	}

	unsigned int Brightness()
	{
		return get(ID_SYS_BRIGHTNESS_KEY, &TestValues::brightness);
	}

	void SetBrightness(unsigned int percent)
	{
		set(ID_SYS_BRIGHTNESS_KEY, &TestValues::brightness, percent);
		if (!s_hook)
		{
			DisplayHelper::setBrightness(percent);
		}
	}

	unsigned int InfoTimeout()
	{
		return get(ID_CNC_INFO_TIMEOUT, &TestValues::infoTimeout);
	}

	void SetInfoTimeout(unsigned int seconds)
	{
		set(ID_CNC_INFO_TIMEOUT, &TestValues::infoTimeout, seconds);
	}

	unsigned int DimTimeout()
	{
		return s_hook ? s_test.dim : (unsigned int)StorageHelper::getData(ID_SCREENSAVER_TIMEOUT).count();
	}

	void SetDimTimeout(unsigned int seconds)
	{
		if (s_hook)
		{
			s_test.dim = seconds;
			return;
		}
		StorageHelper::setData(ID_SCREENSAVER_TIMEOUT, std::chrono::seconds(seconds));
	}

	bool RotationFlipped()
	{
		return get(ID_CNC_ROTATION, &TestValues::rotation) == 270;
	}

	DisplayRotation Rotation()
	{
		return RotationFlipped() ? DisplayRotation::ROTATION_270 : DisplayRotation::ROTATION_90;
	}

	void SetRotationFlipped(bool flipped)
	{
		set(ID_CNC_ROTATION, &TestValues::rotation, flipped ? 270u : 90u);
		if (!s_hook)
		{
			DisplayHelper::setRotation(Rotation());
		}
	}

	unsigned int SoundSetting()
	{
		const unsigned int s = get(ID_CNC_SOUND, &TestValues::sound);
		return (s <= 6) ? s : 1;
	}

	void SetSoundSetting(unsigned int value)
	{
		if (value > 6)
		{
			return;
		}
		set(ID_CNC_SOUND, &TestValues::sound, value);
		s_speakerVolumeSet = 0;
		Click(); // hear the new setting
	}

	// ---- Sound ----

	void Click()
	{
		Beep(ClickFrequency, ClickMs);
	}

	void Beep(unsigned int frequency, unsigned int ms)
	{
		const unsigned int setting = SoundSetting();
		if (setting == 0 || frequency == 0 || ms == 0)
		{
			return;
		}
		ms = std::min(ms, 3000u);
		// The machine reports a beep until it ends: the same one again within its time is not new
		const uint32_t now = State::Now();
		if (frequency == s_lastBeepFreq && ms == s_lastBeepMs && ms > ClickMs && now - s_lastBeepTime < ms)
		{
			return;
		}
		s_lastBeepFreq = frequency;
		s_lastBeepMs = ms;
		s_lastBeepTime = now;
		if (setting == 1)
		{
			run(fmt::format("beep -f {:d} -l {:d}", frequency, ms));
			return;
		}
		const unsigned int level = setting - 1; // 1..5
		if (s_speakerVolumeSet != level)
		{
			s_speakerVolumeSet = level;
			run(fmt::format("amixer -q sset 'Headphone' on; amixer -q sset 'DAC volume' 170,170; "
							"amixer -q sset 'Headphone volume' {:d}",
							(level * 7 + 2) / 5));
		}
		run(fmt::format("aplay -q {:s}", toneFile(frequency, ms)));
	}

	Link Connection()
	{
		if (s_hook)
		{
			return s_test.link;
		}
		switch (Comm::DUET.GetCommunicationType())
		{
		case Comm::CommunicationType::usb:
			return Link::Usb;
		case Comm::CommunicationType::network:
			return Link::Network;
		default:
			return Link::Uart;
		}
	}

	std::string DuetIp()
	{
		return s_hook ? s_test.ip : Comm::DUET.GetIPAddress();
	}

	std::string DuetPassword()
	{
		const std::string pw = s_hook ? s_test.password : std::string(StorageHelper::getData(ID_DUET_PASSWORD));
		return pw.empty() ? std::string("reprap") : pw;
	}

	bool PreviewToolChangesOnly()
	{
		return get(ID_CNC_PREVIEW_SPLIT, &TestValues::split) == 1;
	}

	void SetPreviewToolChangesOnly(bool on)
	{
		set(ID_CNC_PREVIEW_SPLIT, &TestValues::split, on ? 1u : 0u);
	}

	void SetDuetPassword(const std::string& password)
	{
		if (s_hook)
		{
			s_test.password = password;
			return;
		}
		StorageHelper::setData(ID_DUET_PASSWORD, std::string_view(password));
	}

	void SetConnection(Link link, const std::string& ip)
	{
		if (s_hook)
		{
			s_test.link = link;
			if (!ip.empty())
			{
				s_test.ip = ip;
			}
			s_hook(fmt::format("connection {:d} {:s}", (int)link, ip));
			return;
		}
		if (link == Link::Network && !ip.empty())
		{
			Comm::DUET.SetIPAddress(ip);
		}
		Comm::DUET.SetCommunicationType((link == Link::Usb)		  ? Comm::CommunicationType::usb
										: (link == Link::Network) ? Comm::CommunicationType::network
																  : Comm::CommunicationType::uart);
	}

	void SetTestHook(std::function<void(const std::string&)> hook)
	{
		s_hook = std::move(hook);
	}

	void Reset()
	{
		s_test = TestValues();
		s_speakerVolumeSet = 0;
		s_beepDuration = 0;
		s_lastBeepTime = s_lastBeepFreq = s_lastBeepMs = 0;
	}
} // namespace UI::Cnc::Panel
