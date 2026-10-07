/*
 * CncGlyphs.h
 *
 * The PanelDue CNC UI's code-drawn glyphs (CncWidgets.cpp / Display.cpp), ported to LVGL.
 * The drawing code keeps the PanelDue coordinates (pixels relative to the tile centre) and a
 * Pen scales them by 1.25, so the shapes are the same, only larger.
 */
#pragma once

#include "lvgl/lvgl.h"

namespace UI::Cnc
{
	// Draws filled shapes into an LVGL layer. Coordinates are PanelDue pixels relative to (cx, cy),
	// rectangles inclusive as in UTFT.
	class Pen
	{
	  public:
		Pen(lv_layer_t* layer, int32_t cx, int32_t cy) : m_layer(layer), m_cx(cx), m_cy(cy) {}

		void setColour(lv_color_t c) { m_colour = c; }
		void fillRect(int x1, int y1, int x2, int y2);
		void fillRoundRect(int x1, int y1, int x2, int y2);	// UTFT style: corners cut by one pixel
		void fillCircle(int x, int y, int r);
		void fillOctagon(int r);								// centred, r = half the width
		void line(float x1, float y1, float x2, float y2, int32_t width,	// PanelDue coordinates, width in screen px
				  bool roundEnds = false);

	  private:
		void fill(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t radius);
		int32_t X(int v) const;							// left edge of PanelDue column v
		int32_t Y(int v) const;							// top edge of PanelDue row v

		lv_layer_t* m_layer;
		int32_t m_cx, m_cy;
		lv_color_t m_colour = lv_color_white();
	};

	enum class NavIcon
	{
		Joystick,		// CONTROL
		Crosshair,		// WCS
		List,			// JOB
		SdCard,			// MACROS
		Spanner			// SYSTEM
	};

	void DrawNavIcon(Pen& pen, NavIcon icon, lv_color_t glyph, lv_color_t background);

	// Probe origins (PanelDue CncOrigin): corners, edges, centre, BORE, BOSS, Z TOP
	enum class Origin : uint8_t
	{
		TL, TC, TR,
		ML, MC, MR,
		BL, BC, BR,
		Bore, Boss, ZTop,
		None
	};
	// Simplified line icon of an origin (corner = L, edge = bar, centre = crosshair; the dot marks the
	// point that becomes the origin). 'bg' is the colour behind it. PanelDue CncDrawOriginGlyph.
	void DrawOriginGlyph(Pen& pen, Origin kind, lv_color_t glyph, lv_color_t bg);
	// JOB LIST SEARCH tile: magnifying glass ('bg' = the tile colour)
	// Jog direction icons (MOVE buttons, jog prompt): an arrow (triangle head and stem) in 6 directions,
	// or an arrow on an arc (CCW, mirrored CW). Drawn in a size x size box centred on (cx, cy), screen px.
	enum class JogIcon : uint8_t
	{
		None,
		Left,
		Right,
		Down,
		Up,
		DownLeft,		// 225 degrees
		UpRight,		// 45 degrees
		Ccw,
		Cw
	};
	void DrawJogIcon(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t size, JogIcon icon, lv_color_t colour);
	// Warning: a filled triangle (size = its width) with an exclamation mark in 'mark'
	void DrawWarningIcon(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t size, lv_color_t fill, lv_color_t mark);

	void DrawKeyboardIcon(Pen& pen, lv_color_t colour);	// PanelDue IconKeyboard (61 x 29): CONSOLE
	void DrawGridGlyph(Pen& pen, lv_color_t colour);		// four rounded squares (MACROS: to the grid)
	void DrawListGlyph(Pen& pen, lv_color_t colour);		// three bullet-and-line rows (MACROS: to the list)
	void DrawSearchGlyph(Pen& pen, lv_color_t colour, lv_color_t bg);
	// Keyboard SHIFT: arrow up, filled while lower case is on
	void DrawShiftGlyph(Pen& pen, lv_color_t colour, bool filled);
	// Keyboard ENTER (PanelDue IconEnter)
	void DrawEnterIcon(Pen& pen, lv_color_t colour);
	// Check mark of a measured axis (PanelDue CncCheckField), 'size' PanelDue px, centred
	void DrawCheckGlyph(Pen& pen, int size, lv_color_t colour);
	// r = ring radius, t = stroke width (PanelDue CncDrawCrosshair)
	void DrawCrosshair(Pen& pen, int r, int t, lv_color_t glyph, lv_color_t background);
	// White octagon outline with a square inside (PanelDue ModernStopButton)
	void DrawStopIcon(Pen& pen, lv_color_t glyph, lv_color_t background);
	// X and check mark of the red / green buttons in every popup (standard popup, numpad, keyboard).
	// PanelDue shape (IconCancel / IconOk on a 110 x 78 button), 3x thicker lines.
	// half: half the size of the X in PanelDue px (Modern UI: the shorter button side / 2 - 12)
	void DrawCancelIcon(Pen& pen, lv_color_t colour, float half = 21.0f, int32_t width = 5);
	void DrawOkIcon(Pen& pen, lv_color_t colour);
	// Trash can of the Modern UI (ModernIconButton IconTrash): handle, lid, tapering body with three
	// slots cut out in the button colour
	void DrawTrashIcon(Pen& pen, lv_color_t colour, lv_color_t bg);
	// Play triangle (PREVIEW: RUN), pointing right, centred
	void DrawPlayIcon(Pen& pen, lv_color_t colour);
	// Backspace key of the keyboard (PanelDue IconBackspace bitmap)
	void DrawBackspaceIcon(Pen& pen);
	// Backspace key of the numpad (Modern UI icon: outlined tag with an X)
	void DrawNumpadBackspaceIcon(Pen& pen, lv_color_t colour);
}
