/*
 * CncMessages.cpp
 */

#include "CncMessages.h"
#include "CncKeyboard.h"
#include "CncPanel.h"
#include "CncNxt.h"
#include "CncPopup.h"
#include "CncState.h"
#include "Subscribers/Subscribers.h"
#include <algorithm>
#include <cmath>
#include <fmt/format.h>

namespace UI::Cnc::Messages
{
	namespace
	{
		ProbeHooks s_hooks;
		JobHooks s_jobHooks;
		bool s_inited = false;

		// Display messages: the last of each source
		bool s_stateKnown = false; // state.displayMessage seen since connecting (the first is old)
		std::string s_lastState, s_lastPush;
		uint32_t s_lastPushTime = 0;
		std::string s_lastShown;
		uint32_t s_lastShownTime = 0;

		bool autoRunning()
		{
			return s_hooks.autoRunning && s_hooks.autoRunning();
		}

		bool jogOpen()
		{
			return s_hooks.jogPromptOpen && s_hooks.jogPromptOpen();
		}

		bool jogPrompt(const Message& msg, const std::string& title, bool withCancel)
		{
			return s_hooks.showJogPrompt && s_hooks.showJogPrompt(title, msg.text, msg.controls, withCancel, msg.seq);
		}

		void send(const std::string& cmd)
		{
			State::Send(cmd + "\n");
		}

		Popup::Handler cancelHandler(const Message& msg)
		{
			const uint32_t seq = msg.seq;
			return msg.cancelButton ? Popup::Handler([seq] { send(fmt::format("M292 P1 S{:d}", seq)); }) : nullptr;
		}

		// Decimals that show the range and the default exactly (up to 3)
		unsigned int decimalsFor(std::initializer_list<float> values)
		{
			for (unsigned int d = 0; d < 3; ++d)
			{
				const float f = std::pow(10.0f, (float)d);
				bool exact = true;
				for (float v : values)
				{
					const float x = v * f;
					exact = exact && std::fabs(x - std::round(x)) < 1e-3f;
				}
				if (exact)
				{
					return d;
				}
			}
			return 3;
		}

		float clampLimit(float v)
		{
			return std::max((float)-NumpadLimit, std::min((float)NumpadLimit, v));
		}

		void numberPrompt(const Message& msg, const std::string& title, bool isFloat)
		{
			const uint32_t seq = msg.seq;
			Popup::NumpadSpec spec;
			spec.tag = "VALUE";
			spec.promptTitle = title;
			spec.promptText = msg.text;
			spec.cancelAllowed = msg.cancelButton;
			spec.onCancel = cancelHandler(msg);
			if (isFloat)
			{
				spec.min = clampLimit(msg.floatMin);
				spec.max = clampLimit(msg.floatMax);
				spec.value = std::max(spec.min, std::min(spec.max, msg.floatDefault));
				spec.decimals = decimalsFor({spec.min, spec.max, spec.value});
				spec.allowDecimal = true;
				spec.onOk = [seq](float v) { send(fmt::format("M292 R{{{}}} S{:d}", v, seq)); };
			}
			else
			{
				spec.min = clampLimit((float)msg.intMin);
				spec.max = clampLimit((float)msg.intMax);
				spec.value = std::max(spec.min, std::min(spec.max, (float)msg.intDefault));
				spec.decimals = 0;
				spec.onOk = [seq](float v) { send(fmt::format("M292 R{{{:d}}} S{:d}", (long)std::lround(v), seq)); };
			}
			spec.allowMinus = spec.min < 0.0f;
			Popup::Numpad(spec);
		}

		// RRF string: a quote is doubled
		std::string quoted(const std::string& text)
		{
			std::string out;
			for (char c : text)
			{
				out += c;
				if (c == '"')
				{
					out += '"';
				}
			}
			return out;
		}

		void textPrompt(const Message& msg, const std::string& title, const std::string& text)
		{
			Keyboard::Client client;
			client.promptTitle = title;
			client.promptText = msg.text;
			client.cancelAllowed = msg.cancelButton;
			const size_t minLen = (size_t)std::max<int32_t>(0, msg.textMin);
			const size_t maxLen = std::min<size_t>(Keyboard::MaxText, (size_t)std::max<int32_t>(0, msg.textMax));
			client.maxLength = maxLen; // the keys do nothing once the text is this long
			const Popup::Handler onCancel = cancelHandler(msg);
			if (onCancel)
			{
				client.cancel = onCancel;
			}
			client.enter = [msg, title, minLen, maxLen](const std::string& entered)
			{
				if (entered.size() < minLen) // too long cannot be typed
				{
					// The check mark brings the keyboard back with the text typed
					const std::string reason = (minLen == maxLen)
												   ? fmt::format("Enter {:d} characters.", minLen)
												   : fmt::format("Enter {:d} to {:d} characters.", minLen, maxLen);
					Popup::Message(
						"ALERT !", reason, false, [msg, title, entered] { textPrompt(msg, title, entered); });
					Popup::MarkMachineBox(); // part of the prompt: closed with it when answered elsewhere
					return;
				}
				send(fmt::format("M292 R{{\"{:s}\"}} S{:d}", quoted(entered), msg.seq));
			};
			Keyboard::Open(text, client, KeyboardOwner);
		}

		// Subscribers run on the receive thread: the popup is shown by the UI thread
		bool pushMessage(Comm::JsonDecoder*, const char* data, const size_t[])
		{
			State::RunOnUi([text = std::string(data != nullptr ? data : "")] { DisplayMessage(text, true); });
			return true;
		}

		bool stateDisplayMessage(Comm::JsonDecoder*, const char* data, const size_t[])
		{
			State::RunOnUi([text = std::string(data != nullptr ? data : "")] { DisplayMessage(text, false); });
			return true;
		}
	} // namespace

	void Init()
	{
		if (s_inited)
		{
			return;
		}
		s_inited = true;
		SubscriberMap::addSubscriber("message", &pushMessage);
		SubscriberMap::addSubscriber("state:displayMessage", &stateDisplayMessage);
	}

	namespace
	{
		void show(const Message& msg); // a new box (not None)
	}

	void Process(const Message& msg)
	{
		if (msg.mode == Mode::None)
		{
			// Answered elsewhere (DWC, timeout). Only the box's own popup: the machine reports "no box" with
			// every state update, and an M117 MESSAGE or JOB DONE must stay.
			if (Popup::IsMachineBoxOpen() || Popup::IsMachinePromptOpen())
			{
				Popup::Close();
			}
			if (s_hooks.closeJogPrompt)
			{
				s_hooks.closeJogPrompt();
			}
			if (s_jobHooks.closeToolPrompt)
			{
				s_jobHooks.closeToolPrompt();
			}
			return;
		}

		const uint32_t opened = Popup::OpenCount();
		show(msg);
		if (Popup::OpenCount() != opened)
		{
			Popup::MarkMachineBox(); // closed when the machine reports no box
		}
	}

	namespace
	{
		void show(const Message& msg)
		{
			const uint32_t seq = msg.seq;
			const std::string title = msg.title.empty() ? std::string("MESSAGE") : msg.title;
			// Tool change prompt during a job (title starts with TOOL, no jog controls): inline on JOB STATUS.
			// NXT's tool change macros ask with "Install Tool" / "Remove Tool" (and the touch probe's name)
			const bool toolTitle = title.rfind("TOOL", 0) == 0 ||
								   (Nxt::Active() && (title.rfind("Install ", 0) == 0 || title.rfind("Remove ", 0) == 0));
			if ((msg.mode == Mode::InfoConfirm || msg.mode == Mode::ConfirmCancel) && msg.controls == 0 && toolTitle &&
				s_jobHooks.showToolPrompt && s_jobHooks.showToolPrompt(msg.text, seq))
			{
				Popup::CloseResponse(); // a reply popup must not hide the prompt's OK
				return;
			}
			switch (msg.mode)
			{
			case Mode::Info:
			case Mode::InfoClose:
				if (msg.title == "PROBE=TIP" && s_hooks.tipResult)
				{
					s_hooks.tipResult(msg.text); // the tip calibration's result: TIP Ø
					if (msg.mode == Mode::InfoClose)
					{
						send(fmt::format("M292 P0 S{:d}", seq));
					}
				}
				else if (autoRunning())
				{
					s_hooks.autoMessage(msg.title, msg.text); // progress of the auto probe macro
				}
				else if (Popup::CanShowPassive() && !jogOpen())
				{
					// Not over a popup in use (a value being typed is not thrown away).
					// S1 (close button): closing it answers RRF (M292).
					Popup::Handler onOk = nullptr;
					if (msg.mode == Mode::InfoClose)
					{
						onOk = [seq] { send(fmt::format("M292 P0 S{:d}", seq)); };
					}
					Popup::Message(title, msg.text, false, onOk, nullptr, false, true);
				}
				break;

			case Mode::InfoConfirm:
				if (!jogPrompt(msg, title, false))
				{
					Popup::Message(title, msg.text, false, [seq] { send(fmt::format("M292 P0 S{:d}", seq)); });
				}
				break;

			case Mode::ConfirmCancel:
				if (!jogPrompt(msg, title, true))
				{
					Popup::Message(
						title,
						msg.text,
						true,
						[seq] { send(fmt::format("M292 P0 S{:d}", seq)); },
						[seq] { send(fmt::format("M292 P1 S{:d}", seq)); });
				}
				break;

			case Mode::Choices:
				if (msg.choices.empty())
				{
					// Nothing to choose from: answered in DWC. First line first, so wrapping never cuts it.
					Popup::Message(title, "Answer this prompt in DWC.\n" + msg.text, false, nullptr, nullptr, true);
					break;
				}
				Popup::ChoosePrompt(
					title,
					msg.text,
					msg.choices,
					(msg.intDefault >= 0) ? (size_t)msg.intDefault : 0,
					[seq](size_t i) { send(fmt::format("M292 R{{{:d}}} S{:d}", i, seq)); },
					cancelHandler(msg));
				break;

			case Mode::NumberInt:
				numberPrompt(msg, title, false);
				break;

			case Mode::NumberFloat:
				numberPrompt(msg, title, true);
				break;

			case Mode::Text:
				textPrompt(msg, title, msg.textDefault);
				break;

			default:
				break;
			}
		}

	} // namespace

	void Response(const std::string& text, bool isError, bool consoleReply, bool systemShown)
	{
		if (isError && autoRunning() && s_hooks.autoError)
		{
			s_hooks.autoError(text); // shown instead of the auto probe popup
			return;
		}
		if (isError && State::JobInProgress() && s_jobHooks.error && s_jobHooks.error(text))
		{
			return; // ALERT ! during a job, also over other popups
		}
		if (consoleReply || systemShown)
		{
			return; // shown in CONSOLE / SYSTEM > ALERT
		}
		// "Error" replies as ALERT ! until X, others close after INFO TIMEOUT (0 = not shown)
		const bool modernError = text.rfind("Error", 0) == 0;
		const unsigned int timeout = Panel::InfoTimeout();
		if (modernError || timeout != 0)
		{
			Popup::Response(text, modernError, timeout * 1000u);
		}
	}

	void DisplayMessage(const std::string& text, bool push)
	{
		const uint32_t now = State::Now();
		if (push)
		{
			s_lastPush = text;
			s_lastPushTime = now;
		}
		else
		{
			const OM::PrinterStatus status = State::Status();
			const bool connecting =
				status == OM::PrinterStatus::connecting || status == OM::PrinterStatus::disconnected;
			if (!s_stateKnown || connecting)
			{
				s_stateKnown = !connecting;
				s_lastState = text; // already there when the panel connected: not shown
				return;
			}
			if (text == s_lastState)
			{
				return; // reported again with every state update
			}
			s_lastState = text;
			if (text == s_lastPush && now - s_lastPushTime < DuplicateTime)
			{
				return; // the push message showed it
			}
		}
		if (text.empty())
		{
			return;
		}
		if (text == s_lastShown && now - s_lastShownTime < DuplicateTime && !push)
		{
			return;
		}
		s_lastShown = text;
		s_lastShownTime = now;

		if (autoRunning())
		{
			s_hooks.autoMessage("", text); // progress of the auto probe macro
		}
		else if (Popup::CanShowPassive() && !Keyboard::IsOpen() && !jogOpen())
		{
			// never over a prompt that waits for an answer or a popup in use (Modern UI)
			Popup::Message("MESSAGE", text, false, nullptr, nullptr, false, true);
		}
	}

	bool ProbePopupOpen()
	{
		return autoRunning() || jogOpen();
	}

	void SetJobHooks(JobHooks hooks)
	{
		s_jobHooks = std::move(hooks);
	}

	void SetProbeHooks(ProbeHooks hooks)
	{
		s_hooks = std::move(hooks);
	}

	void Reset()
	{
		s_hooks = ProbeHooks();
		s_jobHooks = JobHooks();
		s_stateKnown = false;
		s_lastState.clear();
		s_lastPush.clear();
		s_lastShown.clear();
		s_lastPushTime = s_lastShownTime = 0;
	}
} // namespace UI::Cnc::Messages
