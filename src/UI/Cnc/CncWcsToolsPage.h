/*
 * CncWcsToolsPage.h
 *
 * WCS > TOOLS, ported from the PanelDue CNC UI (CncToolsPage.cpp):
 *
 *   TOOLS  TOOL NAME              Z OFFSET
 *   [T0 ] [6mm 3fl flat      ] [ -42.315 ]  [ ^ ]
 *   [T1 ] [3mm ball          ] [ -38.020 ]  [   ]
 *   [T2 ] [90 deg spot drill ] [ -35.550 ]  [ v ]
 *   [T3 ] [1/4in chamfer     ] [ -40.100 ]  [   ]
 *   [   LOAD   ] [  MEASURE  ] [  SET Z  ]
 *
 * Same behaviour as on PanelDue:
 *  - The list is whatever RRF has defined (M563); up to 56 tools, T0..T99. The loaded (active) tool
 *    has an accent-filled T# tile, the selected row an accent outline on all three tiles. The
 *    arrows page through the list 4 rows at a time.
 *  - T# or the name selects the row; the Z OFFSET tile selects it and opens SET Z. A tool change
 *    moves the selection along when it was the loaded tool (or picked automatically).
 *  - LOAD   no tool in the spindle: "IS T# IN THE SPINDLE?"  YES = T# P0 (RRF only notes it,
 *           nothing moves; also written to sys/cnc-lasttool.g), NO = T# (tool change).
 *           Another tool loaded: tool change confirmation, then T#.
 *  - MEASURE  the loaded tool on the tool setter: M98 P"0:/sys/toolchange/measure_tool.g" K#,
 *           confirmed; needs TOOL SETTER on in SETTINGS.
 *  - SET Z  numpad for the selected tool's Z offset: G10 P# Z..., then M500 P10.
 *  - PROBE MODE = NXT: LOAD is always the real tool change (no "IS T# IN THE SPINDLE?": NeXT's
 *           tpre / tpost ask for the tool and measure it), MEASURE sends G37 (NeXT's toolsetter on;
 *           not for the touch probe).
 *  - LOAD and MEASURE need X, Y and Z homed; all three are refused while a job runs or is paused.
 */
#pragma once

#include "CncLayout.h"
#include "CncWidgets.h"
#include "lvgl/lvgl.h"
#include <cstddef>
#include <cstdint>

namespace UI::Cnc
{
	class WcsToolsPage
	{
	  public:
		static constexpr size_t NumRows = CncLayout::Tools::NumRows;

		WcsToolsPage() = default;
		WcsToolsPage(const WcsToolsPage&) = delete;
		WcsToolsPage& operator=(const WcsToolsPage&) = delete;

		// parentTop = screen y of the parent's top edge (the page content area)
		void create(lv_obj_t* parent, int32_t parentTop);

		// Touches (also used by the tests)
		void rowPressed(size_t row);				// T# or name
		void zPressed(size_t row);					// Z OFFSET tile
		void pagePressed(int direction);			// up = -1, down = 1
		void loadPressed();
		void measurePressed();
		void setZPressed();

		// State, for tests
		int selectedTool() const { return m_selected; }
		size_t firstRow() const { return m_firstRow; }
		int rowTool(size_t row) const { return (row < NumRows) ? m_rowTool[row] : -1; }

	  private:
		void update();								// rows, arrows and action looks from the shared state
		void toolsChanged();						// the list or the active tool changed
		void scrollTo(int tool);
		void openSetZ();
		bool refuseUnhomed();

		lv_obj_t* m_root = nullptr;
		int32_t m_top = 0;

		TextButton m_num[NumRows];
		lv_obj_t* m_nameCard[NumRows] = {};
		lv_obj_t* m_name[NumRows] = {};
		lv_obj_t* m_zCard[NumRows] = {};
		lv_obj_t* m_z[NumRows] = {};
		lv_obj_t* m_rowTouch[NumRows] = {};
		lv_obj_t* m_zTouch[NumRows] = {};
		ArrowButton m_up, m_down;
		TextButton m_load, m_measure, m_setZ;

		int m_rowTool[NumRows] = {-1, -1, -1, -1};	// tool shown in each row, -1 = empty
		int m_selected = -1;
		int m_lastActive = -1;
		bool m_autoSelected = false;				// the selection was not the operator's choice
		size_t m_firstRow = 0;						// index into the tool list
	};
}
