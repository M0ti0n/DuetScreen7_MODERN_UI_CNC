/*
 * CncJobListPage.h
 *
 * JOB > JOB LIST, ported from the PanelDue CNC UI (CncJobPage.cpp, JOB LIST part):
 *
 *   FILES                                0:/gcodes      or   SEARCH "brack"      2 of 14
 *   [ fixtures                    FOLDER ]  [SEARCH]
 *   [ bracket_v2.nc                      ]  [  ^   ]
 *   ...  10 rows                            [  v   ]
 *                                           [  SD  ]    only when a second card is mounted
 *
 * Same behaviour as on PanelDue:
 *  - A fresh listing of the open folder each time JOB LIST is shown (folders first, then files).
 *    Paging and search work on that listing and send nothing to the machine.
 *  - Tap a file to select it, tap it again for the JOB popup (trash / X / check, as the Modern UI's
 *    START PRINT): the check opens PREVIEW of the file, where its RUN tile starts it (an addition to
 *    PanelDue, where the second tap ran it). The trash asks "DELETE" and deletes it (M30); refused
 *    for the running job and without a connection. Tap a folder to open it; ".." goes back up.
 *  - SEARCH opens the keyboard; the list (files of the open folder) filters as you type
 *    (case-insensitive, the match in the accent colour). ENTER keeps the filter, X clears it, a tap
 *    on SEARCH while typing leaves the search, a tap on a row keeps the filter and acts on the row.
 *  - "Loading..." until the listing arrives; a folder that cannot be read ("Cannot read this
 *    folder.", after 5 s without an answer here) gets a ".. BACK" row to the gcodes folder.
 *  - SD (as PanelDue): shown when the machine reports another mounted volume (volumes[].mounted) or
 *    one other than 0 is open; a tap goes to the next mounted card, its root ("1:/"); accent look while
 *    a card other than 0 is open. Card removed while open: back to card 0.
 */
#pragma once

#include "CncLayout.h"
#include "CncPreviewSource.h"
#include "CncWidgets.h"
#include "lvgl/lvgl.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UI::Cnc
{
	// What PREVIEW found in the file (empty: no warnings, or the file was not read)
	struct RunCheck
	{
		std::vector<int> warnedOps;		  // operation numbers (from 1) with warnings
		std::vector<std::string> serious; // "Op 3: T5 not in the tool table": asked once more
	};

	// "Do you want to RUN this job?" and M32 on the check mark; refused while a job runs, when not
	// connected or not idle (also checked again on the check mark). PREVIEW's RUN tile uses it.
	// With warnings: the question in the warning look names the operations; serious ones (cutting with
	// the spindle off, a tool the machine does not have) ask "RUN anyway?" after the check mark.
	void AskRunJob(const std::string& path, const RunCheck& check = RunCheck());
	constexpr size_t MaxSeriousLines = 3;

	class JobListPage
	{
	  public:
		static constexpr unsigned int NumRows = CncLayout::JobList::NumRows;
		static constexpr uint32_t ListTimeout = 5000; // ms without an answer: the folder cannot be read

		struct Entry
		{
			std::string name;
			bool folder;
		};

		JobListPage() = default;
		JobListPage(const JobListPage&) = delete;
		JobListPage& operator=(const JobListPage&) = delete;
		~JobListPage();

		// parentTop = screen y of the parent's top edge (the page content area)
		void create(lv_obj_t* parent, int32_t parentTop);
		void setShown(bool shown);					// JOB LIST shown / left (closes the keyboard, keeps the filter)
		void onPreviewRequest(std::function<void()> h) { m_previewRequest = std::move(h); }	// the check of the JOB popup
		void setJobFile(const std::string& path) { m_jobFile = path; }	// job.file.fileName: cannot be deleted while it runs
		static constexpr uint32_t RefreshAfterDelete = 600;		// ms: a fresh listing after M30

		// Machine data (the listing callback; the tests call it directly)
		void listingArrived(const std::string& dir,
							std::vector<Entry> entries,
							std::map<std::string, PreviewSource::Stamp> stamps = {}); // stamps by file name

		// Touches (also used by the tests)
		void rowPressed(unsigned int row);
		void pagePressed(int direction);
		void searchPressed();
		void cardPressed();							// SD: the next mounted card
		unsigned int card() const { return m_card; }
		bool sdShown() const;
		static void Init();							// subscribes to volumes[].mounted (once)
		static void SetVolumeMounted(size_t volume, bool mounted);	// tests / the subscriber
		static constexpr size_t MaxVolumes = 8;
		void tick();								// listing timeout (every 100 ms)

		// State, for tests
		const std::string& folder() const { return m_path; }	// relative to the gcodes folder ("" = it)
		std::string fullDir() const;							// "0:/gcodes/sub/"
		const std::string& selectedName() const { return m_selected; }
		std::string selectedPath() const;			// "0:/gcodes/folder/part.nc", "" when nothing is selected
		PreviewSource::Stamp stampOf(const std::string& path) const; // size and date from the listing, else empty
		size_t scroll() const { return m_scroll; }
		bool searching() const { return m_searching; }
		const std::string& query() const { return m_query; }
		std::string rowText(unsigned int row) const;			// "" = hidden
		std::string labelLeft() const;
		std::string labelRight() const;
		std::string message() const;							// "Loading...", "No files", ... or ""

	  private:
		enum : int
		{
			ParentEntry = -1,	// ".." UP: parent folder
			RootEntry = -2		// ".." BACK after a listing error: the gcodes folder
		};

		void request();
		void openFolder(const std::string& path);
		void folderChanged();						// another folder: top, nothing selected, no search
		void rebuildView();
		void showRows();
		void updateLabel();
		unsigned int visibleRows() const;
		bool filterOn() const { return m_searching && !m_query.empty(); }
		void askAction(const std::string& name);	// the JOB popup: trash / X / check
		void askDelete(const std::string& name);
		std::string previewRefusal(const std::string& path) const;
		void openKeyboard();
		void keyboardGone();
		void cancelSearch();

		lv_obj_t* m_root = nullptr;
		std::function<void()> m_previewRequest;
		std::map<std::string, PreviewSource::Stamp> m_stamps; // the listed folder's files: full path -> stamp
		std::string m_jobFile;
		uint32_t m_refreshAt = 0;					// State::Now() of the listing after a delete, 0 = none
		int32_t m_top = 0;
		lv_timer_t* m_timer = nullptr;
		std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);	// listing callbacks after destruction

		lv_obj_t* m_labelLeft = nullptr;
		lv_obj_t* m_labelQuery = nullptr;
		lv_obj_t* m_labelRight = nullptr;
		lv_obj_t* m_empty = nullptr;
		FileRow m_rows[NumRows];
		GlyphButton m_search;
		ArrowButton m_up, m_down;
		TextButton m_sd;
		unsigned int m_card = 0;					// 0 = the gcodes folder of card 0, else that card's root
		uint8_t m_mountedSeen = 1;
		void updateSd();

		bool m_shown = false;
		std::string m_path;							// open folder, relative to the gcodes folder
		std::vector<Entry> m_entries;				// listing of the open folder
		bool m_loaded = false;
		bool m_error = false;
		uint32_t m_requestTime = 0;
		std::vector<int> m_view;					// indices into m_entries, or ParentEntry / RootEntry
		size_t m_fileTotal = 0;
		size_t m_scroll = 0;
		std::string m_selected;
		bool m_searching = false;
		bool m_kbMode = false;
		std::string m_query;
		std::string m_queryText, m_rightText, m_message;
	};
}
