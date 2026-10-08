/*
 * CncSettingsPage.h
 *
 * SYSTEM > SETTINGS, ported from the PanelDue CNC UI (CncSettingsPage.cpp), under the compact DRO:
 *
 *   [ IP      192.168.1.50 ][ FACTORY RESET ]
 *   SYSTEM / CUSTOMIZATION / JOG DIRECTION                     PAGE n / 3
 *   [ tile ][ tile ]   x 6                     page 3: per axis [X][<- -> LEFT/RIGHT v][+ ->], "°" tile
 *            [  ^  ][  v  ]
 *
 * Page 1 SYSTEM (the screen, CncPanel): ACCENT COLOR, BRIGHTNESS, INFO TIMEOUT, SCREEN DIMMING,
 *   SCREEN ROTATION, SOUND, CONNECTION, ABOUT. PanelDue's TOUCH CALIBR., MIRROR / INVERT DISPLAY, BAUD
 *   and RAM are PanelDue hardware; rotation, connection and the info replace them.
 * Page 2 CUSTOMIZATION (the machine, CncSettings, as PanelDue): PROBE MODE, TOOL HOLDER, REMEMBER TOOL,
 *   TOOL SETTER, SLOW JOG F, COOL / VAC, CUSTOM 1-3 (MACRO picked on MACROS, LABEL typed, CLEAR), and
 *   new: IDLE TIMEOUT, JOG PRESETS, OVERRIDES.
 * Page 3 JOG DIRECTION (new): per DRO axis the icon pair of its MOVE buttons and the side + sits on,
 *   and whether a CCW / CW axis shows "°" after its step.
 * FACTORY RESET: the screen settings, the machine settings, or both, back to the defaults.
 */
#pragma once

#include "CncLayout.h"
#include "CncMacrosPage.h"
#include "CncWidgets.h"
#include "lvgl/lvgl.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace UI::Cnc
{
	class SettingsPage
	{
	  public:
		static constexpr unsigned int NumPages = 3;
		static constexpr unsigned int NumTiles = CncLayout::Settings::NumTiles;
		static constexpr unsigned int MaxJogRows = CncLayout::Settings::MaxJogRows;
		enum PanelItem : unsigned int
		{
			PAccent,
			PBrightness,
			PInfoTimeout,
			PDimming,
			PRotation,
			PSound,
			PConnection,
			PSplitOps,
			PPreviewInJob,
			PAbout,
			NumPanelTiles
		};
		enum MachineItem : unsigned int
		{
			MProbeMode,
			MToolHolder,
			MRememberTool,
			MToolSetter,
			MJogFeed,
			MAux,
			MCustom1,
			MCustom2,
			MCustom3,
			MIdleTimeout,
			MJogPresets,
			MOverrides,
			NumMachineTiles
		};

		SettingsPage() = default;
		SettingsPage(const SettingsPage&) = delete;
		SettingsPage& operator=(const SettingsPage&) = delete;
		~SettingsPage();

		void create(lv_obj_t* parent, int32_t parentTop);
		void setShown(bool shown);
		void setIp(const std::string& ip);

		// CUSTOM n > MACRO: the frame opens MACROS for the pick and comes back here afterwards
		using PickStarter = std::function<void(MacrosPage::PickHandler)>;
		void onPickMacro(PickStarter starter) { m_pickMacro = std::move(starter); }

		// Touches (also used by the tests)
		void pagePressed(int direction);
		void panelTilePressed(unsigned int item);
		void machineTilePressed(unsigned int item);
		void factoryResetPressed();
		void jogPairPressed(unsigned int row);
		void jogPlusPressed(unsigned int row);
		void jogDegreePressed();
		void tick();

		// State, for tests
		unsigned int page() const { return m_page; }
		std::string tileText(unsigned int page, unsigned int item) const;
		bool tileToggled(unsigned int page, unsigned int item) const;
		std::string sectionText() const;
		std::string ipText() const { return m_ip; }
		unsigned int jogRowCount() const { return m_jogRows; }
		std::string jogRowText(unsigned int row) const;		// "X LEFT / RIGHT +R"
		bool degreeOn() const;

	  private:
		struct JogRow
		{
			lv_obj_t* letter = nullptr;
			lv_obj_t* letterLabel = nullptr;
			lv_obj_t* pair = nullptr;
			lv_obj_t* plus = nullptr;
			char axis = 'X';
		};

		void showPage();
		void updateLooks();
		void updateJogRows();
		void customPressed(size_t slot);
		void openLabelKeyboard(size_t slot);
		void choosePreset();
		void chooseOverride();
		void chooseConnection();
		static void drawPair(lv_event_t* e);
		static void drawPlus(lv_event_t* e);

		lv_obj_t* m_root = nullptr;
		int32_t m_top = 0;
		lv_timer_t* m_timer = nullptr;
		bool m_shown = false;
		unsigned int m_page = 0;
		std::string m_ip = "-";
		PickStarter m_pickMacro;

		lv_obj_t* m_ipLabel = nullptr;
		TextButton m_reset;
		lv_obj_t* m_section = nullptr;
		lv_obj_t* m_pageLabel = nullptr;
		lv_obj_t* m_pageRoot[NumPages] = {};
		TextButton m_panelTiles[NumPanelTiles];
		TextButton m_machineTiles[NumMachineTiles];
		lv_obj_t* m_jogLabels[2] = {};
		JogRow m_jog[MaxJogRows];
		unsigned int m_jogRows = 0;
		TextButton m_degree;
		ArrowButton m_up, m_down;
		bool m_kbOpen = false;
	};
}
