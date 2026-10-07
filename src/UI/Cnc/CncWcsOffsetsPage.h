/*
 * CncWcsOffsetsPage.h
 *
 * WCS > OFFSETS, ported from the PanelDue CNC UI (CncWcsPage.cpp):
 *
 *   WORK OFFSET                                   |  OFFSET
 *   [G54][G55][G56][G57][G58][G59]                |  [  Z+  ]
 *   OFFSET G55                                    |  [+0.040]
 *   X [ +250.000 ]    Y [  +50.000 ]              |  [  Z-  ]
 *   Z [  -25.000 ]    A [   +0.000 ]              |   STEP  [0.02]
 *   [ACTIVATE][COPY TO][ SAVE ]                   |
 *   [ RESET  ][ SHIFT ][APPLY |->|]               |  [CLEAR ]
 *
 * Same behaviour as on PanelDue:
 *  - G5x buttons: a tap views that work offset; the active one is filled with the accent colour, the
 *    viewed one has an accent outline. SAVE (M500 P10) gets an accent outline while there are
 *    unsaved changes (also changes from ZERO, probing or DWC after the first 8 s).
 *  - A tap on an offset cell opens the numpad (G10 L2); POS = the offset that makes the current
 *    position 0. RESET, APPLY OFFSET and CLEAR ask first.
 *  - COPY TO: form (target G5x + axes); a target that already has offsets asks first (old -> new).
 *  - SHIFT: form (axes), then the amount on the numpad: offset + amount on every picked axis.
 *  - OFFSET column: Z+ / Z- babystep Z live (M290), also during a job, without touching the work
 *    offset; APPLY |->| folds it into the active WCS Z; STEP opens the step presets (0.5 and 1 mm
 *    not while a job runs); CLEAR (M290 R0 Z0) asks first.
 *  - During a job only the job's (active) WCS is locked: editing, RESET, SHIFT, COPY TO into it and
 *    ACTIVATE are refused; the other five stay editable. APPLY is refused while the job runs.
 */
#pragma once

#include "CncWidgets.h"
#include "lvgl/lvgl.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace UI::Cnc
{
	class WcsOffsetsPage
	{
	  public:
		static constexpr size_t NumWcs = 6;
		static constexpr size_t MaxAxes = 4;
		static constexpr size_t NumSteps = 5;
		static constexpr size_t DefaultStep = 1;	// 0.02 mm
		static constexpr size_t FirstLargeStep = 3; // 0.5 and 1 mm: not while a job runs

		WcsOffsetsPage() = default;
		WcsOffsetsPage(const WcsOffsetsPage&) = delete;
		WcsOffsetsPage& operator=(const WcsOffsetsPage&) = delete;

		// parentTop = screen y of the parent's top edge (the page content area)
		void create(lv_obj_t* parent, int32_t parentTop);

		// Touches (also used by the tests)
		void wcsPressed(size_t wcs);
		void offsetPressed(size_t axis);
		void activatePressed();
		void copyPressed();
		void savePressed();
		void resetPressed();
		void shiftPressed();
		void applyPressed();
		void stepPressed(int direction);			// Z+ = 1, Z- = -1
		void stepSizePressed();
		void clearPressed();

		// State, for tests
		size_t viewedWcs() const { return m_viewed; }
		size_t stepIndex() const { return m_stepIndex; }

	  private:
		bool editable(size_t wcs) const;
		void refuseJobWcs();
		void refresh();								// everything from the shared state
		void updateLockedLook();

		// Popups and their results
		void offsetEntered(size_t axis, float value);
		void doReset(size_t wcs);
		void doApply();
		void copyChosen(size_t source, size_t target, uint16_t axes);
		void doCopy(size_t source, size_t target, uint16_t axes);
		void shiftAxesChosen(size_t wcs, uint16_t axes);
		void shiftEntered(size_t wcs, uint16_t axes, float amount);
		bool hasOffsets(size_t wcs, uint16_t axes) const;
		std::string axisWords(size_t wcs, uint16_t axes, float add) const;	// " X1.000 Y2.000"

		lv_obj_t* m_root = nullptr;
		int32_t m_top = 0;

		lv_obj_t* m_gridLabel = nullptr;
		TextButton m_wcs[NumWcs];
		lv_obj_t* m_cellLetter[MaxAxes] = {};
		lv_obj_t* m_cellCard[MaxAxes] = {};
		lv_obj_t* m_cellValue[MaxAxes] = {};
		lv_obj_t* m_cellTouch[MaxAxes] = {};
		TextButton m_activate, m_copy, m_save, m_reset, m_shift, m_apply;
		TextButton m_zPlus, m_zMinus, m_stepSize, m_clear;
		lv_obj_t* m_total = nullptr;

		size_t m_viewed = 0;
		size_t m_stepIndex = DefaultStep;
		bool m_jobRunning = false;					// running, not paused
		bool m_jobActive = false;					// running or paused
	};
}
