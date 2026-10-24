/*
 * CncWcsToolsPage.cpp
 *
 * Positions are PanelDue CncLayout::Tools values through S() / SL(), relative to the page area.
 */

#include "CncWcsToolsPage.h"
#include "CncNxt.h"
#include "CncPopup.h"
#include "CncState.h"
#include "CncTheme.h"
#include <cmath>
#include <fmt/format.h>
#include <string>
#include <vector>

namespace UI::Cnc
{
	using namespace CncLayout;
	using namespace CncLayout::Tools;

	namespace
	{
		constexpr const char* LockedJob = "Locked while a job is running or paused.";
		constexpr const char* ListChanged = "The tool list changed. Try again.";

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

		bool isTool(int t)
		{
			return State::FindTool(t) != nullptr;
		}

		std::string format3(float v)
		{
			return fmt::format("{:.3f}", (std::fabs(v) < 0.0005f) ? 0.0f : v);
		}
	} // namespace

	// ---- Building ------------------------------------------------------------------------------

	void WcsToolsPage::create(lv_obj_t* parent, int32_t parentTop)
	{
		m_root = CreateBare(parent, 0, 0, ScreenW, 1);
		lv_obj_set_size(m_root, LV_PCT(100), LV_PCT(100)); // the parent is not laid out yet
		m_top = parentTop;
		auto y = [this](int32_t pdY) { return S(pdY) - m_top; };
		const int32_t smallH = lv_font_get_line_height(Font::Small());

		auto label = [&](int32_t pdX, int32_t pdW, const char* text)
		{
			CreateLabel(m_root,
						S(pdX),
						y(LabelY),
						SL(pdX, pdW),
						smallH,
						Font::Small(),
						Colour::Muted(),
						LV_TEXT_ALIGN_LEFT,
						text);
		};
		label(NumX + 2, NumW, "TOOLS");
		label(NameX + 2, NameW, "TOOL NAME");
		label(ZX + 2, ZW, "Z OFFSET");

		auto touch = [this](lv_obj_t* o, lv_event_cb_t cb)
		{
			lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_event_cb(o, cb, LV_EVENT_PRESSED, this);
		};
		for (size_t r = 0; r < NumRows; ++r)
		{
			const int32_t top = y(RowY((unsigned int)r)), h = SL(RowY((unsigned int)r), RowH);
			m_num[r].create(m_root, S(NumX), top, SL(NumX, NumW), h, "", Font::Small());
			m_num[r].setPressedLook(false); // selection only
			m_num[r].onPress([this, r] { rowPressed(r); });

			m_nameCard[r] = CreateBare(m_root, S(NameX), top, SL(NameX, NameW), h);
			StyleCard(m_nameCard[r], Colour::Tile(), Colour::Border());
			m_name[r] = CreateLabel(m_root,
									S(NameX + 12),
									top,
									SL(NameX + 12, NameW - 20),
									h,
									Font::Small(),
									Colour::Text(),
									LV_TEXT_ALIGN_LEFT,
									"");
			m_zCard[r] = CreateBare(m_root, S(ZX), top, SL(ZX, ZW), h);
			StyleCard(m_zCard[r], Colour::Tile(), Colour::Border());
			m_z[r] = CreateLabel(
				m_root, S(ZX + 8), top, SL(ZX + 8, ZW - 20), h, Font::Small(), Colour::Text(), LV_TEXT_ALIGN_RIGHT, "");

			// Touch areas over the name and the Z OFFSET tiles (no pressed look, as on PanelDue)
			m_rowTouch[r] = CreateBare(m_root, S(NameX), top, SL(NameX, NameW), h);
			touch(m_rowTouch[r],
				  [](lv_event_t* e)
				  {
					  auto* page = static_cast<WcsToolsPage*>(lv_event_get_user_data(e));
					  for (size_t i = 0; i < NumRows; ++i)
					  {
						  if (page->m_rowTouch[i] == lv_event_get_current_target(e))
						  {
							  page->rowPressed(i);
						  }
					  }
				  });
			m_zTouch[r] = CreateBare(m_root, S(ZX), top, SL(ZX, ZW), h);
			touch(m_zTouch[r],
				  [](lv_event_t* e)
				  {
					  auto* page = static_cast<WcsToolsPage*>(lv_event_get_user_data(e));
					  for (size_t i = 0; i < NumRows; ++i)
					  {
						  if (page->m_zTouch[i] == lv_event_get_current_target(e))
						  {
							  page->zPressed(i);
						  }
					  }
				  });
		}

		// Page arrows, each two rows tall
		m_up.create(m_root, S(ArrowX), y(RowY(0)), SL(ArrowX, ArrowW), SL(RowY(0), ArrowH), true);
		m_down.create(m_root, S(ArrowX), y(RowY(2)), SL(ArrowX, ArrowW), SL(RowY(2), ArrowH), false);
		m_up.onPress([this] { pagePressed(-1); });
		m_down.onPress([this] { pagePressed(1); });

		// Actions
		auto action = [&](TextButton& b, int32_t x, const char* text)
		{
			b.create(m_root, S(x), y(ActY), SL(x, ActW), SL(ActY, ActH), text, Font::Small());
			b.setPressedLook(false); // they open a popup
		};
		action(m_load, 6, "LOAD");
		action(m_measure, 6 + ActW + ActGap, "MEASURE");
		action(m_setZ, 6 + 2 * (ActW + ActGap), "SET Z");
		m_load.onPress([this] { loadPressed(); });
		m_measure.onPress([this] { measurePressed(); });
		m_setZ.onPress([this] { setZPressed(); });

		State::AddToolsListener([this] { toolsChanged(); });
		State::AddJobListener([this] { update(); });
		m_lastActive = State::ActiveTool();
		update();
	}

	// ---- Look ----------------------------------------------------------------------------------

	void WcsToolsPage::toolsChanged()
	{
		const int active = State::ActiveTool();
		if (active != m_lastActive)
		{
			// Follow when the selection was the loaded tool, or was picked automatically (at start-up the
			// tool list can arrive before the active tool); a row the operator chose stays selected
			const bool followSelection = (m_selected == m_lastActive) || m_autoSelected;
			m_lastActive = active;
			if (followSelection && isTool(active))
			{
				m_selected = active; // a tool change moves the selection along
				m_autoSelected = false;
				scrollTo(active);
			}
		}
		update();
	}

	void WcsToolsPage::scrollTo(int tool)
	{
		const auto& tools = State::Tools();
		for (size_t i = 0; i < tools.size(); ++i)
		{
			if (tools[i].number == tool)
			{
				m_firstRow = (i / NumRows) * NumRows;
				return;
			}
		}
	}

	void WcsToolsPage::update()
	{
		const auto& tools = State::Tools();
		const int active = State::ActiveTool();
		const size_t n = tools.size();

		// Keep a valid selection: the loaded tool, else the first one
		if (!isTool(m_selected))
		{
			m_selected = isTool(active) ? active : (n > 0) ? tools[0].number : -1;
			m_autoSelected = !isTool(active);
		}
		// Keep the page inside the list
		while (m_firstRow > 0 && m_firstRow >= n)
		{
			m_firstRow = (m_firstRow >= NumRows) ? m_firstRow - NumRows : 0;
		}

		for (size_t r = 0; r < NumRows; ++r)
		{
			const size_t idx = m_firstRow + r;
			const State::ToolEntry* t = (idx < n) ? &tools[idx] : nullptr;
			m_rowTool[r] = t ? t->number : -1;
			for (lv_obj_t* o : {m_nameCard[r], m_name[r], m_zCard[r], m_z[r], m_rowTouch[r], m_zTouch[r]})
			{
				show(o, t != nullptr);
			}
			m_num[r].show(t != nullptr);
			if (t == nullptr)
			{
				continue;
			}
			m_num[r].setText(fmt::format("T{:d}", t->number).c_str());
			lv_label_set_text(m_name[r], t->name.c_str());
			lv_label_set_text(m_z[r], format3(t->z).c_str());
			m_num[r].setSelected(t->number == active); // loaded: accent fill
			const bool selected = (t->number == m_selected);
			m_num[r].setOutlined(selected);
			const lv_color_t c = selected ? Colour::Accent() : Colour::Border();
			lv_obj_set_style_border_color(m_nameCard[r], c, 0);
			lv_obj_set_style_border_color(m_zCard[r], c, 0);
		}
		m_up.setDisabled(m_firstRow == 0);
		m_down.setDisabled(m_firstRow + NumRows >= n);

		const bool jobActive = State::JobActive();
		m_load.setLocked(jobActive || !isTool(m_selected) || m_selected == active);
		m_measure.setLocked(jobActive || !isTool(active) || m_selected != active);
		m_setZ.setLocked(jobActive || !isTool(m_selected));
	}

	// ---- Touches -------------------------------------------------------------------------------

	void WcsToolsPage::rowPressed(size_t row)
	{
		if (row < NumRows && m_rowTool[row] >= 0)
		{
			m_autoSelected = false;
		}
		if (row < NumRows && m_rowTool[row] >= 0 && m_rowTool[row] != m_selected)
		{
			m_selected = m_rowTool[row];
			update();
		}
	}

	void WcsToolsPage::zPressed(size_t row)
	{
		if (row >= NumRows || m_rowTool[row] < 0)
		{
			return;
		}
		rowPressed(row);
		if (State::JobActive())
		{
			Popup::Refuse(LockedJob); // selected, but no editing during a job
			return;
		}
		openSetZ();
	}

	void WcsToolsPage::pagePressed(int direction)
	{
		const size_t n = State::Tools().size();
		if (direction < 0 && m_firstRow > 0)
		{
			m_firstRow = (m_firstRow >= NumRows) ? m_firstRow - NumRows : 0;
		}
		else if (direction > 0 && m_firstRow + NumRows < n)
		{
			m_firstRow += NumRows;
		}
		else
		{
			return; // already at the end: the arrow is greyed
		}
		update();
	}

	bool WcsToolsPage::refuseUnhomed()
	{
		if (!State::XyzHomed())
		{
			Popup::Refuse("Home X, Y and Z first."); // the macros move in machine coordinates
			return true;
		}
		return false;
	}

	// ---- LOAD ----

	void WcsToolsPage::loadPressed()
	{
		if (State::JobActive())
		{
			Popup::Refuse(LockedJob);
			return;
		}
		if (refuseUnhomed())
		{
			return;
		}
		if (!isTool(m_selected))
		{
			Popup::Refuse("Select a tool first.");
			return;
		}
		const int tool = m_selected;
		const int active = State::ActiveTool();
		if (tool == active)
		{
			Popup::Refuse(fmt::format("T{:d} is already loaded.", tool));
			return;
		}
		const std::string title = fmt::format("LOAD T{:d}", tool);

		if (!isTool(active) && Nxt::Active())
		{
			// NXT: always the real tool change, so NeXT's tpre / tpost measure the tool and set its offset
			// (T# P0 would skip them)
			std::vector<std::string> nl = {fmt::format("Load T{:d}?", tool)};
			const std::string& nname = State::FindTool(tool)->name;
			if (!nname.empty())
			{
				nl.push_back(nname);
			}
			nl.push_back("NXT asks you to install it");
			nl.push_back("and measures it.");
			Popup::Confirm(title,
						   nl,
						   [tool]
						   {
							   if (State::JobActive() || !isTool(tool) || isTool(State::ActiveTool()))
							   {
								   Popup::Refuse(State::JobActive() ? LockedJob : ListChanged);
								   return;
							   }
							   State::Send(fmt::format("T{:d}\n", tool));
						   });
			return;
		}

		if (!isTool(active))
		{
			// No tool in the spindle (T-): is the selected one already in?
			Popup::FormQuestion(title,
								fmt::format("IS T{:d} IN THE SPINDLE?", tool),
								{"YES", "NO"},
								[tool](size_t answer)
								{
									if (State::JobActive() || !isTool(tool) || isTool(State::ActiveTool()))
									{
										// a tool was loaded elsewhere (DWC) while the question was open
										Popup::Refuse(State::JobActive() ? LockedJob : ListChanged);
										return;
									}
									if (answer == 0)
									{
										// YES: RRF only notes it (P0: no tool change macros, nothing moves).
										// The last-tool file is what config.g restores at power-on (REMEMBER TOOL).
										State::Send(fmt::format("T{:d} P0\n", tool));
										State::Send(
											fmt::format("echo >\"0:/sys/cnc-lasttool.g\" \"T{:d} P0\"\n", tool));
									}
									else
									{
										State::Send(fmt::format("T{:d}\n", tool)); // NO: tool change
									}
								});
			return;
		}

		std::vector<std::string> lines = {fmt::format("Change T{:d} -> T{:d}?", active, tool)};
		const std::string& name = State::FindTool(tool)->name;
		if (!name.empty())
		{
			lines.push_back(name);
		}
		lines.push_back("Runs the tool change.");
		Popup::Confirm(title,
					   lines,
					   [tool]
					   {
						   if (State::JobActive() || !isTool(tool))
						   {
							   Popup::Refuse(State::JobActive() ? LockedJob : ListChanged);
							   return;
						   }
						   State::Send(fmt::format("T{:d}\n", tool)); // runs the tool change macros
					   });
	}

	// ---- MEASURE ----

	void WcsToolsPage::measurePressed()
	{
		if (State::JobActive())
		{
			Popup::Refuse(LockedJob);
			return;
		}
		if (refuseUnhomed())
		{
			return; // measure_tool.g moves to the setter with G53
		}
		const int active = State::ActiveTool();
		if (!isTool(active))
		{
			Popup::Refuse("No tool is loaded.\nLOAD a tool first.");
			return;
		}
		const bool nxt = Nxt::Active();
		if (nxt ? !Nxt::ToolSetter() : !State::ToolSetter())
		{
			Popup::Refuse(nxt ? "No tool setter.\nThe toolsetter is off in NXT." : "No tool setter.\nTOOL SETTER is off in SETTINGS.");
			return;
		}
		if (nxt && active == Nxt::ProbeToolId())
		{
			Popup::Refuse("The touch probe is not\nmeasured on the tool setter.");
			return;
		}
		if (m_selected != active)
		{
			Popup::Refuse(fmt::format("Only the loaded tool (T{:d}) can be measured.", active));
			return;
		}
		Popup::Confirm(fmt::format("MEASURE T{:d}", active),
					   {fmt::format("Measure T{:d} length", active), "on the tool setter?"},
					   [active, nxt]
					   {
						   if (State::JobActive() || State::ActiveTool() != active)
						   {
							   Popup::Refuse(State::JobActive() ? LockedJob : "The loaded tool changed. Try again.");
							   return;
						   }
						   // NXT: its own tool length probe (G37); otherwise the screen's measure macro
						   State::Send(nxt ? std::string("G37\n")
										   : fmt::format("M98 P\"0:/sys/toolchange/measure_tool.g\" K{:d}\n", active));
					   });
	}

	// ---- SET Z ----

	void WcsToolsPage::setZPressed()
	{
		if (State::JobActive() || !isTool(m_selected))
		{
			Popup::Refuse(State::JobActive() ? LockedJob : "Select a tool first.");
			return;
		}
		openSetZ();
	}

	void WcsToolsPage::openSetZ()
	{
		const int tool = m_selected;
		Popup::NumpadSpec pad;
		pad.tag = fmt::format("T{:d} Z", tool);
		pad.unit = "mm";
		pad.value = State::FindTool(tool)->z;
		pad.decimals = 3;
		pad.allowDecimal = true;
		pad.allowMinus = true;
		pad.min = -999.0f;
		pad.max = 999.0f;
		pad.onOk = [tool](float value)
		{
			if (State::JobActive() || !isTool(tool))
			{
				Popup::Refuse(State::JobActive() ? LockedJob : ListChanged);
				return;
			}
			State::Send(fmt::format("G10 P{:d} Z{:.3f}\nM500 P10\n", tool, value)); // saved right away
			State::MarkOffsetsSaved();												// M500 saved the work offsets too
		};
		Popup::Numpad(pad);
	}
} // namespace UI::Cnc
