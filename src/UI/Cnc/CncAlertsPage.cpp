/*
 * CncAlertsPage.cpp
 *
 * Positions are CncLayout::Alerts values (PanelDue pixels) through S() / SL(), relative to the page.
 */

#include "CncAlertsPage.h"
#include "CncPopup.h"
#include "CncState.h"
#include "CncTheme.h"
#include <fmt/format.h>

namespace UI::Cnc
{
	using namespace CncLayout;
	namespace AL = CncLayout::Alerts;

	namespace
	{
		void showObj(lv_obj_t* o, bool visible)
		{
			if (visible)
			{
				lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
			}
			else
			{
				lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
			}
		}

		// Whole UTF-8 characters: the byte count cut back to a character start
		size_t charBoundary(const std::string& s, size_t n)
		{
			while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80)
			{
				--n;
			}
			return n;
		}
	} // namespace

	// "4m07", "2h15", "3d04" (as PanelDue)
	std::string AlertsPage::FormatAge(uint32_t ms)
	{
		uint32_t age = ms / 1000;
		if (age < 60 * 60)
		{
			return fmt::format("{:d}m{:02d}", age / 60, age % 60);
		}
		age /= 60;
		if (age < 24 * 60)
		{
			return fmt::format("{:d}h{:02d}", age / 60, age % 60);
		}
		age /= 60;
		return fmt::format("{:d}d{:02d}", age / 24, age % 24);
	}

	AlertsPage::~AlertsPage()
	{
		if (m_timer != nullptr)
		{
			lv_timer_delete(m_timer);
		}
	}

	void AlertsPage::create(lv_obj_t* parent, int32_t parentTop)
	{
		m_root = CreateBare(parent, 0, 0, ScreenW, 1);
		lv_obj_set_size(m_root, LV_PCT(100), LV_PCT(100)); // the parent is not laid out yet
		m_top = parentTop;
		auto y = [this](int32_t pdY) { return S(pdY) - m_top; };
		const int32_t smallH = lv_font_get_line_height(Font::Small());

		CreateLabel(m_root,
					S(AL::LabelLeftX),
					y(AL::LabelY),
					SL(AL::LabelLeftX, AL::LabelLeftW),
					smallH,
					Font::Small(),
					Colour::Muted(),
					LV_TEXT_ALIGN_LEFT,
					"ALERTS");
		m_labelRight = CreateLabel(m_root,
								   S(AL::LabelRightX),
								   y(AL::LabelY),
								   SL(AL::LabelRightX, AL::LabelRightW),
								   smallH,
								   Font::Small(),
								   Colour::Muted(),
								   LV_TEXT_ALIGN_RIGHT,
								   "");
		for (unsigned int r = 0; r < NumRows; ++r)
		{
			m_rows[r].create(m_root, S(AL::RowX), y(AL::RowY(r)), SL(AL::RowX, AL::RowW), SL(AL::RowY(r), AL::RowH));
			m_rows[r].onPress([this, r] { rowPressed(r); });
			m_rows[r].show(false);
		}
		m_empty = CreateLabel(m_root,
							  S(AL::RowX),
							  y(AL::EmptyY),
							  SL(AL::RowX, AL::RowW),
							  SL(AL::EmptyY, AL::RowH),
							  Font::Small(),
							  Colour::Muted(),
							  LV_TEXT_ALIGN_CENTER,
							  "No alerts");

		// Page bar
		m_up.create(m_root, S(AL::UpX), y(AL::BarY), SL(AL::UpX, AL::ArrowW), SL(AL::BarY, AL::BarH), true);
		m_down.create(m_root, S(AL::DownX), y(AL::BarY), SL(AL::DownX, AL::ArrowW), SL(AL::BarY, AL::BarH), false);
		m_up.onPress([this] { pagePressed(-1); });
		m_down.onPress([this] { pagePressed(1); });
		m_pageLabel = CreateLabel(m_root,
								  S(AL::UpX + AL::ArrowW),
								  y(AL::BarY),
								  SL(AL::UpX + AL::ArrowW, AL::DownX - AL::UpX - AL::ArrowW),
								  SL(AL::BarY, AL::BarH),
								  Font::Small(),
								  Colour::Muted(),
								  LV_TEXT_ALIGN_CENTER,
								  "");

		m_timer = lv_timer_create(
			[](lv_timer_t* t) { static_cast<AlertsPage*>(lv_timer_get_user_data(t))->tick(); }, 1000, this);
		show();
	}

	// ---- Collecting ----------------------------------------------------------------------------

	// Every line that contains "Error:" (checked first) or "Warning:"
	void AlertsPage::collect(const std::string& reply)
	{
		bool changed = false;
		size_t start = 0;
		while (start < reply.size())
		{
			size_t end = reply.find_first_of("\r\n", start);
			if (end == std::string::npos)
			{
				end = reply.size();
			}
			const std::string line = reply.substr(start, end - start);
			for (int pass = 0; pass < 2; ++pass)
			{
				const char* tag = (pass == 0) ? "Error:" : "Warning:";
				const size_t at = line.find(tag);
				if (at != std::string::npos)
				{
					add(pass == 0, line.substr(at + std::char_traits<char>::length(tag)));
					changed = true;
					break;
				}
			}
			start = end + 1;
		}
		if (changed && m_root != nullptr)
		{
			show();
		}
	}

	void AlertsPage::add(bool error, std::string text)
	{
		// Normalise first (tabs to spaces, trimmed, whole UTF-8 characters) so repeats compare equal
		for (char& c : text)
		{
			if (c == '\t')
			{
				c = ' ';
			}
		}
		const size_t b = text.find_first_not_of(' ');
		const size_t e = text.find_last_not_of(' ');
		text = (b == std::string::npos) ? std::string() : text.substr(b, e - b + 1);
		if (text.size() > MaxTextLen)
		{
			text.resize(charBoundary(text, MaxTextLen));
		}
		if (text.empty())
		{
			text = "(no text)"; // the machine said Error: / Warning: and nothing else
		}

		if (!m_alerts.empty() && m_alerts.front().error == error && m_alerts.front().text == text)
		{
			m_alerts.front().time = State::Now(); // the same again: refresh it
			m_alerts.front().unread = true;
			return;
		}
		m_alerts.push_front({State::Now(), error, true, std::move(text)});
		if (m_alerts.size() > MaxAlerts)
		{
			m_alerts.pop_back(); // the oldest falls out
		}
	}

	// ---- Look ----------------------------------------------------------------------------------

	unsigned int AlertsPage::pageCount() const
	{
		return m_alerts.empty() ? 1 : (unsigned int)((m_alerts.size() + NumRows - 1) / NumRows);
	}

	void AlertsPage::show()
	{
		if (m_page >= pageCount())
		{
			m_page = pageCount() - 1;
		}
		const uint32_t now = State::Now();
		for (unsigned int r = 0; r < NumRows; ++r)
		{
			const size_t i = (size_t)m_page * NumRows + r;
			const bool vis = i < m_alerts.size();
			if (vis)
			{
				const Entry& a = m_alerts[i];
				m_rows[r].setEntry(a.text, a.error, a.unread, FormatAge(now - a.time));
			}
			m_rows[r].show(vis);
		}
		lv_label_set_text(m_labelRight, labelRight().c_str());
		lv_label_set_text(m_pageLabel, pageText().c_str());
		showObj(m_empty, m_alerts.empty());
		m_up.setDisabled(m_page == 0);
		m_down.setDisabled(m_page + 1 >= pageCount());
	}

	std::string AlertsPage::labelRight() const
	{
		return fmt::format("{:d} / {:d}   NEWEST FIRST", m_alerts.size(), MaxAlerts);
	}

	std::string AlertsPage::pageText() const
	{
		return fmt::format("PAGE {:d} / {:d}", m_page + 1, pageCount());
	}

	void AlertsPage::tick()
	{
		const uint32_t now = State::Now();
		for (unsigned int r = 0; r < NumRows; ++r)
		{
			const size_t i = (size_t)m_page * NumRows + r;
			if (i < m_alerts.size())
			{
				const Entry& a = m_alerts[i];
				m_rows[r].setEntry(a.text, a.error, a.unread, FormatAge(now - a.time)); // redraws when it changed
			}
		}
	}

	// ---- Touches -------------------------------------------------------------------------------

	void AlertsPage::rowPressed(unsigned int row)
	{
		const size_t i = (size_t)m_page * NumRows + row;
		if (row >= NumRows || i >= m_alerts.size())
		{
			return;
		}
		Entry& a = m_alerts[i];
		a.unread = false;
		show();
		Popup::Info(a.error ? "ERROR" : "WARNING", a.text);
	}

	void AlertsPage::pagePressed(int direction)
	{
		if (direction < 0 && m_page > 0)
		{
			--m_page;
		}
		else if (direction > 0 && m_page + 1 < pageCount())
		{
			++m_page;
		}
		show();
	}

	// ---- For tests ----

	std::string AlertsPage::rowText(unsigned int row) const
	{
		return (row < NumRows && m_rows[row].visible()) ? m_rows[row].text() : std::string();
	}

	std::string AlertsPage::rowAge(unsigned int row) const
	{
		return (row < NumRows && m_rows[row].visible()) ? m_rows[row].age() : std::string();
	}

	bool AlertsPage::rowUnread(unsigned int row) const
	{
		return row < NumRows && m_rows[row].visible() && m_rows[row].unread();
	}
} // namespace UI::Cnc
