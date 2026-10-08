/*
 * CncStockSim.cpp
 */

#include "CncStockSim.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace UI::Cnc::StockSim
{
	namespace
	{
		bool isBall(const Gcode::Operation& o)
		{
			return o.toolType.find("BALL") != std::string::npos;
		}

		// Pointed tools (V-bit, chamfer, engraver, drill): a cone with this rise per mm from the axis; 0: not
		float coneSlope(const Gcode::Operation& o)
		{
			if (o.toolAngle <= 0 || o.toolAngle >= 180)
			{
				return 0.0f;
			}
			return 1.0f / std::tan(o.toolAngle * 0.5f * 3.14159265f / 180.0f);
		}

		// The tool (radius r, flat or ball end) moved in a straight line from a to b: lower every cell it reaches.
		// Flat end: a cell is reached for the stretch of the move that passes within r of it; the lowest point
		// of that stretch is at one of its ends (the move is straight). Ball end: the sphere's surface above the
		// closest point of the move.
		void sweep(Map& m, float ax, float ay, float az, float bx, float by, float bz, float r, bool ball, float cone)
		{
			if (std::min(az, bz) >= m.top - Epsilon)
			{
				return; // above the stock
			}
			const float dx = bx - ax, dy = by - ay;
			const float len2 = dx * dx + dy * dy;
			const float r2 = r * r;
			const int i0 = std::max(0, (int)std::floor((std::min(ax, bx) - r - m.x0) / m.res));
			const int i1 = std::min(m.nx - 1, (int)std::floor((std::max(ax, bx) + r - m.x0) / m.res));
			const int j0 = std::max(0, (int)std::floor((std::min(ay, by) - r - m.y0) / m.res));
			const int j1 = std::min(m.ny - 1, (int)std::floor((std::max(ay, by) + r - m.y0) / m.res));
			if (i0 > i1 || j0 > j1)
			{
				return;
			}
			const float invLen = (len2 > 1e-12f) ? 1.0f / std::sqrt(len2) : 0.0f;
			for (int j = j0; j <= j1; ++j)
			{
				const float y = m.cellY(j);
				float* row = m.h.data() + (size_t)j * (size_t)m.nx;
				for (int i = i0; i <= i1; ++i)
				{
					const float x = m.cellX(i);
					float t = 0.0f, d2;
					if (len2 > 1e-12f)
					{
						t = std::clamp(((x - ax) * dx + (y - ay) * dy) / len2, 0.0f, 1.0f);
						const float px = ax + t * dx - x, py = ay + t * dy - y;
						d2 = px * px + py * py;
					}
					else
					{
						d2 = (x - ax) * (x - ax) + (y - ay) * (y - ay);
					}
					if (d2 > r2)
					{
						continue;
					}
					// the tool tip at the closest point of the move (straight down: its bottom)
					const float tip = (len2 > 1e-12f) ? az + (bz - az) * t : std::min(az, bz);
					float z;
					if (cone > 0)
					{
						z = tip + std::sqrt(d2) * cone; // rising outwards from the tip
					}
					else if (ball)
					{
						z = tip + r - std::sqrt(r2 - d2);
					}
					else if (len2 > 1e-12f)
					{
						// the stretch of the move within r of the cell: t0 +- w
						const float tc = ((x - ax) * dx + (y - ay) * dy) / len2;
						const float perp2 =
							std::max(0.0f, ((x - ax) * (x - ax) + (y - ay) * (y - ay)) - tc * tc * len2);
						const float w = std::sqrt(std::max(0.0f, r2 - perp2)) * invLen;
						const float ta = std::clamp(tc - w, 0.0f, 1.0f), tb = std::clamp(tc + w, 0.0f, 1.0f);
						z = std::min(az + (bz - az) * ta, az + (bz - az) * tb);
					}
					else
					{
						z = std::min(az, bz); // straight down (a plunge or a drill)
					}
					if (z < row[i])
					{
						row[i] = std::max(z, m.bottom);
					}
				}
			}
		}
	} // namespace

	int Map::column(float x) const
	{
		const int i = (int)std::floor((x - x0) / res);
		return (i >= 0 && i < nx) ? i : -1;
	}

	int Map::row(float y) const
	{
		const int j = (int)std::floor((y - y0) / res);
		return (j >= 0 && j < ny) ? j : -1;
	}

	Map Create(const Gcode::Program& program)
	{
		Map m;
		const Gcode::Box& b = program.stock;
		if (b.empty || b.sizeX() <= 0.0f || b.sizeY() <= 0.0f || b.sizeZ() <= 0.0f)
		{
			return m;
		}
		m.res = std::max(MinResolution, std::sqrt(b.sizeX() * b.sizeY() / (float)MaxCells));
		m.nx = std::max(1, (int)std::ceil(b.sizeX() / m.res));
		m.ny = std::max(1, (int)std::ceil(b.sizeY() / m.res));
		while ((size_t)m.nx * (size_t)m.ny > MaxCells)
		{
			m.res *= 1.05f; // rounding up made it a little too many
			m.nx = std::max(1, (int)std::ceil(b.sizeX() / m.res));
			m.ny = std::max(1, (int)std::ceil(b.sizeY() / m.res));
		}
		m.x0 = b.minX;
		m.y0 = b.minY;
		m.top = b.maxZ;
		m.bottom = b.minZ;
		m.h.assign((size_t)m.nx * (size_t)m.ny, m.top);
		m.lastOp = -1;
		return m;
	}

	size_t CutTo(Map& map, const Gcode::Program& program, int lastOp)
	{
		if (map.empty())
		{
			return 0;
		}
		lastOp = std::min(lastOp, (int)program.ops.size() - 1);
		if (lastOp < map.lastOp)
		{
			std::fill(map.h.begin(), map.h.end(), map.top); // start again from the stock
			map.lastOp = -1;
		}
		size_t moves = 0;
		const auto& pts = program.points;
		for (int k = map.lastOp + 1; k <= lastOp; ++k)
		{
			const Gcode::Operation& o = program.ops[(size_t)k];
			const float r = ((o.toolDiameter > 0.0f) ? o.toolDiameter : DefaultDiameter) / 2.0f;
			const bool ball = isBall(o);
			const float cone = coneSlope(o);
			const uint32_t end = std::min<uint32_t>(o.firstPoint + o.pointCount, (uint32_t)pts.size());
			for (uint32_t p = std::max<uint32_t>(o.firstPoint, 1); p < end; ++p)
			{
				if (pts[p].kind != Gcode::PointKind::Cut)
				{
					continue;
				}
				const Gcode::Point& a = pts[p - 1];
				const Gcode::Point& b = pts[p];
				sweep(map, a.x, a.y, a.z, b.x, b.y, b.z, r, ball, cone);
				++moves;
			}
		}
		map.lastOp = std::max(lastOp, map.lastOp);
		return moves;
	}

	std::vector<Feature> Features(const Map& map, size_t maxFeatures)
	{
		std::vector<Feature> out;
		if (map.empty())
		{
			return out;
		}
		const int nx = map.nx, ny = map.ny;
		const size_t n = (size_t)nx * (size_t)ny;
		// below the part's top (after a facing pass that is lower than the stock's)
		float partTop = map.bottom;
		for (float z : map.h)
		{
			partTop = std::max(partTop, z);
		}
		const float limit = partTop - Epsilon;
		std::vector<int32_t> label(n, 0);
		struct Stats
		{
			int i0, i1, j0, j1;
			size_t cells;
			bool edge;
		};
		std::vector<Stats> stats;
		std::vector<int32_t> stack;
		for (int j = 0; j < ny; ++j)
		{
			for (int i = 0; i < nx; ++i)
			{
				const size_t c = (size_t)j * (size_t)nx + (size_t)i;
				if (label[c] != 0 || map.h[c] >= limit)
				{
					continue;
				}
				const int32_t id = (int32_t)stats.size() + 1;
				stats.push_back({i, i, j, j, 0, false});
				Stats& s = stats.back();
				label[c] = id;
				stack.assign(1, (int32_t)c);
				while (!stack.empty())
				{
					const int32_t cell = stack.back();
					stack.pop_back();
					const int ci = cell % nx, cj = cell / nx;
					++s.cells;
					s.i0 = std::min(s.i0, ci);
					s.i1 = std::max(s.i1, ci);
					s.j0 = std::min(s.j0, cj);
					s.j1 = std::max(s.j1, cj);
					if (ci == 0 || cj == 0 || ci == nx - 1 || cj == ny - 1)
					{
						s.edge = true; // open to the side of the stock: part of the outline, not hidden
					}
					const int ni[4] = {ci - 1, ci + 1, ci, ci};
					const int nj[4] = {cj, cj, cj - 1, cj + 1};
					for (int k = 0; k < 4; ++k)
					{
						if (ni[k] < 0 || nj[k] < 0 || ni[k] >= nx || nj[k] >= ny)
						{
							continue;
						}
						const size_t nc = (size_t)nj[k] * (size_t)nx + (size_t)ni[k];
						if (label[nc] == 0 && map.h[nc] < limit)
						{
							label[nc] = id;
							stack.push_back((int32_t)nc);
						}
					}
				}
			}
		}
		// The largest inner features
		std::vector<int32_t> order;
		for (size_t k = 0; k < stats.size(); ++k)
		{
			if (!stats[k].edge)
			{
				order.push_back((int32_t)k);
			}
		}
		std::sort(order.begin(),
				  order.end(),
				  [&](int32_t a, int32_t b) { return stats[(size_t)a].cells > stats[(size_t)b].cells; });
		if (order.size() > maxFeatures)
		{
			order.resize(maxFeatures);
		}
		std::vector<int32_t> slot(stats.size() + 1, -1);
		const float nan = std::numeric_limits<float>::quiet_NaN();
		for (size_t k = 0; k < order.size(); ++k)
		{
			const Stats& s = stats[(size_t)order[k]];
			Feature f;
			f.i0 = s.i0;
			f.i1 = s.i1;
			f.j0 = s.j0;
			f.j1 = s.j1;
			f.cells = s.cells;
			f.deepestX.assign((size_t)(s.i1 - s.i0 + 1), nan);
			f.deepestY.assign((size_t)(s.j1 - s.j0 + 1), nan);
			slot[(size_t)order[k] + 1] = (int32_t)out.size();
			out.push_back(std::move(f));
		}
		for (int j = 0; j < ny; ++j)
		{
			for (int i = 0; i < nx; ++i)
			{
				const size_t c = (size_t)j * (size_t)nx + (size_t)i;
				const int32_t s = (label[c] > 0) ? slot[(size_t)label[c]] : -1;
				if (s < 0)
				{
					continue;
				}
				Feature& f = out[(size_t)s];
				const float z = map.h[c];
				float& dx = f.deepestX[(size_t)(i - f.i0)];
				float& dy = f.deepestY[(size_t)(j - f.j0)];
				if (std::isnan(dx) || z < dx)
				{
					dx = z;
				}
				if (std::isnan(dy) || z < dy)
				{
					dy = z;
				}
			}
		}
		return out;
	}
} // namespace UI::Cnc::StockSim
