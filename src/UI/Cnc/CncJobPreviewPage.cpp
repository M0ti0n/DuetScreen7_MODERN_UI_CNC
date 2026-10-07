/*
 * CncJobPreviewPage.cpp
 */

#include "CncJobPreviewPage.h"
#include "CncKeyboard.h"
#include "CncLayout.h"
#include "CncPanel.h"
#include "CncPopup.h"
#include "CncState.h"
#include "CncTheme.h"
#include "ObjectModel/Axis.h"
#include "UI/Components/LVGL/LvObj.h"
#include <cmath>
#include <fmt/format.h>

namespace UI::Cnc
{
	using namespace CncLayout;
	using namespace CncLayout::Preview;

	namespace
	{
		// Info strip, first row (inside its border): name button, warning button, time (room for "1:02:03")
		constexpr int32_t InfoInner = InfoW - 2 * CardBorder; // 580
		constexpr int32_t RowX = 8, RowY = 6, RowH = 44;
		constexpr int32_t TimeW = 120, TimeX = InfoInner - 14 - TimeW;	  // 446
		constexpr int32_t WarnBtnW = 56, WarnBtnX = TimeX - 8 - WarnBtnW; // 382
		constexpr int32_t NameBtnW(bool warning)
		{
			return (warning ? WarnBtnX : TimeX) - 8 - RowX;
		} // 366 / 430
		constexpr int32_t NameTextW(bool warning)
		{
			return NameBtnW(warning) - 2 * CardBorder - 10 - 28;
		}
		constexpr int32_t Row2Y = 56, Row3Y = 86;

		// "120", "20.5", "-0.25"
		std::string num(float v)
		{
			std::string s = fmt::format("{:.2f}", (double)v);
			while (!s.empty() && s.back() == '0')
			{
				s.pop_back();
			}
			if (!s.empty() && s.back() == '.')
			{
				s.pop_back();
			}
			return (s == "-0") ? std::string("0") : s;
		}

		std::string fileName(const std::string& path)
		{
			const size_t cut = path.find_last_of("/:");
			return (cut == std::string::npos) ? path : path.substr(cut + 1);
		}

		std::string wcsName(unsigned int wcs)
		{
			return (wcs <= 6) ? fmt::format("G{:d}", 53 + wcs) : fmt::format("G59.{:d}", wcs - 6);
		}

		std::string labelText(lv_obj_t* l)
		{
			return (l != nullptr && !lv_obj_has_flag(l, LV_OBJ_FLAG_HIDDEN)) ? std::string(lv_label_get_text(l))
																			 : std::string();
		}

		const char* presetName(PreviewView::Preset p)
		{
			switch (p)
			{
			case PreviewView::Preset::Top:
				return "TOP";
			case PreviewView::Preset::Front:
				return "FRONT";
			case PreviewView::Preset::Free:
				return "3D";
			default:
				return "ISO";
			}
		}
	} // namespace

	JobPreviewPage::~JobPreviewPage()
	{
		if (m_timer != nullptr)
		{
			lv_timer_delete(m_timer);
		}
	}

	// ---- Building ------------------------------------------------------------------------------

	void JobPreviewPage::create(lv_obj_t* parent, int32_t parentTop)
	{
		m_top = parentTop;
		m_root = CreateBare(parent, 0, 0, ScreenW, Height);
		lv_obj_remove_flag(m_root, LV_OBJ_FLAG_CLICKABLE);

		// 3D view and its overlays
		m_view.create(m_root, ViewX, 0, ViewW, ViewH, InsetTop, InsetBottom);
		lv_obj_t* v = m_view.obj();
		const int32_t inner = ViewW - 2 * CardBorder;
		const int32_t bx = inner - OverlayInset - ButtonW;
		m_fileLabel = CreateLabel(
			v, OverlayInset, 8, bx - OverlayInset - 8, 30, Font::Small(), Colour::Text(), LV_TEXT_ALIGN_LEFT, "");
		lv_label_set_long_mode(m_fileLabel, LV_LABEL_LONG_MODE_DOTS);
		m_summaryLabel = CreateLabel(
			v, OverlayInset, 40, bx - OverlayInset - 8, 24, Font::Tiny(), Colour::Muted(), LV_TEXT_ALIGN_LEFT, "");
		m_stockLabel = CreateLabel(v,
								   inner - 300 - OverlayInset,
								   ViewH - 2 * CardBorder - 34,
								   300,
								   24,
								   Font::Tiny(),
								   Colour::Muted(),
								   LV_TEXT_ALIGN_RIGHT,
								   "");
		m_messageLabel = CreateLabel(v,
									 20,
									 InsetTop,
									 inner - 40,
									 ViewH - InsetTop - InsetBottom,
									 Font::Small(),
									 Colour::Muted(),
									 LV_TEXT_ALIGN_CENTER,
									 "");
		lv_label_set_long_mode(m_messageLabel, LV_LABEL_LONG_MODE_WRAP);
		lv_obj_set_style_text_line_space(m_messageLabel, 8, 0);
		lv_obj_add_flag(m_messageLabel, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_event_cb(
			m_messageLabel,
			[](lv_event_t* e) { static_cast<JobPreviewPage*>(lv_event_get_user_data(e))->pressMessage(); },
			LV_EVENT_CLICKED,
			this);

		m_presetBtn.create(v, bx, 8, ButtonW, ButtonH, "ISO", Font::Small());
		m_presetBtn.onPress([this] { pressPreset(); });
		m_rapidBtn.create(v, bx, 8 + ButtonH + ButtonGap, ButtonW, ButtonH, "G0", Font::Small());
		m_rapidBtn.onPress([this] { pressRapids(); });
		m_view.onDoubleTap([this] { doubleTap(); });
		m_view.onPresetChanged([this] { m_presetBtn.setText(presetName(m_view.preset())); });

		createStrip(m_root);
		createInfo(m_root);

		m_timer = lv_timer_create(
			[](lv_timer_t* t) { static_cast<JobPreviewPage*>(lv_timer_get_user_data(t))->tick(); }, 100, this);
		apply(PreviewSource::Current());
	}

	void JobPreviewPage::createStrip(lv_obj_t* root)
	{
		m_up.create(root, StripX, 0, StripW, ArrowH, true);
		m_up.onPress([this] { pressUp(); });
		m_down.create(root, StripX, ViewH - ArrowH, StripW, ArrowH, false);
		m_down.onPress([this] { pressDown(); });
		for (int k = 0; k < NumOpButtons; ++k)
		{
			OpButton& b = m_ops[(size_t)k];
			const int32_t y = ArrowH + Gap + k * (OpH + Gap);
			b.card = CreateBare(root, StripX, y, StripW, OpH);
			StyleCard(b.card, Colour::Tile(), Colour::Border());
			lv_obj_add_flag(b.card, LV_OBJ_FLAG_CLICKABLE);
			const int32_t w = StripW - 2 * CardBorder;
			b.number = CreateLabel(b.card, 0, 6, w, 38, Font::Large(), Colour::Text(), LV_TEXT_ALIGN_CENTER, "");
			b.name[0] = CreateLabel(b.card, 0, 46, w, 24, Font::Tiny(), Colour::Text(), LV_TEXT_ALIGN_CENTER, "");
			b.name[1] = CreateLabel(b.card, 0, 68, w, 24, Font::Tiny(), Colour::Text(), LV_TEXT_ALIGN_CENTER, "");
			b.tool = CreateLabel(
				b.card, 0, OpH - 2 * CardBorder - 28, w, 24, Font::Tiny(), Colour::Muted(), LV_TEXT_ALIGN_CENTER, "");
			// running operation: a dot in the top right corner
			b.dot = CreateBare(b.card, w - 18, 8, 10, 10);
			lv_obj_set_style_bg_opa(b.dot, LV_OPA_COVER, 0);
			lv_obj_set_style_radius(b.dot, LV_RADIUS_CIRCLE, 0);
			lv_obj_add_flag(b.dot, LV_OBJ_FLAG_HIDDEN);
			// warning: an amber "!" in the top left corner
			// warnings: a small triangle in the top left corner (colours: text colour = fill, bg colour = mark)
			b.warn = CreateBare(b.card, 4, 6, 26, 24);
			lv_obj_remove_flag(b.warn, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_event_cb(
				b.warn,
				[](lv_event_t* e)
				{
					lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
					lv_area_t a;
					lv_obj_get_coords(obj, &a);
					DrawWarningIcon(lv_event_get_layer(e),
									a.x1 + lv_area_get_width(&a) / 2,
									a.y1 + lv_area_get_height(&a) / 2,
									22,
									lv_obj_get_style_text_color(obj, LV_PART_MAIN),
									lv_obj_get_style_bg_color(obj, LV_PART_MAIN));
				},
				LV_EVENT_DRAW_MAIN_END,
				nullptr);
			lv_obj_add_flag(b.warn, LV_OBJ_FLAG_HIDDEN);
			lv_obj_add_event_cb(
				b.card,
				[](lv_event_t* e)
				{
					auto* self = static_cast<JobPreviewPage*>(lv_event_get_user_data(e));
					lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
					for (int i = 0; i < NumOpButtons; ++i)
					{
						if (self->m_ops[(size_t)i].card == target)
						{
							self->pressOp(i);
						}
					}
				},
				LV_EVENT_PRESSED,
				this);
		}
	}

	void JobPreviewPage::createInfo(lv_obj_t* root)
	{
		m_info = CreateBare(root, Left, InfoY, InfoW, InfoH);
		StyleCard(m_info, Colour::Tile(), Colour::Border());
		const int32_t x0 = 14, c1 = 264, c2 = 424;
		const int32_t inner = InfoW - 2 * CardBorder;

		// First row: [n  NAME  i] [warning] time
		m_infoNameBtn = CreateBare(m_info, RowX, RowY, NameBtnW(false), RowH);
		StyleCard(m_infoNameBtn, Colour::Tile(), Colour::Border());
		lv_obj_add_flag(m_infoNameBtn, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_event_cb(
			m_infoNameBtn,
			[](lv_event_t* e) { static_cast<JobPreviewPage*>(lv_event_get_user_data(e))->pressInfo(); },
			LV_EVENT_CLICKED,
			this);
		m_infoName = CreateLabel(m_infoNameBtn, 10, 0, 100, 38, Font::Large(), Colour::Text(), LV_TEXT_ALIGN_LEFT, "");
		lv_label_set_long_mode(m_infoName, LV_LABEL_LONG_MODE_DOTS);
		m_infoMore = CreateLabel(m_infoNameBtn,
								 0,
								 (RowH - 2 * CardBorder - lv_font_get_line_height(Font::Small())) / 2,
								 16,
								 lv_font_get_line_height(Font::Small()),
								 Font::Small(),
								 Colour::Muted(),
								 LV_TEXT_ALIGN_CENTER,
								 "i");

		m_infoWarnBtn = CreateBare(m_info, WarnBtnX, RowY, WarnBtnW, RowH);
		StyleCard(m_infoWarnBtn, Colour::Tile(), Colour::Amber());
		lv_obj_add_flag(m_infoWarnBtn, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_event_cb(
			m_infoWarnBtn,
			[](lv_event_t* e)
			{
				lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
				lv_area_t a;
				lv_obj_get_coords(obj, &a);
				DrawWarningIcon(lv_event_get_layer(e),
								a.x1 + lv_area_get_width(&a) / 2,
								a.y1 + lv_area_get_height(&a) / 2 + 1,
								30,
								Colour::Amber(),
								Colour::DarkText());
			},
			LV_EVENT_DRAW_MAIN_END,
			nullptr);
		lv_obj_add_event_cb(
			m_infoWarnBtn,
			[](lv_event_t* e) { static_cast<JobPreviewPage*>(lv_event_get_user_data(e))->pressWarnings(); },
			LV_EVENT_CLICKED,
			this);
		lv_obj_add_flag(m_infoWarnBtn, LV_OBJ_FLAG_HIDDEN);
		m_infoTime =
			CreateLabel(m_info, TimeX, RowY + 8, TimeW, 30, Font::Small(), Colour::Muted(), LV_TEXT_ALIGN_RIGHT, "");

		// Rows 2 and 3
		m_infoTool =
			CreateLabel(m_info, x0, Row2Y, c1 - x0 - 8, 30, Font::Small(), Colour::Text(), LV_TEXT_ALIGN_LEFT, "");
		lv_label_set_long_mode(m_infoTool, LV_LABEL_LONG_MODE_DOTS);
		m_infoZ =
			CreateLabel(m_info, x0, Row3Y, c1 - x0 - 8, 30, Font::Small(), Colour::Text(), LV_TEXT_ALIGN_LEFT, "");
		m_infoSLabel = CreateLabel(m_info, c1, Row2Y, 24, 30, Font::Small(), Colour::Muted(), LV_TEXT_ALIGN_LEFT, "S");
		m_infoS = CreateLabel(
			m_info, c1 + 24, Row2Y, c2 - c1 - 32, 30, Font::Small(), Colour::Text(), LV_TEXT_ALIGN_LEFT, "");
		m_infoFLabel = CreateLabel(m_info, c1, Row3Y, 24, 30, Font::Small(), Colour::Muted(), LV_TEXT_ALIGN_LEFT, "F");
		m_infoF = CreateLabel(
			m_info, c1 + 24, Row3Y, c2 - c1 - 32, 30, Font::Small(), Colour::Text(), LV_TEXT_ALIGN_LEFT, "");
		m_infoCoolant =
			CreateLabel(m_info, c2, Row2Y, inner - c2 - x0, 30, Font::Small(), Colour::Cyan(), LV_TEXT_ALIGN_LEFT, "");
		m_infoWcs =
			CreateLabel(m_info, c2, Row3Y, inner - c2 - x0, 30, Font::Small(), Colour::Text(), LV_TEXT_ALIGN_LEFT, "");
	}

	// ---- Data ----------------------------------------------------------------------------------

	std::string JobPreviewPage::wantedPath() const
	{
		if (State::JobActive() && !m_jobFile.empty())
		{
			return m_jobFile;
		}
		return m_listPath ? m_listPath() : std::string();
	}

	void JobPreviewPage::setShown(bool shown)
	{
		m_shown = shown;
		if (shown)
		{
			tick();
		}
	}

	void JobPreviewPage::setJobFile(const std::string& path)
	{
		if (!path.empty())
		{
			m_jobFile = path;
		}
	}

	void JobPreviewPage::setJobPosition(uint32_t filePosition)
	{
		m_jobPosition = filePosition;
		if (!m_program || !State::JobActive() || PreviewSource::Current().path != m_jobFile)
		{
			if (m_running != -1)
			{
				m_running = -1;
				updateStrip();
			}
			return;
		}
		const int op = Gcode::OperationAt(*m_program, filePosition);
		if (op != m_running)
		{
			m_running = op;
			if (op >= 0 && (!m_manual || State::Now() - m_manualAt > ManualSelectHoldMs))
			{
				select(op, false);
			}
			updateStrip();
		}
	}

	void JobPreviewPage::tick()
	{
		if (m_shown)
		{
			const std::string path = wantedPath();
			if (path != m_requested)
			{
				m_requested = path;
				PreviewSource::Request(path);
			}
		}
		if (PreviewSource::Generation() != m_generation)
		{
			m_generation = PreviewSource::Generation();
			apply(PreviewSource::Current());
		}
	}

	void JobPreviewPage::apply(const PreviewSource::Result& r)
	{
		UI_LOCK();
		m_status = r.status;
		const std::string name = fileName(r.path);
		switch (r.status)
		{
		case PreviewSource::Status::NoFile:
			showMessage("Select a job in JOB LIST");
			break;
		case PreviewSource::Status::NeedsNetwork:
			showMessage("PREVIEW needs a network connection to the machine (WiFi or Ethernet)");
			break;
		case PreviewSource::Status::Loading:
			showMessage(fmt::format("Reading {:s} ...", name));
			break;
		case PreviewSource::Status::Failed:
			showMessage(r.passwordRejected ? std::string("The machine rejected the password. Tap to enter it.")
										   : fmt::format("Cannot read {:s}: {:s}. Tap to try again.", name, r.message));
			break;
		case PreviewSource::Status::TooLarge:
			showMessage(fmt::format("{:s} is too large for PREVIEW (over {:d} MB)",
									name,
									(int)(PreviewSource::MaxFileSize / (1024 * 1024))));
			break;
		case PreviewSource::Status::Empty:
			showMessage(fmt::format("No moves in {:s}", name));
			break;
		case PreviewSource::Status::Ready:
			showMessage("");
			break;
		}
		const bool ready = r.status == PreviewSource::Status::Ready;
		m_program = ready ? r.program : nullptr;
		m_selected = 0;
		m_first = 0;
		m_running = -1;
		m_manual = false;
		m_view.setProgram(m_program);
		lv_label_set_text(m_fileLabel, r.path.empty() ? "" : name.c_str());
		updateOverlays();
		updateStrip();
		updateInfo();
		if (ready)
		{
			setJobPosition(m_jobPosition); // a running job: its operation
		}
	}

	void JobPreviewPage::showMessage(const std::string& text)
	{
		m_message = text;
		lv_label_set_text(m_messageLabel, text.c_str());
		if (text.empty())
		{
			lv_obj_add_flag(m_messageLabel, LV_OBJ_FLAG_HIDDEN);
		}
		else
		{
			lv_obj_remove_flag(m_messageLabel, LV_OBJ_FLAG_HIDDEN);
		}
	}

	void JobPreviewPage::updateOverlays()
	{
		if (!m_program)
		{
			lv_label_set_text(m_summaryLabel, "");
			lv_label_set_text(m_stockLabel, "");
			return;
		}
		const Gcode::Program& p = *m_program;
		lv_label_set_text(
			m_summaryLabel,
			fmt::format("{:d} OPS · {:d} TOOLS · {:s}", p.ops.size(), p.toolCount, Gcode::FormatTime(p.seconds))
				.c_str());
		const Gcode::Box& b = p.stock;
		lv_label_set_text(m_stockLabel,
						  b.empty ? ""
								  : fmt::format("{:s} {:s} × {:s} × {:s}",
												p.stockFromFile ? "STOCK" : "TOOLPATH",
												num(b.sizeX()),
												num(b.sizeY()),
												num(b.sizeZ()))
										.c_str());
	}

	void JobPreviewPage::updateStrip()
	{
		const int n = m_program ? (int)m_program->ops.size() : 0;
		m_up.setDisabled(m_first <= 0);
		m_down.setDisabled(m_first + NumOpButtons >= n);
		const lv_color_t accent = Colour::Accent();
		for (int k = 0; k < NumOpButtons; ++k)
		{
			OpButton& b = m_ops[(size_t)k];
			const int i = m_first + k;
			if (i >= n)
			{
				lv_obj_add_flag(b.card, LV_OBJ_FLAG_HIDDEN);
				b.text.clear();
				continue;
			}
			lv_obj_remove_flag(b.card, LV_OBJ_FLAG_HIDDEN);
			const Gcode::Operation& o = m_program->ops[(size_t)i];
			const auto shortName = Gcode::ShortName(o.name);
			const std::string tool = (o.tool >= 0) ? fmt::format("T{:d}", o.tool) : std::string();
			lv_label_set_text(b.number, fmt::format("{:d}", i + 1).c_str());
			lv_label_set_text(b.name[0], shortName[0].c_str());
			lv_label_set_text(b.name[1], shortName[1].c_str());
			lv_label_set_text(b.tool, tool.c_str());
			b.text = fmt::format("{:d}|{:s}|{:s}|{:s}", i + 1, shortName[0], shortName[1], tool);
			// one name row: centred between the number and the tool
			lv_obj_set_y(b.name[0], shortName[1].empty() ? 57 : 46);
			const bool sel = i == m_selected;
			StyleCard(b.card, sel ? accent : Colour::Tile(), sel ? accent : Colour::Border());
			const lv_color_t fg = sel ? Colour::DarkText() : Colour::Text();
			lv_obj_set_style_text_color(b.number, fg, 0);
			lv_obj_set_style_text_color(b.name[0], fg, 0);
			lv_obj_set_style_text_color(b.name[1], fg, 0);
			lv_obj_set_style_text_color(b.tool, sel ? Colour::DarkText() : Colour::Muted(), 0);
			lv_obj_set_style_bg_color(b.dot, sel ? Colour::DarkText() : accent, 0);
			if (i == m_running)
			{
				lv_obj_remove_flag(b.dot, LV_OBJ_FLAG_HIDDEN);
				b.text += "|RUN";
			}
			else
			{
				lv_obj_add_flag(b.dot, LV_OBJ_FLAG_HIDDEN);
			}
			lv_obj_set_style_text_color(b.warn, sel ? Colour::DarkText() : Colour::Amber(), 0);
			lv_obj_set_style_bg_color(b.warn, sel ? accent : Colour::DarkText(), 0); // bg_opa 0: only the mark uses it
			lv_obj_invalidate(b.warn);
			if (!warnings(i).empty())
			{
				lv_obj_remove_flag(b.warn, LV_OBJ_FLAG_HIDDEN);
				b.text += "|!";
			}
			else
			{
				lv_obj_add_flag(b.warn, LV_OBJ_FLAG_HIDDEN);
			}
		}
	}

	void JobPreviewPage::updateInfo()
	{
		lv_obj_t* const values[] = {
			m_infoTime, m_infoTool, m_infoZ, m_infoSLabel, m_infoS, m_infoFLabel, m_infoF, m_infoCoolant, m_infoWcs};
		if (!m_program || m_selected < 0 || (size_t)m_selected >= m_program->ops.size())
		{
			lv_obj_add_flag(m_infoWarnBtn, LV_OBJ_FLAG_HIDDEN);
			lv_obj_add_flag(m_infoNameBtn, LV_OBJ_FLAG_HIDDEN);
			lv_label_set_text(m_infoName, "");
			for (lv_obj_t* l : values)
			{
				lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
			}
			return;
		}
		for (lv_obj_t* l : values)
		{
			lv_obj_remove_flag(l, LV_OBJ_FLAG_HIDDEN);
		}
		lv_obj_remove_flag(m_infoNameBtn, LV_OBJ_FLAG_HIDDEN);
		const Gcode::Operation& o = m_program->ops[(size_t)m_selected];
		// The warning button only when needed; the name button takes its place otherwise
		const bool warn = !warnings(m_selected).empty();
		if (warn)
		{
			lv_obj_remove_flag(m_infoWarnBtn, LV_OBJ_FLAG_HIDDEN);
		}
		else
		{
			lv_obj_add_flag(m_infoWarnBtn, LV_OBJ_FLAG_HIDDEN);
		}
		lv_obj_set_width(m_infoNameBtn, NameBtnW(warn));
		lv_obj_set_width(m_infoName, NameTextW(warn));
		lv_obj_set_x(m_infoMore, NameBtnW(warn) - 2 * CardBorder - 24);
		std::string upperName = o.name;
		for (char& c : upperName)
		{
			c = (char)std::toupper((unsigned char)c);
		}
		const std::string title = fmt::format("{:d}  {:s}", m_selected + 1, upperName);
		// The full name: in the smaller font when the large one does not fit ("..." only past that)
		lv_point_t size;
		lv_text_get_size(&size, title.c_str(), Font::Large(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
		const bool large = size.x <= NameTextW(warn);
		const lv_font_t* font = large ? Font::Large() : Font::Small();
		lv_obj_set_style_text_font(m_infoName, font, 0);
		lv_obj_set_height(m_infoName, lv_font_get_line_height(font));
		lv_obj_set_y(m_infoName, (RowH - 2 * CardBorder - lv_font_get_line_height(font)) / 2);
		lv_label_set_text(m_infoName, title.c_str());
		lv_label_set_text(m_infoTime, Gcode::FormatTime(o.seconds).c_str());

		std::string tool = (o.tool >= 0) ? fmt::format("T{:d}", o.tool) : std::string("NO TOOL");
		if (o.toolDiameter > 0)
		{
			tool += fmt::format("  Ø{:s}", num(o.toolDiameter));
		}
		if (!o.toolType.empty())
		{
			tool += " " + o.toolType;
		}
		lv_label_set_text(m_infoTool, tool.c_str());
		const std::string z = (std::fabs(o.zMax - o.zMin) < 0.005f)
								  ? fmt::format("Z {:s}", num(o.zMin))
								  : fmt::format("Z {:s} → {:s}", num(o.zMax), num(o.zMin));
		lv_label_set_text(m_infoZ, z.c_str());
		lv_label_set_text(m_infoS,
						  (o.spindle > 0) ? fmt::format("{:s}{:s}", num(o.spindle), o.spindleCcw ? " CCW" : "").c_str()
										  : "-");
		lv_label_set_text(m_infoF, (o.feed > 0) ? num(o.feed).c_str() : "-");
		lv_label_set_text(m_infoCoolant, o.coolant ? "COOLANT" : "COOL OFF");
		lv_obj_set_style_text_color(m_infoCoolant, o.coolant ? Colour::Cyan() : Colour::Muted(), 0);
		lv_label_set_text(m_infoWcs, fmt::format("WCS {:s}", wcsName(o.wcs)).c_str());
	}

	// ---- Touches -------------------------------------------------------------------------------

	void JobPreviewPage::select(int op, bool byHand)
	{
		if (!m_program || op < 0 || (size_t)op >= m_program->ops.size())
		{
			return;
		}
		if (byHand)
		{
			m_manual = true;
			m_manualAt = State::Now();
		}
		m_selected = op;
		if (op < m_first)
		{
			m_first = op;
		}
		else if (op >= m_first + NumOpButtons)
		{
			m_first = op - NumOpButtons + 1;
		}
		m_view.setSelected(op);
		updateStrip();
		updateInfo();
	}

	void JobPreviewPage::pressOp(int k)
	{
		select(m_first + k, true);
	}

	void JobPreviewPage::pressUp()
	{
		if (m_first > 0)
		{
			m_first = std::max(0, m_first - NumOpButtons);
			updateStrip();
		}
	}

	void JobPreviewPage::pressDown()
	{
		const int n = m_program ? (int)m_program->ops.size() : 0;
		if (m_first + NumOpButtons < n)
		{
			m_first = std::min(m_first + NumOpButtons, n - NumOpButtons);
			updateStrip();
		}
	}

	void JobPreviewPage::pressPreset()
	{
		m_view.nextPreset();
		m_presetBtn.setText(presetName(m_view.preset()));
	}

	void JobPreviewPage::pressRapids()
	{
		m_view.setShowRapids(!m_view.showRapids());
		m_rapidBtn.setToggle(m_view.showRapids());
	}

	void JobPreviewPage::doubleTap()
	{
		m_view.setPreset(PreviewView::Preset::Iso);
		m_presetBtn.setText(presetName(m_view.preset()));
	}

	void JobPreviewPage::pressMessage()
	{
		if (m_status == PreviewSource::Status::Failed && PreviewSource::Current().passwordRejected)
		{
			// The machine's password (M551): typed here, kept in DuetScreen's setting, then try again
			Keyboard::Client client;
			client.promptTitle = "MACHINE PASSWORD";
			client.promptText = "The password set with M551, used to download jobs for PREVIEW";
			client.enter = [](const std::string& text)
			{
				Panel::SetDuetPassword(text);
				PreviewSource::Reload();
			};
			Keyboard::Open("", client, KeyboardOwner);
			return;
		}
		if (m_status == PreviewSource::Status::Failed || m_status == PreviewSource::Status::NeedsNetwork)
		{
			PreviewSource::Reload();
		}
	}

	void JobPreviewPage::pressInfo()
	{
		if (!m_program || m_selected < 0 || (size_t)m_selected >= m_program->ops.size())
		{
			return;
		}
		Popup::Info(fmt::format("OPERATION {:d}", m_selected + 1), details(m_selected));
	}

	// ---- Checks and details --------------------------------------------------------------------

	std::vector<JobPreviewPage::Warning> JobPreviewPage::warnings(int op) const
	{
		std::vector<Warning> out;
		if (!m_program || op < 0 || (size_t)op >= m_program->ops.size())
		{
			return out;
		}
		const Gcode::Program& p = *m_program;
		const Gcode::Operation& o = p.ops[(size_t)op];
		// Deeper than the stock: into the spoilboard or the vise (only with a stock size from the file)
		if (p.stockFromFile && o.cutLength > 0 && o.zMin < p.stock.minZ - 0.01f)
		{
			out.push_back({fmt::format("Cuts {:s} mm below the stock bottom", num(p.stock.minZ - o.zMin)),
						   fmt::format("(Z {:s}, stock {:s} mm)", num(o.zMin), num(p.stock.sizeZ()))});
		}
		if (o.cutsWithSpindleOff)
		{
			out.push_back({"Cuts with the spindle off", "(no M3 / M4 before the cutting moves)"});
		}
		// A tool the machine does not have (once the tool table is known)
		if (o.tool >= 0 && !State::Tools().empty() && State::FindTool(o.tool) == nullptr)
		{
			out.push_back(
				{fmt::format("T{:d} is not in the tool table", o.tool), "(WCS > TOOLS lists the machine's tools)"});
		}
		// X / Y travel: machine = work + work offset (Z depends on the tool length offset: not checked)
		if (!o.travel.empty && o.wcs >= 1 && o.wcs <= State::NumWcs)
		{
			std::string axes, limits;
			const char letters[2] = {'X', 'Y'};
			const float lo[2] = {o.travel.minX, o.travel.minY}, hi[2] = {o.travel.maxX, o.travel.maxY};
			for (int a = 0; a < 2; ++a)
			{
				float mn = 0, mx = 0;
				bool known = false;
				size_t stateAxis = 0;
				for (size_t i = 0; i < State::AxisCount(); ++i)
				{
					if (State::AxisLetter(i) == letters[a])
					{
						stateAxis = i;
						break;
					}
				}
				for (size_t slot = 0; slot < OM::Move::GetAxisCount(); ++slot)
				{
					auto axis = OM::Move::GetAxisBySlot(slot);
					if (axis && axis->letter[0] == letters[a])
					{
						mn = axis->minPosition;
						mx = axis->maxPosition;
						known = mx > mn;
						break;
					}
				}
				if (!known)
				{
					continue;
				}
				const float off = State::WorkplaceOffset(stateAxis, o.wcs - 1);
				if (lo[a] + off < mn - 0.01f || hi[a] + off > mx + 0.01f)
				{
					axes += axes.empty() ? std::string(1, letters[a]) : std::string(" and ") + letters[a];
					limits +=
						fmt::format("{:s}{:c} {:s} → {:s}", limits.empty() ? "" : ", ", letters[a], num(mn), num(mx));
				}
			}
			if (!axes.empty())
			{
				out.push_back({fmt::format("Outside the {:s} travel", axes), fmt::format("(machine {:s})", limits)});
			}
		}
		return out;
	}

	std::string JobPreviewPage::details(int op) const
	{
		if (!m_program || op < 0 || (size_t)op >= m_program->ops.size())
		{
			return {};
		}
		const Gcode::Operation& o = m_program->ops[(size_t)op];
		auto length = [](double mm)
		{
			return (mm >= 1000.0) ? fmt::format("{:.1f} m", mm / 1000.0) : fmt::format("{:d} mm", (int)std::lround(mm));
		};
		std::string text;
		if (!o.bounds.empty)
		{
			text += fmt::format("X {:s} → {:s}   Y {:s} → {:s}\n",
								num(o.bounds.minX),
								num(o.bounds.maxX),
								num(o.bounds.minY),
								num(o.bounds.maxY));
			text += (std::fabs(o.zMax - o.zMin) < 0.005f) ? fmt::format("Z {:s}\n", num(o.zMin))
														  : fmt::format("Z {:s} → {:s}\n", num(o.zMax), num(o.zMin));
		}
		text += fmt::format("CUT {:s}   RAPID {:s}\n", length(o.cutLength), length(o.rapidLength));
		if (o.plungeFeed > 0 && std::fabs(o.plungeFeed - o.feed) > 0.5f)
		{
			text += fmt::format("PLUNGE F{:s}\n", num(o.plungeFeed));
		}
		if (o.tool >= 0)
		{
			std::string t = !o.toolText.empty() ? o.toolText : o.toolType;
			for (char& c : t)
			{
				c = (char)std::toupper((unsigned char)c);
			}
			text += t.empty() ? fmt::format("T{:d}\n", o.tool) : fmt::format("T{:d}  {:s}\n", o.tool, t);
		}
		if (!text.empty() && text.back() == '\n')
		{
			text.pop_back();
		}
		return text;
	}

	// Each warning and its reason; an empty line between them while all of it fits (6 lines)
	std::string JobPreviewPage::warningText(int op) const
	{
		const std::vector<Warning> ws = warnings(op);
		const bool spaced = ws.size() * 3 - 1 <= 6;
		std::string text;
		for (size_t i = 0; i < ws.size(); ++i)
		{
			if (i > 0)
			{
				text += spaced ? "\n\n" : "\n";
			}
			text += ws[i].text + "\n" + ws[i].detail;
		}
		return text;
	}

	void JobPreviewPage::pressWarnings()
	{
		const std::vector<Warning> ws = warnings(m_selected);
		if (ws.empty())
		{
			return;
		}
		Popup::Warning(ws.size() > 1 ? "WARNINGS" : "WARNING", warningText(m_selected));
	}

	bool JobPreviewPage::warningButtonShown() const
	{
		return m_infoWarnBtn != nullptr && !lv_obj_has_flag(m_infoWarnBtn, LV_OBJ_FLAG_HIDDEN);
	}

	// ---- For tests -----------------------------------------------------------------------------

	std::string JobPreviewPage::opButtonText(int k) const
	{
		return (k >= 0 && k < NumOpButtons) ? m_ops[(size_t)k].text : std::string();
	}

	std::string JobPreviewPage::infoText() const
	{
		std::string out = labelText(m_infoName);
		for (lv_obj_t* l : {m_infoTime, m_infoTool, m_infoZ, m_infoS, m_infoF, m_infoCoolant, m_infoWcs})
		{
			const std::string t = labelText(l);
			if (!t.empty())
			{
				out += " | " + t;
			}
		}
		return out;
	}

	std::string JobPreviewPage::message() const
	{
		return m_message;
	}

	std::string JobPreviewPage::fileText() const
	{
		return labelText(m_fileLabel);
	}

	std::string JobPreviewPage::stockText() const
	{
		return labelText(m_stockLabel);
	}
} // namespace UI::Cnc
