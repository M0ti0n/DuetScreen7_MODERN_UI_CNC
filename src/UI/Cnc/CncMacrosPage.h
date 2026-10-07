/*
 * CncMacrosPage.h
 *
 * MACROS page, ported from the PanelDue CNC UI (CncMacrosPage.cpp), under the compact DRO, no
 * sub-tabs:
 *
 *   MACROS                               0:/macros
 *   [ probing                   FOLDER ]   [SEARCH]     list: 11 rows
 *   [ park                             ]   [  ^   ]     grid: 2 x 5 tiles, name centred,
 *   [ safe_z                           ]   [  v   ]           tag bottom-right
 *                                          [ GRID ]     LIST / GRID toggle in the SD slot
 *
 * Same behaviour as on PanelDue:
 *  - A fresh listing of the open folder of 0:/macros each time MACROS is shown (folders first).
 *  - Tap a folder to open it, ".." goes back up. Tap a macro: "RUN MACRO" / "Do you want to run
 *    this macro?" + name, then M98 P"<path>". Refused while a job runs (allowed while paused).
 *  - Names are shown without a leading "!", a leading "12_" number and the ".g" extension.
 *  - SEARCH as on JOB LIST (shared keyboard, live filter on the macro names of the open folder,
 *    ENTER keeps the filter, X clears it); the view stays list or grid while typing (3 rows or
 *    2 tiles above the keyboard).
 *  - SETTINGS > CUSTOM n > MACRO picks a macro here: the label reads SELECT (accent) and a tapped
 *    macro is handed over instead of run; leaving the page cancels it.
 */
#pragma once

#include "CncLayout.h"
#include "CncWidgets.h"
#include "lvgl/lvgl.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UI::Cnc
{
	class MacrosPage
	{
	  public:
		static constexpr unsigned int NumCells = CncLayout::Macros::NumCells;
		static constexpr uint32_t ListTimeout = 5000;		// ms without an answer: the folder cannot be read
		using PickHandler = std::function<void(const std::string& path, const std::string& name)>;

		struct Entry
		{
			std::string name;
			bool folder;
		};

		MacrosPage() = default;
		MacrosPage(const MacrosPage&) = delete;
		MacrosPage& operator=(const MacrosPage&) = delete;
		~MacrosPage();

		void create(lv_obj_t* parent, int32_t parentTop);
		void setShown(bool shown);					// MACROS shown / left (closes the keyboard, cancels a pick)

		// Machine data (the listing callback; the tests call it directly)
		void listingArrived(const std::string& dir, std::vector<Entry> entries);

		// SETTINGS > CUSTOM n > MACRO
		void startPick(PickHandler picked);			// before the page is shown
		void cancelPick();
		bool picking() const { return static_cast<bool>(m_pick); }

		// Touches (also used by the tests)
		void cellPressed(unsigned int cell);
		void pagePressed(int direction);
		void searchPressed();
		void togglePressed();						// LIST <-> GRID
		void tick();

		// State, for tests
		static std::string DisplayName(const std::string& file);
		const std::string& folder() const { return m_path; }	// relative to the macros folder
		std::string fullDir() const;							// "0:/macros/sub/"
		bool gridMode() const { return m_grid; }
		size_t scroll() const { return m_scroll; }
		bool searching() const { return m_searching; }
		const std::string& query() const { return m_query; }
		std::string cellText(unsigned int cell) const;			// "" = hidden
		std::string cellTag(unsigned int cell) const;
		std::string labelLeft() const;
		std::string labelRight() const { return m_rightText; }
		std::string message() const { return m_message; }

	  private:
		enum : int
		{
			ParentEntry = -1,
			RootEntry = -2
		};

		void request();
		void openFolder(const std::string& path);
		void folderChanged();
		void rebuildView();
		void placeCells();
		void showCells();
		void updateLabel();
		unsigned int pageSize() const;
		bool filterOn() const { return m_searching && !m_query.empty(); }
		void askRun(const std::string& file);
		void openKeyboard();
		void keyboardGone(bool alignPage);
		void cancelSearch();

		lv_obj_t* m_root = nullptr;
		int32_t m_top = 0;
		lv_timer_t* m_timer = nullptr;
		std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

		lv_obj_t* m_labelLeft = nullptr;
		lv_obj_t* m_labelQuery = nullptr;
		lv_obj_t* m_labelRight = nullptr;
		lv_obj_t* m_empty = nullptr;
		FileRow m_cells[NumCells];
		GlyphButton m_search, m_toggle;
		ArrowButton m_up, m_down;

		bool m_shown = false;
		bool m_grid = false;
		std::string m_path;
		std::vector<Entry> m_entries;
		bool m_loaded = false;
		bool m_error = false;
		uint32_t m_requestTime = 0;
		std::vector<int> m_view;
		size_t m_fileTotal = 0;
		size_t m_scroll = 0;
		bool m_searching = false;
		bool m_kbMode = false;
		std::string m_query;
		std::string m_queryText, m_rightText, m_message;
		PickHandler m_pick;
	};
}
