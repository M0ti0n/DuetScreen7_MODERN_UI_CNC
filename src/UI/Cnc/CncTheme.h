/*
 * CncTheme.h
 *
 * Colours and fonts of the CNC UI. Colours are the PanelDue CNC UI's (CncCommon.cpp,
 * CncWidgets.cpp), as RGB888: DuetScreen shows them exactly, the PanelDue's 16-bit bus did not.
 * Fonts: the PanelDue fonts are Liberation Sans (glcd19x21, glcd28x32); at 1.25 scale they are
 * Liberation Sans 24 px and 36 px (assets/fonts/LiberationSans.ttf).
 *
 * The CNC screens style every object themselves and do not use DuetScreen's themes.
 */
#pragma once

#include "lvgl/lvgl.h"

namespace UI::Cnc
{
	namespace Colour
	{
		inline lv_color_t PageBg() { return lv_color_hex(0x12161C); }
		inline lv_color_t Tile() { return lv_color_hex(0x1C222B); }
		inline lv_color_t Pressed() { return lv_color_hex(0x2A3240); }
		inline lv_color_t Text() { return lv_color_hex(0xE5E8EC); }
		inline lv_color_t Muted() { return lv_color_hex(0x9AA4B2); }
		inline lv_color_t Border() { return lv_color_hex(0x3B434F); }
		inline lv_color_t DarkText() { return lv_color_hex(0x12161C); }
		inline lv_color_t ButtonText() { return lv_color_hex(0x242424); }
		inline lv_color_t NotHomed() { return lv_color_hex(0x804000); }
		inline lv_color_t StopRed() { return lv_color_hex(0xC93218); }
		inline lv_color_t StopRedPressed() { return lv_color_hex(0xA02723); }
		inline lv_color_t StopGlyph() { return lv_color_hex(0xF5F5F5); }
		inline lv_color_t Green() { return lv_color_hex(0x56B834); }
		inline lv_color_t Cyan() { return lv_color_hex(0x5FC3DC); }
		inline lv_color_t Amber() { return lv_color_hex(0xE0A800); }

		// SETTINGS > ACCENT COLOR (PanelDue order); index 0 is the default
		lv_color_t Accent(unsigned int index);
		lv_color_t Accent();						// the selected one
		void SetAccent(unsigned int index);
	}

	namespace Font
	{
		const lv_font_t* Small();					// glcd19x21 -> Liberation Sans 24 px
		const lv_font_t* Large();					// glcd28x32 -> Liberation Sans 36 px
		const lv_font_t* Tiny();					// Liberation Sans 20 px (PREVIEW operation buttons, overlays)
		lv_font_t* Create(uint32_t size);			// another size, for a short while (the splash); nullptr on failure
		void Release(lv_font_t* font);				// frees one from Create()
	}

	// A PanelDue "ModernCard": filled tile, 2 px border, small rounded corner
	void StyleCard(lv_obj_t* obj, lv_color_t fill, lv_color_t border, bool borderVisible = true);
	// A plain object with no theme styling, no scrolling, no padding
	lv_obj_t* CreateBare(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h);
	// A label in the given font and colour, positioned inside [x, x + w) and centred vertically on [y, y + h)
	// Text drawn in a draw event (custom widgets): clipped to the area, vertically centred in it
	void DrawText(lv_layer_t* layer, const lv_area_t& area, const char* text, const lv_font_t* font, lv_color_t colour,
				  lv_text_align_t align);
	// Outline drawn in a draw event, 'width' px inside the area
	void DrawOutline(lv_layer_t* layer, const lv_area_t& area, int32_t width, lv_color_t colour);
	lv_obj_t* CreateLabel(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, const lv_font_t* font,
						  lv_color_t colour, lv_text_align_t align, const char* text);
}
