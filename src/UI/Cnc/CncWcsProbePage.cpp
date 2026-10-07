/*
 * CncWcsProbePage.cpp
 *
 * Positions are PanelDue CncLayout::Probe values through S() / SL(): the page relative to the page
 * area, the popups relative to their window (inside its 2 px border).
 */

#include "CncWcsProbePage.h"
#include "CncMessages.h"
#include "CncPopup.h"
#include "CncState.h"
#include "CncTheme.h"
#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#include <vector>

namespace UI::Cnc
{
	using namespace CncLayout;
	using namespace CncLayout::Probe;

	namespace
	{
		const char* const OriginCodes[WcsProbePage::NumOrigins] = {
			"TL", "TOP", "TR", "LEFT", "CTR", "RIGHT", "BL", "BOT", "BR", "BORE", "BOSS", "ZTOP"};
		const char* const OriginNames[WcsProbePage::NumOrigins] = {"Top left corner",
																   "Top edge",
																   "Top right corner",
																   "Left edge",
																   "Centre",
																   "Right edge",
																   "Bottom left corner",
																   "Bottom edge",
																   "Bottom right corner",
																   "Bore centre",
																   "Boss centre",
																   "Top of stock (Z)"};
		const char* const BigLabels[3] = {"BORE", "BOSS", "Z TOP"};
		const char* const WcsNames[6] = {"G54", "G55", "G56", "G57", "G58", "G59"};
		const char* const ModeNames[3] = {"AUTO", "SEMI-AUTO", "MANUAL"};
		const char AxisLetters[4] = {'X', 'Y', 'Z', 'A'};

		const float JogSteps[4] = {0.01f, 0.1f, 1.0f, 10.0f};
		const char* const JogStepNames[4] = {"0.01", "0.1", "1", "10"};
		constexpr size_t FirstGuardedJogStep = 3; // 10: not before the last move has finished
		constexpr size_t DefaultJogStep = 1;	  // 0.1 when a prompt opens

		constexpr const char* LockedJob = "Locked while a job is running or paused.";

		const lv_color_t Amber = lv_color_hex(0xE0A800); // in progress

		Origin sideGlyph(char side)
		{
			switch (side)
			{
			case 'L':
				return Origin::ML;
			case 'R':
				return Origin::MR;
			case 'T':
				return Origin::TC;
			case 'B':
				return Origin::BC;
			case 'Z':
				return Origin::ZTop;
			default:
				return Origin::None;
			}
		}

		// Glyph objects: user data points to the Origin variable to draw; colours by kind of place
		struct GlyphSpec
		{
			const Origin* kind;
			bool accent; // accent glyph on the page background (jog sentence), else text on tile
		};
		GlyphSpec s_titleGlyph, s_autoSideGlyph, s_jogSideGlyph;
		Origin s_titleOrigin = Origin::TL;

		void drawGlyph(lv_event_t* e)
		{
			const auto* spec = static_cast<const GlyphSpec*>(lv_event_get_user_data(e));
			lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
			lv_area_t a;
			lv_obj_get_coords(obj, &a);
			Pen pen(lv_event_get_layer(e), a.x1 + lv_area_get_width(&a) / 2, a.y1 + lv_area_get_height(&a) / 2);
			DrawOriginGlyph(pen,
							*spec->kind,
							spec->accent ? Colour::Accent() : Colour::Text(),
							spec->accent ? Colour::PageBg() : Colour::Tile());
		}

		lv_obj_t* glyphObj(lv_obj_t* parent, int32_t x, int32_t y, int32_t size, GlyphSpec* spec)
		{
			lv_obj_t* o = CreateBare(parent, x, y, size, size);
			lv_obj_add_event_cb(o, drawGlyph, LV_EVENT_DRAW_MAIN_END, spec);
			return o;
		}

		void show(lv_obj_t* o, bool visible)
		{
			if (o == nullptr)
			{
				return;
			}
			if (visible)
			{
				lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
			}
			else
			{
				lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
			}
		}

		// Popup window coordinates (PanelDue units inside the 460 x 396 window) -> pixels in its box
		int32_t WX(int32_t v)
		{
			return S(PopX + v) - S(PopX) - CardBorder;
		}
		int32_t WY(int32_t v)
		{
			return S(PopY + v) - S(PopY) - CardBorder;
		}
		int32_t WW(int32_t x, int32_t w)
		{
			return SL(PopX + x, w);
		}
		int32_t WH(int32_t y, int32_t h)
		{
			return SL(PopY + y, h);
		}

		lv_obj_t* winTile(lv_obj_t* box, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t border)
		{
			lv_obj_t* t = CreateBare(box, WX(x), WY(y), WW(x, w), WH(y, h));
			StyleCard(t, Colour::Tile(), border);
			return t;
		}

		lv_obj_t* winLabel(lv_obj_t* box,
						   int32_t x,
						   int32_t textTop,
						   int32_t w,
						   int32_t fontH,
						   const lv_font_t* font,
						   lv_color_t colour,
						   lv_text_align_t align,
						   const char* text)
		{
			return CreateLabel(box, WX(x), WY(textTop), WW(x, w), WH(textTop, fontH), font, colour, align, text);
		}

		// Red X / green button inside a page window (the window may close in the handler)
		lv_obj_t* winButton(
			lv_obj_t* box, int32_t x, int32_t y, int32_t w, int32_t h, bool cancel, std::function<void()>* handler)
		{
			lv_obj_t* b = CreateBare(box, WX(x), WY(y), WW(x, w), WH(y, h));
			StyleCard(b, cancel ? Colour::StopRed() : Colour::Green(), Colour::Border(), false);
			lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
			if (cancel)
			{
				lv_obj_add_event_cb(
					b,
					[](lv_event_t* e)
					{
						lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
						lv_area_t a;
						lv_obj_get_coords(obj, &a);
						Pen pen(
							lv_event_get_layer(e), a.x1 + lv_area_get_width(&a) / 2, a.y1 + lv_area_get_height(&a) / 2);
						DrawCancelIcon(pen, Colour::ButtonText());
					},
					LV_EVENT_DRAW_MAIN_END,
					nullptr);
			}
			lv_obj_add_event_cb(
				b,
				[](lv_event_t* e)
				{
					auto* h = static_cast<std::function<void()>*>(lv_event_get_user_data(e));
					Popup::InEvent(*h);
				},
				LV_EVENT_CLICKED,
				handler);
			return b;
		}

		std::function<void()> s_jogOkHandler, s_jogCancelHandler;
	} // namespace

	// ---- Building ------------------------------------------------------------------------------

	WcsProbePage::~WcsProbePage()
	{
		Messages::SetProbeHooks({}); // the hooks point here
		if (m_timer != nullptr)
		{
			lv_timer_delete(m_timer);
		}
	}

	void WcsProbePage::create(lv_obj_t* parent, int32_t parentTop)
	{
		m_root = CreateBare(parent, 0, 0, ScreenW, 1);
		lv_obj_set_size(m_root, LV_PCT(100), LV_PCT(100)); // the parent is not laid out yet
		m_top = parentTop;
		auto y = [this](int32_t pdY) { return S(pdY) - m_top; };
		const int32_t smallH = lv_font_get_line_height(Font::Small());
		const lv_color_t accent = Colour::Accent();

		// Labels
		auto label = [&](int32_t pdX, int32_t pdW, const char* text, lv_color_t colour, lv_text_align_t align)
		{ return CreateLabel(m_root, S(pdX), y(LabelY), SL(pdX, pdW), smallH, Font::Small(), colour, align, text); };
		label(GridX + 2, 150, "ORIGIN", Colour::Muted(), LV_TEXT_ALIGN_LEFT);
		label(ParX + 2, 60, "MODE", Colour::Muted(), LV_TEXT_ALIGN_LEFT);
		m_modeLabel = label(ParX + 62, ParW - 62, "", accent, LV_TEXT_ALIGN_RIGHT);

		// Origin picker
		for (size_t i = 0; i < NumOrigins; ++i)
		{
			const bool big = (i >= 9);
			const int32_t x = GridX + (int32_t)(i % 3) * (BtnW + BtnGap);
			const int32_t yy = big ? BigRowY : GridY + (int32_t)(i / 3) * (BtnH + BtnGap);
			const int32_t h = big ? BigBtnH : BtnH;
			m_originButtons[i].create(
				m_root, S(x), y(yy), SL(x, BtnW), SL(yy, h), (Origin)i, big ? BigLabels[i - 9] : nullptr, big);
			m_originButtons[i].onPress([this, i] { originPressed(i); });
		}
		lv_obj_t* sep = CreateBare(m_root, S(SepX), y(LabelY), SL(SepX, SeparatorW), SL(LabelY, PickBottom - LabelY));
		lv_obj_set_style_bg_color(sep, accent, 0);
		lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

		// Parameter tiles: AUTO 2 x 3 stacked, SEMI / MANUAL one row each
		auto addTile =
			[&](size_t m, int32_t ty, int32_t tx, int32_t tw, int32_t th, const char* text, Param id, bool stacked)
		{
			ModeTile& t = m_modeTiles[m][m_modeTileCount[m]++];
			t.id = id;
			t.tile.create(m_root, S(tx), y(ty), SL(tx, tw), SL(ty, th), text, stacked, id == PStoreTo);
			t.tile.onPress([this, id] { paramPressed(id); });
		};
		{
			// 97 px fit: CLEARANCE and STORE TO were cut on PanelDue
			static const char* const autoLabels[6] = {
				"TIP \xC3\x98", "CLEAR D", "SEARCH", "Z DEPTH", "STOCK", "SAVE TO"};
			static const Param autoIds[6] = {PTip, PAutoClear, PAutoSearch, PZDepth, PStock, PStoreTo};
			for (size_t k = 0; k < 6; ++k)
			{
				addTile(0,
						GridY + (int32_t)(k / 2) * (AutoTileH + TileGap),
						ParX + (int32_t)(k % 2) * (AutoTileW + TileGap),
						AutoTileW,
						AutoTileH,
						autoLabels[k],
						autoIds[k],
						true);
			}
			static const char* const semiLabels[4] = {"TIP \xC3\x98", "CLEARANCE", "SEARCH", "STORE TO"};
			static const Param semiIds[4] = {PTip, PSemiClear, PSemiSearch, PStoreTo};
			for (size_t k = 0; k < 4; ++k)
			{
				addTile(1,
						GridY + (int32_t)k * (RowTileH + RowTileGap),
						ParX,
						ParW,
						RowTileH,
						semiLabels[k],
						semiIds[k],
						false);
			}
			static const char* const manLabels[2] = {"TOOL \xC3\x98", "STORE TO"};
			static const Param manIds[2] = {PToolDia, PStoreTo};
			for (size_t k = 0; k < 2; ++k)
			{
				addTile(2,
						GridY + (int32_t)k * (RowTileH + RowTileGap),
						ParX,
						ParW,
						RowTileH,
						manLabels[k],
						manIds[k],
						false);
			}
		}

		// Bottom row: probe signal + RUN PROBE, or START (manual)
		m_probeTile.create(m_root, S(GridX), y(BottomY), SL(GridX, StatusW), SL(BottomY, BottomH), "PROBE");
		const int32_t runX = GridX + StatusW + 8;
		auto greenButton = [&](TextButton& b, int32_t x, int32_t w, const char* text)
		{
			b.create(m_root, S(x), y(BottomY), SL(x, w), SL(BottomY, BottomH), text, Font::Large());
			b.setColours(Colour::ButtonText(), Colour::Green());
			b.setBorderVisible(false);
			b.setPressedLook(false);
			b.onPress([this] { runPressed(); });
		};
		greenButton(m_run, runX, RowW - StatusW - 8, "RUN PROBE");
		greenButton(m_start, GridX, RowW, "START");

		// Initial state; the parameter values are saved with the machine settings
		m_originButtons[m_origin].setSelected(true);
		m_values = Settings::Get().probe;
		for (size_t i = 0; i < NumParams; ++i)
		{
			updateTileText((Param)i);
		}
		Settings::AddListener(
			[this]
			{
				if (!(Settings::Get().probe == m_values))
				{
					m_values = Settings::Get().probe; // from the machine (another panel, DWC, FACTORY RESET)
					for (size_t i = 0; i < NumParams; ++i)
					{
						updateTileText((Param)i);
					}
				}
			});
		updateProbeTile();
		showModeLayout();

		State::AddProbeListener(
			[this]
			{
				updateProbeTile();
				showModeLayout();
			});
		State::AddJobListener([this] { statusChanged(); });
		Messages::SetProbeHooks(
			{[this] { return autoRunning(); },
			 [this](const std::string& t, const std::string& x) { autoMessage(t, x); },
			 [this](const std::string& t, const std::string& x, uint32_t c, bool cancel, uint32_t seq)
			 { return showJogPrompt(t, x, c, cancel, seq); },
			 [this] { return jogPromptOpen(); },
			 [this] { closeJogPrompt(); },
			 [this](const std::string& x) { autoError(x); }});

		m_timer = lv_timer_create(
			[](lv_timer_t* t) { static_cast<WcsProbePage*>(lv_timer_get_user_data(t))->tick(); }, 100, this);
	}

	// ---- Page ----------------------------------------------------------------------------------

	void WcsProbePage::showModeLayout()
	{
		const size_t mode = (size_t)State::GetProbeMode();
		lv_label_set_text(m_modeLabel, ModeNames[mode]);
		for (size_t m = 0; m < 3; ++m)
		{
			for (size_t k = 0; k < m_modeTileCount[m]; ++k)
			{
				m_modeTiles[m][k].tile.show(m == mode);
			}
		}
		const bool manual = (State::GetProbeMode() == State::ProbeMode::Manual);
		m_probeTile.show(!manual); // the cutter is the probe: no signal to show
		m_run.show(!manual);
		m_start.show(manual);
	}

	void WcsProbePage::updateProbeTile()
	{
		if (!State::ProbeReported())
		{
			m_probeTile.setState("-", Colour::Border());
		}
		else
		{
			const bool closed = State::ProbeClosed();
			m_probeTile.setState(closed ? "CLOSED" : "OPEN", closed ? Colour::StopRed() : Colour::Green());
		}
	}

	float* WcsProbePage::valueOf(Param p)
	{
		switch (p)
		{
		case PTip:
			return &m_values.tip;
		case PAutoClear:
			return &m_values.autoClear;
		case PAutoSearch:
			return &m_values.autoSearch;
		case PZDepth:
			return &m_values.zDepth;
		case PSemiClear:
			return &m_values.semiClear;
		case PSemiSearch:
			return &m_values.semiSearch;
		case PToolDia:
			return &m_values.toolDia;
		default:
			return nullptr;
		}
	}

	// The text of a parameter on every tile that shows it
	void WcsProbePage::updateTileText(Param p)
	{
		Settings::SetProbe(m_values); // saved when it changed
		if (p == PStock)
		{
			m_tileText[p] = fmt::format("{:g}x{:g}", m_values.stockX, m_values.stockY);
		}
		else if (p == PStoreTo)
		{
			m_tileText[p] = WcsNames[m_values.storeTo];
		}
		else
		{
			m_tileText[p] = fmt::format("{:.3f}", *valueOf(p));
		}
		for (size_t m = 0; m < 3; ++m)
		{
			for (size_t k = 0; k < m_modeTileCount[m]; ++k)
			{
				if (m_modeTiles[m][k].id == p)
				{
					m_modeTiles[m][k].tile.setValue(m_tileText[p]);
				}
			}
		}
	}

	void WcsProbePage::originPressed(size_t origin)
	{
		if (origin >= NumOrigins)
		{
			return;
		}
		m_originButtons[m_origin].setSelected(false);
		m_origin = origin;
		m_originButtons[m_origin].setSelected(true);
	}

	void WcsProbePage::openNumpad(const char* tag, float value, float min, float max, std::function<void(float)> onOk)
	{
		Popup::NumpadSpec pad;
		pad.tag = tag;
		pad.unit = "mm";
		pad.value = value;
		pad.decimals = 3;
		pad.allowDecimal = true;
		pad.allowMinus = false;
		pad.min = min;
		pad.max = max;
		pad.onOk = std::move(onOk);
		Popup::Numpad(pad);
	}

	void WcsProbePage::paramPressed(Param p)
	{
		auto set = [this, p](float v)
		{
			*valueOf(p) = v;
			updateTileText(p);
		};
		switch (p)
		{
		case PTip:
			openNumpad("TIP \xC3\x98", m_values.tip, 0.1f, 20.0f, set);
			break;
		case PAutoClear:
		case PSemiClear:
			openNumpad("CLEAR", *valueOf(p), 0.1f, 100.0f, set);
			break;
		case PAutoSearch:
		case PSemiSearch:
			openNumpad("SEARCH", *valueOf(p), 0.5f, 200.0f, set);
			break;
		case PZDepth:
			openNumpad("Z DEPTH", m_values.zDepth, 0.1f, 50.0f, set);
			break;
		case PToolDia:
			openNumpad("TOOL \xC3\x98", m_values.toolDia, 0.1f, 50.0f, set);
			break;
		case PStock: // X, then the Y size
			openNumpad("STOCK X",
					   m_values.stockX,
					   1.0f,
					   2000.0f,
					   [this](float x)
					   {
						   m_values.stockX = x;
						   updateTileText(PStock);
						   openNumpad("STOCK Y",
									  m_values.stockY,
									  1.0f,
									  2000.0f,
									  [this](float yv)
									  {
										  m_values.stockY = yv;
										  updateTileText(PStock);
									  });
					   });
			break;
		case PStoreTo:
			Popup::Choose("STORE TO",
						  std::vector<std::string>(std::begin(WcsNames), std::end(WcsNames)),
						  m_values.storeTo,
						  [this](size_t choice)
						  {
							  m_values.storeTo = (uint8_t)choice;
							  updateTileText(PStoreTo);
						  });
			break;
		default:
			break;
		}
	}

	void WcsProbePage::runPressed()
	{
		const State::ProbeMode mode = State::GetProbeMode();
		if (State::JobActive())
		{
			Popup::Refuse(LockedJob);
			return;
		}
		if (State::Status() != OM::PrinterStatus::idle)
		{
			Popup::Refuse("Wait until the machine is idle."); // e.g. a probe macro still running
			return;
		}
		if (!State::XyzHomed())
		{
			Popup::Refuse("Home X, Y and Z first.");
			return;
		}
		if (mode != State::ProbeMode::Manual && State::ProbeClosed())
		{
			Popup::Refuse("Probe is closed (triggered).\nCheck the probe and its wiring.");
			return;
		}
		const char* code = OriginCodes[m_origin];
		m_runOrigin = m_origin;
		const unsigned int w = m_values.storeTo + 1u;
		switch (mode)
		{
		case State::ProbeMode::Auto:
			State::Send(fmt::format("M98 P\"0:/sys/probe/probe_auto.g\" O\"{:s}\" D{:.3f} C{:.3f} S{:.3f} Z{:.3f} "
									"L{:.3f} B{:.3f} W{:d}\n",
									code,
									m_values.tip,
									m_values.autoClear,
									m_values.autoSearch,
									m_values.zDepth,
									m_values.stockX,
									m_values.stockY,
									w));
			openAutoPopup(); // the machine now probes on its own
			break;
		case State::ProbeMode::Semi:
			State::Send(fmt::format("M98 P\"0:/sys/probe/probe_semi.g\" O\"{:s}\" D{:.3f} C{:.3f} S{:.3f} W{:d}\n",
									code,
									m_values.tip,
									m_values.semiClear,
									m_values.semiSearch,
									w));
			break;
		default:
			State::Send(fmt::format(
				"M98 P\"0:/sys/probe/probe_manual.g\" O\"{:s}\" D{:.3f} W{:d}\n", code, m_values.toolDia, w));
			break;
		}
	}

	// ---- Auto popup ----------------------------------------------------------------------------

	bool WcsProbePage::autoRunning() const
	{
		return Popup::WindowId() == AutoWindow;
	}

	// Title tile shared by both popups: "PROBE" + the origin glyph (or another title over the whole tile)
	static void titleTile(lv_obj_t* box, const char* text, bool withGlyph)
	{
		lv_obj_t* tile = winTile(box, TitleX, TitleY, TitleW, TitleH, Colour::Accent());
		(void)tile;
		const int32_t textTop = TitleY + (TitleH - 32) / 2 + 2;
		if (withGlyph)
		{
			winLabel(box, TitleX + 12, textTop, 150, 32, Font::Large(), Colour::Text(), LV_TEXT_ALIGN_CENTER, text);
			s_titleGlyph = {&s_titleOrigin, false};
			glyphObj(box, WX(TitleX + TitleW - 60), WY(TitleY + 4), WW(TitleX + TitleW - 60, 38), &s_titleGlyph);
		}
		else
		{
			winLabel(
				box, TitleX + 6, textTop, TitleW - 12, 32, Font::Large(), Colour::Text(), LV_TEXT_ALIGN_CENTER, text);
		}
	}

	void WcsProbePage::openAutoPopup()
	{
		lv_obj_t* box = Popup::OpenWindow(AutoWindow, PopX, PopY, PopW, PopH);
		m_autoBusySeen = false;
		m_autoStartTime = State::Now();
		s_titleOrigin = (Origin)m_runOrigin;
		titleTile(box, "PROBE", true);

		const lv_color_t accent = Colour::Accent();
		constexpr int32_t ix = 25, iy = 72, iw = PopW - 50, ih = PopBtnY - 12 - iy;
		winTile(box, ix, iy, iw, ih, Colour::Border());
		// [side icon] head line
		m_autoSide = (Origin)m_origin;
		s_autoSideGlyph = {&m_autoSide, false};
		m_autoSideGlyph = glyphObj(box, WX(ix + 12), WY(iy + 8), WW(ix + 12, 36), &s_autoSideGlyph);
		m_autoHeadLabel =
			winLabel(box, ix + 58, iy + 16, iw - 78, 21, Font::Small(), Colour::Text(), LV_TEXT_ALIGN_LEFT, "");
		// status line (amber: in progress)
		m_autoStatusLabel = winLabel(box, ix + 20, iy + 50, iw - 40, 21, Font::Small(), Amber, LV_TEXT_ALIGN_LEFT, "");
		// divider
		lv_obj_t* div = CreateBare(box, WX(ix + 20), WY(iy + 84), WW(ix + 20, iw - 40), WH(iy + 84, 2));
		lv_obj_set_style_bg_color(div, Colour::Border(), 0);
		lv_obj_set_style_bg_opa(div, LV_OPA_COVER, 0);
		// axis rows: letter, value, check mark
		for (size_t r = 0; r < 2; ++r)
		{
			const int32_t ry = iy + 94 + (int32_t)r * 40;
			m_autoRowLetter[r] = winLabel(box, ix + 22, ry, 30, 32, Font::Large(), accent, LV_TEXT_ALIGN_LEFT, "");
			m_autoRowValueLabel[r] =
				winLabel(box, ix + 60, ry, 150, 32, Font::Large(), Colour::Text(), LV_TEXT_ALIGN_RIGHT, "");
			m_autoRowCheck[r] = CreateBare(box, WX(ix + 222), WY(ry + 4), WW(ix + 222, 24), WH(ry + 4, 24));
			lv_obj_add_event_cb(
				m_autoRowCheck[r],
				[](lv_event_t* e)
				{
					lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
					lv_area_t a;
					lv_obj_get_coords(obj, &a);
					Pen pen(lv_event_get_layer(e), a.x1 + lv_area_get_width(&a) / 2, a.y1 + lv_area_get_height(&a) / 2);
					DrawCheckGlyph(pen, 24, Colour::Green());
				},
				LV_EVENT_DRAW_MAIN_END,
				nullptr);
		}
		// where the result goes
		winLabel(box,
				 ix + 20,
				 iy + ih - 30,
				 iw - 40,
				 21,
				 Font::Small(),
				 Colour::Muted(),
				 LV_TEXT_ALIGN_LEFT,
				 fmt::format("Result goes to {:s}", WcsNames[m_values.storeTo]).c_str());

		// Wide emergency STOP instead of X: a running macro can only be stopped this way
		lv_obj_t* stop =
			CreateBare(box, WX((PopW - StopW) / 2), WY(PopBtnY), WW((PopW - StopW) / 2, StopW), WH(PopBtnY, PopBtnH));
		StyleCard(stop, Colour::StopRed(), Colour::StopRed(), false);
		lv_obj_set_style_bg_color(stop, Colour::StopRedPressed(), LV_STATE_PRESSED);
		lv_obj_add_flag(stop, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_event_cb(
			stop,
			[](lv_event_t* e)
			{
				lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
				lv_area_t a;
				lv_obj_get_coords(obj, &a);
				Pen pen(lv_event_get_layer(e), a.x1 + lv_area_get_width(&a) / 2, a.y1 + lv_area_get_height(&a) / 2);
				const bool pressed = lv_obj_has_state(obj, LV_STATE_PRESSED);
				DrawStopIcon(pen, Colour::StopGlyph(), pressed ? Colour::StopRedPressed() : Colour::StopRed());
			},
			LV_EVENT_DRAW_MAIN_END,
			nullptr);
		lv_obj_add_event_cb(
			stop, [](lv_event_t*) { Popup::InEvent([] { Popup::EmergencyStop(); }); }, LV_EVENT_PRESSED, nullptr);

		resetAutoTile();
	}

	// Start state of the info tile: the origin, "Probing ...", the axes the origin measures
	void WcsProbePage::resetAutoTile()
	{
		const Origin o = (Origin)m_origin;
		m_autoSide = o;
		m_autoHead = OriginNames[m_origin];
		lv_label_set_text(m_autoHeadLabel, m_autoHead.c_str());
		m_autoStatus = "Probing ...";
		lv_label_set_text(m_autoStatusLabel, m_autoStatus.c_str());
		switch (o)
		{
		case Origin::ML:
		case Origin::MR:
			m_autoRowAxis[0] = 'X';
			m_autoRowAxis[1] = 0;
			break;
		case Origin::TC:
		case Origin::BC:
			m_autoRowAxis[0] = 'Y';
			m_autoRowAxis[1] = 0;
			break;
		case Origin::ZTop:
			m_autoRowAxis[0] = 'Z';
			m_autoRowAxis[1] = 0;
			break;
		default:
			m_autoRowAxis[0] = 'X';
			m_autoRowAxis[1] = 'Y';
			break;
		}
		for (size_t r = 0; r < 2; ++r)
		{
			const bool used = (m_autoRowAxis[r] != 0);
			const char letter[2] = {m_autoRowAxis[r], 0};
			lv_label_set_text(m_autoRowLetter[r], letter);
			m_autoRowValue[r] = "-";
			lv_label_set_text(m_autoRowValueLabel[r], "-");
			show(m_autoRowLetter[r], used);
			show(m_autoRowValueLabel[r], used);
			show(m_autoRowCheck[r], false);
		}
	}

	void WcsProbePage::autoMessage(const std::string& title, const std::string& text)
	{
		if (!autoRunning())
		{
			return;
		}
		if (title.rfind("PROBE:", 0) == 0 && title.size() > 6)
		{
			// "PROBE:<side> <n>/<m>": side icon + "Side n of m", the text is the status line
			const Origin side = sideGlyph(title[6]);
			if (side != Origin::None)
			{
				m_autoSide = side;
				lv_obj_invalidate(m_autoSideGlyph);
			}
			int n = 0, m = 0;
			const size_t sp = title.find(' ');
			if (sp != std::string::npos)
			{
				size_t p = sp + 1;
				while (p < title.size() && title[p] >= '0' && title[p] <= '9')
				{
					n = n * 10 + (title[p++] - '0');
				}
				if (p < title.size() && title[p] == '/')
				{
					++p;
					while (p < title.size() && title[p] >= '0' && title[p] <= '9')
					{
						m = m * 10 + (title[p++] - '0');
					}
				}
			}
			if (n > 0 && m > 0)
			{
				m_autoHead = fmt::format("Side {:d} of {:d}", n, m);
				lv_label_set_text(m_autoHeadLabel, m_autoHead.c_str());
			}
			m_autoStatus = text;
			lv_label_set_text(m_autoStatusLabel, text.c_str());
		}
		else if (title.rfind("PROBE=", 0) == 0 && title.size() > 6)
		{
			// "PROBE=<axis>": the measured value of that axis, with a check mark
			for (size_t r = 0; r < 2; ++r)
			{
				if (m_autoRowAxis[r] == title[6])
				{
					m_autoRowValue[r] = text;
					lv_label_set_text(m_autoRowValueLabel[r], text.c_str());
					show(m_autoRowCheck[r], true);
				}
			}
		}
		else
		{
			m_autoStatus = text;
			lv_label_set_text(m_autoStatusLabel, text.c_str());
		}
	}

	void WcsProbePage::autoError(const std::string& text)
	{
		if (autoRunning())
		{
			Popup::Close();
			Popup::Alert(text);
		}
	}

	void WcsProbePage::statusChanged()
	{
		if (!autoRunning())
		{
			return;
		}
		const OM::PrinterStatus status = State::Status();
		if (status == OM::PrinterStatus::busy)
		{
			m_autoBusySeen = true;
		}
		else if (status == OM::PrinterStatus::idle && m_autoBusySeen)
		{
			Popup::Close(); // the macro ended without a result message
		}
	}

	// The macro never showed up as running (too short, or not started): don't leave the popup open with
	// only an emergency STOP to get out of it
	void WcsProbePage::tick()
	{
		if (autoRunning() && !m_autoBusySeen && State::Now() - m_autoStartTime > AutoStartTimeout &&
			State::Status() == OM::PrinterStatus::idle)
		{
			Popup::Close();
		}
	}

	// ---- Jog prompt ----------------------------------------------------------------------------

	bool WcsProbePage::jogPromptOpen() const
	{
		return Popup::WindowId() == JogWindow;
	}

	void WcsProbePage::closeJogPrompt()
	{
		if (jogPromptOpen())
		{
			Popup::Close();
		}
	}

	std::string WcsProbePage::jogInstruction() const
	{
		if (m_jogSentence)
		{
			return m_jogT1 + " [" + std::string(1, m_jogSideChar) + "] " + m_jogT2;
		}
		return m_jogL2.empty() ? m_jogL1 : m_jogL1 + "\n" + m_jogL2;
	}

	bool WcsProbePage::showJogPrompt(
		const std::string& title, const std::string& text, uint32_t controls, bool withCancel, uint32_t seq)
	{
		// Axis buttons for the axes the prompt allows (X Y Z A)
		m_jogNumAxes = 0;
		const size_t axes = std::max<size_t>(State::AxisCount(), 3);
		for (size_t a = 0; a < 4 && a < axes; ++a)
		{
			if (controls & (1u << a))
			{
				m_jogAxisIndex[m_jogNumAxes++] = (uint8_t)a;
			}
		}
		if (m_jogNumAxes == 0)
		{
			return false; // no axis controls: the standard message popup
		}
		m_jogSeq = seq;
		m_jogWithCancel = withCancel;
		m_jogAxis = 0;
		m_jogStep = DefaultJogStep; // every prompt starts small

		// Probe prompt ("PROBE:<side>" semi-automatic, "READ:<side>" manual) or any other prompt
		const bool semiPrompt = title.rfind("PROBE:", 0) == 0;
		const bool manualPrompt = title.rfind("READ:", 0) == 0;
		m_jogProbePrompt = semiPrompt || manualPrompt;
		const char sideChar = semiPrompt	 ? (title.size() > 6 ? title[6] : 0)
							  : manualPrompt ? (title.size() > 5 ? title[5] : 0)
											 : 0;
		m_jogSide = m_jogProbePrompt ? sideGlyph(sideChar) : Origin::None;
		m_jogSideChar = sideChar;
		m_jogSentence = m_jogProbePrompt && m_jogSide != Origin::None;
		m_jogT1.clear();
		m_jogT2.clear();
		m_jogL1.clear();
		m_jogL2.clear();
		if (m_jogSentence)
		{
			const bool top = (m_jogSide == Origin::ZTop);
			m_jogTitle = "PROBE";
			if (semiPrompt)
			{
				m_jogT1 = "Move near";
				m_jogT2 = top ? "top of stock, press PROBE" : "side of stock, press PROBE";
				m_jogOkLabel = "PROBE";
			}
			else
			{
				m_jogT1 = "Touch";
				m_jogT2 = top ? "top of stock and press READ" : "side of stock and press READ";
				m_jogOkLabel = "READ";
			}
		}
		else
		{
			m_jogTitle = m_jogProbePrompt ? "PROBE" : title;
			m_jogOkLabel = "OK";
			// the text on up to two lines, split at the last space that fits ~34 characters
			size_t cut = text.size();
			if (text.size() > 34)
			{
				cut = 34;
				while (cut > 0 && text[cut] != ' ')
				{
					--cut;
				}
				if (cut == 0)
				{
					cut = 34;
				}
			}
			m_jogL1 = text.substr(0, cut);
			m_jogL2 = (cut < text.size()) ? text.substr(cut + (text[cut] == ' ' ? 1 : 0)) : std::string();
		}
		buildJogPrompt();
		return true;
	}

	void WcsProbePage::buildJogPrompt()
	{
		lv_obj_t* box = Popup::OpenWindow(JogWindow, PopX, PopY, PopW, PopH);
		const lv_color_t accent = Colour::Accent();
		s_titleOrigin = (Origin)m_runOrigin;
		titleTile(box, m_jogTitle.c_str(), m_jogProbePrompt);

		// Instruction: "text1 [side glyph] text2" centred on one line, or two plain lines
		if (m_jogSentence)
		{
			lv_point_t s1, s2;
			lv_text_get_size(&s1, m_jogT1.c_str(), Font::Small(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
			lv_text_get_size(&s2, m_jogT2.c_str(), Font::Small(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
			const int32_t boxW = SL(PopX, PopW) - 2 * CardBorder;
			const int32_t gw = S(36), gap = S(6);
			const int32_t total = s1.x + gap + gw + gap + s2.x;
			const int32_t x0 = (total < boxW - S(20)) ? (boxW - total) / 2 : S(10);
			const int32_t lineH = WH(80, 21);
			CreateLabel(
				box, x0, WY(80), s1.x + 2, lineH, Font::Small(), Colour::Text(), LV_TEXT_ALIGN_LEFT, m_jogT1.c_str());
			s_jogSideGlyph = {&m_jogSide, true};
			glyphObj(box, x0 + s1.x + gap, WY(72), gw, &s_jogSideGlyph);
			CreateLabel(box,
						x0 + s1.x + gap + gw + gap,
						WY(80),
						s2.x + 2,
						lineH,
						Font::Small(),
						Colour::Text(),
						LV_TEXT_ALIGN_LEFT,
						m_jogT2.c_str());
		}
		else
		{
			winLabel(box, 20, 70, PopW - 40, 21, Font::Small(), Colour::Text(), LV_TEXT_ALIGN_CENTER, m_jogL1.c_str());
			winLabel(box, 20, 94, PopW - 40, 21, Font::Small(), Colour::Text(), LV_TEXT_ALIGN_CENTER, m_jogL2.c_str());
		}

		// AXIS, STEP, MOVE
		constexpr int32_t L = 25, W = PopW - 50, labelW = 58;
		winLabel(box, L, 124 + 11, labelW, 21, Font::Small(), Colour::Muted(), LV_TEXT_ALIGN_LEFT, "AXIS");
		winLabel(box, L, 172 + 11, labelW, 21, Font::Small(), Colour::Muted(), LV_TEXT_ALIGN_LEFT, "STEP");
		const int32_t aw = (W - labelW - (int32_t)(m_jogNumAxes - 1) * 8) / (int32_t)m_jogNumAxes;
		for (size_t i = 0; i < 4; ++i)
		{
			if (i >= m_jogNumAxes)
			{
				continue;
			}
			const int32_t x = L + labelW + (int32_t)i * (aw + 8);
			const char name[2] = {AxisLetters[m_jogAxisIndex[i]], 0};
			m_jogAxisButtons[i].create(box, WX(x), WY(124), WW(x, aw), WH(124, 42), name, Font::Small());
			m_jogAxisButtons[i].onPress([this, i] { jogAxisPressed(i); });
			m_jogAxisButtons[i].setSelected(i == m_jogAxis);
		}
		const int32_t sw = (W - labelW - 3 * 8) / 4;
		for (size_t i = 0; i < 4; ++i)
		{
			const int32_t x = L + labelW + (int32_t)i * (sw + 8);
			m_jogStepButtons[i].create(box, WX(x), WY(172), WW(x, sw), WH(172, 42), JogStepNames[i], Font::Small());
			m_jogStepButtons[i].onPress([this, i] { jogStepPressed(i); });
			m_jogStepButtons[i].setSelected(i == m_jogStep);
		}
		const int32_t jw = (W - 12) / 2;
		m_jogMinus.create(box, WX(L), WY(220), WW(L, jw), WH(220, 56), "", Font::Large());
		m_jogPlus.create(box, WX(L + jw + 12), WY(220), WW(L + jw + 12, jw), WH(220, 56), "", Font::Large());
		m_jogMinus.onPress([this] { jogMovePressed(m_jogLeftSign); }); // sign follows JOG DIRECTION
		m_jogPlus.onPress([this] { jogMovePressed(-m_jogLeftSign); });
		updateJogMoveLabels();
		(void)accent;

		// X (M292 P1) and PROBE / READ / OK (M292 P0); without X the OK button is centred
		s_jogCancelHandler = [this] { jogCancelPressed(); };
		s_jogOkHandler = [this] { jogOkPressed(); };
		if (m_jogWithCancel)
		{
			winButton(box, PopCancelX, PopBtnY, PopBtnW, PopBtnH, true, &s_jogCancelHandler);
		}
		const int32_t okX = m_jogWithCancel ? PopOkX : (PopW - PopBtnW) / 2;
		lv_obj_t* ok = winButton(box, okX, PopBtnY, PopBtnW, PopBtnH, false, &s_jogOkHandler);
		CreateLabel(ok,
					0,
					0,
					WW(okX, PopBtnW),
					WH(PopBtnY, PopBtnH),
					Font::Small(),
					Colour::ButtonText(),
					LV_TEXT_ALIGN_CENTER,
					m_jogOkLabel.c_str());
	}

	void WcsProbePage::updateJogMoveLabels()
	{
		const Settings::MoveLook look =
			(m_jogNumAxes > 0) ? Settings::MoveLookFor(AxisLetters[m_jogAxisIndex[m_jogAxis]]) : Settings::MoveLook();
		m_jogLeftSign = look.leftSign;
		m_jogMinus.setText(Settings::MoveText(m_jogLeftSign, JogStepNames[m_jogStep], look.degree).c_str());
		m_jogPlus.setText(Settings::MoveText(-m_jogLeftSign, JogStepNames[m_jogStep], look.degree).c_str());
		m_jogMinus.setIcon(look.left, false);
		m_jogPlus.setIcon(look.right, true);
	}

	void WcsProbePage::jogAxisPressed(size_t i)
	{
		if (!jogPromptOpen() || i >= m_jogNumAxes)
		{
			return;
		}
		m_jogAxis = i;
		for (size_t k = 0; k < m_jogNumAxes; ++k)
		{
			m_jogAxisButtons[k].setSelected(k == i);
		}
		updateJogMoveLabels(); // the axis's arrows
	}

	void WcsProbePage::jogStepPressed(size_t i)
	{
		if (!jogPromptOpen() || i >= 4)
		{
			return;
		}
		m_jogStep = i;
		for (size_t k = 0; k < 4; ++k)
		{
			m_jogStepButtons[k].setSelected(k == i);
		}
		updateJogMoveLabels();
	}

	void WcsProbePage::jogMovePressed(int direction)
	{
		if (!jogPromptOpen() || m_jogNumAxes == 0)
		{
			return;
		}
		// 10: not before the last move has finished. No ALERT: it would replace this prompt while the
		// macro still waits for it; the tap is dropped.
		if (m_jogStep >= FirstGuardedJogStep && !State::GuardedJogAllowed(m_lastGuardedJog, true))
		{
			return;
		}
		const float d = JogSteps[m_jogStep] * (float)direction;
		State::Send(fmt::format(
			"G91\nG1 {:c}{:.3f} F{:d}\nG90\n", AxisLetters[m_jogAxisIndex[m_jogAxis]], d, State::JogFeed()));
	}

	void WcsProbePage::jogOkPressed()
	{
		if (jogPromptOpen())
		{
			State::Send(fmt::format("M292 P0 S{:d}\n", m_jogSeq));
			Popup::Close();
		}
	}

	void WcsProbePage::jogCancelPressed()
	{
		if (jogPromptOpen())
		{
			State::Send(fmt::format("M292 P1 S{:d}\n", m_jogSeq));
			Popup::Close();
		}
	}
} // namespace UI::Cnc
