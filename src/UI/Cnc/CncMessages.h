/*
 * CncMessages.h
 *
 * Messages from the machine (M291, the object model's state.messageBox), routed as on PanelDue
 * (CncUserInterface.cpp ProcessAlert / ClearAlert):
 *
 *   S0 / S1 info      auto probe running: its progress (PROBE:<side> n/m, PROBE=<axis>, status);
 *                     else the standard popup with the check mark, unless a popup is in use
 *                     (S1: the check mark answers M292)
 *   S2 confirm        title TOOL... without axis controls during a job: the tool change row on
 *                     JOB STATUS (OK = M292 P0); axis controls: the jog prompt (PROBE / READ / OK);
 *                     else the standard popup, check mark = M292 P0
 *   S3 confirm/cancel the same with X = M292 P1
 *   S4 choices        the text in a header, the choices below it (pages when they need more), check
 *                     = M292 R{index}; X (only with a cancel button) = M292 P1
 *   S5 / S6 number    header above the numpad; the range of the message (digits / '.' / '-' as it
 *                     allows), check = M292 R{value}
 *   S7 text           header above the keyboard (modal), length checked, ENTER = M292 R{"text"}
 *   cleared           (answered elsewhere, timeout) the message, prompt, jog prompt or tool change row closes
 *
 * Replies (Modern UI): an error ends the auto probe while it runs, else ALERT ! during a job (over
 * other popups). Otherwise, unless it answers a command typed in CONSOLE or SYSTEM is shown: an "Error"
 * reply as ALERT ! until X, others in a popup that closes after INFO TIMEOUT (0 = not shown). Never over
 * a popup in use; the next reply replaces it.
 *
 * Display messages (M117): "MESSAGE" popup with the check mark, unless a popup or the keyboard is in use;
 * progress of the auto probe while it runs. From the PanelDue push message (UART) and from
 * state.displayMessage when it changes (USB / network), the same text not twice.
 *
 * The probe page and JOB STATUS provide the hooks.
 */
#pragma once

#include <cfloat>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UI::Cnc::Messages
{
	// Same values as OM::Alert::Mode
	enum class Mode : int
	{
		None = -1,
		Info = 0,
		InfoClose = 1,
		InfoConfirm = 2,
		ConfirmCancel = 3,
		Choices = 4,
		NumberInt = 5,
		NumberFloat = 6,
		Text = 7,
	};

	struct Message
	{
		Mode mode = Mode::None;
		uint32_t seq = 0;
		uint32_t controls = 0;		// axis controls, bit n = axis n (X Y Z A)
		std::string title, text;
		bool cancelButton = false;					// S4..S7: X answers M292 P1
		std::vector<std::string> choices;			// S4
		int32_t intMin = INT32_MIN, intMax = INT32_MAX, intDefault = 0;		// S5 (S4: default choice)
		float floatMin = -FLT_MAX, floatMax = FLT_MAX, floatDefault = 0.0f;	// S6
		int32_t textMin = 0, textMax = INT32_MAX;	// S7: length
		std::string textDefault;
	};

	void Init();					// subscribes to the display messages (once)
	void Process(const Message& msg);
	// A reply. consoleReply: answers a command typed in CONSOLE; systemShown: SYSTEM is the page shown
	void Response(const std::string& text, bool isError, bool consoleReply = false, bool systemShown = false);
	// M117: push = the PanelDue push message, else state.displayMessage
	void DisplayMessage(const std::string& text, bool push);
	constexpr int KeyboardOwner = 6;					// S7
	constexpr int32_t NumpadLimit = 99999999;			// largest number the numpad takes (9 characters)
	constexpr uint32_t DuplicateTime = 5000;			// ms: the same display message from the other source

	struct ProbeHooks
	{
		std::function<bool()> autoRunning;
		std::function<void(const std::string& title, const std::string& text)> autoMessage;
		std::function<bool(const std::string& title, const std::string& text, uint32_t controls, bool withCancel,
						   uint32_t seq)>
			showJogPrompt;			// false: no axis controls, use the standard popup
		std::function<bool()> jogPromptOpen;
		std::function<void()> closeJogPrompt;
		std::function<void(const std::string& text)> autoError;
	};
	void SetProbeHooks(ProbeHooks hooks);
	bool ProbePopupOpen();			// the auto probe or the jog prompt is in use

	struct JobHooks
	{
		std::function<bool(const std::string& text, uint32_t seq)> showToolPrompt;	// false: no job
		std::function<void()> closeToolPrompt;
		std::function<bool(const std::string& text)> error;							// true: shown
	};
	void SetJobHooks(JobHooks hooks);
	void Reset();					// tests
}
