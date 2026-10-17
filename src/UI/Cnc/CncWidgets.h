/*
 * CncWidgets.h
 *
 * Buttons of the CNC UI, with the PanelDue looks (Display.cpp ModernTextButton, CncWidgets.cpp):
 *   normal    tile fill, 2 px border, light text
 *   selected  accent fill, dark text (a group choice: axis, step) - also while a finger is on it
 *   locked    muted text (the page refuses the touch with an ALERT)
 *   toggle on accent border and accent text (COOLANT / VACUUM)
 * Positions and sizes are screen pixels (already scaled with CncLayout).
 */
#pragma once

#include "CncGlyphs.h"
#include "lvgl/lvgl.h"
#include <functional>
#include <string>

namespace UI::Cnc
{
	class TextButton
	{
	  public:
		using Handler = std::function<void()>;

		void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, const char* text, const lv_font_t* font);
		lv_obj_t* obj() const { return m_obj; }

		void setText(const char* text);
		const char* text() const;
		void setGeometry(int32_t x, int32_t w);
		void setAlignLeft(int32_t inset);			// text left aligned, 'inset' px from the left edge
		void show(bool visible);
		bool visible() const;

		void setSelected(bool selected);			// accent fill (group selection)
		bool selected() const { return m_selected; }
		void setLocked(bool locked);				// muted text, no pressed look
		void setToggle(bool on);					// accent border + text
		void setColours(lv_color_t text, lv_color_t fill);	// HOME: not-homed fill etc.
		void setFilled(lv_color_t fill);			// coloured button (PAUSE, ABORT, OK): dark text, border in the fill
		void resetColours();						// back to the tile look
		void setPressedLook(bool enable);			// false: no accent flash while touched (HOME, locked)
		void setBorderVisible(bool visible);		// sub-tabs have no outline
		void setOutlined(bool on);					// accent outline, text unchanged (viewed G5x, unsaved SAVE)
		// Jog direction icon at the outer edge (left or right); the text is centred in the space beside it
		void setIcon(JogIcon icon, bool rightSide);
		JogIcon icon() const { return m_icon; }

		void onPress(Handler h) { m_onPress = std::move(h); }	// acts on touch down (as PanelDue)

	  private:
		void applyLook();

		lv_obj_t* m_obj = nullptr;
		lv_obj_t* m_label = nullptr;
		bool m_selected = false, m_locked = false, m_toggle = false, m_pressedLook = true, m_outlined = false;
		void layoutLabel();
		static void drawIcon(lv_event_t* e);
		JogIcon m_icon = JogIcon::None;
		bool m_iconRight = false;
		bool m_customColours = false, m_customBorder = false;
		lv_color_t m_text{}, m_fill{}, m_border{};
		Handler m_onPress;
	};

	// RAPID / SLOW: one tall button, two halves one above the other, the active half filled with the
	// accent colour (inset 4 px). A tap anywhere switches it. PanelDue CncSegmentButton (stacked).
	class SegmentToggle
	{
	  public:
		using Handler = std::function<void()>;

		// horizontal: the two halves side by side (OUTSIDE | INSIDE), 'top' is the left one
		void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, const char* top, const char* bottom,
					bool horizontal = false);
		void setSecondActive(bool b);				// false: top / left (RAPID), true: bottom / right (SLOW)
		bool secondActive() const { return m_second; }
		void setLocked(bool locked);
		void onPress(Handler h) { m_onPress = std::move(h); }

	  private:
		void applyLook();

		lv_obj_t* m_obj = nullptr;
		lv_obj_t* m_fill = nullptr;
		lv_obj_t* m_top = nullptr;
		lv_obj_t* m_bottom = nullptr;
		int32_t m_height = 0, m_width = 0;			// the object's size is not laid out yet when applyLook runs
		bool m_second = false, m_locked = false, m_horizontal = false;
		Handler m_onPress;
	};

	// Paging arrow tile (PanelDue CncArrowButton): a filled triangle, 28 x 16, centred; disabled = muted
	// triangle and no reaction. Acts on touch down.
	class ArrowButton
	{
	  public:
		using Handler = std::function<void()>;

		void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, bool up);
		void setDisabled(bool disabled);
		bool disabled() const { return m_disabled; }
		void show(bool visible);
		void onPress(Handler h) { m_onPress = std::move(h); }

	  private:
		static void draw(lv_event_t* e);

		lv_obj_t* m_obj = nullptr;
		bool m_up = true, m_disabled = false;
		Handler m_onPress;
	};

	// Probe origin picker button (PanelDue CncOriginButton). Corners, edges, centre: selected = accent
	// fill, dark glyph. BORE, BOSS, Z TOP (outline style, with a label under the glyph): selected =
	// 3 px accent outline, the glyph keeps its colour. Acts on touch down.
	class OriginButton
	{
	  public:
		using Handler = std::function<void()>;

		void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, Origin kind, const char* label,
					bool outlineStyle);
		void setSelected(bool selected);
		bool selected() const { return m_selected; }
		void setInside(bool inside);				// corners and edges: the probe dot inside the pocket
		bool inside() const { return m_inside; }
		void onPress(Handler h) { m_onPress = std::move(h); }

	  private:
		static void draw(lv_event_t* e);

		lv_obj_t* m_obj = nullptr;
		Origin m_kind = Origin::None;
		const char* m_label = nullptr;
		bool m_outlineStyle = false, m_selected = false, m_inside = false;
		Handler m_onPress;
	};

	// WCS > PROBE: the PROBE / START button. Outline and text in one colour: green = ready, red = the
	// probe is triggered. With a probe signal a dot stands before the text: a ring = open, a disc =
	// closed (so the state does not rely on the colour alone). Acts on touch down.
	class ProbeButton
	{
	  public:
		using Handler = std::function<void()>;
		enum class Signal : uint8_t
		{
			None,		// no dot (START, or the machine does not report the probe)
			Open,
			Closed
		};

		void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, const char* text);
		void setSignal(Signal signal);
		Signal signal() const { return m_signal; }
		void show(bool visible);
		void onPress(Handler h) { m_onPress = std::move(h); }

	  private:
		static void draw(lv_event_t* e);

		lv_obj_t* m_obj = nullptr;
		std::string m_text;
		Signal m_signal = Signal::None;
		Handler m_onPress;
	};

	// Parameter tile (PanelDue CncValueTile): muted label and the value, either stacked (label top
	// left, value bottom right) or in one row; 'dropdown' adds a small down triangle (a choice).
	class ValueTile
	{
	  public:
		using Handler = std::function<void()>;

		void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, const char* label, bool stacked,
					bool dropdown);
		void setValue(const std::string& value);
		const std::string& value() const { return m_value; }
		void show(bool visible);
		void onPress(Handler h) { m_onPress = std::move(h); }

	  private:
		static void draw(lv_event_t* e);

		lv_obj_t* m_obj = nullptr;
		std::string m_label, m_value;
		bool m_stacked = false, m_dropdown = false;
		Handler m_onPress;
	};

	// File list row (PanelDue CncFileRow): name on the left, a muted tag on the right (FOLDER, UP),
	// the part matching a search in the accent colour, ".." when the name does not fit; selected =
	// accent outline. Acts on touch down.
	class FileRow
	{
	  public:
		using Handler = std::function<void()>;

		void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h);
		void setEntry(const std::string& text, const std::string& tag, size_t matchStart = 0, size_t matchLen = 0);
		void setSelected(bool selected);
		void setGeometry(int32_t x, int32_t y, int32_t w, int32_t h);
		void setGridStyle(bool grid);				// MACROS grid tile: name centred, tag bottom-right
		void show(bool visible);
		bool visible() const;
		const std::string& text() const { return m_text; }
		const std::string& tag() const { return m_tag; }
		void onPress(Handler h) { m_onPress = std::move(h); }

	  private:
		static void draw(lv_event_t* e);

		lv_obj_t* m_obj = nullptr;
		std::string m_text, m_tag;
		size_t m_matchStart = 0, m_matchLen = 0;
		bool m_selected = false, m_grid = false;
		Handler m_onPress;
	};

	// SYSTEM > ALERT row (PanelDue CncAlertRow): ERR (red) / WARN (amber) badge, the age (muted), the
	// text cut to fit with "..", an accent dot while unread. Acts on touch down.
	class AlertRow
	{
	  public:
		using Handler = std::function<void()>;

		void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h);
		void setEntry(const std::string& text, bool error, bool unread, const std::string& age);
		void show(bool visible);
		bool visible() const;
		const std::string& text() const { return m_text; }
		const std::string& age() const { return m_age; }
		bool error() const { return m_error; }
		bool unread() const { return m_unread; }
		void onPress(Handler h) { m_onPress = std::move(h); }

	  private:
		static void draw(lv_event_t* e);

		lv_obj_t* m_obj = nullptr;
		std::string m_text, m_age;
		bool m_error = false, m_unread = false;
		Handler m_onPress;
	};

	// Tile with a drawn glyph (PanelDue CncGlyphButton): SEARCH (accent glyph and outline while
	// active), keyboard SHIFT (arrow, filled while active), or one character (keyboard key: accent
	// fill while touched). Acts on touch down.
	class GlyphButton
	{
	  public:
		using Handler = std::function<void()>;
		enum class Kind : uint8_t
		{
			Search,
			Shift,
			Char,
			Grid,									// MACROS view toggle: four squares (switches to the grid)
			List,									// three bullet-and-line rows (switches to the list)
			Keyboard								// CONSOLE: opens the keyboard
		};

		void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, Kind kind);
		void setGlyph(Kind kind, char c = 0);
		void setActive(bool active);
		bool active() const { return m_active; }
		void show(bool visible);
		void onPress(Handler h) { m_onPress = std::move(h); }

	  private:
		static void draw(lv_event_t* e);
		void applyLook();

		lv_obj_t* m_obj = nullptr;
		Kind m_kind = Kind::Search;
		char m_char = 0;
		bool m_active = false;
		Handler m_onPress;
	};

	// Text tile (PanelDue CncTextTile): one or two lines on a tile, left aligned (inset 14) or centred
	// (inset 5), outline 2 or 3 px in any colour; textRight keeps room for a button on the tile.
	class TextTile
	{
	  public:
		void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, bool centred);
		void setText(const std::string& line1, const std::string& line2 = std::string());
		void setTextColour(lv_color_t colour);
		void setOutline(lv_color_t colour, int32_t widthPd);	// width in PanelDue px (2 or 3)
		void setTextRight(int32_t px);				// screen px kept free on the right
		void show(bool visible);
		bool visible() const;
		const std::string& line1() const { return m_line1; }
		const std::string& line2() const { return m_line2; }
		lv_obj_t* obj() const { return m_obj; }

	  private:
		static void draw(lv_event_t* e);

		lv_obj_t* m_obj = nullptr;
		std::string m_line1, m_line2;
		lv_color_t m_textColour{};
		bool m_centred = false;
		int32_t m_textRight = 0;
	};

	// Sub-tab strip above the nav bar (PanelDue AddSubTabs): equal tabs across the content width on a
	// tile-coloured strip, no outlines; the chosen tab is accent-filled with dark text. Acts on touch down.
	class SubTabStrip
	{
	  public:
		using Handler = std::function<void(unsigned int)>;
		static constexpr unsigned int MaxTabs = 3;

		SubTabStrip() = default;
		SubTabStrip(const SubTabStrip&) = delete;
		SubTabStrip& operator=(const SubTabStrip&) = delete;

		void create(lv_obj_t* parent, const char* const labels[], unsigned int count);
		void select(unsigned int index);			// look only; does not call the handler
		unsigned int selected() const { return m_selected; }
		unsigned int count() const { return m_count; }
		void show(bool visible);
		void press(unsigned int index);				// a touch on tab index (also used by the tests)
		void onSelect(Handler h) { m_onSelect = std::move(h); }

	  private:
		lv_obj_t* m_strip = nullptr;
		TextButton m_tabs[MaxTabs];
		unsigned int m_count = 0, m_selected = 0;
		Handler m_onSelect;
	};
}
