/*
 * test_cnc_stock_sim.cpp
 *
 * PREVIEW, PART: the stock simulation (StockSim): facing, a pocket, holes through, a flat and a ball end
 * mill's footprint, starting again for an earlier operation, and the features of the FRONT / SIDE drawings.
 */

#include "UI/Cnc/CncGcode.h"
#include "UI/Cnc/CncStockSim.h"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

using namespace UI::Cnc;

namespace
{
	std::string readFile(const std::string& path)
	{
		std::ifstream f(path, std::ios::binary);
		std::stringstream ss;
		ss << f.rdbuf();
		return ss.str();
	}

	float heightAt(const StockSim::Map& m, float x, float y)
	{
		return m.at(m.column(x), m.row(y));
	}
} // namespace

TEST(TestCncStockSim, BracketOperationByOperation)
{
	const Gcode::Program p = Gcode::Parse(readFile("tests/object_model/cnc/gcode/bracket_fusion.nc"));
	ASSERT_EQ(p.ops.size(), 5u);
	StockSim::Map m = StockSim::Create(p);
	ASSERT_FALSE(m.empty());
	EXPECT_FLOAT_EQ(m.top, 0.0f);
	EXPECT_FLOAT_EQ(m.bottom, -20.0f);
	EXPECT_LE((size_t)m.nx * (size_t)m.ny, StockSim::MaxCells);
	EXPECT_FLOAT_EQ(m.res, StockSim::MinResolution); // 120 x 80: the finest grid

	StockSim::CutTo(m, p, 0); // FACE, 0.5 mm
	EXPECT_NEAR(heightAt(m, 60, 40), -0.5f, 0.01f);
	EXPECT_NEAR(heightAt(m, 2, 2), -0.5f, 0.01f);

	StockSim::CutTo(m, p, 1); // ADAPTIVE: the pocket, 8 mm
	EXPECT_NEAR(heightAt(m, 60, 40), -8.0f, 0.01f);
	EXPECT_NEAR(heightAt(m, 15, 15), -0.5f, 0.01f); // not a hole yet

	StockSim::CutTo(m, p, 4);						   // CONTOUR, POCKET FINISHING, DRILL
	EXPECT_FALSE(m.material(m.column(15), m.row(15))); // drilled through
	EXPECT_FALSE(m.material(m.column(105), m.row(65)));
	EXPECT_FALSE(m.material(m.column(0.3f), m.row(0.3f))); // the contour's rounded corner
	EXPECT_TRUE(m.material(m.column(60), m.row(5)));
	EXPECT_NEAR(heightAt(m, 31, 40), -8.0f, 0.01f); // the finishing pass widened the pocket

	const auto features = StockSim::Features(m);
	ASSERT_EQ(features.size(), 5u);					 // the pocket and four holes; the outside (contour) is not hidden
	EXPECT_GT(features[0].cells, features[1].cells); // largest first: the pocket
	for (const auto& f : features)
	{
		EXPECT_GT(f.i0, 0);
		EXPECT_LT(f.i1, m.nx - 1);
	}

	StockSim::CutTo(m, p, 1); // back to an earlier operation: starts again from the stock
	EXPECT_EQ(m.lastOp, 1);
	EXPECT_TRUE(m.material(m.column(15), m.row(15)));
}

TEST(TestCncStockSim, FlatAndBallFootprints)
{
	const char* const Job = ";CNC_STOCK X=30 Y=20 Z=10 X0=-5 Y0=-10 Z0=0\n"
							";CNC_TOOL T1 D=6 TYPE=FLAT\n"
							";CNC_TOOL T2 D=6 TYPE=BALL\n"
							"(Flat)\nT1\nM3 S10000\nG0 X0 Y5 Z2\nG1 Z-2 F200\nG1 X20\nG0 Z5\n"
							"(Ball)\nT2\nG0 X0 Y-5 Z2\nG1 Z-2 F200\nG1 X20\nG0 Z5\n";
	const Gcode::Program p = Gcode::Parse(Job);
	ASSERT_EQ(p.ops.size(), 2u);
	StockSim::Map m = StockSim::Create(p);
	StockSim::CutTo(m, p, 1);
	// flat: the full depth across its whole width, nothing beyond it
	EXPECT_NEAR(heightAt(m, 10, 5), -2.0f, 0.01f);
	EXPECT_NEAR(heightAt(m, 10, 7.6f), -2.0f, 0.01f);
	EXPECT_NEAR(heightAt(m, 10, 8.5f), 0.0f, 0.01f);
	// ball: the full depth in the middle, shallower to the side
	EXPECT_NEAR(heightAt(m, 10, -5), -2.0f, 0.15f);
	const float side = heightAt(m, 10, -3);
	EXPECT_GT(side, -1.9f);
	EXPECT_LT(side, -1.0f);
	// the plunge at the start of the flat cut: its whole footprint at the bottom
	EXPECT_NEAR(heightAt(m, -2.5f, 5), -2.0f, 0.01f);
}

// How long the bracket takes (printed; the panel is roughly 10x slower)
TEST(TestCncStockSim, Timing)
{
	const Gcode::Program p = Gcode::Parse(readFile("tests/object_model/cnc/gcode/bracket_fusion.nc"));
	const auto t0 = std::chrono::steady_clock::now();
	StockSim::Map m = StockSim::Create(p);
	const size_t moves = StockSim::CutTo(m, p, 4);
	const auto t1 = std::chrono::steady_clock::now();
	const auto features = StockSim::Features(m);
	const auto t2 = std::chrono::steady_clock::now();
	std::printf("[timing] simulation %zu moves %.1f ms, features %.1f ms\n",
				moves,
				std::chrono::duration<double, std::milli>(t1 - t0).count(),
				std::chrono::duration<double, std::milli>(t2 - t1).count());
	EXPECT_EQ(features.size(), 5u);
}

// A V-bit cuts a V-groove (90°: 1 mm higher per mm from the axis); a straight-down plunge of a ball or a
// pointed tool cuts at the bottom of the move
TEST(TestCncStockSim, VBitAndPlunges)
{
	const char* const Job = ";CNC_STOCK X=40 Y=20 Z=10 X0=0 Y0=0 Z0=0\n"
							";CNC_TOOL T3 D=6 TYPE=VBIT A=90\n"
							";CNC_TOOL T2 D=6 TYPE=BALL\n"
							"(Groove)\nT3\nM3 S10000\nG0 X5 Y10 Z2\nG1 Z-2 F200\nG1 X15\nG0 Z5\n"
							"(Plunge)\nT2\nM3 S10000\nG0 X30 Y10 Z2\nG1 Z-4 F100\nG0 Z5\n";
	const Gcode::Program p = Gcode::Parse(Job);
	ASSERT_EQ(p.ops.size(), 2u);
	EXPECT_FLOAT_EQ(p.ops[0].toolAngle, 90.0f);
	EXPECT_FLOAT_EQ(p.ops[1].toolAngle, 0.0f);
	StockSim::Map m = StockSim::Create(p);
	StockSim::CutTo(m, p, 1);
	EXPECT_NEAR(heightAt(m, 10, 10), -2.0f, 0.15f);	  // the tip
	EXPECT_NEAR(heightAt(m, 10, 11), -1.0f, 0.2f);	  // 1 mm out: 1 mm higher
	EXPECT_NEAR(heightAt(m, 10, 12.8f), 0.0f, 0.01f); // beyond the cone's reach at the top: untouched
	EXPECT_NEAR(heightAt(m, 30, 10), -4.0f, 0.05f);	  // the ball's plunge: its bottom
}
