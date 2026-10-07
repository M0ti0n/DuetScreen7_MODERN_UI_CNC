/*
 * CncPreviewView.cpp
 */

#include "CncPreviewView.h"
#include "CncLayout.h"
#include "CncState.h"
#include "CncTheme.h"
#include <algorithm>
#include <cmath>

namespace UI::Cnc
{
	using namespace CncLayout;

	namespace
	{
		constexpr float DegToRad = 3.14159265f / 180.0f;
		constexpr int32_t FitMargin = 14;		   // px around the stock
		constexpr int32_t DragStartPx = 8;		   // movement before a touch becomes a drag
		constexpr int32_t DashOn = 7, DashOff = 6; // px

		// Shades (0xRRGGBB)
		constexpr uint32_t ShadeTop = 0x2B3340, ShadeBottom = 0x1A2029, ShadeY = 0x242B36, ShadeX = 0x1F2630;
		constexpr uint32_t EdgeVisible = 0x556070, EdgeHidden = 0x2E3644;
		constexpr uint32_t PathDim = 0x4C7E90;

		uint32_t rgb(lv_color_t c)
		{
			return ((uint32_t)c.red << 16) | ((uint32_t)c.green << 8) | (uint32_t)c.blue;
		}

		struct Preset
		{
			float yaw, pitch;
		};
		constexpr Preset Presets[3] = {{-35.0f, 32.0f}, {0.0f, 90.0f}, {0.0f, 0.0f}};
	} // namespace

	PreviewView::~PreviewView()
	{
		if (m_card != nullptr && lv_obj_is_valid(m_card))
		{
			lv_obj_remove_event_cb_with_user_data(m_card, touchEvent, this);
		}
	}

	void PreviewView::create(
		lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, int32_t insetTop, int32_t insetBottom)
	{
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
		lv_obj_add_event_cb(m_card, touchEvent, LV_EVENT_ALL, this);
		setPreset(Preset::Iso);
	}

	// ---- Settings ----

	void PreviewView::setProgram(std::shared_ptr<const Gcode::Program> program)
	{
		m_program = std::move(program);
		m_selected = 0;
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
		m_fast = m_program && m_program->points.size() > DragPointBudget;
		redraw(); // same scale while dragging (no size jumps)
		if (changed && m_onPresetChanged)
		{
			m_onPresetChanged();
		}
	}

	void PreviewView::endRotate()
	{
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
		const float availW = (float)(m_w - 2 * FitMargin);
		const float availH = (float)(m_h - m_insetTop - m_insetBottom - 2 * FitMargin);
		const float spanX = std::max(maxX - minX, 1e-3f), spanY = std::max(maxY - minY, 1e-3f);
		m_scale = std::min(availW / spanX, availH / spanY);
		m_ox = (float)m_w / 2 - (minX + maxX) / 2 * m_scale;
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
		auto drawOp = [&](size_t i, bool selected)
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
				if (havePrev && p.kind == Gcode::PointKind::Cut)
				{
					line(prev.x, prev.y, cur.x, cur.y, selected ? accent : PathDim, selected ? 2 : 1, false);
				}
				else if (havePrev && p.kind == Gcode::PointKind::Rapid && selected && m_rapids)
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
				drawOp(i, false);
			}
		}
		if (m_selected >= 0 && (size_t)m_selected < ops.size())
		{
			drawOp((size_t)m_selected, true);
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
		drawStock();
		drawPaths();
		drawTriad();
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
					if (self->m_onDoubleTap)
					{
						self->m_onDoubleTap();
					}
				}
				else
				{
					self->m_lastTap = now;
				}
			}
			break;
		default:
			break;
		}
	}
} // namespace UI::Cnc
