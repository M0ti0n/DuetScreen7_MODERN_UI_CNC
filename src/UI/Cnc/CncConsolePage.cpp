/*
 * CncConsolePage.cpp
 *
 * Positions are CncLayout::Console values (PanelDue pixels) through S() / SL(), relative to the page.
 */

#include "CncConsolePage.h"
#include "CncAlertsPage.h"
#include "CncKeyboard.h"
#include "CncState.h"
#include "CncTheme.h"
#include <algorithm>

namespace UI::Cnc
{
	using namespace CncLayout;
	namespace CN = CncLayout::Console;

	namespace
	{
		constexpr int KeyboardOwner = 4;

		int32_t textWidth(const std::string& t)
		{
			lv_point_t size;
			lv_text_get_size(&size, t.c_str(), Font::Small(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
			return size.x;
		}

		bool isCont(char c)
		{
			return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
		}

		// Never end a piece in the middle of a UTF-8 character
		size_t charBoundary(const std::string& s, size_t from, size_t n)
		{
			while (n > 0 && from + n < s.size() && isCont(s[from + n]))
			{
				--n;
			}
			return n;
		}

		lv_color_t kindColour(ConsolePage::Kind k)
		{
			switch (k)
			{
			case ConsolePage::Kind::Command:
				return Colour::Text();
			case ConsolePage::Kind::Warning:
				return Colour::Amber();
			case ConsolePage::Kind::Error:
				return Colour::StopRed();
			default:
				return Colour::Muted();
			}
		}
	} // namespace

	ConsolePage::~ConsolePage()
	{
		if (Keyboard::IsOpenFor(KeyboardOwner))
		{
			Keyboard::Close(); // its callbacks point here
		}
		if (m_timer != nullptr)
		{
			lv_timer_delete(m_timer);
		}
	}

	// ---- Building ------------------------------------------------------------------------------

	void ConsolePage::create(lv_obj_t* parent, int32_t parentTop)
	{
		m_root = CreateBare(parent, 0, 0, ScreenW, 1);
		lv_obj_set_size(m_root, LV_PCT(100), LV_PCT(100)); // the parent is not laid out yet
		m_top = parentTop;
		auto y = [this](int32_t pdY) { return S(pdY) - m_top; };
		const int32_t smallH = lv_font_get_line_height(Font::Small());

		auto label = [&](int32_t x, int32_t w, const char* text, lv_text_align_t align)
		{ CreateLabel(m_root, S(x), y(CN::LabelY), SL(x, w), smallH, Font::Small(), Colour::Muted(), align, text); };
		label(CN::LabelLeftX, CN::LabelW, "CONSOLE", LV_TEXT_ALIGN_LEFT);
		label(CN::LabelRightX, CN::LabelW, "NEWEST FIRST", LV_TEXT_ALIGN_RIGHT);

		// Log card and its lines
		lv_obj_t* card = CreateBare(m_root, S(CN::LogX), y(CN::LogY), SL(CN::LogX, CN::LogW), SL(CN::LogY, CN::LogH));
		StyleCard(card, Colour::Tile(), Colour::Border());
		for (unsigned int r = 0; r < NumLines; ++r)
		{
			const int32_t ly = y(CN::LineY(r)) - 2;
			m_age[r] = CreateLabel(m_root,
								   S(CN::TimeX),
								   ly,
								   SL(CN::TimeX, CN::TimeW),
								   smallH,
								   Font::Small(),
								   Colour::Muted(),
								   LV_TEXT_ALIGN_RIGHT,
								   "");
			m_text[r] = CreateLabel(m_root,
									S(CN::TextX),
									ly,
									SL(CN::TextX, CN::TextW),
									smallH,
									Font::Small(),
									Colour::Text(),
									LV_TEXT_ALIGN_LEFT,
									"");
		}

		// Input line and keyboard button: both open the keyboard
		m_input.create(m_root,
					   S(CN::InputX),
					   y(CN::InputY),
					   SL(CN::InputX, CN::InputW),
					   SL(CN::InputY, CN::InputH),
					   "Tap to enter G-code...",
					   Font::Small());
		m_input.setColours(Colour::Muted(), Colour::Tile());
		m_input.setAlignLeft(S(18));
		m_input.setPressedLook(false);
		m_input.onPress([this] { inputPressed(); });
		m_kbButton.create(m_root,
						  S(CN::KbButtonX),
						  y(CN::InputY),
						  SL(CN::KbButtonX, CN::KbButtonW),
						  SL(CN::InputY, CN::InputH),
						  GlyphButton::Kind::Keyboard);
		m_kbButton.onPress([this] { inputPressed(); });

		m_timer = lv_timer_create(
			[](lv_timer_t* t) { static_cast<ConsolePage*>(lv_timer_get_user_data(t))->tick(); }, 1000, this);
		showLog();
	}

	void ConsolePage::setShown(bool shown)
	{
		m_shown = shown;
		if (!shown && m_kbOpen)
		{
			if (m_historyPos < 0)
			{
				m_draft = Keyboard::Text(); // keep the unsent text for next time
			}
			Keyboard::Close();
			keyboardGone();
		}
	}

	// ---- Log -----------------------------------------------------------------------------------

	void ConsolePage::response(const std::string& text)
	{
		const bool console = m_commandSent && State::Now() - m_lastCommandTime < ConsoleReplyTime;
		addMessage(Kind::Reply, text); // colour per line (Error: / Warning:)
		m_consoleReplyNow = console;
		if (console)
		{
			m_commandSent = false; // later replies are the job's again
		}
	}

	void ConsolePage::addLine(Kind kind, bool first, std::string text)
	{
		for (char& c : text)
		{
			if (c == '\t')
			{
				c = ' ';
			}
		}
		m_lines.push_back({first ? State::Now() : 0, kind, first, std::move(text)});
		while (m_lines.size() > NumLines)
		{
			m_lines.pop_front();
		}
	}

	// Split a message into lines that fit the text column (line breaks start a new line). A message
	// never takes more than the whole log: its last line then ends in "..".
	void ConsolePage::addMessage(Kind kind, const std::string& text)
	{
		const int32_t maxW = SL(CN::TextX, CN::TextW);
		bool first = true;
		size_t added = 0;
		Kind lineKind = kind;
		size_t p = 0;
		const size_t n = text.size();
		auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
		while (p < n)
		{
			bool newLine = first;
			while (p < n && isSpace(text[p]))
			{
				newLine = newLine || text[p] == '\n';
				++p;
			}
			if (p >= n)
			{
				break;
			}
			size_t len = 0;
			while (p + len < n && text[p + len] != '\n' && text[p + len] != '\r')
			{
				++len;
			}
			if (newLine && kind != Kind::Command)
			{
				// "Error..." / "Warning..." anywhere on this line of the reply
				const std::string line = text.substr(p, len);
				lineKind = (line.find("Error:") != std::string::npos)	  ? Kind::Error
						   : (line.find("Warning:") != std::string::npos) ? Kind::Warning
																		  : Kind::Reply;
			}
			// Longest part that fits, then back to a space if there is one
			size_t take = len;
			if (textWidth(text.substr(p, len)) > maxW)
			{
				size_t lo = 0, hi = len; // lo fits, hi does not
				while (hi - lo > 1)
				{
					const size_t mid = (lo + hi) / 2;
					const size_t cb = charBoundary(text, p, mid);
					if (cb == 0 || textWidth(text.substr(p, cb)) <= maxW)
					{
						lo = mid;
					}
					else
					{
						hi = mid;
					}
				}
				take = charBoundary(text, p, lo);
				if (take == 0)
				{
					take = 1;
					while (take < len && isCont(text[p + take]))
					{
						++take; // one whole character at least
					}
				}
				size_t s = take;
				while (s > take / 2 && text[p + s] != ' ')
				{
					--s;
				}
				if (s > take / 2)
				{
					take = s; // split at the space
				}
			}
			// More text after this piece?
			size_t rest = p + take;
			while (rest < n && isSpace(text[rest]))
			{
				++rest;
			}
			if (added + 1 == NumLines && rest < n)
			{
				// Last line the log can hold: cut it so ".." fits, and mark that the message goes on
				const int32_t dotsW = textWidth("..");
				size_t k = take;
				while (k > 0 && textWidth(text.substr(p, k)) + dotsW > maxW)
				{
					k = charBoundary(text, p, k - 1);
				}
				addLine(lineKind, first, text.substr(p, k) + "..");
				break;
			}
			addLine(lineKind, first, text.substr(p, take));
			++added;
			first = false;
			p += take;
			if (added == NumLines)
			{
				break;
			}
		}
		if (m_root != nullptr)
		{
			showLog();
		}
	}

	// Screen rows: newest message first, each message's lines in reading order.
	// rowLine[r] = index into m_lines (0 = oldest), or -1 for an empty row.
	void ConsolePage::mapRows(int rowLine[NumLines]) const
	{
		std::fill(rowLine, rowLine + NumLines, -1);
		size_t r = 0;
		int last = (int)m_lines.size() - 1;
		while (r < NumLines && last >= 0)
		{
			int start = last;
			while (start > 0 && !m_lines[(size_t)start].first)
			{
				--start; // back to the message's first line
			}
			for (int m = start; m <= last && r < NumLines; ++m)
			{
				rowLine[r++] = m;
			}
			last = start - 1;
		}
	}

	void ConsolePage::showLog()
	{
		int rowLine[NumLines];
		mapRows(rowLine);
		const unsigned int visible = m_kbOpen ? CN::LinesAboveKeyboard : NumLines;
		for (unsigned int r = 0; r < NumLines; ++r)
		{
			if (rowLine[r] >= 0 && r < visible)
			{
				const LogLine& l = m_lines[(size_t)rowLine[r]];
				lv_obj_set_style_text_color(m_text[r], kindColour(l.kind), 0);
				lv_label_set_text(m_text[r], l.text.c_str());
			}
			else
			{
				lv_label_set_text(m_text[r], "");
			}
		}
		showAges();
	}

	void ConsolePage::showAges()
	{
		int rowLine[NumLines];
		mapRows(rowLine);
		const unsigned int visible = m_kbOpen ? CN::LinesAboveKeyboard : NumLines;
		const uint32_t now = State::Now();
		for (unsigned int r = 0; r < NumLines; ++r)
		{
			std::string age;
			if (rowLine[r] >= 0 && r < visible)
			{
				const LogLine& l = m_lines[(size_t)rowLine[r]];
				if (l.first)
				{
					age = AlertsPage::FormatAge(now - l.time);
				}
			}
			if (age != lv_label_get_text(m_age[r]))
			{
				lv_label_set_text(m_age[r], age.c_str()); // redraw only what changed
			}
		}
	}

	void ConsolePage::tick()
	{
		if (m_shown)
		{
			showAges();
		}
	}

	// ---- Keyboard ------------------------------------------------------------------------------

	void ConsolePage::inputPressed()
	{
		if (m_kbOpen)
		{
			return;
		}
		m_historyPos = -1;
		Keyboard::Client client;
		client.changed = [this](const std::string& text)
		{
			if (m_historyPos < 0)
			{
				m_draft = text; // what is being typed, not a recalled command
			}
		};
		client.enter = [this](const std::string& text) { send(text); };
		client.cancel = [this]
		{
			if (m_historyPos < 0)
			{
				m_draft = Keyboard::Text(); // X keeps the unsent text for next time
			}
			keyboardGone();
		};
		client.arrow = [this](int dir) { historyArrow(dir); };
		client.gone = [this]
		{
			if (m_historyPos < 0)
			{
				m_draft = Keyboard::Text();
			}
			keyboardGone();
		};
		client.keepOpenOnEnter = true;
		m_kbOpen = true;
		Keyboard::Open(m_draft, client, KeyboardOwner);
		updateArrows();
		showLog(); // the rows under the sheet are blanked
	}

	void ConsolePage::send(const std::string& text)
	{
		const size_t b = text.find_first_not_of(' ');
		if (b == std::string::npos)
		{
			return; // an empty line sends nothing
		}
		const std::string cmd = text.substr(b);
		State::Send(cmd + "\n");
		addMessage(Kind::Command, cmd);
		m_lastCommandTime = State::Now();
		m_commandSent = true;
		m_draft.clear();

		// History: newest first, no immediate repeat
		if (m_history.empty() || m_history.front() != cmd)
		{
			m_history.insert(m_history.begin(), cmd);
			if (m_history.size() > NumHistory)
			{
				m_history.pop_back();
			}
		}
		m_historyPos = -1;
		Keyboard::SetText("");
		updateArrows();
	}

	// up (-1): older command, down (+1): newer, then back to the draft
	void ConsolePage::historyArrow(int dir)
	{
		if (dir < 0 && m_historyPos + 1 < (int)m_history.size())
		{
			if (m_historyPos < 0)
			{
				m_draft = Keyboard::Text(); // back with the down arrow
			}
			++m_historyPos;
			Keyboard::SetText(m_history[(size_t)m_historyPos]);
		}
		else if (dir > 0 && m_historyPos >= 0)
		{
			--m_historyPos;
			Keyboard::SetText((m_historyPos >= 0) ? m_history[(size_t)m_historyPos] : m_draft);
		}
		updateArrows();
	}

	void ConsolePage::updateArrows()
	{
		Keyboard::SetArrowsEnabled(m_historyPos + 1 < (int)m_history.size(), m_historyPos >= 0);
	}

	void ConsolePage::keyboardGone()
	{
		if (m_kbOpen)
		{
			m_kbOpen = false;
			m_historyPos = -1;
			showLog(); // the rows under the sheet come back
		}
	}

	// ---- For tests ----

	std::string ConsolePage::lineText(unsigned int row) const
	{
		return (row < NumLines) ? lv_label_get_text(m_text[row]) : std::string();
	}

	std::string ConsolePage::lineAge(unsigned int row) const
	{
		return (row < NumLines) ? lv_label_get_text(m_age[row]) : std::string();
	}

	ConsolePage::Kind ConsolePage::lineKind(unsigned int row) const
	{
		int rowLine[NumLines];
		mapRows(rowLine);
		return (row < NumLines && rowLine[row] >= 0) ? m_lines[(size_t)rowLine[row]].kind : Kind::Reply;
	}
} // namespace UI::Cnc
