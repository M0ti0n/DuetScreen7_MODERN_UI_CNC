/*
 * test_cnc_settings.cpp
 *
 * SYSTEM > SETTINGS of the CNC UI: the machine settings (global.cnc* from the object model, set live,
 * cnc-settings.g written line by line and renamed, read back), the three pages, CUSTOM n with the MACROS
 * pick and the label keyboard, the new IDLE TIMEOUT / JOG PRESETS / OVERRIDES, the saved PROBE values,
 * JOG DIRECTION with the arrows on the CONTROL and jog prompt buttons, the screen settings and SOUND,
 * FACTORY RESET, and screenshots (pages 1 and 2: compare with the PanelDue CNC UI references x 1.25).
 */

#include "Debug.h"
#include "UI/Cnc/CncHomeView.h"
#include "UI/Cnc/CncKeyboard.h"
#include "UI/Cnc/CncLayout.h"
#include "UI/Cnc/CncMessages.h"
#include "UI/Cnc/CncPanel.h"
#include "UI/Cnc/CncPopup.h"
#include "UI/Cnc/CncSettings.h"
#include "UI/Cnc/CncState.h"
#include "test_utils/UiTestSuite.h"
#include <algorithm>
#include <fmt/format.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace UI::Cnc;
using Lines = std::vector<std::string>;
using V = UI::CncHomeView;
using SP = SettingsPage;
using G = Settings::Global;

class TestCncSettings : public UiTestSuite
{
  protected:
	TestCncSettings()
	{
		ZoneScoped;
		lv_display_set_resolution(lv_display_get_default(), UI::CncLayout::ScreenW, UI::CncLayout::ScreenH);
		lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
		State::Reset();
		Messages::Reset();
		Panel::Reset();
		Panel::SetTestHook([this](const std::string& c) { shell.push_back(c); });
		State::SetSendHook([this](std::string_view g) { sent.emplace_back(g); });
	}

	~TestCncSettings() override
	{
		Popup::Close();
		view.reset();
		Panel::SetTestHook(nullptr);
		Messages::Reset();
	}

	// The frame with machine data, idle, on SYSTEM > SETTINGS, the machine's settings not known yet
	void makeView()
	{
		load_model_data_from_file("tests/object_model/cnc/model_move_3axis.json");
		load_model_data_from_file("tests/object_model/cnc/model_spindles.json");
		load_model_data_from_file("tests/object_model/cnc/model_tools.json");
		load_model_data_from_file("tests/object_model/cnc/model_state_idle.json");
		view = std::make_unique<V>();
		view->show();
		State::StatusChanged(OM::PrinterStatus::idle);
		view->showPage(V::PageSystem);
		view->showSubTab(V::SystemSettingsTab);
	}

	// The machine's globals (M409 K"global"): the response is complete after a quiet moment
	void globals(const std::string& json)
	{
		load_model_data(R"({"key":"global","flags":"v","result":)" + json + "}");
		lv_tick_inc(Settings::QuietTime + 10);
		Settings::Tick();
	}

	// The machine of the PanelDue simulator: its settings as in 0:/sys/cnc-settings.g
	void machineSettings()
	{
		globals(
			R"({"cncProbeMode":0,"cncMeasureAfterChange":true,"cncRememberTool":true,"cncToolSetter":true,)"
			R"("cncAuxVacuum":false,"cncJogFeed":300,"cncCustom1Label":"PARK","cncCustom1Macro":"0:/macros/01_Park.g",)"
			R"("cncCustom2Label":"LIGHT","cncCustom2Macro":"0:/macros/light.g",)"
			R"("cncCustom3Label":"DUST","cncCustom3Macro":"0:/macros/dust.g"})");
	}

	// Run the file writer to the end (one line per 100 ms)
	std::vector<std::string> writeAll()
	{
		std::vector<std::string> lines;
		for (int i = 0; i < 200 && Settings::Writing(); ++i)
		{
			lv_tick_inc(Settings::WriteInterval);
			const size_t before = sent.size();
			Settings::Tick();
			if (sent.size() > before)
			{
				lines.push_back(sent.back());
			}
		}
		return lines;
	}

	SP& page() { return view->getSettingsPage(); }

	static std::string text()
	{
		std::string t;
		for (const std::string& l : Popup::Lines())
		{
			t += (t.empty() ? "" : " ") + l;
		}
		return t;
	}

	std::unique_ptr<V> view;
	std::vector<std::string> sent;
	std::vector<std::string> shell;
};

// ---- Machine settings: read, change, write ---------------------------------------------------------

TEST_F(TestCncSettings, GlobalsFromTheMachineApply)
{
	makeView();
	EXPECT_FALSE(Settings::Known());
	globals(R"({"cncProbeMode":2,"cncToolSetter":false,"cncAuxVacuum":true,"cncJogFeed":700,)"
			R"("cncCustom2Label":"LIGHT","cncCustom2Macro":"0:/macros/light.g","cncIdleTimeout":30,)"
			R"("cncOverrides":"20,150,40,120","cncJogPresets":"100,200,300,400,500,600",)"
			R"("cncProbeValues":"3,6,12,4,200,80,1.5,4,8,2","cncJogIcons":"XH-YV+ZV+","cncJogDegree":false})");
	EXPECT_TRUE(Settings::Known());
	const Settings::Values& v = Settings::Get();
	EXPECT_EQ(State::GetProbeMode(), State::ProbeMode::Manual);
	EXPECT_FALSE(State::ToolSetter());
	EXPECT_STREQ(State::AuxLabel(), "VACUUM");
	EXPECT_EQ(State::JogFeed(), 700u);
	EXPECT_TRUE(State::CustomUsed(1));
	EXPECT_STREQ(State::CustomLabel(1), "LIGHT");
	EXPECT_FALSE(State::CustomUsed(0));
	EXPECT_EQ(v.idleTimeout, 30u);
	EXPECT_EQ(v.feedMax, 150);
	EXPECT_EQ(v.spindleMin, 40);
	EXPECT_EQ(v.jogPresets[5], 600u);
	EXPECT_FLOAT_EQ(v.probe.semiClear, 1.5f);
	EXPECT_EQ(v.probe.storeTo, 2u);
	EXPECT_FALSE(Settings::IconsFor('X').plusRight);
	EXPECT_EQ(Settings::IconsFor('Y').pair, Settings::IconPair::DownUp);
	EXPECT_EQ(Settings::IconsFor('A').pair, Settings::IconPair::Rotary); // not in the list: rotary letter
	EXPECT_FALSE(v.jogDegree);
	EXPECT_EQ(page().tileText(1, SP::MRememberTool), "REMEMBER TOOL");
	EXPECT_FALSE(page().tileToggled(1, SP::MToolSetter));
}

TEST_F(TestCncSettings, BadValuesKeepTheDefaults)
{
	makeView();
	globals(R"({"cncProbeMode":7,"cncJogFeed":0,"cncOverrides":"1,2","cncJogPresets":"a,b","cncJogIcons":"X?+"})");
	const Settings::Values& v = Settings::Get();
	EXPECT_EQ(v.probeMode, 0u);
	EXPECT_EQ(v.jogFeed, 300u);
	EXPECT_EQ(v.feedMin, 10);
	EXPECT_EQ(v.jogPresets[0], 200u);
	EXPECT_EQ(Settings::IconsFor('X').pair, Settings::IconPair::LeftRight);
}

TEST_F(TestCncSettings, RefusedUntilKnown)
{
	makeView();
	page().machineTilePressed(SP::MRememberTool);
	EXPECT_EQ(text(), "Waiting for the machine settings.");
	State::StatusChanged(OM::PrinterStatus::connecting);
	page().machineTilePressed(SP::MRememberTool);
	EXPECT_EQ(text(), "No connection to the machine.");
	EXPECT_TRUE(sent.empty());
}

TEST_F(TestCncSettings, ChangeSetsLiveThenWritesTheFile)
{
	makeView();
	machineSettings();
	page().machineTilePressed(SP::MRememberTool); // the machine has it: set
	EXPECT_EQ(sent.back(), "set global.cncRememberTool = false\n");
	EXPECT_TRUE(Settings::Writing());
	page().machineTilePressed(SP::MIdleTimeout); // the machine does not have it yet: created
	Popup::PressChoice(2);
	Popup::PressOk();
	EXPECT_EQ(sent.back(), "global cncIdleTimeout = 10\n");
	const auto lines = writeAll();
	ASSERT_GE(lines.size(), 3u);
	EXPECT_EQ(lines.front(),
			  "echo >\"0:/sys/cnc-settings.tmp\" \"; CNC panel settings (SYSTEM > SETTINGS), loaded by config.g at "
			  "power-on: M98 P\"\"0:/sys/cnc-settings.g\"\"\"\n");
	EXPECT_EQ(lines.back(), "M471 S\"0:/sys/cnc-settings.tmp\" T\"0:/sys/cnc-settings.g\" D1\n");
	EXPECT_TRUE(std::find(lines.begin(),
						  lines.end(),
						  "echo >>\"0:/sys/cnc-settings.tmp\" \"if !exists(global.cncRememberTool)\"\n") !=
				lines.end());
	EXPECT_TRUE(std::find(lines.begin(),
						  lines.end(),
						  "echo >>\"0:/sys/cnc-settings.tmp\" \"set global.cncCustom1Label = \"\"PARK\"\"\"\n") !=
				lines.end());
	EXPECT_EQ(lines.size(), 2 + Settings::NumGlobals * 3 + 1);
}

TEST_F(TestCncSettings, FileLines)
{
	const auto lines = Settings::FileLines();
	EXPECT_EQ(lines[2], "if !exists(global.cncProbeMode)");
	EXPECT_EQ(lines[3], "  global cncProbeMode = 0");
	EXPECT_EQ(lines[4], "set global.cncProbeMode = 0");
	EXPECT_EQ(Settings::FormatValue(G::Overrides, Settings::Values()), "\"10,200,50,150\"");
	EXPECT_EQ(Settings::FormatValue(G::JogPresets, Settings::Values()), "\"200,300,500,700,1000,1200\"");
	EXPECT_EQ(Settings::FormatValue(G::ProbeValues, Settings::Values()), "\"2,5,10,3,100,60,2,5,6,0\"");
	EXPECT_EQ(Settings::FormatValue(G::JogIcons, Settings::Values()), "\"XH+YD+ZV+AR+\"");
	Settings::Values v;
	v.labels[0] = "SAY \"HI\"";
	EXPECT_EQ(Settings::FormatValue(G::Custom1Label, v), "\"SAY \"\"HI\"\"\""); // quotes doubled
}

TEST_F(TestCncSettings, MachineValuesIgnoredWhileWritingThenVerified)
{
	makeView();
	machineSettings();
	page().machineTilePressed(SP::MToolSetter); // off
	globals(R"({"cncToolSetter":true})");		// an older report: ignored while writing
	EXPECT_FALSE(Settings::Get().toolSetter);
	writeAll();
	lv_tick_inc(Settings::IgnoreAfterWrite + 100);
	Settings::Tick();
	globals(
		R"({"cncToolSetter":true,"cncRememberTool":true,"cncJogFeed":300,"cncCustom1Label":"PARK",)"
		R"("cncCustom1Macro":"0:/macros/01_Park.g","cncCustom2Label":"LIGHT","cncCustom2Macro":"0:/macros/light.g",)"
		R"("cncCustom3Label":"DUST","cncCustom3Macro":"0:/macros/dust.g"})");
	EXPECT_EQ(text(), "The machine did not keep the setting. See SYSTEM > ALERT.");
	EXPECT_TRUE(Settings::Get().toolSetter); // what the machine has
}

// ---- Pages ---------------------------------------------------------------------------------------

TEST_F(TestCncSettings, ThreePages)
{
	makeView();
	machineSettings();
	page().setIp("192.168.1.50");
	EXPECT_EQ(page().sectionText(), "SYSTEM");
	EXPECT_EQ(page().tileText(0, SP::PSound), "SOUND");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/settings_page1.png")); // PanelDue p10_system_settings_p1
	page().pagePressed(1);
	EXPECT_EQ(page().sectionText(), "CUSTOMIZATION");
	EXPECT_TRUE(page().tileToggled(1, SP::MRememberTool));
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/settings_page2.png")); // PanelDue p11_system_settings_p2
	page().pagePressed(1);
	EXPECT_EQ(page().sectionText(), "JOG DIRECTION");
	EXPECT_EQ(page().jogRowCount(), 3u);
	EXPECT_EQ(page().jogRowText(1), "Y 225\xC2\xB0 / 45\xC2\xB0 +R");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/settings_page3.png"));
	page().pagePressed(1); // the end
	EXPECT_EQ(page().page(), 2u);
}

TEST_F(TestCncSettings, ChoicesOfPage2)
{
	makeView();
	machineSettings();
	page().machineTilePressed(SP::MProbeMode);
	EXPECT_EQ(Popup::Title(), "PROBE MODE");
	Popup::PressChoice(1);
	Popup::PressOk();
	EXPECT_EQ(State::GetProbeMode(), State::ProbeMode::Semi);
	page().machineTilePressed(SP::MAux);
	Popup::PressChoice(1);
	Popup::PressOk();
	EXPECT_STREQ(State::AuxLabel(), "VACUUM");
	page().machineTilePressed(SP::MToolHolder);
	Popup::PressChoice(1);
	Popup::PressOk();
	EXPECT_FALSE(Settings::Get().measureAfterChange);
	page().machineTilePressed(SP::MJogFeed);
	EXPECT_EQ(Popup::Title(), "SLOW JOG FEEDRATE");
	Popup::PressChoice(5);
	Popup::PressOk();
	EXPECT_EQ(State::JogFeed(), 1200u);
}

TEST_F(TestCncSettings, JogPresetsSorted)
{
	makeView();
	machineSettings();
	page().machineTilePressed(SP::MJogPresets);
	Popup::PressChoice(0);
	Popup::PressOk();
	EXPECT_EQ(Popup::NumpadTag(), "PRESET 1");
	for (char c : std::string("2500"))
	{
		Popup::NumpadKey(c);
	}
	Popup::PressOk();
	const auto p = Settings::Get().jogPresets;
	EXPECT_EQ(p[0], 300u);
	EXPECT_EQ(p[5], 2500u);
	auto view2 = &view->getControlPage();
	view2->feedTilePressed(); // CONTROL's F tile shows the new presets
	EXPECT_EQ(Popup::Title(), "SLOW JOG FEEDRATE");
}

TEST_F(TestCncSettings, OverridesLimitJobStatus)
{
	makeView();
	machineSettings();
	page().machineTilePressed(SP::MOverrides);
	Popup::PressChoice(1); // FEED MAX
	Popup::PressOk();
	EXPECT_EQ(Popup::NumpadTag(), "FEED MAX");
	Popup::NumpadKey('1');
	Popup::NumpadKey('2');
	Popup::NumpadKey('0');
	Popup::PressOk();
	EXPECT_EQ(Settings::Get().feedMax, 120);
	EXPECT_EQ(JobStatusPage::MaxPct(0), 120);
	page().machineTilePressed(SP::MOverrides);
	Popup::PressChoice(3);
	Popup::PressOk();
	Popup::NumpadKey('9');
	Popup::PressOk(); // 9 % is out of 100..300: refused
	EXPECT_EQ(Popup::Title(), "ALERT !");
}

TEST_F(TestCncSettings, IdleTimeoutOnControl)
{
	makeView();
	machineSettings();
	page().machineTilePressed(SP::MIdleTimeout);
	Popup::PressChoice(0); // OFF
	Popup::PressOk();
	auto& control = view->getControlPage();
	view->showPage(V::PageControl);
	control.axisPressed(1);
	lv_tick_inc(120000);
	control.tick();
	EXPECT_TRUE(control.axisChosen()); // never times out
}

TEST_F(TestCncSettings, CustomMacroPickAndLabel)
{
	makeView();
	machineSettings();
	page().pagePressed(1);
	page().machineTilePressed(SP::MCustom3);
	EXPECT_EQ(Popup::Title(), "CUSTOM 3");
	Popup::PressChoice(0); // MACRO
	Popup::PressOk();
	EXPECT_EQ(view->getPage(), V::PageMacros);
	auto& macros = view->getMacrosPage();
	EXPECT_EQ(macros.labelLeft(), "SELECT");
	load_model_data_from_file("tests/object_model/cnc/filelist_macros.json");
	macros.cellPressed(2); // Spindle_warmup
	EXPECT_EQ(view->getPage(), V::PageSystem);
	EXPECT_EQ(view->getSubTab(V::PageSystem), (unsigned int)V::SystemSettingsTab);
	EXPECT_EQ(Settings::Get().macros[2], "0:/macros/02_Spindle_warmup.g");
	EXPECT_EQ(Settings::Get().labels[2], "DUST"); // it had a label: kept
	EXPECT_EQ(sent.back(), "set global.cncCustom3Macro = \"0:/macros/02_Spindle_warmup.g\"\n");

	// LABEL: at most 12 characters, no quotes
	page().machineTilePressed(SP::MCustom3);
	Popup::PressChoice(1);
	Popup::PressOk();
	ASSERT_TRUE(Keyboard::IsOpen());
	Keyboard::PressCancel();
	page().machineTilePressed(SP::MCustom3);
	Popup::PressChoice(1);
	Popup::PressOk();
	for (int i = 0; i < 4; ++i)
	{
		Keyboard::PressBackspace();
	}
	for (char c : std::string("WARMUP SPINDLE"))
	{
		Keyboard::PressKey(c == ' ' ? ' ' : c);
	}
	EXPECT_EQ(Keyboard::Text(), "WARMUP SPIND");
	Keyboard::PressEnter();
	EXPECT_EQ(Settings::Get().labels[2], "WARMUP SPIND");
	EXPECT_STREQ(State::CustomLabel(2), "WARMUP SPIND");

	// CLEAR
	page().machineTilePressed(SP::MCustom3);
	Popup::PressChoice(2);
	Popup::PressOk();
	EXPECT_FALSE(State::CustomUsed(2));
	page().machineTilePressed(SP::MCustom3);
	Popup::PressChoice(1); // LABEL needs a macro: greyed
	EXPECT_EQ(Popup::ChoiceSelected(), 0u);
}

TEST_F(TestCncSettings, ProbeValuesSaved)
{
	makeView();
	auto& probe = view->getWcsProbePage();
	probe.paramPressed(WcsProbePage::PToolDia); // not known yet: kept, saved later
	Popup::NumpadKey('8');
	Popup::PressOk();
	EXPECT_TRUE(sent.empty());
	machineSettings();
	EXPECT_FLOAT_EQ(Settings::Get().probe.toolDia, 8.0f);
	EXPECT_TRUE(std::find(sent.begin(), sent.end(), "global cncProbeValues = \"2,5,10,3,100,60,2,5,8,0\"\n") !=
				sent.end());									// then the file is written
	globals(R"({"cncProbeValues":"2,5,10,3,150,90,2,5,8,1"})"); // from another panel: ignored while writing
	writeAll();
	lv_tick_inc(Settings::IgnoreAfterWrite + 100);
	Settings::Tick();
	Popup::Close();
	globals(R"({"cncProbeValues":"2,5,10,3,150,90,2,5,8,1"})");
	Popup::Close();
	EXPECT_EQ(probe.tileText(WcsProbePage::PStock), "150x90");
}

// ---- JOG DIRECTION and the MOVE buttons ------------------------------------------------------------

TEST_F(TestCncSettings, ArrowsOnControlMove)
{
	makeView();
	machineSettings();
	view->showPage(V::PageControl);
	auto& control = view->getControlPage();
	control.axisPressed(1); // Y: 225 / 45
	control.stepPressed(3);
	EXPECT_EQ(control.moveText(false), "- 10");
	EXPECT_EQ(control.moveIcon(false), JogIcon::DownLeft);
	EXPECT_EQ(control.moveIcon(true), JogIcon::UpRight);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/control_move_arrows_y.png"));

	// X with + on the left: the arrows stay, the signs swap
	view->showPage(V::PageSystem);
	page().pagePressed(1);
	page().pagePressed(1);
	page().jogPlusPressed(0);
	EXPECT_EQ(page().jogRowText(0), "X LEFT / RIGHT +L");
	view->showPage(V::PageControl);
	control.axisPressed(0);
	EXPECT_EQ(control.moveText(false), "+ 10");
	EXPECT_EQ(control.moveIcon(false), JogIcon::Left);
	EXPECT_EQ(control.moveText(true), "- 10");
	lv_tick_inc(1300); // a 10 mm jog: the machine has stopped moving
	control.leftMovePressed();
	EXPECT_EQ(sent.back(), "G91\nG0 X10.000\nG90\n"); // the left button now moves + (RAPID)
}

TEST_F(TestCncSettings, RotaryAxisDegreeSign)
{
	load_model_data_from_file("tests/object_model/cnc/model_move_4axis.json");
	makeView();
	load_model_data_from_file("tests/object_model/cnc/model_move_4axis.json");
	view->getPresenter()->newAxesData();
	machineSettings();
	view->showPage(V::PageControl);
	auto& control = view->getControlPage();
	control.axisPressed(3); // A
	EXPECT_EQ(control.moveText(false), "- 0.1\xC2\xB0");
	EXPECT_EQ(control.moveIcon(false), JogIcon::Ccw);
	EXPECT_EQ(control.moveIcon(true), JogIcon::Cw);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/control_move_arrows_a.png"));
	view->showPage(V::PageSystem);
	page().jogDegreePressed();
	EXPECT_FALSE(page().degreeOn());
	EXPECT_EQ(control.moveText(false), "- 0.1");

	// The page lists the 4 axes
	page().pagePressed(1);
	page().pagePressed(1);
	EXPECT_EQ(page().jogRowCount(), 4u);
	EXPECT_EQ(page().jogRowText(3), "A CCW / CW +R");
}

TEST_F(TestCncSettings, IconPairChoice)
{
	makeView();
	machineSettings();
	page().jogPairPressed(1); // Y
	EXPECT_EQ(Popup::Title(), "AXIS Y ICONS");
	EXPECT_EQ(Popup::ChoiceSelected(), 2u);
	Popup::PressChoice(1); // DOWN / UP
	Popup::PressOk();
	EXPECT_EQ(page().jogRowText(1), "Y DOWN / UP +R");
	EXPECT_EQ(sent.back(), "global cncJogIcons = \"XH+YV+ZV+AR+\"\n");
	page().jogPairPressed(2);
	Popup::PressChoice(4); // NONE
	Popup::PressOk();
	view->showPage(V::PageControl);
	view->getControlPage().axisPressed(2);
	EXPECT_EQ(view->getControlPage().moveIcon(false), JogIcon::None);
}

// ---- Page 1: the screen ----------------------------------------------------------------------------

TEST_F(TestCncSettings, ScreenSettings)
{
	makeView();
	page().panelTilePressed(SP::PBrightness);
	Popup::PressChoice(2);
	Popup::PressOk();
	EXPECT_EQ(Panel::Brightness(), 40u);
	page().panelTilePressed(SP::PInfoTimeout);
	EXPECT_EQ(Popup::ChoiceSelected(), 2u); // 6 s
	Popup::PressChoice(1);
	Popup::PressOk();
	EXPECT_EQ(Panel::InfoTimeout(), 3u);
	page().panelTilePressed(SP::PDimming);
	Popup::PressChoice(2);
	Popup::PressOk();
	EXPECT_EQ(Panel::DimTimeout(), 300u);
	EXPECT_TRUE(page().tileToggled(0, SP::PDimming));
	page().panelTilePressed(SP::PRotation);
	Popup::PressChoice(1);
	Popup::PressOk();
	EXPECT_TRUE(Panel::RotationFlipped());
	page().panelTilePressed(SP::PAccent);
	EXPECT_EQ(Popup::Title(), "ACCENT COLOR");
	Popup::PressChoice(2);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/settings_accent.png"));
	Popup::PressOk();
	EXPECT_EQ(Panel::Accent(), 2u);
	EXPECT_EQ(shell.back(), "restart"); // the UI restarts with the colour
}

TEST_F(TestCncSettings, Sound)
{
	makeView();
	Panel::Beep(1000, 200);
	EXPECT_EQ(shell.back(), "beep -f 1000 -l 200"); // BUZZER by default
	page().panelTilePressed(SP::PSound);
	EXPECT_EQ(Popup::ChoiceSelected(), 1u);
	Popup::PressChoice(4); // SPEAKER 3
	Popup::PressOk();
	ASSERT_GE(shell.size(), 2u);
	EXPECT_EQ(
		shell[shell.size() - 2],
		"amixer -q sset 'Headphone' on; amixer -q sset 'DAC volume' 170,170; amixer -q sset 'Headphone volume' 4");
	EXPECT_EQ(shell.back(), "aplay -q /tmp/cnc-tone-3000-12.wav"); // the click of the new setting
	shell.clear();
	load_model_data(R"({"key":"state","flags":"v","result":{"beep":{"duration":500,"frequency":800}}})"); // M300
	EXPECT_EQ(shell, std::vector<std::string>({"aplay -q /tmp/cnc-tone-800-500.wav"}));
	load_model_data(R"({"key":"state","flags":"v","result":{"beep":{"duration":500,"frequency":800}}})");
	EXPECT_EQ(shell.size(), 1u); // the same beep reported again: not played twice
	Panel::SetSoundSetting(0);
	shell.clear();
	Panel::Click();
	EXPECT_TRUE(shell.empty());
}

TEST_F(TestCncSettings, Connection)
{
	makeView();
	page().panelTilePressed(SP::PConnection);
	Popup::PressChoice(2); // NETWORK: the address
	Popup::PressOk();
	ASSERT_TRUE(Keyboard::IsOpen());
	EXPECT_EQ(Keyboard::Text(), "192.168.1.50");
	Keyboard::PressBackspace();
	Keyboard::PressKey('9');
	Keyboard::PressEnter();
	EXPECT_EQ(Panel::Connection(), Panel::Link::Network);
	EXPECT_EQ(shell.back(), "connection 2 192.168.1.59");
	State::StatusChanged(OM::PrinterStatus::processing);
	page().panelTilePressed(SP::PConnection);
	EXPECT_EQ(text(), "Locked while a job is running or paused.");
}

// BOTH with another accent colour: the UI restarts (for the colour) only after the machine settings file
// is written; a restart right away would cut the write off
TEST_F(TestCncSettings, FactoryResetBothRestartsAfterTheFile)
{
	makeView();
	machineSettings();
	Panel::SetAccent(3, false);
	page().factoryResetPressed();
	Popup::PressChoice(2); // BOTH
	Popup::PressOk();
	Popup::PressOk();
	EXPECT_EQ(Panel::Accent(), 0u);
	EXPECT_TRUE(Settings::Writing());
	EXPECT_TRUE(std::find(shell.begin(), shell.end(), "restart") == shell.end());
	page().tick();
	EXPECT_TRUE(std::find(shell.begin(), shell.end(), "restart") == shell.end()); // still writing
	const auto lines = writeAll();
	EXPECT_EQ(lines.back(), "M471 S\"0:/sys/cnc-settings.tmp\" T\"0:/sys/cnc-settings.g\" D1\n");
	page().tick();
	EXPECT_EQ(shell.back(), "restart");
}

TEST_F(TestCncSettings, FactoryReset)
{
	makeView();
	machineSettings();
	Panel::SetSoundSetting(3);
	page().factoryResetPressed();
	EXPECT_EQ(Popup::Title(), "FACTORY RESET");
	Popup::PressChoice(1); // MACHINE
	Popup::PressOk();
	EXPECT_EQ(Popup::Lines(),
			  Lines({"FACTORY RESET will return", "the machine settings", "to the default values. Continue?"}));
	Popup::PressOk();
	EXPECT_FALSE(State::CustomUsed(0));
	EXPECT_FALSE(Settings::Get().rememberTool);
	EXPECT_EQ(Panel::SoundSetting(), 3u); // the screen settings stay
	writeAll();
	page().factoryResetPressed();
	Popup::PressChoice(0); // SCREEN
	Popup::PressOk();
	Popup::PressOk();
	EXPECT_EQ(Panel::SoundSetting(), 1u);
}
