/*
 * CncAlertsPage.h
 *
 * SYSTEM > ALERT, ported from the PanelDue CNC UI (CncSystemPage.cpp, ALERT part), under the compact
 * DRO:
 *
 *   ALERTS                            12 / 50   NEWEST FIRST
 *   [ ERR   0m12  G0/G1: target position outside machine li.. * ]
 *   [ WARN  2m40  Spindle speed limited to 24000 rpm           ]    10 rows per page
 *   ...
 *   [  ^  ]              PAGE 1 / 2                 [  v  ]
 *
 * Same behaviour as on PanelDue:
 *  - Every line of a machine reply that contains "Error:" or "Warning:" is kept, newest first, with
 *    its age and an accent dot while unread. A repeat of the newest only refreshes it (no flooding).
 *  - Tap a row: ERROR / WARNING popup with the whole text (up to 100 characters); the row is read.
 *  - No CLEAR: they go when they fall out of the list or at power off.
 * Differences: 5 pages of 10 (50 alerts) instead of 11 on one page; the page bar uses the room of
 * row 11. A new alert keeps the page you are looking at (page 1 shows it at the top).
 */
#pragma once

#include "CncLayout.h"
#include "CncWidgets.h"
#include "lvgl/lvgl.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace UI::Cnc
{
	class AlertsPage
	{
	  public:
		static constexpr unsigned int NumRows = CncLayout::Alerts::NumRows;
		static constexpr size_t MaxAlerts = CncLayout::Alerts::MaxAlerts;
		static constexpr size_t MaxTextLen = 100;	// fits the popup info tile

		struct Entry
		{
			uint32_t time;
			bool error;
			bool unread;
			std::string text;
		};

		AlertsPage() = default;
		AlertsPage(const AlertsPage&) = delete;
		AlertsPage& operator=(const AlertsPage&) = delete;
		~AlertsPage();

		void create(lv_obj_t* parent, int32_t parentTop);

		// A machine reply (every reply; the Error: / Warning: lines are kept)
		void collect(const std::string& reply);

		// Touches (also used by the tests)
		void rowPressed(unsigned int row);
		void pagePressed(int direction);
		void tick();								// ages (every second)

		// State, for tests
		static std::string FormatAge(uint32_t ms);
		size_t count() const { return m_alerts.size(); }
		const Entry& entry(size_t i) const { return m_alerts[i]; }	// 0 = newest
		unsigned int page() const { return m_page; }
		unsigned int pageCount() const;
		std::string rowText(unsigned int row) const;	// "" = hidden
		std::string rowAge(unsigned int row) const;
		bool rowUnread(unsigned int row) const;
		std::string labelRight() const;
		std::string pageText() const;

	  private:
		void add(bool error, std::string text);
		void show();

		lv_obj_t* m_root = nullptr;
		int32_t m_top = 0;
		lv_timer_t* m_timer = nullptr;
		lv_obj_t* m_labelRight = nullptr;
		lv_obj_t* m_empty = nullptr;
		lv_obj_t* m_pageLabel = nullptr;
		AlertRow m_rows[NumRows];
		ArrowButton m_up, m_down;

		std::deque<Entry> m_alerts;					// newest first
		unsigned int m_page = 0;
	};
}
