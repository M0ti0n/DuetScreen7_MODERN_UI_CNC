/*
 * CncWidgets.cpp
 */

#include "CncWidgets.h"
#include "CncGlyphs.h"
#include "CncLayout.h"
#include "CncTheme.h"
#include <algorithm>
#include <cmath>

namespace UI::Cnc
{
	using namespace CncLayout;

	// ---- TextButton ----------------------------------------------------------------------------

	void TextButton::create(
		lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, const char* text, const lv_font_t* font)
	{
		m_obj = CreateBare(parent, x, y, w, h);
		StyleCard(m_obj, Colour::Tile(), Colour::Border());
		lv_obj_add_flag(m_obj, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_set_style_text_font(m_obj, font, 0);

		m_label = lv_label_create(m_obj);
		lv_obj_remove_style_all(m_label); // font and colour come from the button (inherited)
		lv_label_set_long_mode(m_label, LV_LABEL_LONG_MODE_CLIP);
		lv_obj_set_style_text_align(m_label, LV_TEXT_ALIGN_CENTER, 0);
		lv_obj_set_width(m_label, LV_PCT(100));
		lv_obj_center(m_label);
		lv_label_set_text(m_label, text);

		lv_obj_add_event_cb(
			m_obj,
			[](lv_event_t* e)
			{
				auto* self = static_cast<TextButton*>(lv_event_get_user_data(e));
				if (self->m_onPress)
				{
					self->m_onPress();
				}
			},
			LV_EVENT_PRESSED,
			this);
		lv_obj_add_event_cb(m_obj, drawIcon, LV_EVENT_DRAW_MAIN_END, this);
		applyLook();
	}

	void TextButton::setText(const char* text)
	{
		lv_label_set_text(m_label, text);
	}
	const char* TextButton::text() const
	{
		return lv_label_get_text(m_label);
	}

	// Icon: 34 px, 16 px in from the outer edge; text centred between it and 12 px from the other edge
	constexpr int32_t IconSize = 34, IconInset = 16, IconGap = 6, IconTextEdge = 12;

	void TextButton::setIcon(JogIcon icon, bool rightSide)
	{
		if (icon == m_icon && rightSide == m_iconRight)
		{
			return;
		}
		m_icon = icon;
		m_iconRight = rightSide;
		layoutLabel();
		lv_obj_invalidate(m_obj);
	}

	void TextButton::layoutLabel()
	{
		const int32_t w = lv_obj_get_style_width(m_obj, LV_PART_MAIN) - 2 * CardBorder;
		if (m_icon == JogIcon::None)
		{
			lv_obj_set_width(m_label, LV_PCT(100));
			lv_obj_center(m_label);
			return;
		}
		const int32_t a = m_iconRight ? IconTextEdge : IconInset + IconSize + IconGap;
		const int32_t b = m_iconRight ? w - IconInset - IconSize - IconGap : w - IconTextEdge;
		lv_obj_set_width(m_label, b - a);
		lv_obj_align(m_label, LV_ALIGN_LEFT_MID, a, 0);
	}

	void TextButton::drawIcon(lv_event_t* e)
	{
		auto* self = static_cast<TextButton*>(lv_event_get_user_data(e));
		if (self->m_icon == JogIcon::None)
		{
			return;
		}
		lv_area_t a;
		lv_obj_get_coords(self->m_obj, &a);
		const int32_t cx = self->m_iconRight ? a.x2 - IconInset - IconSize / 2 : a.x1 + IconInset + IconSize / 2;
		// the text's colour in the current state (dark while pressed or selected, muted while locked)
		const lv_color_t c = lv_obj_get_style_text_color(self->m_obj, LV_PART_MAIN);
		DrawJogIcon(lv_event_get_layer(e), cx, (a.y1 + a.y2) / 2, IconSize, self->m_icon, c);
	}

	void TextButton::setAlignLeft(int32_t inset)
	{
		lv_obj_set_style_text_align(m_label, LV_TEXT_ALIGN_LEFT, 0);
		lv_obj_set_width(m_label, lv_obj_get_style_width(m_obj, LV_PART_MAIN) - 2 * inset);
		lv_obj_align(m_label, LV_ALIGN_LEFT_MID, inset - CardBorder, 0);
	}

	void TextButton::setGeometry(int32_t x, int32_t w)
	{
		lv_obj_set_x(m_obj, x);
		lv_obj_set_width(m_obj, w);
	}

	void TextButton::show(bool v)
	{
		if (v)
		{
			lv_obj_remove_flag(m_obj, LV_OBJ_FLAG_HIDDEN);
		}
		else
		{
			lv_obj_add_flag(m_obj, LV_OBJ_FLAG_HIDDEN);
		}
	}

	bool TextButton::visible() const
	{
		return !lv_obj_has_flag(m_obj, LV_OBJ_FLAG_HIDDEN);
	}

	void TextButton::setSelected(bool s)
	{
		m_selected = s;
		applyLook();
	}

	void TextButton::setLocked(bool l)
	{
		m_locked = l;
		applyLook();
	}

	void TextButton::setToggle(bool on)
	{
		m_toggle = on;
		applyLook();
	}

	void TextButton::setColours(lv_color_t text, lv_color_t fill)
	{
		m_customColours = true;
		m_text = text;
		m_fill = fill;
		applyLook();
	}

	void TextButton::setFilled(lv_color_t fill)
	{
		m_customColours = true;
		m_customBorder = true;
		m_text = Colour::ButtonText();
		m_fill = fill;
		m_border = fill;
		applyLook();
	}

	void TextButton::resetColours()
	{
		m_customColours = false;
		m_customBorder = false;
		applyLook();
	}

	void TextButton::setPressedLook(bool enable)
	{
		m_pressedLook = enable;
		applyLook();
	}

	void TextButton::setBorderVisible(bool visible)
	{
		lv_obj_set_style_border_width(m_obj, visible ? CardBorder : 0, 0);
	}

	void TextButton::setOutlined(bool on)
	{
		m_outlined = on;
		applyLook();
	}

	void TextButton::applyLook()
	{
		const lv_color_t accent = Colour::Accent();
		lv_color_t fill = m_customColours ? m_fill : Colour::Tile();
		lv_color_t text = m_customColours ? m_text : Colour::Text();
		lv_color_t border = m_customBorder ? m_border : Colour::Border();
		if (m_toggle)
		{
			border = accent;
			text = accent;
		}
		if (m_outlined)
		{
			border = accent;
		}
		if (m_locked)
		{
			text = Colour::Border();
		}
		if (m_selected)
		{
			fill = accent;
			text = Colour::DarkText();
		}
		lv_obj_set_style_bg_color(m_obj, fill, 0);
		lv_obj_set_style_text_color(m_obj, text, 0);
		lv_obj_set_style_border_color(m_obj, border, 0);

		// Finger on it: accent fill with dark text, unless locked or disabled for this button
		const bool pressedLook = m_pressedLook && !m_locked;
		lv_obj_set_style_bg_color(m_obj, pressedLook ? accent : fill, LV_STATE_PRESSED);
		lv_obj_set_style_text_color(m_obj, pressedLook ? Colour::DarkText() : text, LV_STATE_PRESSED);
	}

	// ---- SegmentToggle -------------------------------------------------------------------------

	void SegmentToggle::create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, const char* top,
							   const char* bottom, bool horizontal)
	{
		m_obj = CreateBare(parent, x, y, w, h);
		m_height = h;
		m_width = w;
		m_horizontal = horizontal;
		StyleCard(m_obj, Colour::Tile(), Colour::Border());
		lv_obj_set_style_bg_color(m_obj, Colour::Pressed(), LV_STATE_PRESSED);
		lv_obj_add_flag(m_obj, LV_OBJ_FLAG_CLICKABLE);

		// Active half: inset 4 PanelDue px from the outer edge (the border is 2 of them)
		const int32_t inset = S(4) - CardBorder;
		const int32_t innerW = w - 2 * CardBorder;
		const int32_t innerH = h - 2 * CardBorder;
		if (horizontal)
		{
			const int32_t half = innerW / 2;
			m_fill = CreateBare(m_obj, inset, inset, half - inset, innerH - 2 * inset);
			m_top = CreateLabel(m_obj, 0, 0, half, innerH, Font::Small(), Colour::Text(), LV_TEXT_ALIGN_CENTER, top);
			m_bottom = CreateLabel(
				m_obj, half, 0, innerW - half, innerH, Font::Small(), Colour::Text(), LV_TEXT_ALIGN_CENTER, bottom);
		}
		else
		{
			const int32_t half = innerH / 2;
			m_fill = CreateBare(m_obj, inset, inset, innerW - 2 * inset, half - inset);
			m_top = CreateLabel(m_obj, 0, 0, innerW, half, Font::Small(), Colour::Text(), LV_TEXT_ALIGN_CENTER, top);
			m_bottom = CreateLabel(m_obj,
								   0,
								   half,
								   innerW,
								   innerH - half,
								   Font::Small(),
								   Colour::Text(),
								   LV_TEXT_ALIGN_CENTER,
								   bottom);
		}
		lv_obj_set_style_bg_opa(m_fill, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(m_fill, CardRadius, 0);
		lv_obj_remove_flag(m_fill, LV_OBJ_FLAG_CLICKABLE);

		lv_obj_add_event_cb(
			m_obj,
			[](lv_event_t* e)
			{
				auto* self = static_cast<SegmentToggle*>(lv_event_get_user_data(e));
				if (self->m_onPress)
				{
					self->m_onPress();
				}
			},
			LV_EVENT_PRESSED,
			this);
		applyLook();
	}

	void SegmentToggle::setSecondActive(bool b)
	{
		m_second = b;
		applyLook();
	}

	void SegmentToggle::setLocked(bool l)
	{
		m_locked = l;
		applyLook();
	}

	void SegmentToggle::applyLook()
	{
		const int32_t inset = S(4) - CardBorder;
		if (m_horizontal)
		{
			const int32_t half = (m_width - 2 * CardBorder) / 2;
			lv_obj_set_x(m_fill, m_second ? half : inset);
			lv_obj_set_width(m_fill, half - inset);
		}
		else
		{
			const int32_t half = (m_height - 2 * CardBorder) / 2;
			// PanelDue: top half from the inset to the middle, bottom half from the middle to the inset
			lv_obj_set_y(m_fill, m_second ? half : inset);
			lv_obj_set_height(m_fill, half - inset);
		}
		lv_obj_set_style_bg_color(m_fill, m_locked ? Colour::Border() : Colour::Accent(), 0);
		const lv_color_t idle = m_locked ? Colour::Border() : Colour::Text();
		const lv_color_t active = m_locked ? Colour::Muted() : Colour::DarkText();
		lv_obj_set_style_text_color(m_top, m_second ? idle : active, 0);
		lv_obj_set_style_text_color(m_bottom, m_second ? active : idle, 0);
		lv_obj_set_style_bg_color(m_obj, m_locked ? Colour::Tile() : Colour::Pressed(), LV_STATE_PRESSED);
	}

	// ---- ArrowButton ---------------------------------------------------------------------------

	void ArrowButton::create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, bool up)
	{
		m_up = up;
		m_obj = CreateBare(parent, x, y, w, h);
		StyleCard(m_obj, Colour::Tile(), Colour::Border());
		lv_obj_add_flag(m_obj, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_event_cb(m_obj, draw, LV_EVENT_DRAW_MAIN_END, this);
		lv_obj_add_event_cb(
			m_obj,
			[](lv_event_t* e)
			{
				auto* self = static_cast<ArrowButton*>(lv_event_get_user_data(e));
				if (!self->m_disabled && self->m_onPress)
				{
					self->m_onPress();
				}
			},
			LV_EVENT_PRESSED,
			this);
		setDisabled(false);
	}

	void ArrowButton::setDisabled(bool disabled)
	{
		m_disabled = disabled;
		lv_obj_set_style_bg_color(m_obj, disabled ? Colour::Tile() : Colour::Pressed(), LV_STATE_PRESSED);
		lv_obj_invalidate(m_obj);
	}

	void ArrowButton::show(bool visible)
	{
		if (visible)
		{
			lv_obj_remove_flag(m_obj, LV_OBJ_FLAG_HIDDEN);
		}
		else
		{
			lv_obj_add_flag(m_obj, LV_OBJ_FLAG_HIDDEN);
		}
	}

	// PanelDue: rows growing from the tip, 16 rows, half width 14
	void ArrowButton::draw(lv_event_t* e)
	{
		auto* self = static_cast<ArrowButton*>(lv_event_get_user_data(e));
		lv_area_t a;
		lv_obj_get_coords(self->m_obj, &a);
		Pen pen(lv_event_get_layer(e), a.x1 + lv_area_get_width(&a) / 2, a.y1 + lv_area_get_height(&a) / 2);
		pen.setColour(self->m_disabled ? Colour::Border() : Colour::Text());
		constexpr int HalfW = 14, H = 16;
		for (int r = 0; r < H; ++r)
		{
			const int hw = (HalfW * (r + 1)) / H;
			const int yy = self->m_up ? (-H / 2 + r) : (H / 2 - 1 - r);
			pen.fillRect(-hw, yy, hw, yy);
		}
	}

	// ---- Helpers for the drawn widgets ----

	namespace
	{
		int32_t px(int v)
		{
			return (int32_t)std::lround(v * 1.25);
		} // PanelDue px -> screen px

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
	} // namespace

	// ---- OriginButton --------------------------------------------------------------------------

	void OriginButton::create(
		lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, Origin kind, const char* label, bool outlineStyle)
	{
		m_kind = kind;
		m_label = label;
		m_outlineStyle = outlineStyle;
		m_obj = CreateBare(parent, x, y, w, h);
		StyleCard(m_obj, Colour::Tile(), Colour::Border());
		lv_obj_add_event_cb(m_obj, draw, LV_EVENT_DRAW_MAIN_END, this);
		lv_obj_add_flag(m_obj, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_event_cb(
			m_obj,
			[](lv_event_t* e)
			{
				auto* self = static_cast<OriginButton*>(lv_event_get_user_data(e));
				if (self->m_onPress)
				{
					self->m_onPress();
				}
			},
			LV_EVENT_PRESSED,
			this);
		setSelected(false);
	}

	void OriginButton::setSelected(bool selected)
	{
		m_selected = selected;
		const bool filled = selected && !m_outlineStyle;
		lv_obj_set_style_bg_color(m_obj, filled ? Colour::Accent() : Colour::Tile(), 0);
		lv_obj_set_style_border_color(m_obj, selected ? Colour::Accent() : Colour::Border(), 0);
		lv_obj_invalidate(m_obj);
	}

	void OriginButton::setInside(bool inside)
	{
		if (inside != m_inside)
		{
			m_inside = inside;
			lv_obj_invalidate(m_obj);
		}
	}

	void OriginButton::draw(lv_event_t* e)
	{
		auto* self = static_cast<OriginButton*>(lv_event_get_user_data(e));
		lv_layer_t* layer = lv_event_get_layer(e);
		lv_area_t a;
		lv_obj_get_coords(self->m_obj, &a);
		const bool filled = self->m_selected && !self->m_outlineStyle;
		const lv_color_t bg = filled ? Colour::Accent() : Colour::Tile();
		const lv_color_t glyph = filled ? Colour::DarkText() : Colour::Text();
		if (self->m_selected && self->m_outlineStyle)
		{
			DrawOutline(layer, a, px(3), Colour::Accent()); // 3 px accent outline
		}
		const int32_t cx = a.x1 + lv_area_get_width(&a) / 2;
		if (self->m_label != nullptr)
		{
			Pen pen(layer, cx, a.y1 + px(28));
			DrawOriginGlyph(pen, self->m_kind, glyph, bg, self->m_inside);
			const lv_area_t t = {a.x1 + px(2), a.y2 - px(26), a.x2 - px(2), a.y2 - px(26) + px(22)};
			DrawText(layer, t, self->m_label, Font::Small(), glyph, LV_TEXT_ALIGN_CENTER);
		}
		else
		{
			Pen pen(layer, cx, a.y1 + lv_area_get_height(&a) / 2);
			DrawOriginGlyph(pen, self->m_kind, glyph, bg, self->m_inside);
		}
	}

	// ---- ProbeButton ---------------------------------------------------------------------------

	void ProbeButton::create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, const char* text)
	{
		m_text = text;
		m_obj = CreateBare(parent, x, y, w, h);
		StyleCard(m_obj, Colour::Tile(), Colour::Green());
		lv_obj_set_style_border_width(m_obj, px(3), 0); // 3 px outline in the state colour
		lv_obj_set_style_bg_color(m_obj, Colour::Pressed(), LV_STATE_PRESSED);
		lv_obj_add_flag(m_obj, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_event_cb(m_obj, draw, LV_EVENT_DRAW_MAIN_END, this);
		lv_obj_add_event_cb(
			m_obj,
			[](lv_event_t* e)
			{
				auto* self = static_cast<ProbeButton*>(lv_event_get_user_data(e));
				if (self->m_onPress)
				{
					self->m_onPress();
				}
			},
			LV_EVENT_PRESSED,
			this);
		setSignal(Signal::None);
	}

	void ProbeButton::setSignal(Signal signal)
	{
		m_signal = signal;
		lv_obj_set_style_border_color(m_obj, signal == Signal::Closed ? Colour::StopRed() : Colour::Green(), 0);
		lv_obj_invalidate(m_obj);
	}

	void ProbeButton::show(bool visible)
	{
		showObj(m_obj, visible);
	}

	void ProbeButton::draw(lv_event_t* e)
	{
		auto* self = static_cast<ProbeButton*>(lv_event_get_user_data(e));
		lv_layer_t* layer = lv_event_get_layer(e);
		lv_area_t a;
		lv_obj_get_coords(self->m_obj, &a);
		const lv_color_t colour = (self->m_signal == Signal::Closed) ? Colour::StopRed() : Colour::Green();
		const lv_color_t bg = lv_obj_has_state(self->m_obj, LV_STATE_PRESSED) ? Colour::Pressed() : Colour::Tile();
		const lv_font_t* font = Font::Large();
		lv_point_t size;
		lv_text_get_size(&size, self->m_text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
		const bool dot = (self->m_signal != Signal::None);
		const int32_t dotW = dot ? px(22) + px(14) : 0;		// the dot (diameter 22) and the gap to the text
		const int32_t x0 = a.x1 + (lv_area_get_width(&a) - (dotW + size.x)) / 2;
		if (dot)
		{
			Pen pen(layer, x0 + px(11), a.y1 + lv_area_get_height(&a) / 2);
			pen.setColour(colour);
			pen.fillCircle(0, 0, 9);
			if (self->m_signal == Signal::Open) // a ring: open
			{
				pen.setColour(bg);
				pen.fillCircle(0, 0, 5);
			}
		}
		const lv_area_t t = {x0 + dotW, a.y1, x0 + dotW + size.x + px(2), a.y2};
		DrawText(layer, t, self->m_text.c_str(), font, colour, LV_TEXT_ALIGN_LEFT);
	}

	// ---- ValueTile -----------------------------------------------------------------------------

	void ValueTile::create(
		lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, const char* label, bool stacked, bool dropdown)
	{
		m_label = label;
		m_stacked = stacked;
		m_dropdown = dropdown;
		m_obj = CreateBare(parent, x, y, w, h);
		StyleCard(m_obj, Colour::Tile(), Colour::Border());
		lv_obj_set_style_bg_color(m_obj, Colour::Pressed(), LV_STATE_PRESSED);
		lv_obj_add_event_cb(m_obj, draw, LV_EVENT_DRAW_MAIN_END, this);
		lv_obj_add_flag(m_obj, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_event_cb(
			m_obj,
			[](lv_event_t* e)
			{
				auto* self = static_cast<ValueTile*>(lv_event_get_user_data(e));
				if (self->m_onPress)
				{
					self->m_onPress();
				}
			},
			LV_EVENT_PRESSED,
			this);
	}

	void ValueTile::setValue(const std::string& value)
	{
		m_value = value;
		lv_obj_invalidate(m_obj);
	}

	void ValueTile::show(bool visible)
	{
		showObj(m_obj, visible);
	}

	void ValueTile::draw(lv_event_t* e)
	{
		auto* self = static_cast<ValueTile*>(lv_event_get_user_data(e));
		lv_layer_t* layer = lv_event_get_layer(e);
		lv_area_t a;
		lv_obj_get_coords(self->m_obj, &a);
		const int32_t fh = px(21); // PanelDue glcd19x21 line
		const int32_t valueRight = self->m_dropdown ? a.x2 - px(24) : a.x2 - px(10);
		int32_t mid;
		if (self->m_stacked)
		{
			const lv_area_t l = {a.x1 + px(10), a.y1 + px(8), a.x2 - px(6), a.y1 + px(8) + fh - 1};
			const lv_area_t v = {a.x1 + px(10), a.y2 - px(8) - fh, valueRight, a.y2 - px(8) - 1};
			DrawText(layer, l, self->m_label.c_str(), Font::Small(), Colour::Muted(), LV_TEXT_ALIGN_LEFT);
			DrawText(layer, v, self->m_value.c_str(), Font::Small(), Colour::Text(), LV_TEXT_ALIGN_RIGHT);
			mid = v.y1 + fh / 2;
		}
		else
		{
			const int32_t half = a.x1 + lv_area_get_width(&a) / 2;
			const lv_area_t l = {a.x1 + px(10), a.y1, half, a.y2};
			const lv_area_t v = {half, a.y1, valueRight, a.y2};
			DrawText(layer, l, self->m_label.c_str(), Font::Small(), Colour::Muted(), LV_TEXT_ALIGN_LEFT);
			DrawText(layer, v, self->m_value.c_str(), Font::Small(), Colour::Text(), LV_TEXT_ALIGN_RIGHT);
			mid = a.y1 + lv_area_get_height(&a) / 2;
		}
		if (self->m_dropdown)
		{
			// small down triangle, 10 x 6
			Pen pen(layer, a.x2 - px(17), mid);
			pen.setColour(Colour::Muted());
			for (int r = 0; r < 6; ++r)
			{
				pen.fillRect(-5 + r, -3 + r, 5 - r, -3 + r);
			}
		}
	}

	// ---- TextTile ------------------------------------------------------------------------------

	void TextTile::create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, bool centred)
	{
		m_centred = centred;
		m_textColour = Colour::Text();
		m_obj = CreateBare(parent, x, y, w, h);
		StyleCard(m_obj, Colour::Tile(), Colour::Border());
		lv_obj_add_event_cb(m_obj, draw, LV_EVENT_DRAW_MAIN_END, this);
	}

	void TextTile::setText(const std::string& line1, const std::string& line2)
	{
		if (line1 != m_line1 || line2 != m_line2)
		{
			m_line1 = line1;
			m_line2 = line2;
			lv_obj_invalidate(m_obj);
		}
	}

	void TextTile::setTextColour(lv_color_t colour)
	{
		m_textColour = colour;
		lv_obj_invalidate(m_obj);
	}

	void TextTile::setOutline(lv_color_t colour, int32_t widthPd)
	{
		lv_obj_set_style_border_color(m_obj, colour, 0);
		lv_obj_set_style_border_width(m_obj, px(widthPd), 0);
	}

	void TextTile::setTextRight(int32_t pixels)
	{
		if (pixels != m_textRight)
		{
			m_textRight = pixels;
			lv_obj_invalidate(m_obj);
		}
	}

	void TextTile::show(bool visible)
	{
		showObj(m_obj, visible);
	}

	bool TextTile::visible() const
	{
		return !lv_obj_has_flag(m_obj, LV_OBJ_FLAG_HIDDEN);
	}

	void TextTile::draw(lv_event_t* e)
	{
		auto* self = static_cast<TextTile*>(lv_event_get_user_data(e));
		lv_layer_t* layer = lv_event_get_layer(e);
		lv_area_t a;
		lv_obj_get_coords(self->m_obj, &a);
		// "ABORTED" needs most of the 110 px result tile: centred text has a small inset
		const int32_t inset = px(self->m_centred ? 5 : 14);
		const int32_t x1 = a.x1 + inset, x2 = a.x2 - inset - self->m_textRight;
		const lv_text_align_t align = self->m_centred ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT;
		const int32_t fh = lv_font_get_line_height(Font::Small());
		const bool two = !self->m_line2.empty();
		const int32_t block = two ? 2 * fh + px(4) : fh;
		const int32_t top = a.y1 + (lv_area_get_height(&a) - block) / 2;
		const lv_area_t l1 = {x1, top, x2, top + fh - 1};
		DrawText(layer, l1, self->m_line1.c_str(), Font::Small(), self->m_textColour, align);
		if (two)
		{
			const lv_area_t l2 = {x1, top + fh + px(4), x2, top + 2 * fh + px(4) - 1};
			DrawText(layer, l2, self->m_line2.c_str(), Font::Small(), self->m_textColour, align);
		}
	}

	// ---- FileRow -------------------------------------------------------------------------------

	void FileRow::create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h)
	{
		m_obj = CreateBare(parent, x, y, w, h);
		StyleCard(m_obj, Colour::Tile(), Colour::Border());
		lv_obj_add_event_cb(m_obj, draw, LV_EVENT_DRAW_MAIN_END, this);
		lv_obj_add_flag(m_obj, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_event_cb(
			m_obj,
			[](lv_event_t* e)
			{
				auto* self = static_cast<FileRow*>(lv_event_get_user_data(e));
				if (self->m_onPress)
				{
					self->m_onPress();
				}
			},
			LV_EVENT_PRESSED,
			this);
	}

	void FileRow::setEntry(const std::string& text, const std::string& tag, size_t matchStart, size_t matchLen)
	{
		m_text = text;
		m_tag = tag;
		m_matchStart = matchStart;
		m_matchLen = matchLen;
		lv_obj_invalidate(m_obj);
	}

	void FileRow::setSelected(bool selected)
	{
		m_selected = selected;
		lv_obj_set_style_border_color(m_obj, selected ? Colour::Accent() : Colour::Border(), 0);
	}

	void FileRow::setGeometry(int32_t x, int32_t y, int32_t w, int32_t h)
	{
		lv_obj_set_pos(m_obj, x, y);
		lv_obj_set_size(m_obj, w, h);
	}

	void FileRow::setGridStyle(bool grid)
	{
		m_grid = grid;
		lv_obj_invalidate(m_obj);
	}

	void FileRow::show(bool visible)
	{
		showObj(m_obj, visible);
	}

	bool FileRow::visible() const
	{
		return !lv_obj_has_flag(m_obj, LV_OBJ_FLAG_HIDDEN);
	}

	namespace
	{
		int32_t textWidth(const std::string& t, const lv_font_t* font)
		{
			lv_point_t size;
			lv_text_get_size(&size, t.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
			return size.x;
		}

		// Number of bytes of 'text' that fit in 'width' together with 'suffixW' (whole UTF-8 characters)
		size_t fittingBytes(const std::string& text, int32_t width, int32_t suffixW, const lv_font_t* font)
		{
			size_t n = text.size();
			while (n > 0 && textWidth(text.substr(0, n), font) + suffixW > width)
			{
				do
				{
					--n;
				} while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80);
			}
			return n;
		}
	} // namespace

	void FileRow::draw(lv_event_t* e)
	{
		auto* self = static_cast<FileRow*>(lv_event_get_user_data(e));
		lv_layer_t* layer = lv_event_get_layer(e);
		const lv_font_t* font = Font::Small();
		lv_area_t a;
		lv_obj_get_coords(self->m_obj, &a);

		// Tag on the right (grid: bottom-right corner)
		int32_t textRight = a.x2 - px(12);
		const int32_t fh = lv_font_get_line_height(font);
		int32_t nameDy = 0;
		if (!self->m_tag.empty())
		{
			const int32_t tw = textWidth(self->m_tag, font);
			if (self->m_grid)
			{
				const lv_area_t t = {a.x2 - px(10) - tw, a.y2 - px(6) - fh, a.x2 - px(8), a.y2 - px(6)};
				DrawText(layer, t, self->m_tag.c_str(), font, Colour::Muted(), LV_TEXT_ALIGN_LEFT);
				nameDy = -px(6); // a little above the centre, clear of the tag
			}
			else
			{
				const lv_area_t t = {a.x2 - px(12) - tw, a.y1, a.x2 - px(10), a.y2};
				DrawText(layer, t, self->m_tag.c_str(), font, Colour::Muted(), LV_TEXT_ALIGN_LEFT);
				textRight = a.x2 - px(12) - tw - px(14);
			}
		}
		a.y1 += nameDy;
		a.y2 += nameDy;

		// Name: as much as fits, then ".."; up to three parts (before the match, the match, after it)
		const std::string& text = self->m_text;
		int32_t x1 = a.x1 + px(14);
		const int32_t avail = (textRight > x1) ? textRight - x1 : 0;
		size_t shown = text.size();
		bool dots = false;
		if (textWidth(text, font) > avail)
		{
			shown = fittingBytes(text, avail, textWidth("..", font), font);
			dots = true;
		}
		if (self->m_grid)
		{
			const int32_t w = textWidth(text.substr(0, shown), font) + (dots ? textWidth("..", font) : 0);
			x1 = a.x1 + (lv_area_get_width(&a) - w) / 2; // centred
		}
		const size_t ms = (self->m_matchLen != 0 && self->m_matchStart < shown) ? self->m_matchStart : shown;
		const size_t me = (self->m_matchLen != 0) ? std::min(self->m_matchStart + self->m_matchLen, shown) : shown;
		int32_t x = x1;
		auto part = [&](size_t from, size_t to, lv_color_t colour)
		{
			if (to <= from)
			{
				return;
			}
			const std::string p = text.substr(from, to - from);
			const int32_t w = textWidth(p, font);
			const lv_area_t r = {x, a.y1, x + w + 2, a.y2};
			DrawText(layer, r, p.c_str(), font, colour, LV_TEXT_ALIGN_LEFT);
			x += w;
		};
		part(0, ms, Colour::Text());
		part(ms, me, Colour::Accent());
		part(me, shown, Colour::Text());
		if (dots)
		{
			const lv_area_t r = {x, a.y1, x + textWidth("..", font) + 2, a.y2};
			DrawText(layer, r, "..", font, Colour::Text(), LV_TEXT_ALIGN_LEFT);
		}
	}

	// ---- AlertRow ------------------------------------------------------------------------------

	void AlertRow::create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h)
	{
		m_obj = CreateBare(parent, x, y, w, h);
		StyleCard(m_obj, Colour::Tile(), Colour::Border());
		lv_obj_add_event_cb(m_obj, draw, LV_EVENT_DRAW_MAIN_END, this);
		lv_obj_add_flag(m_obj, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_event_cb(
			m_obj,
			[](lv_event_t* e)
			{
				auto* self = static_cast<AlertRow*>(lv_event_get_user_data(e));
				if (self->m_onPress)
				{
					self->m_onPress();
				}
			},
			LV_EVENT_PRESSED,
			this);
	}

	void AlertRow::setEntry(const std::string& text, bool error, bool unread, const std::string& age)
	{
		if (text != m_text || error != m_error || unread != m_unread || age != m_age)
		{
			m_text = text;
			m_error = error;
			m_unread = unread;
			m_age = age;
			lv_obj_invalidate(m_obj);
		}
	}

	void AlertRow::show(bool visible)
	{
		showObj(m_obj, visible);
	}

	bool AlertRow::visible() const
	{
		return !lv_obj_has_flag(m_obj, LV_OBJ_FLAG_HIDDEN);
	}

	void AlertRow::draw(lv_event_t* e)
	{
		using namespace CncLayout::Alerts;
		auto* self = static_cast<AlertRow*>(lv_event_get_user_data(e));
		lv_layer_t* layer = lv_event_get_layer(e);
		const lv_font_t* font = Font::Small();
		lv_area_t a;
		lv_obj_get_coords(self->m_obj, &a);
		const int32_t h = lv_area_get_height(&a);

		// Badge (white text reads better on red)
		const int32_t bh = px(BadgeH), by = a.y1 + (h - bh) / 2;
		const lv_area_t badge = {a.x1 + px(BadgeDx), by, a.x1 + px(BadgeDx + BadgeW) - 1, by + bh - 1};
		lv_draw_rect_dsc_t rect;
		lv_draw_rect_dsc_init(&rect);
		rect.bg_color = self->m_error ? Colour::StopRed() : Colour::Amber();
		rect.radius = CardRadius;
		lv_draw_rect(layer, &rect, &badge);
		DrawText(layer,
				 badge,
				 self->m_error ? "ERR" : "WARN",
				 font,
				 self->m_error ? Colour::StopGlyph() : Colour::ButtonText(),
				 LV_TEXT_ALIGN_CENTER);

		// Age, right aligned in its column
		const int32_t ageX = a.x1 + px(AgeX - RowX);
		const lv_area_t age = {ageX, a.y1, ageX + px(AgeW) - 1, a.y2};
		DrawText(layer, age, self->m_age.c_str(), font, Colour::Muted(), LV_TEXT_ALIGN_RIGHT);

		// Text, cut to fit before the dot
		const int32_t x1 = a.x1 + px(TextDx), x2 = a.x1 + px(DotDx) - px(10);
		std::string shown = self->m_text;
		if (textWidth(shown, font) > x2 - x1)
		{
			shown = shown.substr(0, fittingBytes(shown, x2 - x1, textWidth("..", font), font)) + "..";
		}
		const lv_area_t t = {x1, a.y1, x2, a.y2};
		DrawText(layer, t, shown.c_str(), font, Colour::Text(), LV_TEXT_ALIGN_LEFT);

		if (self->m_unread)
		{
			const int32_t cx = a.x1 + px(DotDx), cy = a.y1 + h / 2, r = px(DotR);
			const lv_area_t dot = {cx - r, cy - r, cx + r, cy + r};
			lv_draw_rect_dsc_t d;
			lv_draw_rect_dsc_init(&d);
			d.bg_color = Colour::Accent();
			d.radius = LV_RADIUS_CIRCLE;
			lv_draw_rect(layer, &d, &dot);
		}
	}

	// ---- GlyphButton ---------------------------------------------------------------------------

	void GlyphButton::create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, Kind kind)
	{
		m_kind = kind;
		m_obj = CreateBare(parent, x, y, w, h);
		StyleCard(m_obj, Colour::Tile(), Colour::Border());
		lv_obj_add_event_cb(m_obj, draw, LV_EVENT_DRAW_MAIN_END, this);
		lv_obj_add_flag(m_obj, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_event_cb(
			m_obj,
			[](lv_event_t* e)
			{
				auto* self = static_cast<GlyphButton*>(lv_event_get_user_data(e));
				if (self->m_onPress)
				{
					self->m_onPress();
				}
			},
			LV_EVENT_PRESSED,
			this);
		applyLook();
	}

	void GlyphButton::setGlyph(Kind kind, char c)
	{
		m_kind = kind;
		m_char = c;
		applyLook();
	}

	void GlyphButton::setActive(bool active)
	{
		m_active = active;
		applyLook();
	}

	void GlyphButton::show(bool visible)
	{
		showObj(m_obj, visible);
	}

	void GlyphButton::applyLook()
	{
		const bool charKey = (m_kind == Kind::Char);
		lv_obj_set_style_border_color(m_obj, m_active ? Colour::Accent() : Colour::Border(), 0);
		// A character key flashes like the other keys: accent fill and outline while touched
		lv_obj_set_style_bg_color(m_obj, charKey ? Colour::Accent() : Colour::Tile(), LV_STATE_PRESSED);
		lv_obj_set_style_border_color(
			m_obj, (charKey || m_active) ? Colour::Accent() : Colour::Border(), LV_STATE_PRESSED);
		lv_obj_invalidate(m_obj);
	}

	void GlyphButton::draw(lv_event_t* e)
	{
		auto* self = static_cast<GlyphButton*>(lv_event_get_user_data(e));
		lv_layer_t* layer = lv_event_get_layer(e);
		lv_area_t a;
		lv_obj_get_coords(self->m_obj, &a);
		const bool pressed = lv_obj_has_state(self->m_obj, LV_STATE_PRESSED);
		Pen pen(layer, a.x1 + lv_area_get_width(&a) / 2, a.y1 + lv_area_get_height(&a) / 2);
		switch (self->m_kind)
		{
		case Kind::Search:
			DrawSearchGlyph(pen, self->m_active ? Colour::Accent() : Colour::Text(), Colour::Tile());
			break;
		case Kind::Shift:
			DrawShiftGlyph(pen, self->m_active ? Colour::Accent() : Colour::Text(), self->m_active);
			break;
		case Kind::Char:
		{
			const char s[2] = {self->m_char, 0};
			DrawText(layer, a, s, Font::Small(), pressed ? Colour::DarkText() : Colour::Text(), LV_TEXT_ALIGN_CENTER);
			break;
		}
		case Kind::Grid:
			DrawGridGlyph(pen, self->m_active ? Colour::Accent() : Colour::Text());
			break;
		case Kind::List:
			DrawListGlyph(pen, self->m_active ? Colour::Accent() : Colour::Text());
			break;
		case Kind::Keyboard:
			DrawKeyboardIcon(pen, self->m_active ? Colour::Accent() : Colour::Text());
			break;
		}
	}

	// ---- SubTabStrip ---------------------------------------------------------------------------

	void SubTabStrip::create(lv_obj_t* parent, const char* const labels[], unsigned int count)
	{
		m_count = (count < MaxTabs) ? count : MaxTabs;
		m_strip = CreateBare(parent, Margin, SubTabY, ContentW, SubTabH);
		StyleCard(m_strip, Colour::Tile(), Colour::Tile(), false);
		for (unsigned int i = 0; i < m_count; ++i)
		{
			// PanelDue SubTabX(i, n) edges scaled; the tabs are children of the strip
			const int32_t x = S(SubTabXPd(i, m_count)) - Margin;
			const int32_t w = S(SubTabXPd(i + 1, m_count)) - S(SubTabXPd(i, m_count));
			TextButton& t = m_tabs[i];
			t.create(m_strip, x, 0, w, SubTabH, labels[i], Font::Small());
			t.setBorderVisible(false);
			t.onPress([this, i] { press(i); });
		}
		select(0);
	}

	void SubTabStrip::select(unsigned int index)
	{
		if (index >= m_count)
		{
			return;
		}
		m_selected = index;
		for (unsigned int i = 0; i < m_count; ++i)
		{
			m_tabs[i].setSelected(i == index);
		}
	}

	void SubTabStrip::press(unsigned int index)
	{
		if (index >= m_count)
		{
			return;
		}
		select(index);
		if (m_onSelect)
		{
			m_onSelect(index);
		}
	}

	void SubTabStrip::show(bool visible)
	{
		if (visible)
		{
			lv_obj_remove_flag(m_strip, LV_OBJ_FLAG_HIDDEN);
		}
		else
		{
			lv_obj_add_flag(m_strip, LV_OBJ_FLAG_HIDDEN);
		}
	}
} // namespace UI::Cnc
