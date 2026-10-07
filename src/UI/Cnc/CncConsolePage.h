/*
 * CncConsolePage.h
 *
 * SYSTEM > CONSOLE, ported from the PanelDue CNC UI (CncSystemPage.cpp, CONSOLE part), under the
 * compact DRO:
 *
 *   CONSOLE                                   NEWEST FIRST
 *   [ 0m10  G10 L20 P1 Z0                              ]
 *   [ 0m42  Error: G1: target position outside ...     ]   15 lines, newest message on top
 *   [ ...                                              ]
 *   [ Tap to enter G-code...                ][ KEYBOARD ]
 *
 * Same behaviour as on PanelDue:
 *  - Log colours: typed command = text, reply = muted, a line with "Warning:" = amber, "Error:" = red.
 *  - Long replies wrap at word boundaries; the age is shown on the first line of each message. A
 *    message never takes more than the whole log: its last line then ends in "..".
 *  - The input line and the KEYBOARD button open the shared keyboard. ENTER sends and stays open for
 *    the next command, up / down step through the last 5 commands, X closes it and keeps the unsent
 *    text for next time (also when another page or a popup closes it).
 *  - A reply that comes within 3 s of a typed command belongs to it (lastReplyWasConsole: the
 *    machine replies popup will not show it again).
 * Difference: 7 lines stay visible above the keyboard (PanelDue 6; the sheet is 24 px lower here).
 */
#pragma once

#include "CncLayout.h"
#include "CncWidgets.h"
#include "lvgl/lvgl.h"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace UI::Cnc
{
	class ConsolePage
	{
	  public:
		static constexpr unsigned int NumLines = CncLayout::Console::NumLines;
		static constexpr size_t NumHistory = 5;
		static constexpr uint32_t ConsoleReplyTime = 3000;	// ms: a reply this soon after a command belongs to it

		enum class Kind : uint8_t
		{
			Command,
			Reply,
			Warning,
			Error
		};

		ConsolePage() = default;
		ConsolePage(const ConsolePage&) = delete;
		ConsolePage& operator=(const ConsolePage&) = delete;
		~ConsolePage();

		void create(lv_obj_t* parent, int32_t parentTop);
		void setShown(bool shown);					// CONSOLE left: the keyboard closes, the draft stays

		// Every machine reply
		void response(const std::string& text);
		bool lastReplyWasConsole() const { return m_consoleReplyNow; }

		// Touches (also used by the tests)
		void inputPressed();						// input line or KEYBOARD
		void tick();								// ages (every second)

		// State, for tests
		std::string lineText(unsigned int row) const;	// screen row, "" = empty
		std::string lineAge(unsigned int row) const;
		Kind lineKind(unsigned int row) const;
		bool keyboardOpen() const { return m_kbOpen; }
		const std::string& draft() const { return m_draft; }
		const std::vector<std::string>& history() const { return m_history; }

	  private:
		struct LogLine
		{
			uint32_t time;							// of the message (first line only)
			Kind kind;
			bool first;								// first line of a message
			std::string text;
		};

		void addMessage(Kind kind, const std::string& text);
		void addLine(Kind kind, bool first, std::string text);
		void mapRows(int rowLine[NumLines]) const;
		void showLog();
		void showAges();
		void send(const std::string& text);
		void historyArrow(int dir);
		void updateArrows();
		void keyboardGone();

		lv_obj_t* m_root = nullptr;
		int32_t m_top = 0;
		lv_timer_t* m_timer = nullptr;
		lv_obj_t* m_age[NumLines] = {};
		lv_obj_t* m_text[NumLines] = {};
		TextButton m_input;
		GlyphButton m_kbButton;

		std::deque<LogLine> m_lines;				// oldest to newest, up to NumLines
		bool m_shown = false;

		// Keyboard and history
		std::vector<std::string> m_history;			// 0 = newest
		int m_historyPos = -1;						// -1 = the new command being typed
		std::string m_draft;						// typed but not sent (kept over history and reopening)
		bool m_kbOpen = false;
		uint32_t m_lastCommandTime = 0;
		bool m_commandSent = false;					// waiting for the reply to a typed command
		bool m_consoleReplyNow = false;				// the reply just logged answered a typed command
	};
}
