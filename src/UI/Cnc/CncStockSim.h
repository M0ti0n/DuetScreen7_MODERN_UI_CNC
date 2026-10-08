/*
 * CncStockSim.h
 *
 * JOB > PREVIEW, PART: the stock as it is after an operation. A height map over the stock box (the
 * ;CNC_STOCK block, else the box around the cutting moves): each cell holds the top of the material
 * left there. Every cutting move sweeps the tool's footprint (flat end; a ball end where the file names
 * the tool a ball) along the move and lowers the cells it reaches.
 *
 *  - Resolution: 0.25 mm, coarser for large stock (at most MaxCells cells).
 *  - Each move is swept as one capsule (its two end circles and the band between them), so the work
 *    follows the area the tool covers, not the number of points.
 *  - Tool shapes: flat end; ball end (BALL in the tool's type); pointed tools (V-bit, chamfer, engraver,
 *    drill: Gcode::Operation::toolAngle) as a cone, its tip on the move.
 *  - Unknown tool diameter: DefaultDiameter.
 *  - 3-axis: one height per cell (no undercuts). A rotary axis is not simulated.
 *
 * Features (Features()): connected areas cut below the part's top (its highest material: after a facing
 * pass, that face), inside the stock (not touching its edge), each with its deepest point per column
 * along X and along Y: the hidden outlines of the FRONT / SIDE drawings (a hole behind another one
 * stays a separate outline).
 */
#pragma once

#include "CncGcode.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace UI::Cnc::StockSim
{
	constexpr size_t MaxCells = 250000;
	constexpr float MinResolution = 0.25f; // mm
	constexpr float DefaultDiameter = 3.0f;
	constexpr float Epsilon = 0.02f; // mm: "below the top", "no material left"

	struct Map
	{
		float x0 = 0, y0 = 0, res = 1; // cell (i, j) spans x0 + i * res .. x0 + (i + 1) * res
		int nx = 0, ny = 0;
		float top = 0, bottom = 0; // stock top and bottom (Z)
		std::vector<float> h;	   // top of the material in each cell, row by row (j * nx + i)
		int lastOp = -1;		   // operations 0..lastOp have cut it

		bool empty() const { return h.empty(); }
		float at(int i, int j) const { return h[(size_t)j * (size_t)nx + (size_t)i]; }
		bool material(int i, int j) const { return at(i, j) > bottom + Epsilon; }
		float cellX(int i) const { return x0 + ((float)i + 0.5f) * res; }
		float cellY(int j) const { return y0 + ((float)j + 0.5f) * res; }
		int column(float x) const; // -1 outside
		int row(float y) const;
	};

	// A new, uncut map for the program's stock (empty when there is none)
	Map Create(const Gcode::Program& program);

	// Operations up to lastOp cut: continues from map.lastOp, or starts again from the stock when lastOp is
	// before it. Returns the number of moves swept.
	size_t CutTo(Map& map, const Gcode::Program& program, int lastOp);

	struct Feature
	{
		int i0 = 0, i1 = 0, j0 = 0, j1 = 0; // its cells' bounding box
		size_t cells = 0;
		std::vector<float> deepestX; // per column i0..i1: its deepest point (NaN: no cell there)
		std::vector<float> deepestY; // per row j0..j1
	};
	std::vector<Feature> Features(const Map& map, size_t maxFeatures = 300);
} // namespace UI::Cnc::StockSim
