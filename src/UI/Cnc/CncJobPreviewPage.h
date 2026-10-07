/*
 * CncJobPreviewPage.h
 *
 * JOB > PREVIEW: a last check of a job before running it (no PanelDue page; layout agreed on the
 * mock-up, CncLayout::Preview).
 *
 *   +-------------------------------+ +----+
 *   | file name            [ ISO ]  | | ^  |   3D view (CncPreviewView): stock box and toolpaths,
 *   | 5 OPS . 4 TOOLS . 11:50 [ G0 ] | | 1  |   the selected operation in the accent colour.
 *   |                               | |FACE|   ISO: ISO -> TOP -> FRONT. G0: the selected
 *   |        (3D view)              | | 2  |   operation's rapid moves. Drag rotates, double tap
 *   |                               | | 3  |   goes back to ISO.
 *   | XYZ          STOCK 120x80x20  | | v  |   Operations: number, short name (2 rows of 7), tool.
 *   +-------------------------------+ +----+
 *   | 2  2D ADAPTIVE CLEARING1                    4:35 |   full name, time
 *   | T2  Ø10 FLAT          S 18000      COOLANT       |   tool, spindle, coolant
 *   | Z 0 → -8              F 1200       WCS G54       |   depth, feed, work offset
 *   +---------------------------------------------------+
 *
 * The file: the running job's (its running operation marked and selected), else the one selected in
 * JOB LIST. Downloaded over the network (CncPreviewSource), whatever the machine connection.
 *
 * Info strip, first row: [n  NAME  i] opens the operation's info popup (facts); [warning triangle], only
 * when the operation has warnings, opens the warning popup; the time on the right. A small triangle on an
 * operation button marks its warnings. A rejected download password: tap the notice to type it.
 */
#pragma once

#include "CncGcode.h"
#include "CncPreviewSource.h"
#include "CncPreviewView.h"
#include "CncWidgets.h"
#include "lvgl/lvgl.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UI::Cnc
{
	class JobPreviewPage
	{
	  public:
		~JobPreviewPage();
		void create(lv_obj_t* parent, int32_t parentTop);
		void setShown(bool shown);
		void setListPath(std::function<std::string()> fn) { m_listPath = std::move(fn); }	// JOB LIST selection
		void setJobFile(const std::string& path);		// job.file.fileName (the running job)
		void setJobPosition(uint32_t filePosition);		// job.filePosition
		void tick();									// every 100 ms (and from tests)

		// Tests
		PreviewView& view() { return m_view; }
		int selected() const { return m_selected; }
		int firstShown() const { return m_first; }
		int runningOp() const { return m_running; }
		std::string opButtonText(int k) const;			// "2|ADAPT|CLEAR|T2", "" hidden
		std::string infoText() const;					// the info strip's labels, " | " between
		std::string message() const;					// the notice in the view, "" none
		std::string fileText() const;
		std::string stockText() const;
		void pressOp(int k);							// visible button 0..2
		void pressUp();
		void pressDown();
		void pressPreset();
		void pressRapids();
		void doubleTap();
		void pressMessage();							// tap on the notice (after a failure: try again)
		void pressInfo();								// the name button: info popup
		void pressWarnings();							// the warning button: warning popup
		bool warningButtonShown() const;
		// Checks of an operation against the stock and the machine: below the stock bottom, cutting with the
		// spindle off, a tool not in the tool table, outside the X / Y travel. text: one line; detail: why.
		struct Warning
		{
			std::string text, detail;
		};
		std::vector<Warning> warnings(int op) const;
		std::string details(int op) const;				// the info popup's text
		std::string warningText(int op) const;			// the warning popup's text
		static constexpr int KeyboardOwner = 7;			// machine password

		static constexpr uint32_t ManualSelectHoldMs = 15000;	// a pick by hand beats the running operation

	  private:
		struct OpButton
		{
			lv_obj_t* card = nullptr;
			lv_obj_t* number = nullptr;
			lv_obj_t* name[2] = {nullptr, nullptr};
			lv_obj_t* tool = nullptr;
			lv_obj_t* dot = nullptr;
			lv_obj_t* warn = nullptr;					// small warning triangle
			std::string text;
		};

		std::string wantedPath() const;
		void apply(const PreviewSource::Result& r);
		void select(int op, bool byHand);
		void updateStrip();
		void updateInfo();
		void updateOverlays();
		void showMessage(const std::string& text);
		void createStrip(lv_obj_t* root);
		void createInfo(lv_obj_t* root);

		lv_obj_t* m_root = nullptr;
		int32_t m_top = 0;
		PreviewView m_view;
		lv_obj_t* m_fileLabel = nullptr;
		lv_obj_t* m_summaryLabel = nullptr;
		lv_obj_t* m_stockLabel = nullptr;
		lv_obj_t* m_messageLabel = nullptr;
		TextButton m_presetBtn, m_rapidBtn;
		ArrowButton m_up, m_down;
		std::array<OpButton, 3> m_ops;
		lv_obj_t* m_info = nullptr;
		lv_obj_t* m_infoName = nullptr;
		lv_obj_t* m_infoTime = nullptr;
		lv_obj_t* m_infoTool = nullptr;
		lv_obj_t* m_infoZ = nullptr;
		lv_obj_t* m_infoSLabel = nullptr;
		lv_obj_t* m_infoS = nullptr;
		lv_obj_t* m_infoFLabel = nullptr;
		lv_obj_t* m_infoF = nullptr;
		lv_obj_t* m_infoCoolant = nullptr;
		lv_obj_t* m_infoWcs = nullptr;
		lv_obj_t* m_infoNameBtn = nullptr;				// [n  NAME  i]
		lv_obj_t* m_infoMore = nullptr;					// its "i"
		lv_obj_t* m_infoWarnBtn = nullptr;				// [warning triangle]
		lv_timer_t* m_timer = nullptr;

		std::function<std::string()> m_listPath;
		std::string m_jobFile, m_requested, m_message;
		PreviewSource::Status m_status = PreviewSource::Status::NoFile;
		std::shared_ptr<const Gcode::Program> m_program;
		uint32_t m_generation = 0xFFFFFFFFu;
		uint32_t m_jobPosition = 0, m_manualAt = 0;
		bool m_manual = false;
		int m_selected = 0, m_first = 0, m_running = -1;
		bool m_shown = false;
	};
}
