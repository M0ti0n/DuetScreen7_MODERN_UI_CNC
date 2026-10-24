/*
 * CncMacrosPage.cpp
 *
 * Positions are PanelDue CncLayout::Macros values through S() / SL(), relative to the page area.
 */

#include "CncMacrosPage.h"
#include "CncKeyboard.h"
#include "CncPopup.h"
#include "CncState.h"
#include "CncTheme.h"
#include "Debug.h"
#include "ObjectModel/Directories.h"
#include "ObjectModel/Files.h"
#include "UI/Components/LVGL/LvObj.h"
#include <algorithm>
#include <cctype>
#include <fmt/format.h>

namespace UI::Cnc
{
	using namespace CncLayout;
	namespace MC = CncLayout::Macros;

	namespace
	{
		constexpr int KeyboardOwner = 2;

		size_t matchPos(const std::string& s, const std::string& q)
		{
			auto it = std::search(
				s.begin(),
				s.end(),
				q.begin(),
				q.end(),
				[](char a, char b)
				{ return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
			return (it == s.end()) ? std::string::npos : (size_t)(it - s.begin());
		}

		int32_t textWidth(const std::string& t)
		{
			lv_point_t size;
			lv_text_get_size(&size, t.c_str(), Font::Small(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
			return size.x;
		}

		std::string fitPath(const std::string& path, int32_t width)
		{
			for (size_t p = 0; p <= path.size(); ++p)
			{
				const std::string out = (p == 0) ? path : ".." + path.substr(p);
				if (p == path.size() || textWidth(out) <= width)
				{
					return out;
				}
			}
			return path;
		}

		void show(lv_obj_t* o, bool visible)
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
	} // namespace

	// Without a leading "!", a leading "12_" number and ".g"
	std::string MacrosPage::DisplayName(const std::string& file)
	{
		size_t p = (!file.empty() && file[0] == '!') ? 1 : 0;
		size_t d = p;
		while (d < file.size() && std::isdigit(static_cast<unsigned char>(file[d])))
		{
			++d;
		}
		if (d > p && d < file.size() && file[d] == '_')
		{
			p = d + 1;
		}
		std::string name = file.substr(p);
		if (name.size() > 2 && std::tolower(static_cast<unsigned char>(name[name.size() - 1])) == 'g' &&
			name[name.size() - 2] == '.')
		{
			name.resize(name.size() - 2);
		}
		return name;
	}

	// ---- Building ------------------------------------------------------------------------------

	MacrosPage::~MacrosPage()
	{
		*m_alive = false;
		if (Keyboard::IsOpenFor(KeyboardOwner))
		{
			Keyboard::Close(); // its callbacks point here
		}
		if (m_timer != nullptr)
		{
			lv_timer_delete(m_timer);
		}
	}

	void MacrosPage::create(lv_obj_t* parent, int32_t parentTop)
	{
		m_root = CreateBare(parent, 0, 0, ScreenW, 1);
		lv_obj_set_size(m_root, LV_PCT(100), LV_PCT(100)); // the parent is not laid out yet
		m_top = parentTop;
		auto y = [this](int32_t pdY) { return S(pdY) - m_top; };
		const int32_t smallH = lv_font_get_line_height(Font::Small());

		// Label row
		auto label = [&](int32_t x, int32_t w, lv_color_t colour, lv_text_align_t align)
		{ return CreateLabel(m_root, S(x), y(MC::LabelY), SL(x, w), smallH, Font::Small(), colour, align, ""); };
		m_labelLeft = label(MC::LabelLeftX, MC::LabelLeftW, Colour::Muted(), LV_TEXT_ALIGN_LEFT);
		m_labelQuery = label(MC::QueryX, MC::QueryW, Colour::Accent(), LV_TEXT_ALIGN_LEFT);
		m_labelRight = label(MC::PathX, MC::PathW, Colour::Muted(), LV_TEXT_ALIGN_RIGHT);

		// Cells (list rows or grid tiles), message
		for (unsigned int i = 0; i < NumCells; ++i)
		{
			m_cells[i].create(
				m_root, S(MC::AreaX), y(MC::AreaY), SL(MC::AreaX, MC::AreaW), SL(MC::AreaY, MC::ListRowH));
			m_cells[i].onPress([this, i] { cellPressed(i); });
			m_cells[i].show(false);
		}
		m_empty = CreateLabel(m_root,
							  S(MC::AreaX),
							  y(MC::EmptyY),
							  SL(MC::AreaX, MC::AreaW),
							  SL(MC::EmptyY, MC::ListRowH),
							  Font::Small(),
							  Colour::Muted(),
							  LV_TEXT_ALIGN_CENTER,
							  "");

		// Side column: SEARCH, up, down, LIST / GRID
		m_search.create(m_root,
						S(MC::SideX),
						y(MC::SearchY),
						SL(MC::SideX, MC::SideW),
						SL(MC::SearchY, MC::SearchH),
						GlyphButton::Kind::Search);
		m_search.onPress([this] { searchPressed(); });
		m_up.create(m_root, S(MC::SideX), y(MC::UpY), SL(MC::SideX, MC::SideW), SL(MC::UpY, MC::ArrowH), true);
		m_down.create(m_root, S(MC::SideX), y(MC::DownY), SL(MC::SideX, MC::SideW), SL(MC::DownY, MC::ArrowH), false);
		m_up.onPress([this] { pagePressed(-1); });
		m_down.onPress([this] { pagePressed(1); });
		m_toggle.create(m_root,
						S(MC::SideX),
						y(MC::ToggleY),
						SL(MC::SideX, MC::SideW),
						SL(MC::ToggleY, MC::ToggleH),
						GlyphButton::Kind::Grid);
		m_toggle.setActive(true); // accent outline and glyph, as in the mock-ups
		m_toggle.onPress([this] { togglePressed(); });

		placeCells();
		rebuildView();
		showCells();
		m_timer = lv_timer_create(
			[](lv_timer_t* t) { static_cast<MacrosPage*>(lv_timer_get_user_data(t))->tick(); }, 100, this);
	}

	// ---- Listing -------------------------------------------------------------------------------

	std::string MacrosPage::fullDir() const
	{
		return fmt::format(
			"{}{}", m_sys ? OM::Directories::GetSystemDirectory() : OM::Directories::GetMacrosDirectory(), m_path);
	}

	bool MacrosPage::ConfigFile(const std::string& name)
	{
		std::string n = name;
		std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
		return n == "config.g" || n == "config-override.g" || n == "daemon.g" || n == "runonce.g";
	}

	void MacrosPage::setShown(bool shown)
	{
		if (shown && !m_shown)
		{
			m_shown = true;
			request(); // fresh listing each time MACROS is opened
			rebuildView();
			showCells();
		}
		else if (!shown && m_shown)
		{
			m_shown = false;
			if (m_kbMode)
			{
				Keyboard::Close();
				keyboardGone(true); // another page: keep the filter
			}
			cancelPick();
		}
	}

	void MacrosPage::request()
	{
		m_loaded = false;
		m_error = false;
		m_requestTime = State::Now();
		std::weak_ptr<bool> alive = m_alive;
		const std::string dir = fullDir();
		OM::FileSystem::RequestFiles(
			m_sys ? OM::Directories::DirectoryType::SYSTEM : OM::Directories::DirectoryType::MACROS,
			m_path,
			[this, alive, dir](OM::FileSystem::ItemList items)
			{
				auto a = alive.lock();
				if (!a || !*a)
				{
					return; // the page is gone
				}
				std::vector<Entry> entries;
				for (const auto& item : items)
				{
					if (item)
					{
						entries.push_back(
							{item->GetName(), item->GetType() == OM::FileSystem::FileSystemItemType::folder});
					}
				}
				UI_LOCK();
				listingArrived(dir, std::move(entries));
			},
			true);
	}

	void MacrosPage::listingArrived(const std::string& dir, std::vector<Entry> entries)
	{
		if (dir != fullDir())
		{
			return; // a folder left meanwhile
		}
		std::stable_sort(entries.begin(),
						 entries.end(),
						 [](const Entry& a, const Entry& b)
						 {
							 if (a.folder != b.folder)
							 {
								 return a.folder;
							 }
							 return a.name < b.name;
						 });
		m_entries = std::move(entries);
		m_loaded = true;
		m_error = false;
		rebuildView();
		showCells();
	}

	void MacrosPage::tick()
	{
		if (m_shown && !m_loaded && !m_error && State::Now() - m_requestTime > ListTimeout)
		{
			m_error = true; // no answer: the folder cannot be read
			rebuildView();
			showCells();
		}
	}

	void MacrosPage::openFolder(const std::string& path)
	{
		folderChanged();
		m_path = path;
		m_entries.clear();
		request();
		rebuildView();
		showCells();
	}

	void MacrosPage::folderChanged()
	{
		m_scroll = 0;
		if (m_kbMode)
		{
			Keyboard::Close();
			m_kbMode = false;
		}
		m_searching = false;
		m_query.clear();
	}

	unsigned int MacrosPage::pageSize() const
	{
		return m_grid ? MC::GridCols * (m_kbMode ? MC::GridRowsKeyboard : MC::GridRows)
					  : (m_kbMode ? MC::ListRowsKeyboard : MC::ListRows);
	}

	void MacrosPage::rebuildView()
	{
		m_view.clear();
		m_fileTotal = 0;
		if (!m_loaded)
		{
			if (m_error)
			{
				m_view.push_back(RootEntry);
			}
			return;
		}
		if (!filterOn())
		{
			if (!m_path.empty() || m_sys)
			{
				m_view.push_back(ParentEntry); // in the root of SYS: back to the macros
			}
			else if (!m_searching)
			{
				m_view.push_back(SysEntry); // not while searching: the search is for the macros here
			}
		}
		for (size_t i = 0; i < m_entries.size(); ++i)
		{
			const Entry& e = m_entries[i];
			if (!e.folder)
			{
				++m_fileTotal;
			}
			if (filterOn() && (e.folder || matchPos(DisplayName(e.name), m_query) == std::string::npos))
			{
				continue; // search: macros of this folder only
			}
			m_view.push_back((int)i);
		}
		const unsigned int page = pageSize();
		if (m_scroll >= m_view.size())
		{
			m_scroll = m_view.empty() ? 0 : ((m_view.size() - 1) / page) * page;
		}
	}

	// Cell i as a list row or a grid tile
	void MacrosPage::placeCells()
	{
		for (unsigned int i = 0; i < NumCells; ++i)
		{
			if (m_grid)
			{
				const int32_t col = (int32_t)(i % MC::GridCols), row = (int32_t)(i / MC::GridCols);
				const int32_t x = MC::AreaX + col * (MC::GridTileW + MC::GridGap);
				const int32_t yy = MC::AreaY + row * MC::GridPitchY;
				m_cells[i].setGeometry(S(x), S(yy) - m_top, SL(x, MC::GridTileW), SL(yy, MC::GridTileH));
			}
			else
			{
				const int32_t yy = MC::AreaY + (int32_t)i * MC::ListPitch;
				m_cells[i].setGeometry(S(MC::AreaX), S(yy) - m_top, SL(MC::AreaX, MC::AreaW), SL(yy, MC::ListRowH));
			}
			m_cells[i].setGridStyle(m_grid);
		}
	}

	void MacrosPage::updateLabel()
	{
		std::string path = fullDir();
		if (path.size() > 1 && path.back() == '/')
		{
			path.pop_back(); // "0:/macros" as on PanelDue
		}
		if (m_searching)
		{
			lv_label_set_text(m_labelLeft, "SEARCH");
			lv_obj_set_style_text_color(m_labelLeft, Colour::Muted(), 0);
			size_t n = m_query.size();
			while (n > 0 && textWidth(m_query.substr(0, n)) > S(MC::QueryTextW))
			{
				--n;
			}
			m_queryText = "\"" + m_query.substr(0, n) + (n < m_query.size() ? ".." : "") + "\"";
			lv_label_set_text(m_labelQuery, m_queryText.c_str());
			show(m_labelQuery, true);
			m_rightText = fmt::format("{:d} of {:d}", filterOn() ? m_view.size() : m_fileTotal, m_fileTotal);
			lv_obj_set_x(m_labelRight, S(MC::CountX));
			lv_obj_set_width(m_labelRight, SL(MC::CountX, MC::CountW));
		}
		else
		{
			lv_label_set_text(m_labelLeft, picking() ? "SELECT" : "MACROS");
			lv_obj_set_style_text_color(m_labelLeft, picking() ? Colour::Accent() : Colour::Muted(), 0);
			show(m_labelQuery, false);
			m_rightText = fitPath(path, SL(MC::PathX, MC::PathW));
			lv_obj_set_x(m_labelRight, S(MC::PathX));
			lv_obj_set_width(m_labelRight, SL(MC::PathX, MC::PathW));
		}
		lv_label_set_text(m_labelRight, m_rightText.c_str());
	}

	void MacrosPage::showCells()
	{
		const unsigned int page = pageSize();
		for (unsigned int i = 0; i < NumCells; ++i)
		{
			const size_t vi = m_scroll + i;
			const bool vis = (i < page && vi < m_view.size());
			if (vis)
			{
				const int e = m_view[vi];
				if (e == ParentEntry || e == RootEntry)
				{
					m_cells[i].setEntry("..", (e == ParentEntry) ? "UP" : "BACK");
				}
				else if (e == SysEntry)
				{
					m_cells[i].setEntry("sys", "SYSTEM");
				}
				else if (m_entries[(size_t)e].folder)
				{
					m_cells[i].setEntry(m_entries[(size_t)e].name, "FOLDER");
				}
				else if (m_sys && m_path.empty() && ConfigFile(m_entries[(size_t)e].name))
				{
					m_cells[i].setEntry(m_entries[(size_t)e].name, "CONFIG"); // the real name: config.g
				}
				else
				{
					const std::string name = DisplayName(m_entries[(size_t)e].name);
					const size_t m = filterOn() ? matchPos(name, m_query) : std::string::npos;
					m_cells[i].setEntry(
						name, "", (m == std::string::npos) ? 0 : m, (m == std::string::npos) ? 0 : m_query.size());
				}
			}
			m_cells[i].show(vis);
		}

		// Side column
		const bool upOk = m_scroll > 0, downOk = m_scroll + page < m_view.size();
		m_up.setDisabled(!upOk);
		m_down.setDisabled(!downOk);
		m_up.show(!m_kbMode);
		m_down.show(!m_kbMode);
		m_toggle.show(!m_kbMode);
		m_toggle.setGlyph(m_grid ? GlyphButton::Kind::List : GlyphButton::Kind::Grid); // the view it switches to
		m_search.setActive(m_searching);
		if (m_kbMode)
		{
			Keyboard::SetArrowsEnabled(upOk, downOk);
		}
		updateLabel();

		// Message: loading, error, an empty folder, nothing matches
		const bool onlyParent =
			(m_view.size() == 1 && (m_view[0] == ParentEntry || m_view[0] == SysEntry) && !m_kbMode);
		m_message.clear();
		if (!m_loaded)
		{
			m_message = m_error ? "Cannot read this folder." : "Loading...";
		}
		else if (m_view.empty() || onlyParent)
		{
			m_message = filterOn() ? "No macros match" : "No macros";
		}
		// grid while typing: only tile row 0 is above the sheet, so the message goes there
		const int32_t ey = !m_grid ? MC::EmptyY : m_kbMode ? MC::EmptyGridKbY : MC::EmptyGridY;
		const int32_t eh = m_grid ? MC::GridTileH : MC::ListRowH;
		lv_obj_set_y(m_empty, S(ey) - m_top);
		lv_obj_set_height(m_empty, SL(ey, eh));
		lv_label_set_text(m_empty, m_message.c_str());
		show(m_empty, !m_message.empty());
	}

	// ---- Touches -------------------------------------------------------------------------------

	void MacrosPage::cellPressed(unsigned int cell)
	{
		if (m_kbMode)
		{
			Keyboard::Close(); // a macro / folder: keep the filter, then act on it
			keyboardGone(false);
		}
		const size_t vi = m_scroll + cell;
		if (cell >= pageSize() || vi >= m_view.size())
		{
			return;
		}
		const int e = m_view[vi];
		if (e == SysEntry)
		{
			m_sys = true;
			openFolder("");
		}
		else if (e == ParentEntry && m_sys && m_path.empty())
		{
			m_sys = false; // root of SYS: back to the macros
			openFolder("");
		}
		else if (e == ParentEntry)
		{
			std::string p = m_path;
			if (!p.empty() && p.back() == '/')
			{
				p.pop_back();
			}
			const size_t cut = p.find_last_of('/');
			openFolder((cut == std::string::npos) ? std::string() : p.substr(0, cut + 1));
		}
		else if (e == RootEntry)
		{
			m_sys = false;
			openFolder(""); // back to 0:/macros after a listing error
		}
		else if (m_entries[(size_t)e].folder)
		{
			openFolder(m_path + m_entries[(size_t)e].name + "/");
		}
		else if (m_pick)
		{
			const PickHandler h = std::move(m_pick); // the handler leaves the page
			m_pick = nullptr;
			const std::string& file = m_entries[(size_t)e].name;
			updateLabel();
			h(fullDir() + file, DisplayName(file));
		}
		else if (m_sys && m_path.empty() && ConfigFile(m_entries[(size_t)e].name))
		{
			Popup::Refuse("Config files are not run from the screen."); // config.g etc.: never by hand
		}
		else
		{
			askRun(m_entries[(size_t)e].name);
		}
	}

	void MacrosPage::pagePressed(int direction)
	{
		const unsigned int page = pageSize();
		if (direction < 0 && m_scroll > 0)
		{
			m_scroll = (m_scroll > page) ? m_scroll - page : 0;
		}
		else if (direction > 0 && m_scroll + page < m_view.size())
		{
			m_scroll += page;
		}
		showCells();
	}

	void MacrosPage::togglePressed()
	{
		m_grid = !m_grid;
		m_scroll = (m_scroll / pageSize()) * pageSize();
		placeCells();
		rebuildView();
		showCells();
	}

	// ---- RUN MACRO ----

	void MacrosPage::askRun(const std::string& file)
	{
		constexpr const char* Locked = "Locked while a job is running.";
		if (State::MachineBusy())
		{
			Popup::Refuse(Locked); // allowed again while paused
			return;
		}
		const std::string path = fullDir() + file;
		std::vector<std::string> lines = {"Do you want to run this macro?", DisplayName(file)};
		if (m_sys)
		{
			lines.push_back(path); // a system file: say where it is
		}
		Popup::Confirm("RUN MACRO",
					   lines,
					   [path, Locked]
					   {
						   if (State::MachineBusy())
						   {
							   Popup::Refuse(Locked);
							   return;
						   }
						   State::Send(fmt::format("M98 P\"{:s}\"\n", State::Quoted(path)));
					   });
	}

	// ---- SETTINGS pick ----

	void MacrosPage::startPick(PickHandler picked)
	{
		m_pick = std::move(picked);
		if (m_root != nullptr)
		{
			updateLabel();
		}
	}

	void MacrosPage::cancelPick()
	{
		if (m_pick)
		{
			m_pick = nullptr;
			updateLabel();
		}
	}

	// ---- SEARCH and the keyboard ----

	void MacrosPage::searchPressed()
	{
		if (m_kbMode)
		{
			Keyboard::Close(); // SEARCH while typing: leave the search
			cancelSearch();
			return;
		}
		openKeyboard();
	}

	void MacrosPage::openKeyboard()
	{
		m_searching = true;
		m_kbMode = true;
		m_scroll = 0;
		rebuildView();
		Keyboard::Client client;
		client.changed = [this](const std::string& text)
		{
			m_query = text;
			m_scroll = 0;
			rebuildView();
			showCells();
		};
		client.enter = [this](const std::string& text)
		{
			m_query = text;
			m_kbMode = false;
			if (m_query.empty())
			{
				m_searching = false;
			}
			m_scroll = 0;
			rebuildView();
			showCells();
		};
		client.cancel = [this] { cancelSearch(); };
		client.arrow = [this](int dir) { pagePressed(dir); };
		client.gone = [this] { keyboardGone(true); };
		Keyboard::Open(m_query, client, KeyboardOwner);
		showCells();
	}

	void MacrosPage::cancelSearch()
	{
		m_kbMode = false;
		m_searching = false;
		m_query.clear();
		m_scroll = 0;
		rebuildView();
		showCells();
	}

	// The keyboard went without ENTER / X: keep the filter
	void MacrosPage::keyboardGone(bool alignPage)
	{
		if (m_kbMode)
		{
			m_kbMode = false;
			if (m_query.empty())
			{
				m_searching = false;
			}
			if (alignPage)
			{
				m_scroll = (m_scroll / pageSize()) * pageSize(); // back on the full page grid
			}
			rebuildView();
			showCells();
		}
	}

	// ---- For tests ----

	std::string MacrosPage::cellText(unsigned int cell) const
	{
		return (cell < NumCells && m_cells[cell].visible()) ? m_cells[cell].text() : std::string();
	}

	std::string MacrosPage::cellTag(unsigned int cell) const
	{
		return (cell < NumCells && m_cells[cell].visible()) ? m_cells[cell].tag() : std::string();
	}

	std::string MacrosPage::labelLeft() const
	{
		return lv_label_get_text(m_labelLeft);
	}
} // namespace UI::Cnc
