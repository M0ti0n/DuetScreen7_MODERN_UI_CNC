/*
 * CncControlPage.h
 *
 * CONTROL page of the CNC UI, ported from the PanelDue CNC UI (CncControlPage.cpp):
 *
 *   AXIS           [ X ][ Y ][ Z ]( [ A ] )          |  HOME  [ALL]
 *   STEP DISTANCE  [0.01][0.1][1][10][100]            |        [ Z ]
 *   MOVE           [  - 0.1  ][  + 0.1  ]             |        [ X ]
 *   ACTION         [RAPID][SAFE Z][ XY0 ]             |        [ Y ]
 *                  [SLOW ][ COOL  ][CUSTOM 1]         |        [ A ]
 *
 * Same behaviour as on PanelDue:
 *  - moves are relative (G91 ... G90); RAPID uses G0, SLOW uses G1 at the jog feed
 *  - no axis is chosen at power-up; an axis or step unused for 5 s is cleared again
 *  - 10 and 100 are not queued: a tap while such a jog still moves is refused
 *  - every HOME asks first; SAFE Z and XY0 need X, Y and Z homed
 *  - job running: the whole page is locked; paused: all works except HOME
 *  - CUSTOM 1 shows only when a macro is assigned; a '!' macro asks first
 *  - DRO tiles: T switches the spindle on / standby, S opens the speed numpad, F the SLOW jog
 *    feed presets (locked while a job runs, except F)
 * The hardware dial of the PanelDue version is not here yet (DuetScreen has no dial input).
 */
#pragma once

#include "CncWidgets.h"
#include "lvgl/lvgl.h"
#include <cstdint>
#include <string>

namespace UI::Cnc
{
	class ControlPage
	{
	  public:
		static constexpr size_t MaxAxes = 4;			// X Y Z A
		static constexpr size_t NumSteps = 5;
		static constexpr size_t DefaultStep = 1;		// 0.1
		static constexpr size_t NumHome = 5;			// ALL Z X Y A
		// Idle timeout: SETTINGS > IDLE TIMEOUT (global.cncIdleTimeout, default 5 s, 0 = never)

		ControlPage() = default;
		ControlPage(const ControlPage&) = delete;
		ControlPage& operator=(const ControlPage&) = delete;
		~ControlPage();

		// parentTop = screen y of the parent's top edge (the page area)
		void create(lv_obj_t* parent, int32_t parentTop);
		void show(bool visible);

		// Machine state
		void setAxisCount(size_t visibleAxes);		// 3 or 4 (A)
		void setJogFeed(unsigned int mmPerMin);		// SLOW feed (global.cncJogFeed)

		// Touches (also used by the tests)
		void axisPressed(size_t axis);
		void stepPressed(size_t step);
		void feedModePressed();
		void movePressed(int direction);			// coordinate sign
		void leftMovePressed() { movePressed(m_leftSign); }	// the left MOVE button (sign per JOG DIRECTION)
		void safeZPressed();
		void xy0Pressed();
		void homePressed(size_t index);				// 0 = ALL, then Z X Y A
		void customPressed();
		void auxPressed();
		void selectSlow();

		// DRO tiles (full DRO): T = spindle on / standby, S = speed numpad, F = SLOW jog feed presets
		void toolTilePressed();
		void spindleTilePressed();
		void feedTilePressed();

		void tick();								// idle timeout (called every 100 ms)

		// State, for tests
		bool axisChosen() const { return m_axisChosen; }
		size_t currentAxis() const { return m_currentAxis; }
		size_t currentStep() const { return m_currentStep; }
		bool slowMode() const { return m_feedMode.secondActive(); }
		std::string moveText(bool right) const { return right ? m_plus.text() : m_minus.text(); }
		JogIcon moveIcon(bool right) const { return right ? m_plus.icon() : m_minus.icon(); }

	  private:
		bool refuseLocked(bool isHome);
		void selectAxis(size_t axis);
		void selectStep(size_t step);
		void clearAxisSelection();
		void resetSelection();
		void layoutAxisRow();
		void updateMoveLabels();
		void updateLockedLook();
		void updateHomeColours();
		void updateCustom();
		void updateAux();
		bool jogAllowed();
		void jog(int direction);
		void askHome(int axis);

		lv_obj_t* m_root = nullptr;
		int32_t m_top = 0;							// screen y of the page area

		TextButton m_axis[MaxAxes];
		TextButton m_step[NumSteps];
		TextButton m_minus, m_plus;
		SegmentToggle m_feedMode;
		TextButton m_safeZ, m_xy0, m_aux, m_custom;
		TextButton m_home[NumHome];
		lv_timer_t* m_timer = nullptr;

		size_t m_visibleAxes = 3;
		bool m_axisChosen = false;
		size_t m_currentAxis = 0;
		size_t m_currentStep = DefaultStep;
		uint32_t m_lastActivity = 0;
		uint32_t m_lastGuardedJog = 0;
		bool m_lockAll = false;						// job running
		bool m_lockHome = false;					// job running or paused
		int m_leftSign = -1;						// coordinate sign of the left MOVE button
		std::string m_minusText, m_plusText;
	};
}
