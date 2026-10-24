/*
 * CncNxtProbePage.h
 *
 * WCS > PROBE with NeXT (SETTINGS > PROBE MODE = NXT and NeXT detected on the machine). New with
 * DuetScreen. Two steps under the full DRO:
 *
 *   Step 1                                    Step 2
 *   PROBING CYCLE                    NXT      [< BACK]  BORE                NXT G6500
 *   [ BORE         ][ BOSS         ]          [DIAMETER   ][DEPTH      ]
 *   [ RECT POCKET  ][ RECT BLOCK   ]          [OVERTRAVEL ][SAMPLES    ]
 *   [ WEB          ][ POCKET       ]          [SAVE TO  v ][ROTATION v ]
 *   [ OUT CORNER   ][ IN CORNER    ]          [           ][           ]
 *   [ SURFACE      ][ VISE CORNER  ]
 *   [ ROTATION     ][ CALIBRATE    ]          [  (o) RUN BORE                 ]
 *
 * A cycle tile opens its parameters (each cycle its own: bore diameter, rectangle width / height,
 * corner, ...); RUN sends the cycle with U<work offset>, so NeXT stores the result in it itself (and
 * asks about the skew with ROTATION = ASK). RUN is refused as on the standard page (job, not idle, not
 * homed, probe triggered) and while the touch probe tool is not selected. CALIBRATE: NeXT's datum setup,
 * probe / toolsetter repeatability and, with the MOS Fourth Axis macros, the rotary.
 * The values stay on the screen while it runs (not written to the machine).
 */
#pragma once

#include "CncLayout.h"
#include "CncNxt.h"
#include "CncWidgets.h"
#include "lvgl/lvgl.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace UI::Cnc
{
	class NxtProbePage
	{
	  public:
		static constexpr size_t NumGrid = 12;				// 11 cycles + CALIBRATE

		NxtProbePage() = default;
		NxtProbePage(const NxtProbePage&) = delete;
		NxtProbePage& operator=(const NxtProbePage&) = delete;
		~NxtProbePage();

		// onActive(true): NXT shown, the standard page must hide (and the other way round)
		void create(lv_obj_t* parent, int32_t parentTop, std::function<void(bool)> onActive);
		void tick();										// NXT on / off (own timer; the tests call it)

		// Touches (also used by the tests)
		void gridPressed(size_t index);						// a cycle or CALIBRATE
		void backPressed();
		void paramPressed(size_t index);					// index in the cycle's parameter list
		void runPressed();

		// State, for tests
		bool active() const { return m_active; }
		bool inCycle() const { return m_inCycle; }
		Nxt::Cycle cycle() const { return m_cycle; }
		std::string gridText(size_t index) const;
		std::string paramText(size_t index) const;			// "DIAMETER 25.000", "" = hidden
		std::string runText() const { return m_runText; }

	  private:
		void showStep();
		void refreshParams();
		void updateSignal();
		bool refuseRun();

		lv_obj_t* m_root = nullptr;
		int32_t m_top = 0;
		lv_timer_t* m_timer = nullptr;
		std::function<void(bool)> m_onActive;
		bool m_active = false;
		bool m_inCycle = false;
		Nxt::Cycle m_cycle = Nxt::Cycle::Bore;

		// Step 1
		lv_obj_t* m_step1 = nullptr;
		lv_obj_t* m_grid[NumGrid] = {};
		lv_obj_t* m_gridName[NumGrid] = {};
		// Step 2
		lv_obj_t* m_step2 = nullptr;
		lv_obj_t* m_cycleLabel = nullptr;
		lv_obj_t* m_codeLabel = nullptr;
		TextButton m_back;
		ValueTile m_params[Nxt::MaxCycleParams];
		std::string m_paramLabels[Nxt::MaxCycleParams];
		ProbeButton m_run;
		std::string m_runText;
	};
}
