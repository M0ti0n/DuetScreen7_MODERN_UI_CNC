/*
 * CncHomeView.cpp
 *
 * Every position below is a PanelDue CncLayout value passed through S() / SL() (x 1.25).
 */

#include "CncHomeView.h"
#include "CncLayout.h"
#include "CncMessages.h"
#include "CncPanel.h"
#include "CncPopup.h"
#include "CncSettings.h"
#include "CncState.h"
#include "CncTheme.h"
#include "Debug.h"
#include <fmt/format.h>
#include <string>

namespace UI
{
	using namespace CncLayout;
	using namespace Cnc;

	// Sub-tabs (PanelDue CncWcsPage / CncJobPage / CncSystemPage; JOB gains PREVIEW, ordered as the
	// workflow: pick, check, run). Each page opens on its first sub-tab
	struct SubTabSet
	{
		unsigned int count;
		const char* names[CncHomeView::MaxSubTabs];
	};
	static const SubTabSet SubTabs[CncHomeView::NumPages] = {
		{0, {}},
		{3, {"OFFSETS", "TOOLS", "PROBE"}},
		{3, {"JOB LIST", "PREVIEW", "JOB STATUS"}},
		{0, {}},
		{3, {"ALERT", "CONSOLE", "SETTINGS"}},
	};
	static const NavIcon PageIcons[CncHomeView::NumPages] = {
		NavIcon::Joystick, NavIcon::Crosshair, NavIcon::List, NavIcon::SdCard, NavIcon::Spanner};

	CncHomeView::CncHomeView()
		: View("CncHomeView")
	{
		ZoneScoped;
		UI_LOCK();
		Cnc::Panel::Load();	   // the accent colour, before anything is built
		Cnc::Settings::Init(); // the machine settings (global.cnc*) from the object model
		Cnc::Messages::Init(); // display messages (M117)
		m_root = getRootPtr();
		lv_obj_remove_style_all(m_root); // the CNC UI does its own styling
		lv_obj_set_size(m_root, ScreenW, ScreenH);
		lv_obj_set_style_bg_color(m_root, Colour::PageBg(), 0);
		lv_obj_set_style_bg_opa(m_root, LV_OPA_COVER, 0);
		lv_obj_remove_flag(m_root, LV_OBJ_FLAG_SCROLLABLE);

		createDro();
		createCompactDro();
		createPages();
		createNav();
		Cnc::Panel::Init(); // touch click, the machine's beeps
		showPage(0);

		// STOP keeps working over a popup; ZERO is locked while a job runs or is paused
		Cnc::Popup::SetStopTarget(
			Nav::X(NumPages), Nav::Y, Nav::Tile, Nav::Tile, [this] { getPresenter()->emergencyStop(); });
		Cnc::State::AddJobListener([this] { setZeroLocked(Cnc::State::JobActive()); });
		Cnc::State::AddSpindleListener([this] { setSpindle(Cnc::State::SpindleRpm(), Cnc::State::SpindleRunning()); });
	}

	void CncHomeView::setZeroLocked(bool locked)
	{
		if (locked == m_zeroLocked)
		{
			return;
		}
		m_zeroLocked = locked;
		for (ZeroButton& z : m_zero)
		{
			lv_obj_set_style_text_color(z.letter, locked ? Colour::Border() : Colour::Text(), 0);
			lv_obj_set_style_bg_color(z.obj, locked ? Colour::Tile() : Colour::Pressed(), LV_STATE_PRESSED);
			lv_obj_invalidate(z.obj);
		}
	}

	// ---- Full DRO ------------------------------------------------------------------------------

	void CncHomeView::createDro()
	{
		using namespace Dro;
		const lv_color_t accent = Colour::Accent();
		const int32_t smallH = lv_font_get_line_height(Font::Small());
		m_fullDro = CreateBare(m_root, 0, 0, ScreenW, S(SeparatorY + SeparatorH));
		lv_obj_t* const root = m_fullDro;

		// Column headers
		m_workHeader = CreateLabel(root,
								   S(WorkX),
								   S(HeaderY - 2),
								   SL(WorkX, WorkW),
								   smallH,
								   Font::Small(),
								   Colour::Muted(),
								   LV_TEXT_ALIGN_CENTER,
								   "WORK G54");
		CreateLabel(root,
					S(MachX),
					S(HeaderY - 2),
					SL(MachX, MachW),
					smallH,
					Font::Small(),
					Colour::Muted(),
					LV_TEXT_ALIGN_CENTER,
					"MACHINE");
		CreateLabel(root,
					S(ZeroX),
					S(HeaderY - 2),
					SL(ZeroX, ZeroW),
					smallH,
					Font::Small(),
					Colour::Muted(),
					LV_TEXT_ALIGN_CENTER,
					"ZERO");

		// Rows (placed by layoutDro for 3 or 4 axes)
		static const char* const letters[DroRows] = {"X", "Y", "Z", "A"};
		for (size_t i = 0; i < DroRows; ++i)
		{
			m_letter[i] = CreateLabel(
				root, 0, 0, SL(AxisLabelX, AxisLabelW), 1, Font::Large(), accent, LV_TEXT_ALIGN_CENTER, letters[i]);

			m_workCard[i] = CreateBare(root, S(WorkX), 0, SL(WorkX, WorkW), 1);
			StyleCard(m_workCard[i], Colour::Tile(), Colour::Border());
			m_work[i] =
				CreateLabel(m_workCard[i], 0, 0, 1, 1, Font::Large(), Colour::Text(), LV_TEXT_ALIGN_RIGHT, "0.000");

			m_machCard[i] = CreateBare(root, S(MachX), 0, SL(MachX, MachW), 1);
			StyleCard(m_machCard[i], Colour::Tile(), Colour::Border());
			m_mach[i] =
				CreateLabel(m_machCard[i], 0, 0, 1, 1, Font::Small(), Colour::Muted(), LV_TEXT_ALIGN_RIGHT, "0.000");

			ZeroButton& z = m_zero[i];
			z.view = this;
			z.row = i;
			z.obj = CreateBare(root, S(ZeroX), 0, SL(ZeroX, ZeroW), 1);
			StyleCard(z.obj, Colour::Tile(), Colour::Border());
			lv_obj_set_style_bg_color(z.obj, Colour::Pressed(), LV_STATE_PRESSED);
			lv_obj_add_flag(z.obj, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_event_cb(z.obj, drawZeroButton, LV_EVENT_DRAW_MAIN_END, &z);
			lv_obj_add_event_cb(
				z.obj,
				[](lv_event_t* e)
				{
					auto* zb = static_cast<ZeroButton*>(lv_event_get_user_data(e));
					if (zb->view->m_zeroLocked)
					{
						Cnc::Popup::Refuse("ZERO is locked while a job is running or paused.");
						return;
					}
					zb->view->getPresenter()->zeroAxis(zb->row);
				},
				LV_EVENT_CLICKED,
				&z);
			z.letter = CreateLabel(z.obj, 0, 0, 1, 1, Font::Large(), Colour::Text(), LV_TEXT_ALIGN_LEFT, letters[i]);
		}
		layoutDro();

		// Tool / spindle / feed row
		auto card = [root](int32_t x, int32_t w)
		{
			lv_obj_t* c = CreateBare(root, S(x), S(TsfY), SL(x, w), SL(TsfY, TsfH));
			StyleCard(c, Colour::Tile(), Colour::Border());
			return c;
		};
		const int32_t tsfH = SL(TsfY, TsfH);

		m_toolCard = card(ToolX, ToolW);
		lv_obj_t* toolCard = m_toolCard;
		m_toolValue = CreateLabel(toolCard,
								  S(ToolX + 6) - S(ToolX),
								  0,
								  SL(ToolX + 6, ToolW - 12),
								  tsfH,
								  Font::Large(),
								  Colour::Text(),
								  LV_TEXT_ALIGN_CENTER,
								  "T-");

		// S and F: small muted letter on the left, value right-aligned (PanelDue: letter 2 px below centre)
		auto valueCard = [&](int32_t x, int32_t w, const char* letter, lv_obj_t*& value)
		{
			lv_obj_t* c = card(x, w);
			CreateLabel(c,
						S(x + 12) - S(x),
						S(2),
						SL(x + 12, 20),
						tsfH,
						Font::Small(),
						Colour::Muted(),
						LV_TEXT_ALIGN_LEFT,
						letter);
			value = CreateLabel(c,
								S(x + 34) - S(x),
								0,
								SL(x + 34, w - 46),
								tsfH,
								Font::Large(),
								Colour::Text(),
								LV_TEXT_ALIGN_RIGHT,
								"0");
			return c;
		};
		m_spindleCard = valueCard(SpindleX, SpindleW, "S", m_spindleValue);
		m_feedCard = valueCard(FeedX, FeedW, "F", m_feedValue);

		// T switches the spindle on / standby, S opens the speed numpad, F the SLOW jog feed presets.
		// They act on the touch, as on PanelDue (no pressed look there either).
		auto tileAction = [this](lv_obj_t* tile, void (Cnc::ControlPage::*action)())
		{
			m_tileActions.push_back({this, action});
			lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_event_cb(
				tile,
				[](lv_event_t* e)
				{
					auto* t = static_cast<TileAction*>(lv_event_get_user_data(e));
					(t->view->m_control.*(t->action))();
				},
				LV_EVENT_PRESSED,
				&m_tileActions.back());
		};
		m_tileActions.reserve(3); // the event user data points into the vector
		tileAction(m_toolCard, &Cnc::ControlPage::toolTilePressed);
		tileAction(m_spindleCard, &Cnc::ControlPage::spindleTilePressed);
		tileAction(m_feedCard, &Cnc::ControlPage::feedTilePressed);

		// Accent separator under the DRO
		lv_obj_t* sep = CreateBare(root, Margin, S(SeparatorY), ContentW, SL(SeparatorY, SeparatorH));
		lv_obj_set_style_bg_color(sep, accent, 0);
		lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
	}

	// 3 axes: 3 rows of 68 px, 4 axes: 4 rows of 50 px; both end at the same y (PanelDue LayoutFullDro)
	void CncHomeView::layoutDro()
	{
		using namespace Dro;
		const int32_t h = (m_rows == 4) ? RowH4 : RowH3;
		const int32_t gap = (m_rows == 4) ? Gap4 : Gap3;
		for (size_t i = 0; i < DroRows; ++i)
		{
			const int32_t y = RowsTop + (int32_t)i * (h + gap);
			const int32_t top = S(y), height = SL(y, h);

			lv_obj_set_y(m_letter[i], top + (height - lv_font_get_line_height(Font::Large())) / 2);

			lv_obj_set_y(m_workCard[i], top);
			lv_obj_set_height(m_workCard[i], height);
			lv_obj_set_width(m_work[i], SL(WorkX + 6, WorkW - 18));
			lv_obj_set_pos(m_work[i],
						   S(WorkX + 6) - S(WorkX) - CardBorder,
						   (height - lv_font_get_line_height(Font::Large())) / 2 - CardBorder);

			lv_obj_set_y(m_machCard[i], top);
			lv_obj_set_height(m_machCard[i], height);
			lv_obj_set_width(m_mach[i], SL(MachX + 6, MachW - 18));
			lv_obj_set_pos(m_mach[i],
						   S(MachX + 6) - S(MachX) - CardBorder,
						   (height - lv_font_get_line_height(Font::Small())) / 2 - CardBorder);

			lv_obj_set_y(m_zero[i].obj, top);
			lv_obj_set_height(m_zero[i].obj, height);
			lv_obj_set_width(m_zero[i].letter, SL(ZeroX + 52, ZeroW - 56));
			lv_obj_set_pos(m_zero[i].letter,
						   S(ZeroX + 52) - S(ZeroX) - CardBorder,
						   (height - lv_font_get_line_height(Font::Large())) / 2 - CardBorder);
			lv_obj_invalidate(m_zero[i].obj);

			const bool show = (i < m_rows);
			for (lv_obj_t* o : {m_letter[i], m_workCard[i], m_machCard[i], m_zero[i].obj})
			{
				if (show)
				{
					lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
				}
				else
				{
					lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
				}
			}
		}
	}

	void CncHomeView::drawZeroButton(lv_event_t* e)
	{
		auto* z = static_cast<ZeroButton*>(lv_event_get_user_data(e));
		lv_area_t a;
		lv_obj_get_coords(z->obj, &a);
		const int32_t h = lv_area_get_height(&a);
		const bool pressed = lv_obj_has_state(z->obj, LV_STATE_PRESSED);
		// PanelDue: crosshair 27 px from the left, ring radius 8 (rows under 60 px) or 9, stroke 3
		Pen pen(lv_event_get_layer(e), a.x1 + S(Dro::ZeroX + 27) - S(Dro::ZeroX), a.y1 + h / 2);
		const lv_color_t fg = z->view->m_zeroLocked ? Colour::Border() : Colour::Text(); // muted while locked
		DrawCrosshair(
			pen, (h < S(60)) ? 8 : 9, 3, fg, (pressed && !z->view->m_zeroLocked) ? Colour::Pressed() : Colour::Tile());
	}

	void CncHomeView::setAxisRowCount(size_t rows)
	{
		UI_LOCK();
		rows = (rows == 4) ? 4 : 3;
		m_control.setAxisCount(rows);
		if (rows != m_rows)
		{
			m_rows = rows;
			layoutDro();
			layoutCompactDro();
		}
	}

	void CncHomeView::setAxisRow(size_t row, char letter, float work, float machine, bool homed)
	{
		if (row >= DroRows)
		{
			return;
		}
		UI_LOCK();
		const char l[2] = {letter, 0};
		lv_label_set_text(m_letter[row], l);
		lv_label_set_text(m_zero[row].letter, l);
		const std::string w = fmt::format("{:.3f}", work);
		lv_label_set_text(m_work[row], w.c_str());
		lv_label_set_text(m_cmpLetter[row], l);
		lv_label_set_text(m_cmpWork[row], w.c_str());
		lv_label_set_text(m_mach[row], fmt::format("{:.3f}", machine).c_str());
		(void)homed; // the not-homed colouring is on CONTROL > HOME (comes with the CONTROL page)
	}

	void CncHomeView::setWorkplace(uint8_t n)
	{
		UI_LOCK();
		// G54..G59, G59.1..G59.3; anything else (not reported yet): unknown, as "T-"
		const std::string text = (n < 6)   ? fmt::format("WORK G{:d}", 54 + n)
								 : (n < 9) ? fmt::format("WORK G59.{:d}", n - 5)
										   : std::string("WORK -");
		lv_label_set_text(m_workHeader, text.c_str());
		lv_label_set_text(m_cmpHeader, text.c_str());
	}

	void CncHomeView::setTool(int tool)
	{
		UI_LOCK();
		const std::string text = (tool < 0) ? std::string("T-") : fmt::format("T{:d}", tool);
		lv_label_set_text(m_toolValue, text.c_str());
		lv_label_set_text(m_cmpTool, text.c_str());
	}

	// A running spindle outlines the T and S tiles in the accent colour (PanelDue SetSpindleOutline)
	void CncHomeView::setSpindle(int32_t rpm, bool running)
	{
		UI_LOCK();
		lv_label_set_text_fmt(m_spindleValue, "%d", (int)rpm);
		lv_label_set_text_fmt(m_cmpSpindle, "%d", (int)rpm);
		const lv_color_t outline = running ? Colour::Accent() : Colour::Border();
		for (lv_obj_t* c : {m_spindleCard, m_toolCard, m_cmpSpindleCard, m_cmpToolCard})
		{
			lv_obj_set_style_border_color(c, outline, 0);
		}
	}

	void CncHomeView::setFeed(int32_t mmPerMin)
	{
		UI_LOCK();
		lv_label_set_text_fmt(m_feedValue, "%d", (int)mmPerMin);
		lv_label_set_text_fmt(m_cmpFeed, "%d", (int)mmPerMin);
	}

	// ---- Compact DRO (JOB, MACROS, SYSTEM) -------------------------------------------------------

	// PanelDue CreateCompactDro: X Y Z in row 1; row 2 holds T S F, with A under X on 4-axis machines
	void CncHomeView::createCompactDro()
	{
		using namespace Compact;
		const lv_color_t accent = Colour::Accent();
		const int32_t smallH = lv_font_get_line_height(Font::Small());
		m_compactDro = CreateBare(m_root, 0, 0, ScreenW, S(SeparatorY + SeparatorH));
		lv_obj_t* const root = m_compactDro;

		// Same text offset as the full DRO's headers
		m_cmpHeader = CreateLabel(root,
								  S(HeaderX),
								  S(HeaderY - 2),
								  SL(HeaderX, HeaderW),
								  smallH,
								  Font::Small(),
								  Colour::Muted(),
								  LV_TEXT_ALIGN_LEFT,
								  "WORK G54");

		static const char* const letters[DroRows] = {"X", "Y", "Z", "A"};
		for (size_t i = 0; i < DroRows; ++i)
		{
			const int32_t lx = AxisX((i < 3) ? (unsigned int)i : 0u);
			const int32_t y = (i < 3) ? Row1Y : Row2Y;
			const int32_t top = S(y), height = SL(y, RowH);
			m_cmpLetter[i] = CreateLabel(
				root, S(lx), top, SL(lx, LetterW), height, Font::Large(), accent, LV_TEXT_ALIGN_CENTER, letters[i]);
			m_cmpCard[i] = CreateBare(root, S(lx + LetterW), top, SL(lx + LetterW, AxisTileW), height);
			StyleCard(m_cmpCard[i], Colour::Tile(), Colour::Border());
			m_cmpWork[i] = CreateLabel(root,
									   S(lx + LetterW + 4),
									   top,
									   SL(lx + LetterW + 4, AxisTileW - 10),
									   height,
									   Font::Large(),
									   Colour::Text(),
									   LV_TEXT_ALIGN_RIGHT,
									   "0.000");
		}

		// T, S, F: x and width are set by layoutCompactDro. Cards first so the text draws on top.
		const int32_t top = S(Row2Y), height = SL(Row2Y, RowH);
		auto card = [&]()
		{
			lv_obj_t* c = CreateBare(root, 0, top, 1, height);
			StyleCard(c, Colour::Tile(), Colour::Border());
			return c;
		};
		m_cmpToolCard = card();
		m_cmpSpindleCard = card();
		m_cmpFeedCard = card();
		m_cmpTool = CreateLabel(root, 0, top, 1, height, Font::Large(), Colour::Text(), LV_TEXT_ALIGN_CENTER, "T-");
		// S and F letters: small and muted, 2 px below the centre (PanelDue)
		m_cmpSpindleLetter =
			CreateLabel(root, 0, top + S(2), 1, height, Font::Small(), Colour::Muted(), LV_TEXT_ALIGN_LEFT, "S");
		m_cmpFeedLetter =
			CreateLabel(root, 0, top + S(2), 1, height, Font::Small(), Colour::Muted(), LV_TEXT_ALIGN_LEFT, "F");
		m_cmpSpindle = CreateLabel(root, 0, top, 1, height, Font::Large(), Colour::Text(), LV_TEXT_ALIGN_RIGHT, "0");
		m_cmpFeed = CreateLabel(root, 0, top, 1, height, Font::Large(), Colour::Text(), LV_TEXT_ALIGN_RIGHT, "0");

		lv_obj_t* sep = CreateBare(root, Margin, S(SeparatorY), ContentW, SL(SeparatorY, SeparatorH));
		lv_obj_set_style_bg_color(sep, accent, 0);
		lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

		layoutCompactDro();
	}

	// PanelDue LayoutCompactRow2
	void CncHomeView::layoutCompactDro()
	{
		using namespace Compact;
		const bool four = (m_rows == 4);
		const int32_t tx = four ? T4X : T3X, tw = four ? T4W : T3W;
		const int32_t sx = four ? S4X : S3X, sw = four ? S4W : S3W;
		const int32_t fx = four ? F4X : F3X, fw = four ? F4W : F3W;
		auto place = [](lv_obj_t* o, int32_t x, int32_t w)
		{
			lv_obj_set_x(o, S(x));
			lv_obj_set_width(o, SL(x, w));
		};
		place(m_cmpToolCard, tx, tw);
		place(m_cmpTool, tx + 4, tw - 8);
		place(m_cmpSpindleCard, sx, sw);
		place(m_cmpSpindleLetter, sx + 10, 16);
		place(m_cmpSpindle, sx + 28, sw - 38);
		place(m_cmpFeedCard, fx, fw);
		place(m_cmpFeedLetter, fx + 10, 16);
		place(m_cmpFeed, fx + 28, fw - 38);
		for (lv_obj_t* o : {m_cmpLetter[3], m_cmpCard[3], m_cmpWork[3]})
		{
			if (four)
			{
				lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
			}
			else
			{
				lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
			}
		}
	}

	// ---- Pages ---------------------------------------------------------------------------------

	unsigned int CncHomeView::getSubTabCount(unsigned int page)
	{
		return (page < NumPages) ? SubTabs[page].count : 0;
	}

	bool CncHomeView::isCompactPage(unsigned int page)
	{
		return page >= PageJob;
	}

	int32_t CncHomeView::contentTop(unsigned int page)
	{
		return isCompactPage(page) ? CompactContentTop : ContentTop;
	}

	int32_t CncHomeView::contentBottom(unsigned int page)
	{
		return (getSubTabCount(page) != 0) ? ContentBottom : ContentBottomNoTabs;
	}

	lv_obj_t* CncHomeView::getContent(unsigned int page, unsigned int subTab) const
	{
		if (page >= NumPages)
		{
			return nullptr;
		}
		const unsigned int n = (SubTabs[page].count != 0) ? SubTabs[page].count : 1;
		return (subTab < n) ? m_content[page][subTab] : nullptr;
	}

	// One container per page, one content container per sub-tab inside it (pages without sub-tabs: one)
	void CncHomeView::createPages()
	{
		for (unsigned int p = 0; p < NumPages; ++p)
		{
			const int32_t top = contentTop(p), height = contentBottom(p) - top;
			m_pageObj[p] = CreateBare(m_root, 0, top, ScreenW, height);

			const SubTabSet& tabs = SubTabs[p];
			const unsigned int n = (tabs.count != 0) ? tabs.count : 1;
			for (unsigned int s = 0; s < n; ++s)
			{
				m_content[p][s] = CreateBare(m_pageObj[p], 0, 0, ScreenW, height); // every page is ported
			}

			if (tabs.count != 0)
			{
				m_tabs[p].create(m_root, tabs.names, tabs.count);
				m_tabs[p].onSelect([this](unsigned int s) { showSubTab(s); });
			}
		}
		m_control.create(m_content[PageControl][0], ContentTop);
		m_wcsOffsets.create(m_content[PageWcs][0], ContentTop);
		m_wcsTools.create(m_content[PageWcs][1], ContentTop);
		m_wcsProbe.create(m_content[PageWcs][2], ContentTop);
		m_jobList.create(m_content[PageJob][JobListTab], CompactContentTop);
		m_jobPreview.create(m_content[PageJob][JobPreviewTab], CompactContentTop);
		m_jobPreview.setListPath([this] { return m_jobList.selectedPath(); });
		m_jobList.onPreviewRequest([this] { showJobPreview(); });
		m_jobStatus.create(m_content[PageJob][JobStatusTab], CompactContentTop);
		m_jobStatus.onShowRequest([this] { showJobStatus(); });
		m_macros.create(m_content[PageMacros][0], CompactContentTop);
		m_alerts.create(m_content[PageSystem][SystemAlertTab], CompactContentTop);
		m_console.create(m_content[PageSystem][SystemConsoleTab], CompactContentTop);
		m_settings.create(m_content[PageSystem][SystemSettingsTab], CompactContentTop);
		// CUSTOM n > MACRO: pick on MACROS, then back to SETTINGS
		m_settings.onPickMacro(
			[this](Cnc::MacrosPage::PickHandler handler)
			{
				m_macros.startPick(
					[this, handler](const std::string& path, const std::string& name)
					{
						m_subTab[PageSystem] = SystemSettingsTab;
						m_tabs[PageSystem].select(SystemSettingsTab);
						showPage(PageSystem);
						handler(path, name);
					});
				showPage(PageMacros);
			});
	}

	void CncHomeView::showJobStatus()
	{
		if (m_page != PageJob || m_subTab[PageJob] != JobStatusTab)
		{
			m_subTab[PageJob] = JobStatusTab;
			m_tabs[PageJob].select(JobStatusTab);
			showPage(PageJob);
		}
	}

	void CncHomeView::showJobPreview()
	{
		if (m_page != PageJob || m_subTab[PageJob] != JobPreviewTab)
		{
			m_subTab[PageJob] = JobPreviewTab;
			m_tabs[PageJob].select(JobPreviewTab);
			showPage(PageJob);
		}
	}

	void CncHomeView::showPage(unsigned int page)
	{
		if (page >= NumPages)
		{
			return;
		}
		UI_LOCK();
		m_page = page;
		for (unsigned int i = 0; i < NumPages; ++i)
		{
			m_nav[i].active = (i == page);
			lv_obj_set_style_border_width(m_navObj[i], (i == page) ? CardBorder : 0, 0);
			lv_obj_invalidate(m_navObj[i]);
		}

		// Full DRO on CONTROL and WCS, compact DRO on the others
		const bool compact = isCompactPage(page);
		lv_obj_t* const shown = compact ? m_compactDro : m_fullDro;
		lv_obj_t* const hidden = compact ? m_fullDro : m_compactDro;
		lv_obj_remove_flag(shown, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(hidden, LV_OBJ_FLAG_HIDDEN);

		for (unsigned int p = 0; p < NumPages; ++p)
		{
			if (p == page)
			{
				lv_obj_remove_flag(m_pageObj[p], LV_OBJ_FLAG_HIDDEN);
			}
			else
			{
				lv_obj_add_flag(m_pageObj[p], LV_OBJ_FLAG_HIDDEN);
			}
			if (SubTabs[p].count != 0)
			{
				m_tabs[p].show(p == page);
			}
		}
		m_control.show(page == PageControl);
		updateContentVisibility();
		pageShownChanged();
	}

	void CncHomeView::showSubTab(unsigned int subTab)
	{
		if (subTab >= SubTabs[m_page].count)
		{
			return;
		}
		UI_LOCK();
		m_subTab[m_page] = subTab;
		m_tabs[m_page].select(subTab);
		updateContentVisibility();
		pageShownChanged();
	}

	void CncHomeView::pageShownChanged()
	{
		m_jobList.setShown(m_page == PageJob && m_subTab[PageJob] == JobListTab);
		m_jobPreview.setShown(m_page == PageJob && m_subTab[PageJob] == JobPreviewTab);
		m_macros.setShown(m_page == PageMacros);
		m_console.setShown(m_page == PageSystem && m_subTab[PageSystem] == SystemConsoleTab);
		m_settings.setShown(m_page == PageSystem && m_subTab[PageSystem] == SystemSettingsTab);
	}

	void CncHomeView::updateContentVisibility()
	{
		for (unsigned int p = 0; p < NumPages; ++p)
		{
			for (unsigned int s = 0; s < MaxSubTabs; ++s)
			{
				if (m_content[p][s] == nullptr)
				{
					continue;
				}
				if (s == m_subTab[p])
				{
					lv_obj_remove_flag(m_content[p][s], LV_OBJ_FLAG_HIDDEN);
				}
				else
				{
					lv_obj_add_flag(m_content[p][s], LV_OBJ_FLAG_HIDDEN);
				}
			}
		}
	}

	// ---- Navigation ----------------------------------------------------------------------------

	void CncHomeView::createNav()
	{
		const lv_color_t accent = Colour::Accent();
		for (unsigned int i = 0; i <= NumPages; ++i)
		{
			const bool stop = (i == NumPages);
			NavTile& t = m_nav[i];
			t.view = this;
			t.index = i;
			t.icon = stop ? NavIcon::Joystick : PageIcons[i];
			t.active = false;

			lv_obj_t* o = CreateBare(m_root, Nav::X(i), Nav::Y, Nav::Tile, Nav::Tile);
			m_navObj[i] = o;
			if (stop)
			{
				StyleCard(o, Colour::StopRed(), Colour::StopRed(), false);
				lv_obj_set_style_bg_color(o, Colour::StopRedPressed(), LV_STATE_PRESSED);
			}
			else
			{
				StyleCard(o, Colour::Tile(), accent, false);
			}
			lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_event_cb(o, drawNavTile, LV_EVENT_DRAW_MAIN_END, &t);
			lv_obj_add_event_cb(
				o,
				[](lv_event_t* e)
				{
					auto* nt = static_cast<NavTile*>(lv_event_get_user_data(e));
					if (nt->index == NumPages)
					{
						nt->view->getPresenter()->emergencyStop();
					}
					else
					{
						nt->view->getPresenter()->showPage(nt->index);
					}
				},
				// STOP acts on the press, as on PanelDue; page tiles on release
				stop ? LV_EVENT_PRESSED : LV_EVENT_CLICKED,
				&t);
		}
	}

	void CncHomeView::drawNavTile(lv_event_t* e)
	{
		auto* t = static_cast<NavTile*>(lv_event_get_user_data(e));
		lv_obj_t* o = t->view->m_navObj[t->index];
		lv_area_t a;
		lv_obj_get_coords(o, &a);
		Pen pen(lv_event_get_layer(e), a.x1 + lv_area_get_width(&a) / 2, a.y1 + lv_area_get_height(&a) / 2);
		if (t->index == NumPages)
		{
			const bool pressed = lv_obj_has_state(o, LV_STATE_PRESSED);
			DrawStopIcon(pen, Colour::StopGlyph(), pressed ? Colour::StopRedPressed() : Colour::StopRed());
		}
		else
		{
			DrawNavIcon(pen, t->icon, t->active ? Colour::Accent() : Colour::Muted(), Colour::Tile());
		}
	}
} // namespace UI
