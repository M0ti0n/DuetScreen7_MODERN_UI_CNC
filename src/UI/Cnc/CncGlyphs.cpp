/*
 * CncGlyphs.cpp
 *
 * Port of the PanelDue CNC glyphs. Each Draw function is the PanelDue code with lcd.* replaced
 * by pen.*; the numbers are unchanged.
 */

#include "CncGlyphs.h"
#include <algorithm>
#include <cmath>

namespace UI::Cnc
{
	// ---- Pen -------------------------------------------------------------------------------

	int32_t Pen::X(int v) const
	{
		return m_cx + (int32_t)std::lround(v * 1.25);
	}
	int32_t Pen::Y(int v) const
	{
		return m_cy + (int32_t)std::lround(v * 1.25);
	}

	void Pen::fill(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t radius)
	{
		if (x2 < x1 || y2 < y1)
		{
			return;
		}
		lv_draw_rect_dsc_t dsc;
		lv_draw_rect_dsc_init(&dsc);
		dsc.bg_color = m_colour;
		dsc.bg_opa = LV_OPA_COVER;
		dsc.radius = radius;
		dsc.border_width = 0;
		const lv_area_t area = {x1, y1, x2, y2};
		lv_draw_rect(m_layer, &dsc, &area);
	}

	// UTFT rectangles include both corners: column x2 ends where column x2 + 1 starts
	void Pen::fillRect(int x1, int y1, int x2, int y2)
	{
		fill(X(x1), Y(y1), X(x2 + 1) - 1, Y(y2 + 1) - 1, 0);
	}

	void Pen::fillRoundRect(int x1, int y1, int x2, int y2)
	{
		fill(X(x1), Y(y1), X(x2 + 1) - 1, Y(y2 + 1) - 1, 2);
	}

	void Pen::fillCircle(int x, int y, int r)
	{
		fill(X(x - r), Y(y - r), X(x + r + 1) - 1, Y(y + r + 1) - 1, LV_RADIUS_CIRCLE);
	}

	// PanelDue FillOctagon: row by row
	void Pen::fillOctagon(int r)
	{
		const int flatHalf = (r * 4142) / 10000; // r * tan(22.5 degrees)
		for (int dy = -r; dy <= r; ++dy)
		{
			const int ady = (dy < 0) ? -dy : dy;
			const int hw = (ady <= flatHalf) ? r : r - (ady - flatHalf);
			fillRect(-hw, dy, hw, dy);
		}
	}

	void Pen::line(float x1, float y1, float x2, float y2, int32_t width, bool roundEnds)
	{
		lv_draw_line_dsc_t dsc;
		lv_draw_line_dsc_init(&dsc);
		dsc.color = m_colour;
		dsc.width = width;
		dsc.round_start = roundEnds ? 1 : 0;
		dsc.round_end = roundEnds ? 1 : 0;
		dsc.p1.x = (lv_value_precise_t)(m_cx + std::lround(x1 * 1.25f));
		dsc.p1.y = (lv_value_precise_t)(m_cy + std::lround(y1 * 1.25f));
		dsc.p2.x = (lv_value_precise_t)(m_cx + std::lround(x2 * 1.25f));
		dsc.p2.y = (lv_value_precise_t)(m_cy + std::lround(y2 * 1.25f));
		lv_draw_line(m_layer, &dsc);
	}

	// ---- Glyphs ----------------------------------------------------------------------------

	void DrawCrosshair(Pen& pen, int r, int t, lv_color_t glyph, lv_color_t background)
	{
		// Ring
		pen.setColour(glyph);
		pen.fillCircle(0, 0, r + t / 2);
		pen.setColour(background);
		pen.fillCircle(0, 0, r - (t + 1) / 2);

		// Arms: from 0.45 r to 1.6 r
		const int inner = (r * 45) / 100;
		const int outer = (r * 160) / 100;
		const int h = t / 2;
		pen.setColour(glyph);
		pen.fillRect(-h, -outer, -h + t - 1, -inner); // up
		pen.fillRect(-h, inner, -h + t - 1, outer);	  // down
		pen.fillRect(-outer, -h, -inner, -h + t - 1); // left
		pen.fillRect(inner, -h, outer, -h + t - 1);	  // right

		// Centre dot
		pen.fillCircle(0, 0, (t * 9) / 10);
	}

	void DrawNavIcon(Pen& pen, NavIcon icon, lv_color_t glyph, lv_color_t background)
	{
		pen.setColour(glyph);
		switch (icon)
		{
		case NavIcon::Joystick: // base, stick and ball
			pen.fillRoundRect(-13, 9, 13, 19);
			pen.fillRect(-2, -9, 2, 10);
			pen.fillCircle(0, -13, 8);
			break;

		case NavIcon::Crosshair:
			DrawCrosshair(pen, 12, 4, glyph, background);
			break;

		case NavIcon::List: // three bullet-and-line rows
			for (int row = -1; row <= 1; ++row)
			{
				const int ry = row * 9;
				pen.fillCircle(-11, ry, 3);
				pen.fillRect(-3, ry - 2, 13, ry + 2);
			}
			break;

		case NavIcon::SdCard: // 28 x 36 card, top-right corner cut off, notch on the left edge
		{
			const int x0 = -14, y0 = -18, w = 28, h = 36, cut = 9;
			for (int r = 0; r < h; ++r)
			{
				int l = x0;
				if (r >= 7 && r <= 16)
				{
					l += (r <= 9) ? (r - 7) : (r >= 13) ? (16 - r) : 3; // notch, 3 px deep
				}
				int rr = x0 + w - 1;
				if (r < cut)
				{
					rr -= cut - r; // chamfer
				}
				pen.fillRect(l, y0 + r, rr, y0 + r);
			}
		}
		break;

		case NavIcon::Spanner: // open-end spanner, 60 px tall
		default:
		{
			const int iconTop = -30;
			const int headRadius = 13;
			const int headY = iconTop + headRadius;
			const int neckTop = headY + 6;
			const int neckBottom = headY + 19;
			const int neckRows = neckBottom - neckTop;

			pen.fillCircle(0, headY, headRadius);
			for (int yy = neckTop; yy <= neckBottom; ++yy)
			{
				const int remaining = neckRows - (yy - neckTop);
				const int halfWidth =
					6 + (6 * remaining * remaining + (neckRows * neckRows) / 2) / (neckRows * neckRows);
				pen.fillRect(-halfWidth, yy, halfWidth, yy);
			}
			pen.fillRoundRect(-6, neckBottom, 6, 30);
			pen.setColour(background);
			pen.fillRect(-4, iconTop, 4, headY + 1); // open jaw
		}
		break;
		}
	}

	void DrawStopIcon(Pen& pen, lv_color_t glyph, lv_color_t background)
	{
		pen.setColour(glyph);
		pen.fillOctagon(27);
		pen.setColour(background);
		pen.fillOctagon(22);
		pen.setColour(glyph);
		pen.fillRect(-8, -8, 8, 8);
	}
	// Shape measured on the PanelDue popup (an X spanning +-28 px, a check (-17.5, 0) (-6, 27) (27.5, -28)),
	// drawn 25% smaller here: X +-21 px. Round ends so the two strokes of the check join cleanly.
	constexpr int32_t ActionIconWidth = 5;
	constexpr float ActionIconScale = 0.75f;
	void DrawCancelIcon(Pen& pen, lv_color_t colour, float half, int32_t width)
	{
		pen.setColour(colour);
		pen.line(-half, -half, half, half, width, true);
		pen.line(half, -half, -half, half, width, true);
	}

	// PanelDue IconBackspace (LARGE_FONT, 30 x 24): black tag, white X with a dark outline; the outline
	// and the corners are left to the key's fill, as on PanelDue (IconPaletteDark). Drawn in runs.
	void DrawBackspaceIcon(Pen& pen)
	{
		static const char* const rows[] = {
			"..........dddddddddddddddddd..", ".........d##################d.", "........d####################d",
			".......d#####################d", "......d######################d", ".....d#######################d",
			"....d#######dd#######dd######d", "...d#######dwwd#####dwwd#####d", "..d#########dwwd###dwwd######d",
			".d###########dwwd#dwwd#######d", "d#############dwwdwwd########d", "d##############dwwwd#########d",
			"d##############dwwwd#########d", "d#############dwwdwwd########d", ".d###########dwwd#dwwd#######d",
			"..d#########dwwd###dwwd######d", "...d#######dwwd#####dwwd#####d", "....d#######dd#######dd######d",
			".....d#######################d", "......d######################d", ".......d#####################d",
			"........d####################d", ".........d##################d.", "..........dddddddddddddddddd.."};
		constexpr int W = 30, H = 24;
		for (int y = 0; y < H; ++y)
		{
			for (char c : {'#', 'w'})
			{
				pen.setColour((c == '#') ? lv_color_black() : lv_color_white());
				for (int x = 0; x < W;)
				{
					if (rows[y][x] != c)
					{
						++x;
						continue;
					}
					int end = x;
					while (end + 1 < W && rows[y][end + 1] == c)
					{
						++end;
					}
					pen.fillRect(x - W / 2, y - H / 2, end - W / 2, y - H / 2);
					x = end + 1;
				}
			}
		}
	}

	// Numpad backspace (the Modern UI icon): an outlined tag pointing left with an X in it.
	// About 34 x 23 PanelDue pixels, centred on the key; round joins.
	void DrawNumpadBackspaceIcon(Pen& pen, lv_color_t colour)
	{
		constexpr int32_t Width = 3;
		pen.setColour(colour);
		// Tag outline: point, top-left, top-right, bottom-right, bottom-left, back to the point
		const float pts[][2] = {{-17.0f, 0.0f}, {-5.0f, -11.5f}, {17.0f, -11.5f}, {17.0f, 11.5f}, {-5.0f, 11.5f}};
		for (size_t i = 0; i < 5; ++i)
		{
			const float* a = pts[i];
			const float* b = pts[(i + 1) % 5];
			pen.line(a[0], a[1], b[0], b[1], Width, true);
		}
		// X inside the box part
		pen.line(-0.5f, -5.5f, 10.5f, 5.5f, Width, true);
		pen.line(10.5f, -5.5f, -0.5f, 5.5f, Width, true);
	}

	void DrawOkIcon(Pen& pen, lv_color_t colour)
	{
		pen.setColour(colour);
		constexpr float k = ActionIconScale;
		pen.line(-17.5f * k, 0, -6 * k, 27 * k, ActionIconWidth, true);
		pen.line(-6 * k, 27 * k, 27.5f * k, -28 * k, ActionIconWidth, true);
	}

	// ---- Probe origins ---------------------------------------------------------------------

	namespace
	{
		void thickLine(Pen& pen, int x1, int y1, int x2, int y2, int t) // horizontal or vertical only
		{
			const int h = t / 2;
			if (y1 == y2)
			{
				pen.fillRect(std::min(x1, x2), y1 - h, std::max(x1, x2), y1 - h + t - 1);
			}
			else
			{
				pen.fillRect(x1 - h, std::min(y1, y2), x1 - h + t - 1, std::max(y1, y2));
			}
		}

		void drawRect(Pen& pen, int x1, int y1, int x2, int y2) // 1 px outline (UTFT drawRect)
		{
			pen.fillRect(x1, y1, x2, y1);
			pen.fillRect(x1, y2, x2, y2);
			pen.fillRect(x1, y1, x1, y2);
			pen.fillRect(x2, y1, x2, y2);
		}
	} // namespace

	void DrawOriginGlyph(Pen& pen, Origin kind, lv_color_t glyph, lv_color_t bg)
	{
		constexpr int off = 9, ln = 13, sw = 4, dot = 4;
		pen.setColour(glyph);
		switch (kind)
		{
		case Origin::Bore: // thin ring with a centre dot
			pen.fillCircle(0, 0, 12);
			pen.setColour(bg);
			pen.fillCircle(0, 0, 10);
			pen.setColour(glyph);
			pen.fillCircle(0, 0, dot);
			return;

		case Origin::Boss: // solid disc with a small hole
			pen.fillCircle(0, 0, 13);
			pen.setColour(bg);
			pen.fillCircle(0, 0, 4);
			return;

		case Origin::ZTop: // arrow down onto a block
			drawRect(pen, -14, 3, 14, 12);
			drawRect(pen, -13, 4, 13, 11);
			pen.fillRect(-1, -14, 0, 0);
			for (int i = 0; i < 6; ++i)
			{
				pen.fillRect(-6 + i, -5 + i, 5 - i, -5 + i); // arrow head
			}
			return;

		case Origin::MC: // square with cross-hair, centre dot
			drawRect(pen, -9, -7, 9, 7);
			drawRect(pen, -8, -6, 8, 6);
			pen.fillRect(-1, -15, 0, 15);
			pen.fillRect(-17, -1, 17, 0);
			pen.fillCircle(0, 0, dot);
			return;

		case Origin::TC:
		case Origin::BC: // top / bottom edge: bar, dot in its middle
		{
			const int yy = (kind == Origin::TC) ? -off : off;
			thickLine(pen, -ln, yy, ln, yy, sw);
			pen.fillCircle(0, yy, dot);
			return;
		}

		case Origin::ML:
		case Origin::MR: // left / right edge
		{
			const int xx = (kind == Origin::ML) ? -off : off;
			thickLine(pen, xx, -ln, xx, ln, sw);
			pen.fillCircle(xx, 0, dot);
			return;
		}

		case Origin::TL:
		case Origin::TR:
		case Origin::BL:
		case Origin::BR: // corner: L shape, dot on the vertex
		{
			const bool left = (kind == Origin::TL || kind == Origin::BL);
			const bool topSide = (kind == Origin::TL || kind == Origin::TR);
			const int xx = left ? -off : off;
			const int yy = topSide ? -off : off;
			const int ex = xx + (left ? (ln + 4) : -(ln + 4));
			const int ey = yy + (topSide ? (ln + 4) : -(ln + 4));
			thickLine(pen, xx, ey, xx, yy, sw);
			thickLine(pen, xx, yy, ex, yy, sw);
			pen.fillCircle(xx, yy, dot + 1);
			return;
		}

		default:
			return;
		}
	}

	// Short stroke down-right, long stroke up-right (PanelDue: 3 px lines in a size x size box)
	void DrawCheckGlyph(Pen& pen, int size, lv_color_t colour)
	{
		pen.setColour(colour);
		const float s = (float)size, o = -s / 2.0f;
		const float x0 = o + s / 8, y0 = o + s / 2;
		const float x1 = o + (3 * s) / 8, y1 = o + (3 * s) / 4;
		const float x2 = o + (7 * s) / 8, y2 = o + s / 6;
		pen.line(x0, y0, x1, y1, 4, true);
		pen.line(x1, y1, x2, y2, 4, true);
	}

	// ---- Keyboard and list glyphs (PanelDue CncGlyphButton, IconEnter) ----------------------

	// Ring (radius 11, 4 px) up-left of the centre, handle to the bottom right
	// ---- Jog direction icons -------------------------------------------------------------------

	namespace
	{
		// Shapes in a 100 x 100 box (centre 50, 50), the arrow pointing up
		struct Pt
		{
			float x, y;
		};

		void triangle(lv_layer_t* layer, const Pt (&p)[3], lv_color_t colour)
		{
			lv_draw_triangle_dsc_t dsc;
			lv_draw_triangle_dsc_init(&dsc);
			dsc.color = colour;
			dsc.opa = LV_OPA_COVER;
			for (int i = 0; i < 3; ++i)
			{
				dsc.p[i].x = (lv_value_precise_t)std::lround(p[i].x);
				dsc.p[i].y = (lv_value_precise_t)std::lround(p[i].y);
			}
			lv_draw_triangle(layer, &dsc);
		}
	} // namespace

	void DrawWarningIcon(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t size, lv_color_t fill, lv_color_t mark)
	{
		const float w = (float)size, h = w * 0.87f;
		const float x = (float)cx, top = (float)cy - h / 2, bottom = (float)cy + h / 2;
		const Pt tri[3] = {{x, top}, {x + w / 2, bottom}, {x - w / 2, bottom}};
		triangle(layer, tri, fill);
		lv_draw_rect_dsc_t dsc;
		lv_draw_rect_dsc_init(&dsc);
		dsc.bg_color = mark;
		dsc.bg_opa = LV_OPA_COVER;
		const int32_t bar = std::max<int32_t>(2, size / 9);
		dsc.radius = bar / 2;
		lv_area_t a = {cx - bar / 2,
					   (int32_t)std::lround(top + h * 0.34f),
					   cx - bar / 2 + bar - 1,
					   (int32_t)std::lround(bottom - h * 0.30f)};
		lv_draw_rect(layer, &dsc, &a);
		const int32_t dotY = (int32_t)std::lround(bottom - h * 0.20f);
		a = {cx - bar / 2, dotY, cx - bar / 2 + bar - 1, dotY + bar - 1};
		lv_draw_rect(layer, &dsc, &a);
	}

	void DrawJogIcon(lv_layer_t* layer, int32_t cx, int32_t cy, int32_t size, JogIcon icon, lv_color_t colour)
	{
		const float k = (float)size / 100.0f;
		const float ox = (float)cx - 50.0f * k, oy = (float)cy - 50.0f * k;
		switch (icon)
		{
		case JogIcon::None:
			return;
		case JogIcon::Ccw:
		case JogIcon::Cw:
		{
			// Arc of ~280 degrees, open at the top; the head at its upper end points along the turn
			const bool cw = (icon == JogIcon::Cw);
			lv_draw_arc_dsc_t arc;
			lv_draw_arc_dsc_init(&arc);
			arc.color = colour;
			arc.width = (int32_t)std::lround(9.0f * k);
			arc.center.x = cx;
			arc.center.y = (int32_t)std::lround(oy + 54.0f * k);
			arc.radius = (uint16_t)std::lround((34.0f + 4.5f) * k);
			arc.start_angle = cw ? 340 : 280; // degrees, clockwise from 3 o'clock
			arc.end_angle = cw ? 260 : 200;
			lv_draw_arc(layer, &arc);
			const float r = 34.0f, a = (float)(-80.0 * 3.14159265 / 180.0);
			float tx = 50.0f + r * std::cos(a), ty = 54.0f + r * std::sin(a);
			Pt head[3] = {{tx - 26.0f, ty}, {tx + 2.0f, ty - 15.0f}, {tx + 2.0f, ty + 15.0f}};
			for (Pt& p : head)
			{
				if (cw)
				{
					p.x = 100.0f - p.x; // mirrored
				}
				p = {ox + p.x * k, oy + p.y * k};
			}
			triangle(layer, head, colour);
			return;
		}
		default:
			break;
		}
		float deg = 0.0f;
		switch (icon)
		{
		case JogIcon::Right:
			deg = 90.0f;
			break;
		case JogIcon::Down:
			deg = 180.0f;
			break;
		case JogIcon::Left:
			deg = 270.0f;
			break;
		case JogIcon::UpRight:
			deg = 45.0f;
			break;
		case JogIcon::DownLeft:
			deg = 225.0f;
			break;
		default:
			break;
		}
		const float rad = deg * 3.14159265f / 180.0f, c = std::cos(rad), s = std::sin(rad);
		auto map = [&](Pt p) -> Pt
		{
			const float x = p.x - 50.0f, y = p.y - 50.0f;
			return {ox + (50.0f + x * c - y * s) * k, oy + (50.0f + x * s + y * c) * k};
		};
		const Pt head[3] = {map({50, 6}), map({80, 58}), map({20, 58})};
		triangle(layer, head, colour);
		const Pt s1[3] = {map({44.5f, 56}), map({55.5f, 56}), map({55.5f, 94})};
		const Pt s2[3] = {map({44.5f, 56}), map({55.5f, 94}), map({44.5f, 94})};
		triangle(layer, s1, colour);
		triangle(layer, s2, colour);
	}

	// PanelDue IconKeyboard (LARGE_FONT, 61 x 29): three rows of 5 x 5 keys, staggered, and the space bar
	void DrawKeyboardIcon(Pen& pen, lv_color_t colour)
	{
		constexpr int W = 61, H = 29;
		pen.setColour(colour);
		auto key = [&](int x, int y, int w) { pen.fillRect(x - W / 2, y - H / 2, x + w - 1 - W / 2, y + 4 - H / 2); };
		for (int x = 0; x + 5 <= W; x += 8)
		{
			key(x, 0, 5);
		}
		for (int x = 3; x + 5 <= W - 5; x += 8)
		{
			key(x, 8, 5);
		}
		for (int x = 6; x + 5 <= W - 2; x += 8)
		{
			key(x, 16, 5);
		}
		key(15, 24, 33); // space bar
	}

	void DrawGridGlyph(Pen& pen, lv_color_t colour)
	{
		pen.setColour(colour);
		for (int r = 0; r < 2; ++r)
		{
			for (int c = 0; c < 2; ++c)
			{
				const int x0 = -13 + c * 15, y0 = -13 + r * 15;
				pen.fillRoundRect(x0, y0, x0 + 10, y0 + 10);
			}
		}
	}

	void DrawListGlyph(Pen& pen, lv_color_t colour)
	{
		pen.setColour(colour);
		for (int row = -1; row <= 1; ++row)
		{
			const int ry = row * 9;
			pen.fillCircle(-10, ry, 3);
			pen.fillRect(-2, ry - 2, 14, ry + 2);
		}
	}

	void DrawTrashIcon(Pen& pen, lv_color_t colour, lv_color_t bg)
	{
		// Modern UI Display.cpp (IconTrash), the same PanelDue pixel coordinates around the centre (+1 down)
		constexpr int Dy = 1;
		pen.setColour(colour);
		pen.fillRect(-8, Dy - 24, 8, Dy - 20);		  // handle
		pen.fillRoundRect(-22, Dy - 19, 22, Dy - 12); // lid
		for (int i = 0; i < 34; ++i)				  // body, 37 px wide at the top narrowing to 29
		{
			int halfWidth = 18 - (i * 4) / 33;
			if (i >= 32)
			{
				--halfWidth; // soften the bottom corners
			}
			pen.fillRect(-halfWidth, Dy - 10 + i, halfWidth, Dy - 10 + i);
		}
		pen.setColour(bg);
		for (int k = -1; k <= 1; ++k) // three slots
		{
			pen.fillRect(k * 9 - 2, Dy - 4, k * 9 + 2, Dy + 17);
		}
	}

	void DrawPlayIcon(Pen& pen, lv_color_t colour)
	{
		// 26 x 30 PanelDue px, the tip to the right
		constexpr int HalfH = 15, W = 26, Left = -11;
		pen.setColour(colour);
		for (int yy = -HalfH; yy <= HalfH; ++yy)
		{
			const int ay = (yy < 0) ? -yy : yy;
			const int w = (W * (HalfH - ay)) / HalfH;
			if (w > 0)
			{
				pen.fillRect(Left, yy, Left + w, yy);
			}
		}
	}

	void DrawSearchGlyph(Pen& pen, lv_color_t colour, lv_color_t bg)
	{
		const int rx = -4, ry = -4, r = 11;
		pen.setColour(colour);
		pen.fillCircle(rx, ry, r + 2);
		pen.setColour(bg);
		pen.fillCircle(rx, ry, r - 2);
		pen.setColour(colour);
		const float h = (float)rx + (float)(r * 72) / 100.0f;
		pen.line(h, h, 13.0f, 13.0f, 6, true);
	}

	// Arrow up: head 22 wide, stem 10 wide; filled (lower case on) or outlined
	void DrawShiftGlyph(Pen& pen, lv_color_t colour, bool filled)
	{
		pen.setColour(colour);
		constexpr int headTop = -11, headBase = 1, stemBottom = 11;
		if (filled)
		{
			for (int yy = headTop; yy <= headBase; ++yy)
			{
				const int hw = ((yy - headTop) * 11) / (headBase - headTop);
				pen.fillRect(-hw, yy, hw, yy);
			}
			pen.fillRect(-5, headBase, 5, stemBottom);
			return;
		}
		const int32_t w = 3;
		pen.line(0, headTop, -11, headBase, w, true);
		pen.line(0, headTop, 11, headBase, w, true);
		pen.line(-11, headBase, -5, headBase, w, true);
		pen.line(5, headBase, 11, headBase, w, true);
		pen.line(-5, headBase, -5, stemBottom, w, true);
		pen.line(5, headBase, 5, stemBottom, w, true);
		pen.line(-5, stemBottom, 5, stemBottom, w, true);
	}

	// PanelDue IconEnter (LARGE_FONT, 30 x 30): return arrow, drawn in runs
	void DrawEnterIcon(Pen& pen, lv_color_t colour)
	{
		static const char* const rows[] = {
			"..............................", "..............................", "..............................",
			"..............................", "...........................###", "...........................###",
			"...........................###", "...........................###", "...........................###",
			"...........................###", "...........................###", "...........................###",
			"......##...................###", ".....###...................###", "....####...................###",
			"...####....................###", "..####.....................###", ".#############################",
			"##############################", ".#############################", "..####........................",
			"...####.......................", "....####......................", ".....###......................",
			"......##......................", "..............................", "..............................",
			"..............................", "..............................", ".............................."};
		constexpr int W = 30, H = 30;
		pen.setColour(colour);
		for (int y = 0; y < H; ++y)
		{
			for (int x = 0; x < W;)
			{
				if (rows[y][x] != '#')
				{
					++x;
					continue;
				}
				int end = x;
				while (end + 1 < W && rows[y][end + 1] == '#')
				{
					++end;
				}
				pen.fillRect(x - W / 2, y - H / 2, end - W / 2, y - H / 2);
				x = end + 1;
			}
		}
	}
} // namespace UI::Cnc
