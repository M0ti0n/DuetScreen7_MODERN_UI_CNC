/*
 * CncHomeView.h
 *
 * The CNC UI's frame on DuetScreen (portrait 600 x 1024), as on PanelDue (CncUserInterface.cpp):
 *   - DRO at the top: the full one (with MACHINE and ZERO) on CONTROL and WCS, the compact
 *     read-only one on JOB, MACROS and SYSTEM
 *   - the page content
 *   - the sub-tab strip on WCS (OFFSETS TOOLS PROBE), JOB (JOB LIST, PREVIEW, JOB STATUS) and
 *     SYSTEM (ALERT CONSOLE SETTINGS); each page opens on its first sub-tab and remembers the last one
 *   - the bottom navigation (CONTROL, WCS, JOB, MACROS, SYSTEM, STOP)
 *
 * Every page / sub-tab has its own content container (getContent). CONTROL, WCS (OFFSETS,
 * TOOLS, PROBE), JOB (JOB LIST, PREVIEW, JOB STATUS), MACROS and SYSTEM (ALERT, CONSOLE, SETTINGS).
 */
#pragma once

#include "CncAlertsPage.h"
#include "CncConsolePage.h"
#include "CncControlPage.h"
#include "CncGlyphs.h"
#include "CncHomePresenter.h"
#include "CncJobListPage.h"
#include "CncJobPreviewPage.h"
#include "CncJobStatusPage.h"
#include "CncMacrosPage.h"
#include "CncSettingsPage.h"
#include "CncWcsOffsetsPage.h"
#include "CncWcsProbePage.h"
#include "CncWcsToolsPage.h"
#include "UI/Components/Screen/Screen.h"
#include "UI/Core/View.h"
#include <vector>

namespace UI
{
	class CncHomeView : public View<CncHomePresenter, Screen>
	{
	  public:
		static constexpr size_t DroRows = CncHomePresenter::MaxDroAxes;
		static constexpr unsigned int NumPages = 5;
		static constexpr unsigned int MaxSubTabs = Cnc::SubTabStrip::MaxTabs;
		enum Page : unsigned int { PageControl = 0, PageWcs, PageJob, PageMacros, PageSystem };
		enum JobTab : unsigned int { JobListTab = 0, JobPreviewTab, JobStatusTab };
		enum SystemTab : unsigned int { SystemAlertTab = 0, SystemConsoleTab, SystemSettingsTab };

		CncHomeView();

		~CncHomeView() override;

		// DRO
		void setAxisRowCount(size_t rows);			// 3 rows of 68 px or 4 rows of 50 px (PanelDue units)
		void setAxisRow(size_t row, char letter, float work, float machine, bool homed);
		void setWorkplace(uint8_t workplaceNumber);	// 0 = G54 .. 8 = G59.3
		void setTool(int tool);						// -1 = none
		void setSpindle(int32_t rpm, bool running);
		void setFeed(int32_t mmPerMin);

		// Pages and sub-tabs
		void showPage(unsigned int page);
		unsigned int getPage() const { return m_page; }
		void showSubTab(unsigned int subTab);			// on the current page
		void showJobPreview();							// JOB > PREVIEW (PREVIEW in JOB LIST's RUN question)
		unsigned int getSubTab(unsigned int page) const { return (page < NumPages) ? m_subTab[page] : 0; }
		static unsigned int getSubTabCount(unsigned int page);
		static bool isCompactPage(unsigned int page);	// JOB, MACROS, SYSTEM
		static int32_t contentTop(unsigned int page);	// screen y of the page content
		static int32_t contentBottom(unsigned int page);	// above the sub-tabs / nav bar
		lv_obj_t* getContent(unsigned int page, unsigned int subTab) const;
		Cnc::ControlPage& getControlPage() { return m_control; }
		Cnc::WcsOffsetsPage& getWcsOffsetsPage() { return m_wcsOffsets; }
		Cnc::WcsToolsPage& getWcsToolsPage() { return m_wcsTools; }
		Cnc::WcsProbePage& getWcsProbePage() { return m_wcsProbe; }
		Cnc::JobListPage& getJobListPage() { return m_jobList; }
		Cnc::JobStatusPage& getJobStatusPage() { return m_jobStatus; }
		Cnc::JobPreviewPage& getJobPreviewPage() { return m_jobPreview; }
		Cnc::MacrosPage& getMacrosPage() { return m_macros; }
		Cnc::AlertsPage& getAlertsPage() { return m_alerts; }
		Cnc::ConsolePage& getConsolePage() { return m_console; }
		Cnc::SettingsPage& getSettingsPage() { return m_settings; }
		void showJobStatus();						// a job started / a tool change prompt: JOB > JOB STATUS
		Cnc::SubTabStrip& getSubTabStrip(unsigned int page) { return m_tabs[page]; }

	  private:
		struct NavTile
		{
			CncHomeView* view;
			unsigned int index;						// page, or NumPages for STOP
			Cnc::NavIcon icon;
			bool active;
		};
		struct TileAction
		{
			CncHomeView* view;
			void (Cnc::ControlPage::*action)();
		};
		struct ZeroButton
		{
			CncHomeView* view;
			size_t row;
			lv_obj_t* obj;
			lv_obj_t* letter;
		};

		void createDro();
		void createCompactDro();
		void createPages();
		void createNav();
		void layoutDro();
		void layoutCompactDro();
		void updateContentVisibility();
		void pageShownChanged();					// tells the pages that load data when shown (JOB LIST)
		void setZeroLocked(bool locked);

		static void drawNavTile(lv_event_t* e);
		static void drawZeroButton(lv_event_t* e);

		lv_obj_t* m_root;							// the Screen's LVGL object

		// Full DRO
		lv_obj_t* m_fullDro;
		lv_obj_t* m_workHeader;
		lv_obj_t* m_letter[DroRows];
		lv_obj_t* m_workCard[DroRows];
		lv_obj_t* m_work[DroRows];
		lv_obj_t* m_machCard[DroRows];
		lv_obj_t* m_mach[DroRows];
		ZeroButton m_zero[DroRows];
		lv_obj_t* m_toolCard;
		lv_obj_t* m_toolValue;
		lv_obj_t* m_spindleCard;
		lv_obj_t* m_spindleValue;
		lv_obj_t* m_feedValue;
		lv_obj_t* m_feedCard;
		std::vector<TileAction> m_tileActions;		// T / S / F tile touches
		size_t m_rows = 3;
		bool m_zeroLocked = false;					// job running or paused

		// Compact DRO
		lv_obj_t* m_compactDro;
		lv_obj_t* m_cmpHeader;
		lv_obj_t* m_cmpLetter[DroRows];
		lv_obj_t* m_cmpCard[DroRows];
		lv_obj_t* m_cmpWork[DroRows];
		lv_obj_t* m_cmpToolCard;
		lv_obj_t* m_cmpTool;
		lv_obj_t* m_cmpSpindleCard;
		lv_obj_t* m_cmpSpindleLetter;
		lv_obj_t* m_cmpSpindle;
		lv_obj_t* m_cmpFeedCard;
		lv_obj_t* m_cmpFeedLetter;
		lv_obj_t* m_cmpFeed;

		// Pages
		lv_obj_t* m_pageObj[NumPages];
		lv_obj_t* m_content[NumPages][MaxSubTabs] = {};
		Cnc::SubTabStrip m_tabs[NumPages];
		unsigned int m_subTab[NumPages] = {};
		Cnc::ControlPage m_control;
		Cnc::WcsOffsetsPage m_wcsOffsets;
		Cnc::WcsToolsPage m_wcsTools;
		Cnc::WcsProbePage m_wcsProbe;
		Cnc::JobListPage m_jobList;
		Cnc::JobPreviewPage m_jobPreview;
		lv_timer_t* m_queueTimer = nullptr; // runs what the receive thread queued (State::RunOnUi)
		Cnc::JobStatusPage m_jobStatus;
		Cnc::MacrosPage m_macros;
		Cnc::AlertsPage m_alerts;
		Cnc::ConsolePage m_console;
		Cnc::SettingsPage m_settings;
		unsigned int m_page = 0;

		// Navigation
		NavTile m_nav[NumPages + 1];
		lv_obj_t* m_navObj[NumPages + 1];
	};
} // namespace UI
