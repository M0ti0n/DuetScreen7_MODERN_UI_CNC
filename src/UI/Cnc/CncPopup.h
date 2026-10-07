/*
 * CncPopup.h
 *
 * The CNC UI's shared popups (PanelDue CncPopups.cpp), PanelDue geometry x 1.25:
 *
 *  - Standard popup, 460 x 480 centred: title tile, then an information tile (up to 6 centred
 *    lines) or a set of preset choices, red X and green check.
 *    Used for ALERT (refused actions), confirmations and preset choices (SLOW JOG FEEDRATE, ...).
 *  - Standard numpad, 460 x 396 directly under the full DRO's accent line, so the DRO stays live
 *    while a value is typed: value tile with unit, tag tile, digits, backspace / X / check, optional
 *    POS key. Used for every number the UI asks for; only the tag, unit and allowed keys change.
 *    A value outside the range is refused with an ALERT whose X brings the numpad back as it was.
 *
 * While a popup is open the rest of the screen does not react, except STOP, which always acts.
 * One popup at a time: opening one replaces the one shown.
 *
 * Forms (labelled button groups, e.g. COPY TO: target + axes) use the standard popup too.
 *
 * Machine popups (PanelDue Modern UI):
 *  - Replies: an error as ALERT ! until X, others without a title tile, closing by themselves after
 *    INFO TIMEOUT. The newest reply replaces the one shown; never over a popup in use.
 *  - Prompts that need an answer typed or chosen (M291 S4..S7): a header with the title and the text
 *    (up to 3 lines) on top of the choices, the numpad or the keyboard. Nothing else replaces them
 *    except the next machine message or STOP.
 */
#pragma once

#include "lvgl/lvgl.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace UI::Cnc::Popup
{
	using Handler = std::function<void()>;

	// Confirmation: title, information lines, X and check. onOk runs on the check mark.
	void Confirm(std::string_view title, const std::vector<std::string>& lines, Handler onOk);

	// ALERT ! with the reason (word-wrapped), only X. Used for every refused action.
	void Alert(std::string_view text);
	inline void Refuse(std::string_view reason) { Alert(reason); }
	// The same with another title (SYSTEM > ALERT: ERROR / WARNING and the whole text)
	void Info(std::string_view title, std::string_view text);
	// The same as a warning (PREVIEW): amber title outline with a warning triangle, amber text
	void Warning(std::string_view title, std::string_view text);

	// Preset choice: title and choice buttons, 'selected' highlighted at the start. A tap selects,
	// the check mark runs onOk with the selected index. columns 0 = automatic (up to 4 in one row,
	// else 3 per row). allowed: false = that choice is greyed and a tap on it does nothing.
	void Choose(std::string_view title,
				const std::vector<std::string>& choices,
				size_t selected,
				std::function<void(size_t)> onOk,
				size_t columns = 0,
				std::function<bool(size_t)> allowed = nullptr);

	// The same with colour swatches instead of words (ACCENT COLOR), 4 per row
	void ChooseColour(std::string_view title,
					  const std::vector<lv_color_t>& colours,
					  size_t selected,
					  std::function<void(size_t)> onOk);

	// Form: up to 2 labelled groups of buttons (up to 6 each: 4 in one row, else rows of 3).
	// Single-pick group: one item selected. Multi group: toggles. The check mark is greyed (and does
	// nothing) while a group has nothing selected. onOk gets the selection of each group (bit masks).
	struct FormGroup
	{
		std::string label;							// small label above the group
		std::vector<std::string> items;
		bool multi = false;
		uint16_t selected = 0;						// initial selection
		uint16_t disabled = 0;						// greyed, cannot be selected
		uint16_t marked = 0;						// a dot: the item already holds something
	};
	void Form(std::string_view title,
			  const std::vector<FormGroup>& groups,
			  std::function<void(const std::vector<uint16_t>&)> onOk);
	// A question ("IS T3 IN THE SPINDLE?") in a tile under the title, the answers below it, none
	// selected yet; onOk gets the index of the chosen answer. Lines() returns the question.
	void FormQuestion(std::string_view title,
					  std::string_view question,
					  const std::vector<std::string>& answers,
					  std::function<void(size_t)> onOk);

	// Reply from the machine: isError = "ALERT !" until X, else no title and closing after timeoutMs
	// (0 = until X). Replaces a reply or a message that needs no answer; force (job errors): also over
	// other popups, but never over a prompt the machine waits for.
	void Response(std::string_view text, bool isError, uint32_t timeoutMs, bool force = false);
	void CloseResponse();
	bool IsResponseOpen();

	// M291 S4: the text in a header, the choices as buttons below it (as many columns as the longest
	// fits), < > in the corners of the action row when they need more pages. onCancel nullptr: no X.
	void ChoosePrompt(std::string_view title,
					  std::string_view text,
					  const std::vector<std::string>& choices,
					  size_t selected,
					  std::function<void(size_t)> onOk,
					  Handler onCancel);
	size_t ChoicePage();
	size_t ChoicePages();
	void PressChoicePage(int dir);					// -1 previous, +1 next page

	// Header of a machine prompt (title tile, text tile of up to 3 lines) at the top of a window, inner
	// width in pixels; the content goes PromptHeaderH() lower. Marks the open window as a machine prompt.
	int32_t PromptHeaderH();
	void DrawPromptHeader(lv_obj_t* box, int32_t innerW, std::string_view title, std::string_view text);
	bool IsMachinePromptOpen();						// a prompt with header (choices, numpad, keyboard)

	struct NumpadSpec
	{
		std::string tag;							// tag tile: "S", "F", "G55 X", ...
		std::string unit;							// in the value tile: "rpm", "mm/min", "mm"
		float value = 0.0f;							// shown when the numpad opens (the first key replaces it)
		unsigned int decimals = 0;					// decimals of that value and of the range in the ALERT
		bool allowDecimal = false;					// '.' key active
		bool allowMinus = false;					// '-' key active
		float min = 0.0f, max = 0.0f;				// accepted range: outside it the check mark is refused
		std::function<float()> pos;					// set: POS key shown, fills in this value
		std::function<void(float)> onOk;
		// Machine prompt (M291 S5 / S6): header above the numpad; cancelAllowed false: X greyed, no reaction
		std::string promptTitle, promptText;
		bool cancelAllowed = true;
		Handler onCancel;
	};
	void Numpad(const NumpadSpec& spec);

	// Message from the machine (M291) in the standard popup: title, text (word-wrapped), the check mark
	// alone (centred) or X and check. waits: the machine waits for an answer given in DWC. passive:
	// M291 S1, the check mark only closes it (a message that needs no answer).
	void Message(std::string_view title,
				 std::string_view text,
				 bool withCancel,
				 Handler onOk,
				 Handler onCancel = nullptr,
				 bool waits = false,
				 bool passive = false);
	bool IsMessageOpen();
	bool IsBlockingMessageOpen();					// a message whose answer the machine waits for
	bool CanShowPassive();							// nothing in use (or a reply): a message that needs no answer may be shown

	// A page's own popup window (probe progress, jog prompt): the frame (blocker with STOP pass-through,
	// page background, accent border) comes from here, the content from the caller. Position in PanelDue
	// pixels; children are placed inside the 2 px border. id tells who owns it (WindowId, 0 = none).
	lv_obj_t* OpenWindow(int id, int32_t pdX, int32_t pdY, int32_t pdW, int32_t pdH);
	// The same in screen pixels. modal false: the rest of the screen keeps reacting (the keyboard over
	// the JOB LIST rows). onClosed runs when the window closes, also when another popup replaces it.
	lv_obj_t* OpenWindowPx(int id, int32_t x, int32_t y, int32_t w, int32_t h, bool modal, Handler onClosed);
	int WindowId();
	// The STOP action (wide STOP buttons inside popups)
	void EmergencyStop();
	// Run a handler from an event of an object inside a popup (the popup may close during it)
	void InEvent(const Handler& fn);

	bool IsOpen();
	void Close();

	// STOP keeps working over a popup: the frame tells where it is and what it does
	void SetStopTarget(int32_t x, int32_t y, int32_t w, int32_t h, Handler onStop);

	// Tests: what is shown, and pressing the buttons
	std::string Title();
	std::vector<std::string> Lines();
	bool HasOk();
	void PressOk();
	void PressCancel();
	bool IsChooseOpen();
	size_t ChoiceSelected();
	void PressChoice(size_t index);
	bool IsFormOpen();
	uint16_t FormSelection(size_t group);
	bool FormOkEnabled();
	void PressFormItem(size_t group, size_t index);
	bool IsNumpadOpen();
	std::string NumpadTag();
	std::string NumpadValue();
	// '0'..'9', '.', '-' (ignored while that key is inactive), '\b' = backspace, 'P' = POS
	void NumpadKey(char key);
}
