/*
 * CncTheme.cpp
 *
 * Colours, fonts and the basic tile style of the CNC UI (see CncTheme.h).
 */

#include "CncTheme.h"
#include "CncLayout.h"
#include "Debug.h"
#include "UI/Styles/Styles.h" // ASSETS_FOLDER
#include <string>

namespace UI::Cnc
{
	namespace Colour
	{
		static unsigned int s_accent = 0;

		lv_color_t Accent(unsigned int index)
		{
			switch (index)
			{
			case 0:
				return lv_color_hex(0xEE7004); // Orange Fire
			case 1:
				return lv_color_hex(0xFF0000); // Red Voron
			case 2:
				return lv_color_hex(0x007CF7); // Duet Blue
			case 3:
				return lv_color_hex(0x00FFFF); // Cyan
			case 4:
				return lv_color_hex(0x509638); // RepRap Green
			case 5:
				return lv_color_hex(0xFCD10A); // Yellow / Gold
			case 6:
				return lv_color_hex(0x67FF00); // Lime
			default:
				return lv_color_hex(0xFF00FF); // Magenta
			}
		}

		lv_color_t Accent()
		{
			return Accent(s_accent);
		}
		void SetAccent(unsigned int index)
		{
			s_accent = index;
		}
	} // namespace Colour

	namespace Font
	{
		static const char* const TtfPath = ASSETS_FOLDER "cnc/LiberationSans-Regular.ttf";

		static const lv_font_t* Load(uint32_t size)
		{
			lv_font_t* f = lv_freetype_font_create(
				TtfPath, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, size, LV_FREETYPE_FONT_STYLE_NORMAL);
			if (f == nullptr)
			{
				LOG_ERROR("CNC UI: cannot load {:s} at {:d} px", TtfPath, size);
				return LV_FONT_DEFAULT;
			}
			return f;
		}

		// Created on first use and kept for the life of the program (a handful of sizes)
		const lv_font_t* Small()
		{
			static const lv_font_t* f = Load(24);
			return f;
		}

		const lv_font_t* Large()
		{
			static const lv_font_t* f = Load(36);
			return f;
		}

		const lv_font_t* Tiny()
		{
			static const lv_font_t* f = Load(20);
			return f;
		}

		lv_font_t* Create(uint32_t size)
		{
			lv_font_t* f = lv_freetype_font_create(
				TtfPath, LV_FREETYPE_FONT_RENDER_MODE_BITMAP, size, LV_FREETYPE_FONT_STYLE_NORMAL);
			if (f == nullptr)
			{
				LOG_ERROR("CNC UI: cannot load {:s} at {:d} px", TtfPath, size);
			}
			return f;
		}

		void Release(lv_font_t* font)
		{
			if (font != nullptr)
			{
				lv_freetype_font_delete(font);
			}
		}
	} // namespace Font

	void StyleCard(lv_obj_t* obj, lv_color_t fill, lv_color_t border, bool borderVisible)
	{
		lv_obj_set_style_bg_color(obj, fill, 0);
		lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(obj, CncLayout::CardRadius, 0);
		lv_obj_set_style_border_color(obj, border, 0);
		lv_obj_set_style_border_width(obj, borderVisible ? CncLayout::CardBorder : 0, 0);
	}

	lv_obj_t* CreateBare(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h)
	{
		lv_obj_t* obj = lv_obj_create(parent);
		lv_obj_remove_style_all(obj); // no DuetScreen theme on CNC objects
		lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
		lv_obj_set_pos(obj, x, y);
		lv_obj_set_size(obj, w, h);
		return obj;
	}

	lv_obj_t* CreateLabel(lv_obj_t* parent,
						  int32_t x,
						  int32_t y,
						  int32_t w,
						  int32_t h,
						  const lv_font_t* font,
						  lv_color_t colour,
						  lv_text_align_t align,
						  const char* text)
	{
		lv_obj_t* label = lv_label_create(parent);
		lv_obj_remove_style_all(label);
		lv_obj_set_style_text_font(label, font, 0);
		lv_obj_set_style_text_color(label, colour, 0);
		lv_obj_set_style_text_align(label, align, 0);
		lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
		lv_obj_set_width(label, w);
		lv_obj_set_pos(label, x, y + (h - lv_font_get_line_height(font)) / 2);
		lv_label_set_text(label, text);
		return label;
	}

	void DrawText(lv_layer_t* layer,
				  const lv_area_t& area,
				  const char* text,
				  const lv_font_t* font,
				  lv_color_t colour,
				  lv_text_align_t align)
	{
		lv_draw_label_dsc_t dsc;
		lv_draw_label_dsc_init(&dsc);
		dsc.font = font;
		dsc.color = colour;
		dsc.align = align;
		// Cut text that does not fit (PanelDue fields clip it) instead of wrapping it
		std::string fitted(text != nullptr ? text : "");
		const int32_t maxW = lv_area_get_width(&area);
		lv_point_t size;
		for (;;)
		{
			lv_text_get_size(&size, fitted.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
			if (size.x <= maxW || fitted.empty())
			{
				break;
			}
			// Drop the last whole UTF-8 character: its continuation bytes, then its first byte
			while (!fitted.empty() && (static_cast<unsigned char>(fitted.back()) & 0xC0) == 0x80)
			{
				fitted.pop_back();
			}
			if (!fitted.empty())
			{
				fitted.pop_back();
			}
		}
		dsc.text = fitted.c_str();
		dsc.text_local = 1; // copied by the draw task
		dsc.flag = LV_TEXT_FLAG_EXPAND;
		const int32_t lineH = lv_font_get_line_height(font);
		lv_area_t a = area;
		a.y1 = area.y1 + (lv_area_get_height(&area) - lineH) / 2;
		a.y2 = a.y1 + lineH - 1;
		lv_draw_label(layer, &dsc, &a);
	}

	void DrawOutline(lv_layer_t* layer, const lv_area_t& area, int32_t width, lv_color_t colour)
	{
		lv_draw_rect_dsc_t dsc;
		lv_draw_rect_dsc_init(&dsc);
		dsc.bg_opa = LV_OPA_TRANSP;
		dsc.border_color = colour;
		dsc.border_width = width;
		dsc.border_opa = LV_OPA_COVER;
		dsc.radius = CncLayout::CardRadius;
		lv_draw_rect(layer, &dsc, &area);
	}
} // namespace UI::Cnc
