/*
 * CncJobListPage.cpp
 *
 * Positions are PanelDue CncLayout::JobList values through S() / SL(), relative to the page area.
 */

#include "CncJobListPage.h"
#include "CncKeyboard.h"
#include "CncPanel.h"
#include "CncPopup.h"
#include "CncPreviewSource.h"
#include "CncState.h"
#include "CncTheme.h"
#include "Debug.h"
#include "ObjectModel/Directories.h"
#include "ObjectModel/Files.h"
#include "Subscribers/Subscribers.h"
#include "UI/Components/LVGL/LvObj.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <fmt/format.h>

namespace UI::Cnc
{
	using namespace CncLayout;
	namespace JL = CncLayout::JobList;

	namespace
	{
		constexpr int KeyboardOwner = 1;

		// Case-insensitive position of 'q' in 's', npos if not found
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

		// The tail of a path that fits in 'width' pixels, with ".." in front when cut
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

	// ---- Building ------------------------------------------------------------------------------

	JobListPage::~JobListPage()
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

	void JobListPage::create(lv_obj_t* parent, int32_t parentTop)
	{
		m_root = CreateBare(parent, 0, 0, ScreenW, 1);
		lv_obj_set_size(m_root, LV_PCT(100), LV_PCT(100)); // the parent is not laid out yet
		m_top = parentTop;
		auto y = [this](int32_t pdY) { return S(pdY) - m_top; };
		const int32_t smallH = lv_font_get_line_height(Font::Small());

		// Label row
		auto label = [&](int32_t x, int32_t w, lv_color_t colour, lv_text_align_t align)
		{ return CreateLabel(m_root, S(x), y(JL::LabelY), SL(x, w), smallH, Font::Small(), colour, align, ""); };
		m_labelLeft = label(JL::LabelLeftX, JL::LabelLeftW, Colour::Muted(), LV_TEXT_ALIGN_LEFT);
		m_labelQuery = label(JL::QueryX, JL::QueryW, Colour::Accent(), LV_TEXT_ALIGN_LEFT);
		m_labelRight = label(JL::RightX, JL::RightW, Colour::Muted(), LV_TEXT_ALIGN_RIGHT);

		// Rows
		for (unsigned int r = 0; r < NumRows; ++r)
		{
			m_rows[r].create(m_root, S(JL::RowX), y(JL::RowY(r)), SL(JL::RowX, JL::RowW), SL(JL::RowY(r), JL::RowH));
			m_rows[r].onPress([this, r] { rowPressed(r); });
			m_rows[r].show(false);
		}
		m_empty = CreateLabel(m_root,
							  S(JL::RowX),
							  y(JL::EmptyY),
							  SL(JL::RowX, JL::RowW),
							  SL(JL::EmptyY, JL::RowH),
							  Font::Small(),
							  Colour::Muted(),
							  LV_TEXT_ALIGN_CENTER,
							  "");

		// Side column: SEARCH, up, down
		m_search.create(m_root,
						S(JL::SideX),
						y(JL::SearchY),
						SL(JL::SideX, JL::SideW),
						SL(JL::SearchY, JL::SearchH),
						GlyphButton::Kind::Search);
		m_search.onPress([this] { searchPressed(); });
		m_up.create(m_root, S(JL::SideX), y(JL::UpY), SL(JL::SideX, JL::SideW), SL(JL::UpY, JL::ArrowH), true);
		m_down.create(m_root, S(JL::SideX), y(JL::DownY), SL(JL::SideX, JL::SideW), SL(JL::DownY, JL::ArrowH), false);
		m_up.onPress([this] { pagePressed(-1); });
		m_sd.create(
			m_root, S(JL::SideX), y(JL::SdY), SL(JL::SideX, JL::SideW), SL(JL::SdY, JL::SdH), "SD", Font::Small());
		m_sd.onPress([this] { cardPressed(); });
		Init();
		updateSd();
		m_down.onPress([this] { pagePressed(1); });

		rebuildView();
		showRows();
		m_timer = lv_timer_create(
			[](lv_timer_t* t) { static_cast<JobListPage*>(lv_timer_get_user_data(t))->tick(); }, 100, this);
	}

	// ---- Listing -------------------------------------------------------------------------------

	std::string JobListPage::selectedPath() const
	{
		return m_selected.empty() ? std::string() : fullDir() + m_selected;
	}

	PreviewSource::Stamp JobListPage::stampOf(const std::string& path) const
	{
		const auto it = m_stamps.find(path);
		return (it != m_stamps.end()) ? it->second : PreviewSource::Stamp();
	}

	std::string JobListPage::fullDir() const
	{
		return (m_card == 0) ? fmt::format("{}{}", OM::Directories::GetGcodesDirectory(), m_path)
							 : fmt::format("{:d}:/{}", m_card, m_path);
	}

	// ---- SD: other cards ----

	namespace
	{
		std::atomic<uint8_t> s_mounted{1}; // bit per volume, card 0 always

		bool volumeMounted(Comm::JsonDecoder*, const bool& mounted, const size_t indices[])
		{
			JobListPage::SetVolumeMounted(indices[0], mounted);
			return true;
		}
	} // namespace

	void JobListPage::Init()
	{
		static bool inited = false;
		if (!inited)
		{
			inited = true;
			SubscriberMap::addSubscriber("volumes^:mounted", &volumeMounted);
		}
	}

	void JobListPage::SetVolumeMounted(size_t volume, bool mounted)
	{
		if (volume == 0 || volume >= MaxVolumes)
		{
			return;
		}
		const uint8_t bit = (uint8_t)(1u << volume);
		if (mounted)
		{
			s_mounted.fetch_or(bit);
		}
		else
		{
			s_mounted.fetch_and((uint8_t)~bit);
		}
	}

	bool JobListPage::sdShown() const
	{
		return m_sd.obj() != nullptr && m_sd.visible();
	}

	void JobListPage::updateSd()
	{
		const uint8_t mounted = s_mounted.load();
		m_mountedSeen = mounted;
		if (m_card != 0 && (mounted & (1u << m_card)) == 0)
		{
			m_card = 0; // the card shown was removed
			openFolder("");
		}
		m_sd.show((mounted & ~1u) != 0 || m_card != 0);
		m_sd.setToggle(m_card != 0);
	}

	void JobListPage::cardPressed()
	{
		const uint8_t mounted = s_mounted.load();
		for (unsigned int k = 1; k <= MaxVolumes; ++k)
		{
			const unsigned int c = (m_card + k) % MaxVolumes;
			if (c == 0 || (mounted & (1u << c)) != 0)
			{
				if (c != m_card)
				{
					m_card = c;
					openFolder(""); // the card's root (card 0: its gcodes folder)
					updateSd();
				}
				return;
			}
		}
	}

	void JobListPage::setShown(bool shown)
	{
		if (shown && !m_shown)
		{
			m_shown = true;
			request(); // fresh listing each time JOB LIST is opened
			rebuildView();
			showRows();
		}
		else if (!shown && m_shown)
		{
			m_shown = false;
			if (m_kbMode)
			{
				Keyboard::Close();
				keyboardGone(); // another page: keep the filter
			}
		}
	}

	void JobListPage::request()
	{
		m_loaded = false;
		m_error = false;
		m_requestTime = State::Now();
		std::weak_ptr<bool> alive = m_alive;
		const std::string dir = fullDir();
		OM::FileSystem::RequestFilesAt(
			dir,
			[this, alive, dir](OM::FileSystem::ItemList items)
			{
				auto a = alive.lock();
				if (!a || !*a)
				{
					return; // the page is gone
				}
				std::vector<Entry> entries;
				std::map<std::string, PreviewSource::Stamp> stamps;
				for (const auto& item : items)
				{
					if (item)
					{
						const bool folder = item->GetType() == OM::FileSystem::FileSystemItemType::folder;
						entries.push_back({item->GetName(), folder});
						if (!folder)
						{
							stamps[item->GetName()] = {item->GetSize(), item->GetDate()};
						}
					}
				}
				UI_LOCK();
				listingArrived(dir, std::move(entries), std::move(stamps));
			},
			true);
	}

	void JobListPage::listingArrived(const std::string& dir,
									 std::vector<Entry> entries,
									 std::map<std::string, PreviewSource::Stamp> stamps)
	{
		if (dir != fullDir())
		{
			return; // a folder left meanwhile
		}
		m_stamps.clear();
		for (auto& [name, stamp] : stamps)
		{
			m_stamps[dir + name] = std::move(stamp);
		}
		// Folders first, then files, by name (as the PanelDue listing)
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
		showRows();
	}

	void JobListPage::tick()
	{
		if (s_mounted.load() != m_mountedSeen)
		{
			updateSd(); // a card mounted or removed
		}
		if (m_refreshAt != 0 && State::Now() >= m_refreshAt)
		{
			m_refreshAt = 0;
			request(); // after a delete: what the machine has now
		}
		if (m_shown && !m_loaded && !m_error && State::Now() - m_requestTime > ListTimeout)
		{
			m_error = true; // no answer: the folder cannot be read
			rebuildView();
			showRows();
		}
	}

	void JobListPage::openFolder(const std::string& path)
	{
		folderChanged();
		m_path = path;
		m_entries.clear();
		request(); // "Loading..." until the listing arrives
		rebuildView();
		showRows();
	}

	void JobListPage::folderChanged()
	{
		m_scroll = 0;
		m_selected.clear();
		if (m_kbMode)
		{
			Keyboard::Close();
			m_kbMode = false;
		}
		m_searching = false;
		m_query.clear();
	}

	unsigned int JobListPage::visibleRows() const
	{
		return m_kbMode ? JL::NumRowsKeyboard : JL::NumRows;
	}

	// The rows from the listing and the filter
	void JobListPage::rebuildView()
	{
		m_view.clear();
		m_fileTotal = 0;
		if (!m_loaded)
		{
			if (m_error)
			{
				m_view.push_back(RootEntry); // way out of a folder that cannot be read
			}
			return;
		}
		if (!filterOn() && !m_path.empty())
		{
			m_view.push_back(ParentEntry);
		}
		bool selectedSeen = false;
		for (size_t i = 0; i < m_entries.size(); ++i)
		{
			const Entry& e = m_entries[i];
			if (!e.folder)
			{
				++m_fileTotal;
			}
			if (filterOn() && (e.folder || matchPos(e.name, m_query) == std::string::npos))
			{
				continue;
			}
			if (!e.folder && e.name == m_selected)
			{
				selectedSeen = true;
			}
			m_view.push_back((int)i);
		}
		if (!selectedSeen)
		{
			m_selected.clear(); // selected file gone or filtered out
		}
		const unsigned int rows = visibleRows();
		if (m_scroll >= m_view.size())
		{
			m_scroll = m_view.empty() ? 0 : ((m_view.size() - 1) / rows) * rows;
		}
	}

	void JobListPage::updateLabel()
	{
		std::string path = fullDir();
		if (path.size() > 1 && path.back() == '/')
		{
			path.pop_back(); // "0:/gcodes" as on PanelDue
		}
		if (m_searching)
		{
			lv_label_set_text(m_labelLeft, "SEARCH");
			size_t n = m_query.size();
			while (n > 0 && textWidth(m_query.substr(0, n)) > S(JL::QueryTextW))
			{
				--n;
			}
			m_queryText = "\"" + m_query.substr(0, n) + (n < m_query.size() ? ".." : "") + "\"";
			lv_label_set_text(m_labelQuery, m_queryText.c_str());
			show(m_labelQuery, true);
			const std::string counts =
				fmt::format("{:d} of {:d}", filterOn() ? m_view.size() : m_fileTotal, m_fileTotal);
			const std::string full = fmt::format("{:s} in {:s}", counts, path);
			m_rightText = (textWidth(full) <= SL(JL::RightSearchX, JL::RightSearchW)) ? full : counts;
			lv_obj_set_x(m_labelRight, S(JL::RightSearchX));
			lv_obj_set_width(m_labelRight, SL(JL::RightSearchX, JL::RightSearchW));
		}
		else
		{
			lv_label_set_text(m_labelLeft, "FILES");
			show(m_labelQuery, false);
			m_rightText = fitPath(path, SL(JL::RightX, JL::RightW));
			lv_obj_set_x(m_labelRight, S(JL::RightX));
			lv_obj_set_width(m_labelRight, SL(JL::RightX, JL::RightW));
		}
		lv_label_set_text(m_labelRight, m_rightText.c_str());
	}

	void JobListPage::showRows()
	{
		const unsigned int rows = visibleRows();
		for (unsigned int r = 0; r < NumRows; ++r)
		{
			const size_t vi = m_scroll + r;
			const bool vis = (r < rows && vi < m_view.size());
			if (vis)
			{
				const int e = m_view[vi];
				if (e == ParentEntry || e == RootEntry)
				{
					m_rows[r].setEntry("..", (e == ParentEntry) ? "UP" : "BACK");
					m_rows[r].setSelected(false);
				}
				else if (m_entries[(size_t)e].folder)
				{
					m_rows[r].setEntry(m_entries[(size_t)e].name, "FOLDER");
					m_rows[r].setSelected(false);
				}
				else
				{
					const std::string& name = m_entries[(size_t)e].name;
					const size_t m = filterOn() ? matchPos(name, m_query) : std::string::npos;
					m_rows[r].setEntry(
						name, "", (m == std::string::npos) ? 0 : m, (m == std::string::npos) ? 0 : m_query.size());
					m_rows[r].setSelected(!m_selected.empty() && m_selected == name);
				}
			}
			m_rows[r].show(vis);
		}

		// Side column
		m_search.setActive(m_searching);
		const bool upOk = m_scroll > 0, downOk = m_scroll + rows < m_view.size();
		m_up.setDisabled(!upOk);
		m_down.setDisabled(!downOk);
		m_up.show(!m_kbMode);
		m_down.show(!m_kbMode);
		if (m_kbMode)
		{
			Keyboard::SetArrowsEnabled(upOk, downOk);
		}
		updateLabel();

		// Message in row 2: loading, error, nothing there, nothing matches
		m_message.clear();
		if (!m_loaded)
		{
			m_message = m_error ? "Cannot read this folder." : "Loading...";
		}
		else if (m_view.empty())
		{
			m_message = filterOn() ? "No files match " + m_queryText : std::string("No files");
		}
		lv_label_set_text(m_empty, m_message.c_str());
		show(m_empty, !m_message.empty());
	}

	// ---- Touches -------------------------------------------------------------------------------

	void JobListPage::rowPressed(unsigned int row)
	{
		if (m_kbMode)
		{
			Keyboard::Close(); // a row: keep the filter, then act on the row
			keyboardGone();
		}
		const size_t vi = m_scroll + row;
		if (row >= visibleRows() || vi >= m_view.size())
		{
			return;
		}
		const int e = m_view[vi];
		if (e == ParentEntry)
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
			openFolder(""); // back to the gcodes folder
		}
		else if (m_entries[(size_t)e].folder)
		{
			openFolder(m_path + m_entries[(size_t)e].name + "/");
		}
		else if (m_selected == m_entries[(size_t)e].name)
		{
			askAction(m_selected); // second tap on the selected file
		}
		else
		{
			m_selected = m_entries[(size_t)e].name;
			showRows();
		}
	}

	void JobListPage::pagePressed(int direction)
	{
		const unsigned int rows = visibleRows();
		if (direction < 0 && m_scroll > 0)
		{
			m_scroll = (m_scroll > rows) ? m_scroll - rows : 0;
		}
		else if (direction > 0 && m_scroll + rows < m_view.size())
		{
			m_scroll += rows;
		}
		showRows();
	}

	// ---- RUN (PREVIEW's RUN tile) ----

	namespace
	{
		// Why a job cannot start now, "" when it can
		std::string runRefusal()
		{
			if (State::JobInProgress())
			{
				return "A job is already running.";
			}
			const OM::PrinterStatus status = State::Status();
			if (status == OM::PrinterStatus::connecting || status == OM::PrinterStatus::disconnected)
			{
				return "No connection to the machine.";
			}
			if (status == OM::PrinterStatus::off)
			{
				return "The machine power is off.";
			}
			if (status != OM::PrinterStatus::idle)
			{
				return "Wait until the machine is idle.";
			}
			return "";
		}

		std::string baseName(const std::string& path)
		{
			const size_t cut = path.rfind('/');
			return (cut == std::string::npos) ? path : path.substr(cut + 1);
		}
	} // namespace

	void AskRunJob(const std::string& path, const RunCheck& check)
	{
		if (path.empty())
		{
			return;
		}
		const std::string why = runRefusal();
		if (!why.empty())
		{
			Popup::Refuse(why);
			return;
		}
		auto start = [path]
		{
			const std::string again = runRefusal(); // the machine may have changed meanwhile
			if (!again.empty())
			{
				Popup::Refuse(again);
				return;
			}
			State::Send(fmt::format("M32 \"{:s}\"\n", State::Quoted(path))); // the job name comes from the machine
		};
		if (check.warnedOps.empty())
		{
			Popup::Confirm("ALERT !", {"Do you want to RUN", "this job?", baseName(path)}, start);
			return;
		}
		std::string ops;
		for (int op : check.warnedOps)
		{
			ops += fmt::format("{:s}{:d}", ops.empty() ? "" : ", ", op);
		}
		const std::string warned =
			(check.warnedOps.size() == 1) ? "Warning: operation " + ops : "Warnings: operations " + ops;
		std::vector<std::string> serious;
		for (size_t i = 0; i < check.serious.size() && i < MaxSeriousLines; ++i)
		{
			serious.push_back(check.serious[i]);
		}
		if (check.serious.size() > MaxSeriousLines)
		{
			serious.push_back(fmt::format("and {:d} more", check.serious.size() - MaxSeriousLines));
		}
		Popup::ConfirmWarning("WARNING",
							  {"Do you want to RUN", "this job?", baseName(path), "", warned},
							  [serious, start]
							  {
								  if (serious.empty())
								  {
									  start();
									  return;
								  }
								  std::vector<std::string> lines = serious;
								  lines.emplace_back();
								  lines.emplace_back("RUN anyway?");
								  Popup::ConfirmWarning("WARNING", lines, start);
							  });
	}

	// ---- The JOB popup: PREVIEW or delete ----

	// Why PREVIEW cannot open this file now (SETTINGS > PREVIEW IN JOB while a job runs), "" when it can
	std::string JobListPage::previewRefusal(const std::string& path) const
	{
		if (!State::JobActive() || path == m_jobFile)
		{
			return "";
		}
		switch (Panel::PreviewDuringJob())
		{
		case Panel::PreviewInJob::RunningJob:
			return "While a job runs, PREVIEW shows only that job.\n(SETTINGS > PREVIEW IN JOB)";
		case Panel::PreviewInJob::Off:
			return "PREVIEW is off while a job runs.\n(SETTINGS > PREVIEW IN JOB)";
		default:
			return "";
		}
	}

	void JobListPage::askAction(const std::string& name)
	{
		std::weak_ptr<bool> alive = m_alive;
		const std::string path = fullDir() + name;
		const bool previewOk = previewRefusal(path).empty();
		Popup::ConfirmWithTrash(
			"JOB",
			{name, "", previewOk ? "Check: open in PREVIEW" : "PREVIEW: not during this job", "Trash: delete the file"},
			[this, alive, name]
			{
				if (!alive.expired())
				{
					askDelete(name);
				}
			},
			[this, alive, path]
			{
				if (alive.expired())
				{
					return;
				}
				const std::string why = previewRefusal(path); // the job may have ended meanwhile
				if (!why.empty())
				{
					Popup::Refuse(why);
					return;
				}
				if (m_previewRequest)
				{
					m_previewRequest();
				}
			},
			previewOk);
	}

	void JobListPage::askDelete(const std::string& name)
	{
		const std::string path = fullDir() + name;
		auto refused = [this, path]
		{
			if (State::JobActive() && path == m_jobFile)
			{
				Popup::Refuse("This job is running. It cannot be deleted.");
				return true;
			}
			const OM::PrinterStatus status = State::Status();
			if (status == OM::PrinterStatus::connecting || status == OM::PrinterStatus::disconnected)
			{
				Popup::Refuse("No connection to the machine.");
				return true;
			}
			return false;
		};
		if (refused())
		{
			return;
		}
		std::weak_ptr<bool> alive = m_alive;
		Popup::Confirm("DELETE",
					   {"Delete this file?", name},
					   [this, alive, path, name, refused]
					   {
						   if (alive.expired() || refused())
						   {
							   return;
						   }
						   State::Send(fmt::format("M30 \"{:s}\"\n", State::Quoted(path)));
						   PreviewSource::Forget(path); // not kept for PREVIEW any more
						   // Gone from the list at once; the machine's own listing follows
						   m_entries.erase(std::remove_if(m_entries.begin(),
														  m_entries.end(),
														  [&name](const Entry& e)
														  { return !e.folder && e.name == name; }),
										   m_entries.end());
						   if (m_selected == name)
						   {
							   m_selected.clear();
						   }
						   rebuildView();
						   showRows();
						   m_refreshAt = State::Now() + RefreshAfterDelete;
						   if (m_refreshAt == 0)
						   {
							   m_refreshAt = 1;
						   }
					   });
	}

	// ---- SEARCH and the keyboard ----

	void JobListPage::searchPressed()
	{
		if (m_kbMode)
		{
			Keyboard::Close(); // SEARCH while typing: leave the search
			cancelSearch();
			return;
		}
		openKeyboard(); // a new search, or edit the kept one
	}

	void JobListPage::openKeyboard()
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
			showRows();
		};
		client.enter = [this](const std::string& text)
		{
			m_query = text;
			m_kbMode = false;
			if (m_query.empty())
			{
				m_searching = false; // nothing typed: back to the plain list
			}
			m_scroll = 0;
			rebuildView();
			showRows();
		};
		client.cancel = [this] { cancelSearch(); };
		client.arrow = [this](int dir) { pagePressed(dir); };
		client.gone = [this] { keyboardGone(); };
		Keyboard::Open(m_query, client, KeyboardOwner);
		showRows(); // fewer rows and the keyboard arrows
	}

	void JobListPage::cancelSearch()
	{
		m_kbMode = false;
		m_searching = false;
		m_query.clear();
		m_scroll = 0;
		rebuildView();
		showRows();
	}

	// The keyboard closed without ENTER or X (another popup replaced it, a row, another page)
	void JobListPage::keyboardGone()
	{
		if (m_kbMode)
		{
			m_kbMode = false;
			if (m_query.empty())
			{
				m_searching = false;
			}
			rebuildView();
			showRows();
		}
	}

	// ---- For tests ----

	std::string JobListPage::rowText(unsigned int row) const
	{
		return (row < NumRows && m_rows[row].visible()) ? m_rows[row].text() : std::string();
	}

	std::string JobListPage::labelLeft() const
	{
		return lv_label_get_text(m_labelLeft);
	}

	std::string JobListPage::labelRight() const
	{
		return m_rightText;
	}

	std::string JobListPage::message() const
	{
		return m_message;
	}
} // namespace UI::Cnc
