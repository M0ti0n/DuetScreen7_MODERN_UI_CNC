/*
 * CncPreviewView.cpp
 */

#include "CncPreviewView.h"
#include "CncLayout.h"
#include "CncState.h"
#include "CncTheme.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace UI::Cnc
{
	using namespace CncLayout;

	namespace
	{
		constexpr int32_t ToolW = 14, ToolH = 38; // the tool marker
		constexpr float DegToRad = 3.14159265f / 180.0f;
		constexpr int32_t FitMargin = 14;		   // px around the stock
		constexpr int32_t DragStartPx = 8;		   // movement before a touch becomes a drag
		constexpr int32_t DashOn = 7, DashOff = 6; // px

		// Shades (0xRRGGBB)
		constexpr uint32_t ShadeTop = 0x2B3340, ShadeBottom = 0x1A2029, ShadeY = 0x242B36, ShadeX = 0x1F2630;
		constexpr uint32_t EdgeVisible = 0x556070, EdgeHidden = 0x2E3644;
		constexpr uint32_t PathDim = 0x4C7E90;
		constexpr uint32_t PathDimPart = 0x33454F; // with PART: the other operations stay in the background
		constexpr uint32_t PathDone = 0x8C96A3;	   // a running job: the selected operation's moves already run
		// PART
		constexpr uint32_t PartBase = 0x8C97A6, PartEdge = 0xB4BDC9, PartHidden = 0x9AA4B2, PartLight = 0xA9B4C2;
		constexpr uint32_t CutFace = 0x5A626D, CutHatch = 0x8A94A2;
		constexpr float RimMm = 0.3f;									  // a step at least this high has an edge drawn
		constexpr float LightX = -0.45f, LightY = -0.55f, LightZ = 0.70f; // towards the light (normalised below)

		uint32_t scale(uint32_t c, float f)
		{
			auto ch = [&](int shift)
			{ return (uint32_t)std::clamp((float)((c >> shift) & 0xFF) * f, 0.0f, 255.0f) << shift; };
			return ch(16) | ch(8) | ch(0);
		}

		uint32_t rgb(lv_color_t c)
		{
			return ((uint32_t)c.red << 16) | ((uint32_t)c.green << 8) | (uint32_t)c.blue;
		}

		struct Preset
		{
			float yaw, pitch;
		};
		constexpr Preset Presets[4] = {{-35.0f, 32.0f}, {0.0f, 90.0f}, {0.0f, 0.0f}, {-90.0f, 0.0f}};
	} // namespace

	PreviewView::~PreviewView()
	{
		if (m_card != nullptr && lv_obj_is_valid(m_card))
		{
			lv_obj_remove_event_cb_with_user_data(m_card, touchEvent, this);
		}
	}

	void PreviewView::create(lv_obj_t* parent,
							 int32_t x,
							 int32_t y,
							 int32_t w,
							 int32_t h,
							 int32_t insetTop,
							 int32_t insetBottom,
							 int32_t insetRight)
	{
		m_insetRight = insetRight;
		m_card = CreateBare(parent, x, y, w, h);
		StyleCard(m_card, Colour::Tile(), Colour::Border());
		lv_obj_add_flag(m_card, LV_OBJ_FLAG_CLICKABLE);
		m_w = w - 2 * CardBorder;
		m_h = h - 2 * CardBorder;
		m_insetTop = insetTop;
		m_insetBottom = insetBottom;
		m_buf.assign((size_t)m_w * (size_t)m_h, 0xFF000000u | rgb(Colour::Tile()));
		m_canvas = lv_canvas_create(m_card);
		lv_obj_remove_style_all(m_canvas);
		lv_canvas_set_buffer(m_canvas, m_buf.data(), m_w, m_h, LV_COLOR_FORMAT_ARGB8888);
		lv_obj_set_pos(m_canvas, 0, 0);
		lv_obj_remove_flag(m_canvas, LV_OBJ_FLAG_CLICKABLE);
		static const char* const Names[3] = {"X", "Y", "Z"};
		static const uint32_t Colours[3] = {0xE05040, 0x56B834, 0x5FA8DC};
		for (int i = 0; i < 3; ++i)
		{
			m_axisLabels[i] = CreateLabel(
				m_card, 0, 0, 20, 22, Font::Tiny(), lv_color_hex(Colours[i]), LV_TEXT_ALIGN_CENTER, Names[i]);
		}
		// Tool marker: a shank and a tip dot, its tip at the machine's position
		m_tool = CreateBare(m_card, 0, 0, ToolW, ToolH);
		lv_obj_remove_flag(m_tool, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_t* shank = CreateBare(m_tool, (ToolW - 6) / 2, 0, 6, ToolH - 8);
		lv_obj_set_style_bg_color(shank, Colour::Cyan(), 0);
		lv_obj_set_style_bg_opa(shank, LV_OPA_80, 0);
		lv_obj_set_style_radius(shank, 2, 0);
		lv_obj_t* tip = CreateBare(m_tool, (ToolW - 12) / 2, ToolH - 12, 12, 12);
		lv_obj_set_style_bg_color(tip, Colour::Text(), 0);
		lv_obj_set_style_bg_opa(tip, LV_OPA_COVER, 0);
		lv_obj_set_style_radius(tip, LV_RADIUS_CIRCLE, 0);
		lv_obj_set_style_border_color(tip, Colour::DarkText(), 0);
		lv_obj_set_style_border_width(tip, 2, 0);
		lv_obj_remove_flag(shank, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_remove_flag(tip, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_flag(m_tool, LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_event_cb(m_card, touchEvent, LV_EVENT_ALL, this);
		setPreset(Preset::Iso);
	}

	void PreviewView::setDone(uint32_t points)
	{
		if (points == m_done)
		{
			return;
		}
		const bool sel = m_program && m_selected >= 0 && (size_t)m_selected < m_program->ops.size();
		if (!sel || points < m_done || sectionShown() || m_fast)
		{
			m_done = points; // back (another job) or nothing to add to: all again
			redraw();
			return;
		}
		// Only the moves run since the last time, over what is drawn (same projection, same width)
		const auto& pts = m_program->points;
		const Gcode::Operation& o = m_program->ops[(size_t)m_selected];
		const uint32_t first = std::max<uint32_t>(std::max(m_done, o.firstPoint), 1);
		const uint32_t end = std::min<uint32_t>({points, o.firstPoint + o.pointCount, (uint32_t)pts.size()});
		for (uint32_t k = first; k < end; ++k)
		{
			if (pts[k].kind != Gcode::PointKind::Cut)
			{
				continue;
			}
			const Proj a = project(pts[k - 1].x, pts[k - 1].y, pts[k - 1].z);
			const Proj b = project(pts[k].x, pts[k].y, pts[k].z);
			line(a.x, a.y, b.x, b.y, PathDone, 2, false);
		}
		m_done = points;
		if (first < end)
		{
			lv_obj_invalidate(m_canvas);
		}
	}

	void PreviewView::setPartLimit(int lastOp)
	{
		if (lastOp == m_partLimit)
		{
			return;
		}
		m_partLimit = lastOp;
		if (m_part || sectionShown())
		{
			redraw(); // the part as the operations finished left it
		}
	}

	void PreviewView::setTool(bool show, float x, float y, float z)
	{
		if (show == m_toolWanted && (!show || (x == m_toolX && y == m_toolY && z == m_toolZ)))
		{
			return;
		}
		m_toolWanted = show;
		m_toolX = x;
		m_toolY = y;
		m_toolZ = z;
		placeTool();
	}

	void PreviewView::placeTool()
	{
		if (m_tool == nullptr)
		{
			return;
		}
		const lv_point_t p = canvasPoint(m_toolX, m_toolY, m_toolZ);
		const bool inside = p.x >= 0 && p.y >= 0 && p.x < m_w && p.y < m_h;
		if (!m_toolWanted || !m_program || !inside)
		{
			lv_obj_add_flag(m_tool, LV_OBJ_FLAG_HIDDEN);
			return;
		}
		lv_obj_set_pos(m_tool, p.x - ToolW / 2, p.y - (ToolH - 6)); // the tip dot's centre on the point
		lv_obj_remove_flag(m_tool, LV_OBJ_FLAG_HIDDEN);
	}

	bool PreviewView::toolShown() const
	{
		return m_tool != nullptr && !lv_obj_has_flag(m_tool, LV_OBJ_FLAG_HIDDEN);
	}

	lv_point_t PreviewView::toolTip() const
	{
		return canvasPoint(m_toolX, m_toolY, m_toolZ);
	}

	// ---- Settings ----

	void PreviewView::setProgram(std::shared_ptr<const Gcode::Program> program)
	{
		m_program = std::move(program);
		m_selected = 0;
		m_done = 0;
		m_partLimit = NoPartLimit;
		m_cutSet = false; // the point belonged to the other file
		m_tapPending = false;
		fit();
		redraw();
	}

	void PreviewView::setSelected(int op)
	{
		m_selected = op;
		redraw();
	}

	void PreviewView::setShowRapids(bool show)
	{
		m_rapids = show;
		redraw();
	}

	void PreviewView::setPreset(Preset p)
	{
		m_preset = (p == Preset::Free) ? Preset::Iso : p;
		m_yaw = Presets[(int)m_preset].yaw;
		m_pitch = Presets[(int)m_preset].pitch;
		fit();
		redraw();
		if (m_onPresetChanged)
		{
			m_onPresetChanged();
		}
	}

	void PreviewView::nextPreset()
	{
		switch (m_preset)
		{
		case Preset::Iso:
			setPreset(Preset::Top);
			break;
		case Preset::Top:
			setPreset(Preset::Front);
			break;
		case Preset::Front:
			setPreset(Preset::Side);
			break;
		default:
			setPreset(Preset::Iso);
			break;
		}
	}

	void PreviewView::rotate(float dYawDeg, float dPitchDeg)
	{
		m_yaw = std::fmod(m_yaw + dYawDeg, 360.0f);
		m_pitch = std::clamp(m_pitch + dPitchDeg, -90.0f, 90.0f);
		const bool changed = m_preset != Preset::Free;
		m_preset = Preset::Free;
		m_cosY = std::cos(m_yaw * DegToRad);
		m_sinY = std::sin(m_yaw * DegToRad);
		m_cosP = std::cos(m_pitch * DegToRad);
		m_sinP = std::sin(m_pitch * DegToRad);
		m_rotating = true;
		m_fast = m_program && m_program->points.size() > DragPointBudget;
		redraw(); // same scale while dragging (no size jumps)
		if (changed && m_onPresetChanged)
		{
			m_onPresetChanged();
		}
	}

	void PreviewView::endRotate()
	{
		m_rotating = false;
		m_fast = false; // full detail again
		fit();
		redraw();
	}

	// ---- Projection ----

	PreviewView::Proj PreviewView::project(float x, float y, float z) const
	{
		const float dx = x - m_cx, dy = y - m_cy, dz = z - m_cz;
		const float xr = dx * m_cosY - dy * m_sinY;
		const float yr = dx * m_sinY + dy * m_cosY;
		const float sy = dz * m_cosP + yr * m_sinP;
		return {m_ox + xr * m_scale, m_oy - sy * m_scale};
	}

	float PreviewView::depthOf(float nx, float ny, float nz) const
	{
		const float yr = nx * m_sinY + ny * m_cosY;
		return yr * m_cosP - nz * m_sinP;
	}

	void PreviewView::fit()
	{
		m_cosY = std::cos(m_yaw * DegToRad);
		m_sinY = std::sin(m_yaw * DegToRad);
		m_cosP = std::cos(m_pitch * DegToRad);
		m_sinP = std::sin(m_pitch * DegToRad);
		Gcode::Box box;
		if (m_program)
		{
			box = !m_program->stock.empty ? m_program->stock : m_program->cutBounds;
		}
		if (box.empty)
		{
			box.add(0, 0, 0);
			box.add(10, 10, -1);
		}
		m_cx = (box.minX + box.maxX) / 2;
		m_cy = (box.minY + box.maxY) / 2;
		m_cz = (box.minZ + box.maxZ) / 2;
		// extents of the box's corners at scale 1
		m_scale = 1;
		m_ox = m_oy = 0;
		float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
		for (int i = 0; i < 8; ++i)
		{
			const Proj p =
				project((i & 1) ? box.maxX : box.minX, (i & 2) ? box.maxY : box.minY, (i & 4) ? box.maxZ : box.minZ);
			minX = std::min(minX, p.x);
			maxX = std::max(maxX, p.x);
			minY = std::min(minY, p.y);
			maxY = std::max(maxY, p.y);
		}
		const float availW = (float)(m_w - 2 * FitMargin - m_insetRight);
		const float availH = (float)(m_h - m_insetTop - m_insetBottom - 2 * FitMargin);
		const float spanX = std::max(maxX - minX, 1e-3f), spanY = std::max(maxY - minY, 1e-3f);
		m_scale = std::min(availW / spanX, availH / spanY);
		m_ox = (float)(m_w - m_insetRight) / 2 - (minX + maxX) / 2 * m_scale;
		m_oy = (float)(m_insetTop + FitMargin) + availH / 2 - (minY + maxY) / 2 * m_scale;
	}

	// ---- Pixels ----

	void PreviewView::clear(uint32_t colour)
	{
		std::fill(m_buf.begin(), m_buf.end(), 0xFF000000u | colour);
	}

	void PreviewView::plot(int32_t x, int32_t y, uint32_t colour)
	{
		if (x >= 0 && y >= 0 && x < m_w && y < m_h)
		{
			m_buf[(size_t)y * (size_t)m_w + (size_t)x] = 0xFF000000u | colour;
		}
	}

	// Liang-Barsky clip to the canvas, then Bresenham
	void PreviewView::line(float x0, float y0, float x1, float y1, uint32_t colour, int width, bool dashed)
	{
		const float xmin = 0, ymin = 0, xmax = (float)(m_w - 1), ymax = (float)(m_h - 1);
		float t0 = 0, t1 = 1;
		const float dx = x1 - x0, dy = y1 - y0;
		const float p[4] = {-dx, dx, -dy, dy};
		const float q[4] = {x0 - xmin, xmax - x0, y0 - ymin, ymax - y0};
		for (int i = 0; i < 4; ++i)
		{
			if (p[i] == 0.0f)
			{
				if (q[i] < 0)
				{
					return;
				}
			}
			else
			{
				const float r = q[i] / p[i];
				if (p[i] < 0)
				{
					t0 = std::max(t0, r);
				}
				else
				{
					t1 = std::min(t1, r);
				}
			}
		}
		if (t0 > t1)
		{
			return;
		}
		int32_t ax = (int32_t)std::lround(x0 + t0 * dx), ay = (int32_t)std::lround(y0 + t0 * dy);
		const int32_t bx = (int32_t)std::lround(x0 + t1 * dx), by = (int32_t)std::lround(y0 + t1 * dy);
		const int32_t sx = (ax < bx) ? 1 : -1, sy = (ay < by) ? 1 : -1;
		const int32_t ddx = std::abs(bx - ax), ddy = -std::abs(by - ay);
		const bool xMajor = ddx >= -ddy;
		int32_t err = ddx + ddy;
		++m_drawn;
		for (;;)
		{
			const bool on = !dashed || (m_dashPhase % (uint32_t)(DashOn + DashOff)) < (uint32_t)DashOn;
			++m_dashPhase;
			if (on)
			{
				plot(ax, ay, colour);
				if (width > 1)
				{
					xMajor ? plot(ax, ay + 1, colour) : plot(ax + 1, ay, colour);
				}
			}
			if (ax == bx && ay == by)
			{
				break;
			}
			const int32_t e2 = 2 * err;
			if (e2 >= ddy)
			{
				err += ddy;
				ax += sx;
			}
			if (e2 <= ddx)
			{
				err += ddx;
				ay += sy;
			}
		}
	}

	// Convex quad, scanline fill
	void PreviewView::fillQuad(const Proj q[4], uint32_t colour)
	{
		float minY = q[0].y, maxY = q[0].y;
		for (int i = 1; i < 4; ++i)
		{
			minY = std::min(minY, q[i].y);
			maxY = std::max(maxY, q[i].y);
		}
		const int32_t y0 = std::max<int32_t>(0, (int32_t)std::ceil(minY));
		const int32_t y1 = std::min<int32_t>(m_h - 1, (int32_t)std::floor(maxY));
		for (int32_t y = y0; y <= y1; ++y)
		{
			const float fy = (float)y;
			float xl = 1e30f, xr = -1e30f;
			for (int i = 0; i < 4; ++i)
			{
				const Proj& a = q[i];
				const Proj& b = q[(i + 1) % 4];
				if ((fy >= a.y && fy <= b.y) || (fy >= b.y && fy <= a.y))
				{
					if (a.y == b.y)
					{
						xl = std::min({xl, a.x, b.x});
						xr = std::max({xr, a.x, b.x});
					}
					else
					{
						const float x = a.x + (fy - a.y) * (b.x - a.x) / (b.y - a.y);
						xl = std::min(xl, x);
						xr = std::max(xr, x);
					}
				}
			}
			const int32_t a = std::max<int32_t>(0, (int32_t)std::ceil(xl));
			const int32_t b = std::min<int32_t>(m_w - 1, (int32_t)std::floor(xr));
			uint32_t* row = m_buf.data() + (size_t)y * (size_t)m_w;
			for (int32_t x = a; x <= b; ++x)
			{
				row[x] = 0xFF000000u | colour;
			}
		}
	}

	// ---- Drawing ----

	void PreviewView::drawStock()
	{
		if (!m_program || m_program->stock.empty)
		{
			return;
		}
		const Gcode::Box& b = m_program->stock;
		auto corner = [&](int i)
		{ return project((i & 1) ? b.maxX : b.minX, (i & 2) ? b.maxY : b.minY, (i & 4) ? b.maxZ : b.minZ); };
		Proj c[8];
		for (int i = 0; i < 8; ++i)
		{
			c[i] = corner(i);
		}
		// faces: corner indices (bit 0 = x max, bit 1 = y max, bit 2 = z max), outward normal, shade
		struct Face
		{
			int v[4];
			float nx, ny, nz;
			uint32_t shade;
		};
		static const Face Faces[6] = {{{4, 5, 7, 6}, 0, 0, 1, ShadeTop},
									  {{0, 1, 3, 2}, 0, 0, -1, ShadeBottom},
									  {{0, 1, 5, 4}, 0, -1, 0, ShadeY},
									  {{2, 3, 7, 6}, 0, 1, 0, ShadeY},
									  {{0, 2, 6, 4}, -1, 0, 0, ShadeX},
									  {{1, 3, 7, 5}, 1, 0, 0, ShadeX}};
		static const int Edges[12][2] = {
			{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
		for (const auto& e : Edges)
		{
			line(c[e[0]].x, c[e[0]].y, c[e[1]].x, c[e[1]].y, EdgeHidden, 1, false);
		}
		bool visible[6];
		for (int f = 0; f < 6; ++f)
		{
			visible[f] = depthOf(Faces[f].nx, Faces[f].ny, Faces[f].nz) < -1e-4f;
			if (visible[f])
			{
				const Proj q[4] = {c[Faces[f].v[0]], c[Faces[f].v[1]], c[Faces[f].v[2]], c[Faces[f].v[3]]};
				fillQuad(q, Faces[f].shade);
			}
		}
		for (int f = 0; f < 6; ++f)
		{
			if (visible[f])
			{
				for (int k = 0; k < 4; ++k)
				{
					const Proj& a = c[Faces[f].v[k]];
					const Proj& d = c[Faces[f].v[(k + 1) % 4]];
					line(a.x, a.y, d.x, d.y, EdgeVisible, 1, false);
				}
			}
		}
	}

	void PreviewView::drawPaths()
	{
		if (!m_program)
		{
			return;
		}
		const auto& pts = m_program->points;
		const auto& ops = m_program->ops;
		const uint32_t accent = rgb(Colour::Accent());
		const uint32_t rapid = rgb(lv_color_mix(lv_color_white(), Colour::Accent(), LV_OPA_50));
		const uint32_t dim = m_part ? PathDimPart : PathDim;
		// pass 0: everything but the done moves; pass 1 (selected, a running job): the done moves, on top, as
		// they are drawn when the job goes on (setDone)
		auto drawOp = [&](size_t i, bool selected, int pass)
		{
			const Gcode::Operation& o = ops[i];
			const uint32_t end = std::min<uint32_t>(o.firstPoint + o.pointCount, (uint32_t)pts.size());
			Proj prev{0, 0};
			bool havePrev = false;
			if (o.firstPoint > 0)
			{
				const Gcode::Point& p0 = pts[o.firstPoint - 1];
				prev = project(p0.x, p0.y, p0.z);
				havePrev = true;
			}
			m_dashPhase = 0;
			// While dragging a large job: every n-th point (line breaks kept), full detail on release
			const uint32_t stride = m_fast ? (uint32_t)(pts.size() / DragPointBudget) + 1 : 1;
			for (uint32_t k = o.firstPoint; k < end; ++k)
			{
				if (stride > 1 && (k - o.firstPoint) % stride != 0 && k + 1 < end &&
					pts[k + 1].kind != Gcode::PointKind::Start && pts[k].kind != Gcode::PointKind::Start)
				{
					continue;
				}
				const Gcode::Point& p = pts[k];
				const Proj cur = project(p.x, p.y, p.z);
				const bool done = selected && k < m_done;
				if (havePrev && p.kind == Gcode::PointKind::Cut && (pass == 1) == done)
				{
					line(prev.x,
						 prev.y,
						 cur.x,
						 cur.y,
						 !selected ? dim
						 : done	   ? PathDone
								   : accent,
						 selected ? 2 : 1,
						 false);
				}
				else if (pass == 0 && havePrev && p.kind == Gcode::PointKind::Rapid && selected && m_rapids)
				{
					line(prev.x, prev.y, cur.x, cur.y, rapid, 2, true);
				}
				prev = cur;
				havePrev = true;
			}
		};
		for (size_t i = 0; i < ops.size(); ++i)
		{
			if ((int)i != m_selected)
			{
				drawOp(i, false, 0);
			}
		}
		if (m_selected >= 0 && (size_t)m_selected < ops.size())
		{
			drawOp((size_t)m_selected, true, 0);
			if (m_done > ops[(size_t)m_selected].firstPoint)
			{
				drawOp((size_t)m_selected, true, 1);
			}
		}
	}

	void PreviewView::drawTriad()
	{
		if (!m_program)
		{
			for (lv_obj_t* l : m_axisLabels)
			{
				lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN); // nothing loaded: no axes
			}
			return;
		}
		const float ox = 40, oy = (float)(m_h - 36), len = 24;
		static const uint32_t Colours[3] = {0xE05040, 0x56B834, 0x5FA8DC};
		const float dir[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
		for (int i = 0; i < 3; ++i)
		{
			const float xr = dir[i][0] * m_cosY - dir[i][1] * m_sinY;
			const float yr = dir[i][0] * m_sinY + dir[i][1] * m_cosY;
			const float sy = dir[i][2] * m_cosP + yr * m_sinP;
			const float ex = ox + xr * len, ey = oy - sy * len;
			line(ox, oy, ex, ey, Colours[i], 2, false);
			const float lx = ox + xr * len * 1.6f, ly = oy - sy * len * 1.6f;
			lv_obj_set_pos(m_axisLabels[i], (int32_t)std::lround(lx) - 10, (int32_t)std::lround(ly) - 11);
			// an axis pointing at the viewer has no length: its label would sit on the origin
			if (std::hypot(xr, sy) < 0.2f)
			{
				lv_obj_add_flag(m_axisLabels[i], LV_OBJ_FLAG_HIDDEN);
			}
			else
			{
				lv_obj_remove_flag(m_axisLabels[i], LV_OBJ_FLAG_HIDDEN);
			}
		}
	}

	void PreviewView::redraw()
	{
		if (m_canvas == nullptr)
		{
			return;
		}
		m_drawn = 0;
		clear(rgb(Colour::Tile()));
		const bool section = sectionShown();
		if ((m_part || section) && ensureMap())
		{
			if (section)
			{
				drawSection(m_preset == Preset::Front);
			}
			else
			{
				switch (m_preset)
				{
				case Preset::Top:
					drawTopDepth();
					break;
				case Preset::Front:
				case Preset::Side:
					drawDrawing(m_preset == Preset::Front);
					break;
				default:
					drawGlass();
					break;
				}
				drawPaths();
			}
		}
		else
		{
			drawStock();
			drawPaths();
		}
		drawCutMarker();
		drawTriad();
		placeTool(); // the view may have turned or zoomed
		lv_obj_invalidate(m_canvas);
		m_lastRedraw = State::Now();
	}

	uint32_t PreviewView::pixel(int32_t x, int32_t y) const
	{
		if (x < 0 || y < 0 || x >= m_w || y >= m_h)
		{
			return 0;
		}
		return m_buf[(size_t)y * (size_t)m_w + (size_t)x] & 0xFFFFFFu;
	}

	// ---- PART: the simulated stock ----

	void PreviewView::setPart(bool on)
	{
		m_part = on;
		redraw();
	}

	void PreviewView::setCutPoint(float x, float y)
	{
		m_cutSet = true;
		m_cutX = x;
		m_cutY = y;
		m_cutZ = NAN;
		redraw();
	}

	void PreviewView::clearCutPoint()
	{
		m_cutSet = false;
		redraw();
	}

	bool PreviewView::ensureMap()
	{
		if (!m_program)
		{
			return false;
		}
		if (m_mapProgram != m_program)
		{
			m_map = StockSim::Create(*m_program);
			m_mapProgram = m_program;
			m_featuresFor = -2;
		}
		if (m_map.empty())
		{
			return false;
		}
		if (m_map.lastOp != partOp())
		{
			const uint32_t t0 = State::Now();
			StockSim::CutTo(m_map, *m_program, partOp());
			m_simMs = State::Now() - t0;
		}
		return true;
	}

	void PreviewView::blend(int32_t x, int32_t y, uint32_t colour, float a)
	{
		if (x < 0 || y < 0 || x >= m_w || y >= m_h)
		{
			return;
		}
		uint32_t& d = m_buf[(size_t)y * (size_t)m_w + (size_t)x];
		const uint32_t ai = (uint32_t)std::clamp(a * 256.0f, 0.0f, 256.0f);
		auto ch = [&](int shift)
		{
			const uint32_t dc = (d >> shift) & 0xFF, sc = (colour >> shift) & 0xFF;
			return ((dc * (256 - ai) + sc * ai) >> 8) << shift;
		};
		d = 0xFF000000u | ch(16) | ch(8) | ch(0);
	}

	void PreviewView::fillRect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t colour)
	{
		x0 = std::max<int32_t>(x0, 0);
		y0 = std::max<int32_t>(y0, 0);
		x1 = std::min<int32_t>(x1, m_w - 1);
		y1 = std::min<int32_t>(y1, m_h - 1);
		for (int32_t y = y0; y <= y1; ++y)
		{
			uint32_t* row = m_buf.data() + (size_t)y * (size_t)m_w;
			for (int32_t x = x0; x <= x1; ++x)
			{
				row[x] = 0xFF000000u | colour;
			}
		}
	}

	// ISO / 3D: the part shaded (cells far from the viewer first, so nearer ones cover them), then laid over
	// the background at GlassOpacity; its edges on top
	void PreviewView::drawGlass()
	{
		const StockSim::Map& m = m_map;
		std::vector<uint32_t> saved;
		saved.swap(m_buf);
		m_buf.assign(saved.size(), 0); // 0 = no part here
		const float ln = std::sqrt(LightX * LightX + LightY * LightY + LightZ * LightZ);
		const float lx = LightX / ln, ly = LightY / ln, lz = LightZ / ln;
		const float ddx = m_sinY * m_cosP, ddy = m_cosY * m_cosP;					  // depth grows with X / Y by this
		const int step = (m_rotating && (size_t)m.nx * (size_t)m.ny > 60000) ? 2 : 1; // full detail when it stops
		const float cellPx = m.res * m_scale * (float)step;
		const int32_t halfW = (int32_t)std::ceil(cellPx / 2) + 1;
		const int32_t halfH = (int32_t)std::ceil(cellPx * std::max(std::fabs(m_sinP), 0.3f) / 2) + 1;
		const int iFirst = (ddx > 0) ? m.nx - 1 : 0, iDir = (ddx > 0) ? -step : step;
		const int jFirst = (ddy > 0) ? m.ny - 1 : 0, jDir = (ddy > 0) ? -step : step;
		const int wallDi = (ddx > 0) ? -1 : 1, wallDj = (ddy > 0) ? -1 : 1; // the neighbour towards the viewer
		const uint32_t wallXShade = scale(PartBase, 0.40f + 0.55f * std::max(0.0f, (float)wallDi * lx));
		const uint32_t wallYShade = scale(PartBase, 0.40f + 0.55f * std::max(0.0f, (float)wallDj * ly));
		auto height = [&](int i, int j) -> float
		{
			if (i < 0 || j < 0 || i >= m.nx || j >= m.ny)
			{
				return m.bottom;
			}
			return m.at(i, j);
		};
		for (int jn = 0, j = jFirst; jn < m.ny; jn += step, j += jDir)
		{
			for (int in = 0, i = iFirst; in < m.nx; in += step, i += iDir)
			{
				if (!m.material(i, j))
				{
					continue;
				}
				const float h = m.at(i, j);
				const float x = m.cellX(i), y = m.cellY(j);
				// walls towards the viewer: down to the neighbour (or the bottom)
				const float hx = height(i + wallDi * step, j), hy = height(i, j + wallDj * step);
				if (hx < h - 1e-3f)
				{
					const Proj a = project(x + (float)wallDi * m.res * (float)step / 2, y, h);
					const Proj b = project(x + (float)wallDi * m.res * (float)step / 2, y, std::max(hx, m.bottom));
					fillRect((int32_t)a.x - halfW,
							 (int32_t)std::min(a.y, b.y),
							 (int32_t)a.x + halfW,
							 (int32_t)std::max(a.y, b.y),
							 wallXShade);
				}
				if (hy < h - 1e-3f)
				{
					const Proj a = project(x, y + (float)wallDj * m.res * (float)step / 2, h);
					const Proj b = project(x, y + (float)wallDj * m.res * (float)step / 2, std::max(hy, m.bottom));
					fillRect((int32_t)a.x - halfW,
							 (int32_t)std::min(a.y, b.y),
							 (int32_t)a.x + halfW,
							 (int32_t)std::max(a.y, b.y),
							 wallYShade);
				}
				// top: shaded by its slope
				const float gx = (height(i + 1, j) - height(i - 1, j)) / (2 * m.res);
				const float gy = (height(i, j + 1) - height(i, j - 1)) / (2 * m.res);
				const float nl = std::sqrt(gx * gx + gy * gy + 1);
				const float lam = std::max(0.0f, (-gx * lx - gy * ly + lz) / nl);
				const Proj c = project(x, y, h);
				fillRect((int32_t)c.x - halfW,
						 (int32_t)c.y - halfH,
						 (int32_t)c.x + halfW,
						 (int32_t)c.y + halfH,
						 scale(PartBase, 0.45f + 0.65f * lam));
			}
		}
		m_layer.swap(m_buf);
		m_buf.swap(saved);
		for (size_t k = 0; k < m_buf.size(); ++k)
		{
			if (m_layer[k] != 0)
			{
				const int32_t x = (int32_t)(k % (size_t)m_w), y = (int32_t)(k / (size_t)m_w);
				blend(x, y, m_layer[k] & 0xFFFFFFu, GlassOpacity);
			}
		}
		drawRims(PartEdge);
	}

	// The edges of the part: where the material steps down by RimMm or more (pocket rims and floors, holes,
	// the outside of the part), at the top of the step and at its foot
	void PreviewView::drawRims(uint32_t colour)
	{
		const StockSim::Map& m = m_map;
		const uint32_t foot = scale(colour, 0.7f);
		const bool top = m_preset == Preset::Top;
		for (int j = 0; j < m.ny; ++j)
		{
			for (int i = 0; i < m.nx; ++i)
			{
				if (!m.material(i, j))
				{
					continue;
				}
				const float h = m.at(i, j);
				const float x = m.cellX(i), y = m.cellY(j), r = m.res / 2;
				const int ni[4] = {i - 1, i + 1, i, i};
				const int nj[4] = {j, j, j - 1, j + 1};
				for (int k = 0; k < 4; ++k)
				{
					const bool inside = ni[k] >= 0 && nj[k] >= 0 && ni[k] < m.nx && nj[k] < m.ny;
					const float hn = inside && m.material(ni[k], nj[k]) ? m.at(ni[k], nj[k]) : m.bottom;
					if (h - hn < RimMm)
					{
						continue;
					}
					// the shared side of the two cells
					float x0, y0, x1, y1;
					if (k < 2)
					{
						x0 = x1 = x + (k == 0 ? -r : r);
						y0 = y - r;
						y1 = y + r;
					}
					else
					{
						y0 = y1 = y + (k == 2 ? -r : r);
						x0 = x - r;
						x1 = x + r;
					}
					const Proj a = project(x0, y0, h), b = project(x1, y1, h);
					line(a.x, a.y, b.x, b.y, colour, 1, false);
					if (!top)
					{
						const Proj c = project(x0, y0, hn), d = project(x1, y1, hn);
						line(c.x, c.y, d.x, d.y, foot, 1, false);
					}
				}
			}
		}
	}

	// TOP: the part shaded by depth (deeper darker), its edges on top
	void PreviewView::drawTopDepth()
	{
		const StockSim::Map& m = m_map;
		const float span = std::max(m.top - m.bottom, 1e-3f);
		const float cellPx = m.res * m_scale;
		const int32_t half = (int32_t)std::ceil(cellPx / 2) + 1;
		for (int j = 0; j < m.ny; ++j)
		{
			for (int i = 0; i < m.nx; ++i)
			{
				if (!m.material(i, j))
				{
					continue;
				}
				const float h = m.at(i, j);
				const Proj c = project(m.cellX(i), m.cellY(j), h);
				fillRect((int32_t)c.x - half,
						 (int32_t)c.y - half,
						 (int32_t)c.x + half,
						 (int32_t)c.y + half,
						 scale(PartLight, 0.95f - 0.55f * (m.top - h) / span));
			}
		}
		drawRims(PartEdge);
	}

	// FRONT (along X) / SIDE (along Y) as a drawing: the part's outline, where it has material somewhere
	// (light) and all through (denser), each hidden feature's outline dashed
	void PreviewView::drawDrawing(bool alongX)
	{
		const StockSim::Map& m = m_map;
		if (m_featuresFor != m.lastOp)
		{
			m_features = StockSim::Features(m);
			m_featuresFor = m.lastOp;
		}
		const int n = alongX ? m.nx : m.ny, other = alongX ? m.ny : m.nx;
		const float cOther = alongX ? (m.y0 + (float)m.ny * m.res / 2) : (m.x0 + (float)m.nx * m.res / 2);
		auto at = [&](int u, int v) { return alongX ? m.at(u, v) : m.at(v, u); };
		auto has = [&](int u, int v) { return alongX ? m.material(u, v) : m.material(v, u); };
		auto pos = [&](int u) { return alongX ? m.cellX(u) : m.cellY(u); };
		auto proj = [&](float u, float z) { return alongX ? project(u, cOther, z) : project(cOther, u, z); };
		const int32_t half = (int32_t)std::ceil(m.res * m_scale / 2) + 1;
		std::vector<float> maxH((size_t)n, std::numeric_limits<float>::quiet_NaN());
		for (int u = 0; u < n; ++u)
		{
			float hi = -1e30f, lo = 1e30f;
			bool any = false;
			for (int v = 0; v < other; ++v)
			{
				const float z = has(u, v) ? at(u, v) : m.bottom;
				lo = std::min(lo, z);
				if (has(u, v))
				{
					hi = std::max(hi, z);
					any = true;
				}
			}
			if (!any)
			{
				continue;
			}
			maxH[(size_t)u] = hi;
			const Proj b = proj(pos(u), m.bottom), t = proj(pos(u), hi), a = proj(pos(u), lo);
			for (int32_t y = (int32_t)t.y; y <= (int32_t)b.y; ++y)
			{
				for (int32_t x = (int32_t)b.x - half; x <= (int32_t)b.x + half; ++x)
				{
					blend(x, y, PartBase, (y >= (int32_t)a.y) ? 0.55f : 0.30f);
				}
			}
		}
		// hidden features: lighter inside, dashed outline
		for (const StockSim::Feature& f : m_features)
		{
			const std::vector<float>& d = alongX ? f.deepestX : f.deepestY;
			const int u0 = alongX ? f.i0 : f.j0;
			int first = -1, last = -1;
			m_dashPhase = 0;
			Proj prev{0, 0};
			for (size_t k = 0; k < d.size(); ++k)
			{
				if (std::isnan(d[k]))
				{
					continue;
				}
				const int u = u0 + (int)k;
				const float top = std::isnan(maxH[(size_t)u]) ? m.top : maxH[(size_t)u];
				const Proj p = proj(pos(u), d[k]), t = proj(pos(u), top);
				for (int32_t y = (int32_t)t.y; y < (int32_t)p.y; ++y)
				{
					for (int32_t x = (int32_t)p.x - half; x <= (int32_t)p.x + half; ++x)
					{
						blend(x, y, PartLight, 0.18f);
					}
				}
				const Proj l = proj(pos(u) - m.res / 2, d[k]), r = proj(pos(u) + m.res / 2, d[k]);
				if (first >= 0 && std::fabs(prev.y - l.y) > 0.5f)
				{
					line(l.x, prev.y, l.x, l.y, PartHidden, 1, true); // a step in the floor
				}
				line(l.x, l.y, r.x, r.y, PartHidden, 1, true);
				prev = r;
				if (first < 0)
				{
					first = u;
				}
				last = u;
			}
			if (first >= 0)
			{
				for (int u : {first, last})
				{
					const float edge = pos(u) + ((u == first) ? -m.res / 2 : m.res / 2);
					const float dz = d[(size_t)(u - u0)];
					const float top = std::isnan(maxH[(size_t)u]) ? m.top : maxH[(size_t)u];
					const Proj a = proj(edge, dz), b = proj(edge, top);
					line(a.x, a.y, b.x, b.y, PartHidden, 1, true);
				}
			}
		}
		// outline: the top profile, the ends and the bottom
		int firstU = -1, lastU = -1;
		Proj prev{0, 0};
		for (int u = 0; u < n; ++u)
		{
			if (std::isnan(maxH[(size_t)u]))
			{
				continue;
			}
			const Proj l = proj(pos(u) - m.res / 2, maxH[(size_t)u]), r = proj(pos(u) + m.res / 2, maxH[(size_t)u]);
			if (firstU >= 0 && u == lastU + 1 && std::fabs(prev.y - l.y) > 0.5f)
			{
				line(l.x, prev.y, l.x, l.y, PartEdge, 1, false);
			}
			line(l.x, l.y, r.x, r.y, PartEdge, 1, false);
			prev = r;
			if (firstU < 0)
			{
				firstU = u;
			}
			lastU = u;
		}
		if (firstU >= 0)
		{
			const Proj a = proj(pos(firstU) - m.res / 2, m.bottom),
					   b = proj(pos(firstU) - m.res / 2, maxH[(size_t)firstU]);
			const Proj c = proj(pos(lastU) + m.res / 2, m.bottom),
					   d = proj(pos(lastU) + m.res / 2, maxH[(size_t)lastU]);
			line(a.x, a.y, b.x, b.y, PartEdge, 1, false);
			line(c.x, c.y, d.x, d.y, PartEdge, 1, false);
			line(a.x, a.y, c.x, c.y, PartEdge, 1, false);
		}
	}

	// CUT X (along X at the cut point's Y, seen from the front) / CUT Y (along Y at its X, from the right):
	// the part behind the cut faint, the cut face hatched, the selected operation's moves that reach the
	// cut, a dot where one passes through it, a mark over the cut point
	void PreviewView::drawSection(bool alongX)
	{
		const StockSim::Map& m = m_map;
		const float c = alongX ? m_cutY : m_cutX;
		const int line0 = alongX ? m.row(c) : m.column(c);
		const int n = alongX ? m.nx : m.ny, other = alongX ? m.ny : m.nx;
		auto at = [&](int u, int v) { return alongX ? m.at(u, v) : m.at(v, u); };
		auto has = [&](int u, int v) { return alongX ? m.material(u, v) : m.material(v, u); };
		auto pos = [&](int u) { return alongX ? m.cellX(u) : m.cellY(u); };
		auto proj = [&](float u, float z) { return alongX ? project(u, c, z) : project(c, u, z); };
		const int32_t half = (int32_t)std::ceil(m.res * m_scale / 2) + 1;
		// behind the cut: the viewer is in front (-Y) for CUT X, on the right (+X) for CUT Y
		const int v0 = (line0 < 0) ? 0 : (alongX ? line0 : 0);
		const int v1 = (line0 < 0) ? other - 1 : (alongX ? other - 1 : line0);
		for (int u = 0; u < n; ++u)
		{
			float hi = -1e30f;
			for (int v = v0; v <= v1; ++v)
			{
				if (has(u, v))
				{
					hi = std::max(hi, at(u, v));
				}
			}
			if (hi < -1e29f)
			{
				continue;
			}
			const Proj b = proj(pos(u), m.bottom), t = proj(pos(u), hi);
			for (int32_t y = (int32_t)t.y; y <= (int32_t)b.y; ++y)
			{
				for (int32_t x = (int32_t)b.x - half; x <= (int32_t)b.x + half; ++x)
				{
					blend(x, y, PartBase, 0.16f);
				}
			}
			const Proj l = proj(pos(u) - m.res / 2, hi), r = proj(pos(u) + m.res / 2, hi);
			line(l.x, l.y, r.x, r.y, scale(PartEdge, 0.6f), 1, false);
		}
		if (line0 >= 0)
		{
			// the cut face, column by column, hatched
			int runStart = -1;
			Proj prev{0, 0};
			for (int u = 0; u <= n; ++u)
			{
				const bool mat = u < n && has(u, line0);
				if (mat)
				{
					const float h = at(u, line0);
					const Proj b = proj(pos(u), m.bottom), t = proj(pos(u), h);
					for (int32_t y = (int32_t)t.y; y <= (int32_t)b.y; ++y)
					{
						if (y < 0 || y >= m_h)
						{
							continue;
						}
						uint32_t* row = m_buf.data() + (size_t)y * (size_t)m_w;
						for (int32_t x = std::max<int32_t>(0, (int32_t)b.x - half);
							 x <= std::min<int32_t>(m_w - 1, (int32_t)b.x + half);
							 ++x)
						{
							row[x] = 0xFF000000u | (((x + y) % 7 == 0) ? CutHatch : CutFace);
						}
					}
				}
				// outline of each run of material
				if (mat && runStart < 0)
				{
					runStart = u;
				}
				if (!mat && runStart >= 0)
				{
					const int a = runStart, z = u - 1;
					for (int k = a; k <= z; ++k)
					{
						const float h = at(k, line0);
						const Proj l = proj(pos(k) - m.res / 2, h), r = proj(pos(k) + m.res / 2, h);
						if (k > a && std::fabs(prev.y - l.y) > 0.5f)
						{
							line(l.x, prev.y, l.x, l.y, PartEdge, 1, false);
						}
						line(l.x, l.y, r.x, r.y, PartEdge, 1, false);
						prev = r;
					}
					const Proj p0 = proj(pos(a) - m.res / 2, m.bottom), p1 = proj(pos(a) - m.res / 2, at(a, line0));
					const Proj q0 = proj(pos(z) + m.res / 2, m.bottom), q1 = proj(pos(z) + m.res / 2, at(z, line0));
					line(p0.x, p0.y, p1.x, p1.y, PartEdge, 1, false);
					line(q0.x, q0.y, q1.x, q1.y, PartEdge, 1, false);
					line(p0.x, p0.y, q0.x, q0.y, PartEdge, 1, false);
					runStart = -1;
				}
			}
		}
		// the selected operation's moves that reach the cut
		if (m_program && m_selected >= 0 && (size_t)m_selected < m_program->ops.size())
		{
			const Gcode::Operation& o = m_program->ops[(size_t)m_selected];
			const float r = ((o.toolDiameter > 0.0f) ? o.toolDiameter : StockSim::DefaultDiameter) / 2.0f;
			const auto& pts = m_program->points;
			const uint32_t accent = rgb(Colour::Accent());
			const uint32_t end = std::min<uint32_t>(o.firstPoint + o.pointCount, (uint32_t)pts.size());
			for (uint32_t k = std::max<uint32_t>(o.firstPoint, 1); k < end; ++k)
			{
				if (pts[k].kind != Gcode::PointKind::Cut)
				{
					continue;
				}
				const Gcode::Point& a = pts[k - 1];
				const Gcode::Point& b = pts[k];
				const float wa = alongX ? a.y : a.x, wb = alongX ? b.y : b.x;
				const bool crosses = (wa - c) * (wb - c) < 0;
				if (!crosses && std::min(std::fabs(wa - c), std::fabs(wb - c)) > r)
				{
					continue;
				}
				const float ua = alongX ? a.x : a.y, ub = alongX ? b.x : b.y;
				const Proj pa = proj(ua, a.z), pb = proj(ub, b.z);
				line(pa.x, pa.y, pb.x, pb.y, accent, 2, false);
				if (crosses)
				{
					const float t = (c - wa) / (wb - wa);
					const Proj d = proj(ua + (ub - ua) * t, a.z + (b.z - a.z) * t);
					for (int32_t dy = -3; dy <= 3; ++dy)
					{
						for (int32_t dx = -3; dx <= 3; ++dx)
						{
							if (dx * dx + dy * dy <= 9)
							{
								plot((int32_t)d.x + dx, (int32_t)d.y + dy, accent);
							}
						}
					}
				}
			}
		}
		// the cut point: a mark above the part
		const Proj mk = proj(alongX ? m_cutX : m_cutY, m.top);
		const uint32_t accent = rgb(Colour::Accent());
		for (int32_t k = 0; k < 9; ++k)
		{
			for (int32_t dx = -(8 - k) / 2; dx <= (8 - k) / 2; ++dx)
			{
				plot((int32_t)mk.x + dx, (int32_t)mk.y - 12 + k, accent);
			}
		}
	}

	// The cut point in the other views: a crosshair on the part's top
	void PreviewView::drawCutMarker()
	{
		if (!m_cutSet || sectionShown() || !m_program)
		{
			return;
		}
		// On the part's surface when PART shows this program's part, else at the tapped move (or the stock top)
		float z = !std::isnan(m_cutZ) ? m_cutZ : m_program->stock.empty ? 0.0f : m_program->stock.maxZ;
		if (m_part && !m_map.empty() && m_mapProgram == m_program)
		{
			const int i = m_map.column(m_cutX), j = m_map.row(m_cutY);
			if (i >= 0 && j >= 0)
			{
				z = m_map.at(i, j);
			}
		}
		const Proj p = project(m_cutX, m_cutY, z);
		const uint32_t accent = rgb(Colour::Accent());
		line(p.x - 10, p.y, p.x + 10, p.y, accent, 2, false);
		line(p.x, p.y - 10, p.x, p.y + 10, accent, 2, false);
	}

	// A tap: the nearest cutting move within PickPx, the point on it under the finger
	void PreviewView::pick(int32_t x, int32_t y)
	{
		if (!m_program)
		{
			return;
		}
		const auto& pts = m_program->points;
		const auto& ops = m_program->ops;
		// the nearest move; one of the selected operation's when it has one within reach (moves of different
		// operations often lie on top of each other, e.g. a drill under a facing pass in TOP)
		float best = (float)(PickPx * PickPx), bestSel = best;
		int bestOp = -1, selOp = -1;
		float bx = 0, by = 0, bz = 0, sx = 0, sy = 0, sz = 0;
		const float fx = (float)x, fy = (float)y;
		for (size_t o = 0; o < ops.size(); ++o)
		{
			const uint32_t end = std::min<uint32_t>(ops[o].firstPoint + ops[o].pointCount, (uint32_t)pts.size());
			for (uint32_t k = std::max<uint32_t>(ops[o].firstPoint, 1); k < end; ++k)
			{
				if (pts[k].kind != Gcode::PointKind::Cut)
				{
					continue;
				}
				const Gcode::Point& a = pts[k - 1];
				const Gcode::Point& b = pts[k];
				const Proj pa = project(a.x, a.y, a.z), pb = project(b.x, b.y, b.z);
				const float dx = pb.x - pa.x, dy = pb.y - pa.y, l2 = dx * dx + dy * dy;
				const float t =
					(l2 > 1e-6f) ? std::clamp(((fx - pa.x) * dx + (fy - pa.y) * dy) / l2, 0.0f, 1.0f) : 0.0f;
				const float ex = pa.x + t * dx - fx, ey = pa.y + t * dy - fy;
				const float d2 = ex * ex + ey * ey;
				if (d2 < best)
				{
					best = d2;
					bestOp = (int)o;
					bx = a.x + (b.x - a.x) * t;
					by = a.y + (b.y - a.y) * t;
					bz = a.z + (b.z - a.z) * t;
				}
				if ((int)o == m_selected && d2 < bestSel)
				{
					bestSel = d2;
					selOp = (int)o;
					sx = a.x + (b.x - a.x) * t;
					sy = a.y + (b.y - a.y) * t;
					sz = a.z + (b.z - a.z) * t;
				}
			}
		}
		if (selOp >= 0)
		{
			bestOp = selOp;
			bx = sx;
			by = sy;
			bz = sz;
		}
		if (bestOp >= 0)
		{
			m_cutSet = true;
			m_cutX = bx;
			m_cutY = by;
			m_cutZ = bz;
		}
		else
		{
			m_cutSet = false;
		}
		if (m_onPick)
		{
			m_onPick(bestOp, bx, by); // the page selects the operation (and redraws)
		}
		redraw();
	}

	lv_point_t PreviewView::canvasPoint(float x, float y, float z) const
	{
		const Proj p = project(x, y, z);
		return {(int32_t)std::lround(p.x), (int32_t)std::lround(p.y)};
	}

	void PreviewView::tapAt(int32_t x, int32_t y)
	{
		m_tapPending = false;
		pick(x, y);
	}

	void PreviewView::tick()
	{
		if (m_tapPending && State::Now() - m_lastTap > DoubleTapMs)
		{
			m_tapPending = false;
			pick(m_tapAt.x, m_tapAt.y);
		}
	}

	// ---- Touch ----

	void PreviewView::touchEvent(lv_event_t* e)
	{
		auto* self = static_cast<PreviewView*>(lv_event_get_user_data(e));
		const lv_event_code_t code = lv_event_get_code(e);
		lv_indev_t* indev = lv_indev_active();
		lv_point_t p{0, 0};
		if (indev != nullptr)
		{
			lv_indev_get_point(indev, &p);
		}
		switch (code)
		{
		case LV_EVENT_PRESSED:
			self->m_last = self->m_downAt = p;
			self->m_dragging = false;
			break;
		case LV_EVENT_PRESSING:
		{
			if (!self->m_dragging && std::abs(p.x - self->m_downAt.x) + std::abs(p.y - self->m_downAt.y) >= DragStartPx)
			{
				self->m_dragging = true;
			}
			if (self->m_dragging && State::Now() - self->m_lastRedraw >= MinRedrawMs)
			{
				self->rotate((float)(p.x - self->m_last.x) * DragDegreesPerPixel,
							 (float)(p.y - self->m_last.y) * DragDegreesPerPixel);
				self->m_last = p;
			}
			break;
		}
		case LV_EVENT_RELEASED:
			if (self->m_dragging)
			{
				if (p.x != self->m_last.x || p.y != self->m_last.y)
				{
					self->rotate((float)(p.x - self->m_last.x) * DragDegreesPerPixel,
								 (float)(p.y - self->m_last.y) * DragDegreesPerPixel);
				}
				self->endRotate();
			}
			break;
		case LV_EVENT_SHORT_CLICKED:
			if (!self->m_dragging)
			{
				const uint32_t now = State::Now();
				if (self->m_lastTap != 0 && now - self->m_lastTap <= DoubleTapMs)
				{
					self->m_lastTap = 0;
					self->m_tapPending = false; // not a single tap
					if (self->m_onDoubleTap)
					{
						self->m_onDoubleTap();
					}
				}
				else
				{
					self->m_lastTap = now;
					lv_area_t a;
					lv_obj_get_coords(self->m_canvas, &a);
					self->m_tapAt = {p.x - a.x1, p.y - a.y1};
					self->m_tapPending = true; // acted on in tick() unless a second tap follows
				}
			}
			break;
		default:
			break;
		}
	}
} // namespace UI::Cnc
