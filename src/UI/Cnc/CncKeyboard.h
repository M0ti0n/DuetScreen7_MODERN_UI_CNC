/*
 * CncKeyboard.h
 *
 * Shared portrait keyboard of the CNC UI (PanelDue CncKeyboard.cpp), used by JOB LIST search and
 * (later) SYSTEM > CONSOLE. The sheet spans the screen width and ends above the nav bar, so STOP
 * stays visible; the screen above it keeps reacting (the JOB LIST rows):
 *
 *   [^][v][ text_                          ][ X ]
 *   [1][2][3][4][5][6][7][8][9][0]
 *   [Q][W][E][R][T][Y][U][I][O][P]          #+= page:  [ ] { } ( ) < > = +
 *     [A][S][D][F][G][H][J][K][L]                        _ : ; " ' # $ % &
 *   [SHIFT][Z][X][C][V][B][N][M][BKSP]                 ~ * / \ ^ ! ? @
 *   [#+=][.][-][     SPACE     ][ ENTER ]              ABC . - SPACE ENTER
 *
 * Letters are upper case; SHIFT (sticky) switches them to lower case. Per user, through the client:
 *   up / down    CONSOLE: command history; JOB LIST: scroll the results
 *   X            close without using the text
 *   ENTER        use the text; closes the keyboard unless the client keeps it open (CONSOLE)
 * Every change of the text is reported (JOB LIST filters as you type). 'gone' tells the client
 * that the keyboard closed without X or ENTER (another popup replaced it).
 * A machine prompt (M291 S7) adds a header with the title and text above the sheet and is modal.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace UI::Cnc::Keyboard
{
	constexpr size_t MaxText = 64;
	constexpr int WindowId = 3;						// Popup::WindowId while open

	struct Client
	{
		std::function<void(const std::string&)> changed;	// text edited (may be empty)
		std::function<void(const std::string&)> enter;		// ENTER (closed unless keepOpenOnEnter)
		std::function<void()> cancel;						// X: the keyboard has closed
		std::function<void(int)> arrow;						// -1 up, +1 down (empty: arrows muted)
		std::function<void()> gone;							// closed by something else
		bool keepOpenOnEnter = false;						// CONSOLE: send and type the next command
		// Machine prompt (M291 S7): header with title and text above the sheet, the rest of the screen
		// blocked (STOP still acts); cancelAllowed false: X greyed, no reaction
		std::string promptTitle, promptText;
		bool cancelAllowed = true;
		size_t maxLength = MaxText;							// keys do nothing once the text is this long
		// Editing (file editor): a caret "|" in the text; a tap in the text field places it, keys insert
		// and BACKSPACE deletes at it. Without it the text is only typed at the end.
		bool cursor = false;
		bool secret = false;								// passwords: each character shown as *
	};

	void Open(const std::string& text, Client client, int owner);
	void Close();									// closes without calling the client
	bool IsOpen();
	bool IsOpenFor(int owner);
	void SetArrowsEnabled(bool up, bool down);
	const std::string& Text();
	void SetText(const std::string& text);			// replace the text (history, clear after sending)
	size_t Cursor();								// byte position of the caret (the end without 'cursor')
	void SetCursor(size_t pos);						// cursor mode: move the caret (whole UTF-8 characters)

	// Tests
	void PressKey(char c);							// a character key (the shown page must have it)
	void PressBackspace();
	void PressShift();
	void PressMode();								// #+= / ABC
	void PressEnter();
	void PressCancel();								// X
	void PressArrow(int dir);
	bool LowerCase();
	bool Symbols();
}
