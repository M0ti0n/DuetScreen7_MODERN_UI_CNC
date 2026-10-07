/*
 * CncWcsProbePage.h
 *
 * WCS > PROBE, ported from the PanelDue CNC UI (CncProbePage.cpp):
 *
 *   ORIGIN               |  MODE                      AUTO
 *   [TL ][TC ][TR ]      |  [TIP Ø     ][CLEAR D   ]
 *   [ML ][MC ][MR ]      |  [SEARCH    ][Z DEPTH   ]
 *   [BL ][BC ][BR ]      |  [STOCK     ][SAVE TO  v]
 *   [BORE][BOSS][Z TOP]  |
 *   [PROBE OPEN] [          RUN PROBE             ]
 *
 * Same behaviour as on PanelDue:
 *  - One origin is picked at a time: corners, edges and centre fill with the accent colour, BORE,
 *    BOSS and Z TOP get an accent outline.
 *  - The mode (AUTO / SEMI-AUTO / MANUAL) comes from SETTINGS and is shown as a label; each mode has
 *    its parameter tiles (numpad; STOCK: X, then Y; STORE TO: G54..G59).
 *  - The PROBE tile shows probe 0: green outline OPEN, red outline CLOSED (triggered).
 *  - RUN PROBE / START calls the mode's macro:
 *      AUTO    M98 P"0:/sys/probe/probe_auto.g"   O"TL" D<tip> C<clear> S<search> Z<depth> L<stock X> B<stock Y> W<1..6>
 *      SEMI    M98 P"0:/sys/probe/probe_semi.g"   O"TL" D<tip> C<clear> S<search> W<1..6>
 *      MANUAL  M98 P"0:/sys/probe/probe_manual.g" O"TL" D<tool diameter> W<1..6>
 *    Refused during a job, when the machine is not idle, not homed, or (not MANUAL) the probe is
 *    already closed.
 *  - AUTO opens the progress popup under the DRO, with a wide emergency STOP: the macro's M291 S1
 *    messages fill it in ("PROBE:<side> n/m" + text, "PROBE=<axis>" + value); it closes when the
 *    macro ends (busy -> idle), shows an error, never started (5 s), or a result message (S2) comes.
 *  - SEMI-AUTO / MANUAL: the macro asks with M291 S2/S3 and axis controls -> the jog prompt:
 *      "PROBE:<side>"  "Move near [side] side of stock, press PROBE"
 *      "READ:<side>"   "Touch [side] side of stock and press READ"
 *    side: L R T B or Z (top of the stock). Any other M291 with axis controls gets the same prompt
 *    with its own title and text and an OK button. OK = M292 P0, X = M292 P1.
 */
#pragma once

#include "CncGlyphs.h"
#include "CncLayout.h"
#include "CncSettings.h"
#include "CncWidgets.h"
#include "lvgl/lvgl.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace UI::Cnc
{
	class WcsProbePage
	{
	  public:
		static constexpr size_t NumOrigins = 12;
		static constexpr int AutoWindow = 1, JogWindow = 2;		// Popup::WindowId of the two popups
		static constexpr uint32_t AutoStartTimeout = 5000;		// ms: the macro never showed up as running

		enum Param : uint8_t
		{
			PTip,
			PAutoClear,
			PAutoSearch,
			PZDepth,
			PStock,
			PStoreTo,
			PSemiClear,
			PSemiSearch,
			PToolDia,
			NumParams
		};

		WcsProbePage() = default;
		WcsProbePage(const WcsProbePage&) = delete;
		WcsProbePage& operator=(const WcsProbePage&) = delete;
		~WcsProbePage();

		// parentTop = screen y of the parent's top edge (the page content area)
		void create(lv_obj_t* parent, int32_t parentTop);

		// Touches (also used by the tests)
		void originPressed(size_t origin);
		void paramPressed(Param p);
		void runPressed();

		// The popups (Messages calls these; the tests too)
		bool autoRunning() const;
		void autoMessage(const std::string& title, const std::string& text);
		void autoError(const std::string& text);
		bool showJogPrompt(const std::string& title, const std::string& text, uint32_t controls, bool withCancel,
						   uint32_t seq);
		bool jogPromptOpen() const;
		void closeJogPrompt();
		void jogAxisPressed(size_t i);
		void jogStepPressed(size_t i);
		void jogMovePressed(int direction);
		void jogOkPressed();
		void jogCancelPressed();
		void tick();								// auto popup start timeout (every 100 ms)

		// State, for tests
		size_t origin() const { return m_origin; }
		const std::string& autoHead() const { return m_autoHead; }
		const std::string& autoStatus() const { return m_autoStatus; }
		std::string autoRowValue(size_t r) const { return (r < 2) ? m_autoRowValue[r] : std::string(); }
		const std::string& jogTitle() const { return m_jogTitle; }
		const std::string& jogOkLabel() const { return m_jogOkLabel; }
		std::string jogInstruction() const;			// the sentence or the two lines
		size_t jogAxisCount() const { return m_jogNumAxes; }
		std::string tileText(Param p) const { return (p < NumParams) ? m_tileText[p] : std::string(); }

	  private:
		void showModeLayout();
		void updateTileText(Param p);
		void updateProbeTile();
		float* valueOf(Param p);
		void openNumpad(const char* tag, float value, float min, float max, std::function<void(float)> onOk);
		void statusChanged();
		void openAutoPopup();
		void resetAutoTile();
		void buildJogPrompt();
		void updateJogMoveLabels();

		lv_obj_t* m_root = nullptr;
		int32_t m_top = 0;
		lv_timer_t* m_timer = nullptr;

		// Page
		OriginButton m_originButtons[NumOrigins];
		lv_obj_t* m_modeLabel = nullptr;
		static constexpr size_t MaxModeTiles = 6;
		struct ModeTile
		{
			ValueTile tile;
			Param id = NumParams;
		};
		ModeTile m_modeTiles[3][MaxModeTiles];		// per mode: AUTO, SEMI, MANUAL
		size_t m_modeTileCount[3] = {0, 0, 0};
		StatusTile m_probeTile;
		TextButton m_run, m_start;
		std::string m_tileText[NumParams];

		// Values
		Settings::Probe m_values;					// saved as global.cncProbeValues (CncSettings)
		size_t m_origin = 0;						// TL
		size_t m_runOrigin = 0;						// origin of the last RUN (glyph in the popups)

		// Auto popup (only valid while it is open)
		bool m_autoBusySeen = false;
		uint32_t m_autoStartTime = 0;
		lv_obj_t* m_autoSideGlyph = nullptr;
		lv_obj_t* m_autoHeadLabel = nullptr;
		lv_obj_t* m_autoStatusLabel = nullptr;
		lv_obj_t* m_autoRowLetter[2] = {};
		lv_obj_t* m_autoRowValueLabel[2] = {};
		lv_obj_t* m_autoRowCheck[2] = {};
		Origin m_autoSide = Origin::None;
		char m_autoRowAxis[2] = {'X', 'Y'};			// 0 = row not used
		std::string m_autoHead, m_autoStatus, m_autoRowValue[2];

		// Jog prompt (only valid while it is open)
		std::string m_jogTitle, m_jogOkLabel, m_jogT1, m_jogT2, m_jogL1, m_jogL2;
		Origin m_jogSide = Origin::None;
		char m_jogSideChar = 0;
		bool m_jogSentence = false, m_jogProbePrompt = false, m_jogWithCancel = false;
		uint8_t m_jogAxisIndex[4] = {};
		size_t m_jogNumAxes = 0, m_jogAxis = 0, m_jogStep = 1;
		int m_jogLeftSign = -1;						// coordinate sign of the left move button
		uint32_t m_jogSeq = 0;
		uint32_t m_lastGuardedJog = 0;
		TextButton m_jogAxisButtons[4];
		TextButton m_jogStepButtons[4];
		TextButton m_jogMinus, m_jogPlus, m_jogOk;
	};
}
