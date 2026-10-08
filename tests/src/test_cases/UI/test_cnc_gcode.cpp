/*
 * test_cnc_gcode.cpp
 *
 * The G-code reader of JOB > PREVIEW: operations (Fusion 360 comment lines, FreeCAD markers, tool
 * changes), tools, stock, moves (arcs, G91, G20, G53), statistics and the short operation names.
 */

#include "UI/Cnc/CncGcode.h"
#include <cmath>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

using namespace UI::Cnc::Gcode;

namespace
{
	std::string readFile(const char* path)
	{
		std::ifstream f(path, std::ios::binary);
		std::stringstream ss;
		ss << f.rdbuf();
		return ss.str();
	}
} // namespace

TEST(TestCncGcode, FusionOperationsAndTools)
{
	const std::string text = readFile("tests/object_model/cnc/gcode/bracket_fusion.nc");
	ASSERT_FALSE(text.empty());
	const Program p = Parse(text);
	ASSERT_EQ(p.ops.size(), 5u);
	EXPECT_EQ(p.ops[0].name, "Face1");
	EXPECT_EQ(p.ops[1].name, "2D Adaptive Clearing1");
	EXPECT_EQ(p.ops[2].name, "2D Contour2"); // same tool: started by its comment line
	EXPECT_EQ(p.ops[3].name, "Pocket Finishing3");
	EXPECT_EQ(p.ops[4].name, "Drill4");

	EXPECT_EQ(p.ops[0].tool, 1);
	EXPECT_FLOAT_EQ(p.ops[0].toolDiameter, 50.0f);
	EXPECT_EQ(p.ops[0].toolType, "FACE MILL");
	EXPECT_EQ(p.ops[1].tool, 2);
	EXPECT_EQ(p.ops[2].tool, 2);
	EXPECT_EQ(p.ops[1].toolType, "FLAT");
	EXPECT_FLOAT_EQ(p.ops[3].toolDiameter, 6.0f);
	EXPECT_EQ(p.ops[4].toolType, "DRILL");
	EXPECT_EQ(p.toolCount, 4u);

	EXPECT_FLOAT_EQ(p.ops[0].spindle, 8000.0f);
	EXPECT_FLOAT_EQ(p.ops[0].feed, 2500.0f);
	EXPECT_TRUE(p.ops[0].coolant);
	EXPECT_FLOAT_EQ(p.ops[1].zMin, -8.0f);
	EXPECT_FLOAT_EQ(p.ops[1].feed, 1200.0f);
	EXPECT_FLOAT_EQ(p.ops[2].zMin, -20.5f);
	EXPECT_FLOAT_EQ(p.ops[2].feed, 900.0f);
	EXPECT_FLOAT_EQ(p.ops[3].feed, 800.0f); // most of the length, not the 300 plunge
	EXPECT_FLOAT_EQ(p.ops[4].zMin, -22.0f);
	EXPECT_FALSE(p.ops[4].coolant);
	EXPECT_EQ(p.ops[4].wcs, 1u);

	// Stock from ;CNC_STOCK
	EXPECT_TRUE(p.stockFromFile);
	EXPECT_FLOAT_EQ(p.stock.sizeX(), 120.0f);
	EXPECT_FLOAT_EQ(p.stock.sizeY(), 80.0f);
	EXPECT_FLOAT_EQ(p.stock.maxZ, 0.0f);
	EXPECT_FLOAT_EQ(p.stock.minZ, -20.0f);

	// Time: every operation has some, the job is their sum
	double sum = 0;
	for (const Operation& o : p.ops)
	{
		EXPECT_GT(o.seconds, 0.0);
		EXPECT_GT(o.cutLength, 0.0);
		sum += o.seconds;
	}
	EXPECT_NEAR(p.seconds, sum, 1e-6);

	// Points: each operation's points follow the previous one's, all of them covered
	uint32_t next = 0;
	for (const Operation& o : p.ops)
	{
		EXPECT_EQ(o.firstPoint, next);
		next += o.pointCount;
	}
	EXPECT_EQ(next, p.points.size());

	// The live highlight: a position inside the drilling code is operation 5
	const size_t drillAt = text.find("(Drill4)");
	EXPECT_EQ(OperationAt(p, (uint32_t)drillAt + 40), 4);
	EXPECT_EQ(OperationAt(p, (uint32_t)text.find("(Face1)") + 20), 0);
}

TEST(TestCncGcode, ArcsFollowTheCircle)
{
	// Full circle, centre (10, 0), radius 10, counter-clockwise from (0, 0)
	const Program p = Parse("G21 G90\nG0 X0 Y0 Z0\nG1 Z-1 F100\nG3 X0 Y0 I10 J0 F500\n");
	ASSERT_EQ(p.ops.size(), 1u);
	double maxErr = 0;
	size_t arcPoints = 0;
	for (const Point& pt : p.points)
	{
		if (pt.kind == PointKind::Cut && pt.z < -0.5f)
		{
			const double r = std::hypot((double)pt.x - 10.0, (double)pt.y);
			maxErr = std::max(maxErr, std::fabs(r - 10.0));
			++arcPoints;
		}
	}
	EXPECT_GT(arcPoints, 20u);
	EXPECT_LT(maxErr, 1e-3);
	EXPECT_NEAR(p.ops[0].cutLength, 1.0 + 2 * 3.14159265 * 10, 0.05); // plunge + circumference

	// R form, clockwise quarter: (0,10) to (10,0) around (0,0)
	const Program q = Parse("G0 X0 Y10 Z0\nG2 X10 Y0 R10 F500\n");
	ASSERT_FALSE(q.points.empty());
	for (const Point& pt : q.points)
	{
		EXPECT_NEAR(std::hypot((double)pt.x, (double)pt.y), 10.0, 1e-3);
	}
	EXPECT_NEAR(q.ops[0].cutLength, 3.14159265 * 10 / 2, 0.02);
}

TEST(TestCncGcode, InchesIncrementalAndMachineMoves)
{
	const Program p = Parse("G20 G90\nG0 X0 Y0 Z0\nG1 X1 F10\nG91\nG1 Y1\nG90\nG53 G0 Z0\nG0 X0 Y0\nG0 Z0.5\nG1 Z0\n");
	ASSERT_EQ(p.ops.size(), 1u);
	EXPECT_FLOAT_EQ(p.cutBounds.maxX, 25.4f);
	EXPECT_FLOAT_EQ(p.cutBounds.maxY, 25.4f);
	EXPECT_FLOAT_EQ(p.ops[0].feed, 254.0f); // F in inch/min
	// After G53 the work Z is unknown: the next point starts a new line (no line drawn to it)
	size_t starts = 0;
	for (const Point& pt : p.points)
	{
		starts += pt.kind == PointKind::Start ? 1u : 0u;
	}
	EXPECT_EQ(starts, 2u);
}

TEST(TestCncGcode, FreeCadMarkersAndToolControllers)
{
	const Program p = Parse(readFile("tests/object_model/cnc/gcode/plate_freecad.nc"));
	ASSERT_EQ(p.ops.size(), 3u);
	EXPECT_EQ(p.ops[0].name, "Profile");
	EXPECT_EQ(p.ops[1].name, "Pocket");
	EXPECT_EQ(p.ops[2].name, "Drilling");
	EXPECT_EQ(p.ops[0].tool, 2);
	EXPECT_FLOAT_EQ(p.ops[0].toolDiameter, 6.0f);
	EXPECT_EQ(p.ops[0].toolType, "FLAT");
	EXPECT_EQ(p.ops[2].tool, 5);
	EXPECT_FLOAT_EQ(p.ops[2].toolDiameter, 3.0f);
	EXPECT_EQ(p.ops[2].toolType, "DRILL");
	// G91 inside the pocket
	EXPECT_FLOAT_EQ(p.ops[1].zMin, -2.0f);
	EXPECT_NEAR(p.ops[1].cutLength, 3.0 + 80.0, 1e-3);
	// No ;CNC_STOCK: the box around the cutting moves, grown by the largest tool radius, top at Z0
	EXPECT_FALSE(p.stockFromFile);
	EXPECT_FLOAT_EQ(p.stock.minX, -3.0f);
	EXPECT_FLOAT_EQ(p.stock.maxX, 53.0f);
	EXPECT_FLOAT_EQ(p.stock.maxZ, 0.0f);
	EXPECT_FLOAT_EQ(p.stock.minZ, -10.0f);
}

TEST(TestCncGcode, ToolChangesWithoutNames)
{
	const Program p =
		Parse("T1 M6\nG0 X0 Y0 Z5\nG1 Z-1 F100\nG1 X10\nT2 M6\nG0 X0 Y0 Z5\nG1 Z-2 F100\nG1 Y10\nG0 Z10\n");
	ASSERT_EQ(p.ops.size(), 2u);
	EXPECT_EQ(p.ops[0].name, "T1");
	EXPECT_EQ(p.ops[1].name, "T2");
	EXPECT_FLOAT_EQ(p.ops[0].toolDiameter, 0.0f); // not known
	EXPECT_EQ(p.ops[1].toolType, "");
}

TEST(TestCncGcode, MetaCommandsAndOddWords)
{
	const Program p = Parse("var depth = 3\nif move.axes[2].homed\n  echo \"ok\"\nN10 G1 X{var.depth} Y2 F100*45\n"
							"G0X0Y0Z0\ng1x5y0f200\n");
	ASSERT_EQ(p.ops.size(), 1u);
	EXPECT_FLOAT_EQ(p.cutBounds.maxX, 5.0f);
}

TEST(TestCncGcode, ManyPointsAreMerged)
{
	std::string text = "G0 X0 Y0 Z0\nG1 F1000\n";
	for (int i = 1; i <= 20000; ++i)
	{
		text += "G1 X" + std::to_string(i * 0.01) + "\n";
	}
	Options o;
	o.maxPoints = 1000;
	const Program p = Parse(text, o);
	EXPECT_TRUE(p.truncated);
	EXPECT_LT(p.points.size(), 6000u);
	EXPECT_NEAR(p.ops[0].cutLength, 200.0, 0.01); // statistics from every move
	EXPECT_NEAR(p.points.back().x, 200.0f, 0.01f);
}

TEST(TestCncGcode, ShortNames)
{
	auto s = [](const char* n)
	{
		const auto a = ShortName(n);
		return a[1].empty() ? a[0] : a[0] + "/" + a[1];
	};
	EXPECT_EQ(s("Face1"), "FACE");
	EXPECT_EQ(s("2D Adaptive Clearing1"), "ADAPT/CLEAR");
	EXPECT_EQ(s("2D Contour2"), "2D/CONTOUR");
	EXPECT_EQ(s("Pocket Finishing3"), "POCKET/FINISH");
	EXPECT_EQ(s("Drill4"), "DRILL");
	EXPECT_EQ(s("3D Parallel Finishing"), "PARALL/FINISH");
	EXPECT_EQ(s("Engraving_text_logo"), "ENGRAVE/TEXT");
	EXPECT_EQ(s("Superlongword"), "SUPERLO");
	EXPECT_EQ(s("T3"), "T3");
	EXPECT_EQ(s("--"), "OP");
}

TEST(TestCncGcode, TimeFormat)
{
	EXPECT_EQ(FormatTime(65), "1:05");
	EXPECT_EQ(FormatTime(760), "12:40");
	EXPECT_EQ(FormatTime(3723), "1:02:03");
}

TEST(TestCncGcode, DepthRangeIgnoresPlungeEntry)
{
	const Program p = Parse("G0 X0 Y0 Z5\nG1 Z-2 F100\nG1 X10 F500\nG1 Z-4\nG1 X0\nG0 Z5\n");
	ASSERT_EQ(p.ops.size(), 1u);
	EXPECT_FLOAT_EQ(p.ops[0].zMax, -2.0f); // not 5: the plunge starts above the stock
	EXPECT_FLOAT_EQ(p.ops[0].zMin, -4.0f);
	// a ramp in from above (XY and Z together) does not count either
	const Program q = Parse("G0 X0 Y0 Z5\nG1 X10 Z-3 F100\nG1 X20\nG0 Z5\n");
	EXPECT_FLOAT_EQ(q.ops[0].zMax, -3.0f);
}

TEST(TestCncGcode, BoundsPlungeSpindleAndToolText)
{
	const Program p = Parse(readFile("tests/object_model/cnc/gcode/bracket_fusion.nc"));
	ASSERT_EQ(p.ops.size(), 5u);
	EXPECT_FLOAT_EQ(p.ops[1].bounds.minX, 35.0f);
	EXPECT_FLOAT_EQ(p.ops[1].bounds.maxY, 55.0f);
	EXPECT_FLOAT_EQ(p.ops[0].travel.minX, -30.0f); // rapids count for the travel
	EXPECT_FLOAT_EQ(p.ops[0].travel.maxX, 150.0f);
	EXPECT_FLOAT_EQ(p.ops[3].plungeFeed, 300.0f);
	EXPECT_FLOAT_EQ(p.ops[4].plungeFeed, 150.0f);
	EXPECT_EQ(p.ops[1].toolText, "flat end mill");
	EXPECT_EQ(p.ops[0].toolText, "face mill");
	for (const Operation& o : p.ops)
	{
		EXPECT_FALSE(o.cutsWithSpindleOff) << o.name;
	}
	const Program q = Parse("T1\nG0 X0 Y0 Z5\nG1 Z-1 F100\nG1 X10\n");
	EXPECT_TRUE(q.ops[0].cutsWithSpindleOff);
	const Program r = Parse("(T3: 6mm Endmill)\nT3\nM3 S9000\nG0 X0 Y0 Z5\nG1 Z-1 F100\n");
	EXPECT_EQ(r.ops[0].toolText, "6mm Endmill");
	EXPECT_FALSE(r.ops[0].cutsWithSpindleOff);
}

// ---- Splitting operations ----

namespace
{
	// One tool, no names: roughing (two pockets, retract between them, same feeds), then finishing
	const char* const SameToolNoNames = "G21 G90\n"
										"T2\n"
										"S18000 M3\n"
										"G0 X0 Y0 Z15\n"
										// pocket 1
										"G0 Z2\nG1 Z-3 F300\nG1 X10 F1200\nG1 Y10\nG0 Z15\n"
										// pocket 2: same feeds after the retract, not a new operation
										"G0 X30 Y0\nG0 Z2\nG1 Z-3 F300\nG1 X40 F1200\nG1 Y10\nG0 Z15\n"
										// finishing: a new feed after the retract
										"G0 X0 Y0\nG0 Z2\nG1 Z-3 F300\nG1 X10 F600\nG1 Y10\nG0 Z15\n";
} // namespace

TEST(TestCncGcode, SameToolSplitByNewFeedAfterRetract)
{
	const Program p = Parse(SameToolNoNames);
	ASSERT_EQ(p.ops.size(), 2u);
	EXPECT_EQ(p.ops[0].name, "T2 (1)");
	EXPECT_EQ(p.ops[1].name, "T2 (2)");
	EXPECT_FLOAT_EQ(p.ops[0].feed, 1200.0f);
	EXPECT_FLOAT_EQ(p.ops[1].feed, 600.0f);
	// the finishing operation starts at the retract (its approach moves are its own)
	EXPECT_EQ(p.points[p.ops[1].firstPoint].kind, PointKind::Rapid);
	const auto s = ShortName(p.ops[1].name);
	EXPECT_EQ(s[0], "T2");
	EXPECT_EQ(s[1], "(2)");

	// SPLIT OPERATIONS: TOOL CHANGES
	Options o;
	o.toolChangesOnly = true;
	const Program q = Parse(SameToolNoNames, o);
	ASSERT_EQ(q.ops.size(), 1u);
	EXPECT_EQ(q.ops[0].name, "T2");
}

TEST(TestCncGcode, SameToolSplitBySpindleRestart)
{
	const Program p = Parse("T1\nS10000 M3\nG54\nG0 X0 Y0 Z5\nG1 Z-1 F500\nG1 X10\nG0 Z5\n"
							"S10000 M3\nG54\nG0 X0 Y0\nG1 Z-2 F500\nG1 X10\nG0 Z5\n");
	ASSERT_EQ(p.ops.size(), 2u); // the start block again, even with the same speed and feed
	EXPECT_FLOAT_EQ(p.ops[1].zMin, -2.0f);
}

TEST(TestCncGcode, NamesWinOverGuessesAndRepeatedCommentsAreNoNames)
{
	// Named: no guessing, the same-tool rule does not split "Rough" although its feed changes
	const Program p =
		Parse("T2\nS18000 M3\n(Rough)\nG0 X0 Y0 Z15\nG1 Z-3 F300\nG1 X10 F1200\nG0 Z15\n"
			  "G0 X0 Y0\nG1 Z-3 F300\nG1 X10 F600\nG0 Z15\n(Finish)\nG0 X0 Y0\nG1 Z-4 F300\nG1 X10 F500\n");
	ASSERT_EQ(p.ops.size(), 2u);
	EXPECT_EQ(p.ops[0].name, "Rough");
	EXPECT_EQ(p.ops[1].name, "Finish");

	// A post's "(Retract)" between moves is not a name: it does not split
	const Program q =
		Parse("(Pocket1)\nT1\nM3 S9000\nG0 X0 Y0 Z5\nG1 Z-1 F100\nG1 X10\n(Retract)\nG0 Z5\nG0 X20\nG1 Z-1\n"
			  "G1 X30\n(Retract)\nG0 Z5\nG0 X40\nG1 Z-1\nG1 X50\n(Retract)\nG0 Z5\n");
	ASSERT_EQ(q.ops.size(), 1u);
	EXPECT_EQ(q.ops[0].name, "Pocket1");

	// Own marker
	const Program r =
		Parse("T1\nM3 S9000\n;CNC_OP Roughing\nG0 X0 Y0 Z5\nG1 Z-1 F100\nG1 X10\n;CNC_OP Finishing\nG1 X20\n");
	ASSERT_EQ(r.ops.size(), 2u);
	EXPECT_EQ(r.ops[0].name, "Roughing");
	EXPECT_EQ(r.ops[1].name, "Finishing");

	// TOOL CHANGES: the comment names, but no split inside one tool
	Options o;
	o.toolChangesOnly = true;
	const Program t = Parse(readFile("tests/object_model/cnc/gcode/bracket_fusion.nc"), o);
	ASSERT_EQ(t.ops.size(), 4u); // Adaptive and Contour share T2
	EXPECT_EQ(t.ops[1].name, "2D Adaptive Clearing1");
	EXPECT_FLOAT_EQ(t.ops[1].zMin, -20.5f);
}

// M563 P<n> in the file: tools the job creates itself (not reported as missing from the tool table)
TEST(TestCncGcode, ToolsTheFileDefines)
{
	const Program p = Parse(readFile("tests/object_model/cnc/gcode/own_tools.nc"));
	ASSERT_EQ(p.ops.size(), 2u);
	EXPECT_EQ(p.definedTools, std::vector<int>({5}));
	EXPECT_TRUE(p.definesTool(5));
	EXPECT_FALSE(p.definesTool(1));
	EXPECT_TRUE(Parse("M563 P7 S\"a\"\nM563 P3\nM563 P7\nG1 X1 F100\n").definedTools == std::vector<int>({3, 7}));
	EXPECT_TRUE(Parse(readFile("tests/object_model/cnc/gcode/bracket_fusion.nc")).definedTools.empty());
}

// An inch program: Fusion's D= tool lines are inches like the moves; "6mm" in a tool text stays mm
TEST(TestCncGcode, InchToolDiameters)
{
	const Program p = Parse("G20\n(T1  D=0.25 CR=0 - ZMIN=-0.5 - flat end mill)\nT1 M6\nM3 S10000\nG0 X0 Y0 Z0.1\n"
							"G1 Z-0.1 F10\nG1 X1\n");
	ASSERT_EQ(p.ops.size(), 1u);
	EXPECT_NEAR(p.ops[0].toolDiameter, 6.35f, 0.001f);
	const Program mm = Parse("G21\n(T1  D=6 CR=0 - ZMIN=-5 - flat end mill)\nT1 M6\nM3 S10000\nG0 X0 Y0 Z1\n"
							 "G1 Z-1 F100\nG1 X10\n");
	ASSERT_EQ(mm.ops.size(), 1u);
	EXPECT_NEAR(mm.ops[0].toolDiameter, 6.0f, 0.001f);
}

// S and F of other commands are not the spindle speed / feed
TEST(TestCncGcode, OtherCommandsDoNotSetSpindleOrFeed)
{
	const Program p = Parse("T1 M6\nM3 S18000\nG4 S3\nM42 P0 S1\nM106 S255\nM568 P0 F12000\nG0 X0 Y0 Z1\n"
							"G1 Z-1 F600\nG1 X20\n");
	ASSERT_EQ(p.ops.size(), 1u);
	EXPECT_FLOAT_EQ(p.ops[0].spindle, 18000.0f);
	EXPECT_FLOAT_EQ(p.ops[0].feed, 600.0f);
}

// G2 with R ending where it starts: no circle (RRF refuses it): drawn as a straight move, no NaN
TEST(TestCncGcode, ZeroLengthRadiusArc)
{
	const Program p = Parse("M3 S1000\nG0 X10 Y10 Z1\nG1 Z-1 F100\nG2 X10 Y10 R5\nG1 X20\n");
	for (const Point& pt : p.points)
	{
		EXPECT_FALSE(std::isnan(pt.x) || std::isnan(pt.y) || std::isnan(pt.z));
	}
	EXPECT_FALSE(p.cutBounds.empty);
	EXPECT_NEAR(p.cutBounds.maxX, 20.0f, 0.001f);
	EXPECT_NEAR(p.cutBounds.minX, 10.0f, 0.001f);
}

// The tip angle of pointed tools: Fusion's TAPER (drill: the tip angle; chamfer / engrave: from the axis),
// ";CNC_TOOL ... A=", "60°" in a description; defaults 90 (V) and 118 (drill)
TEST(TestCncGcode, PointedToolAngles)
{
	const Program p = Parse("(T1  D=6 CR=0 TAPER=118deg - ZMIN=-22 - drill)\n"
							"(T2  D=6.35 CR=0 TAPER=45deg - ZMIN=-1 - chamfer mill)\n"
							"(T3: 60deg V-bit)\n"
							"(T4: engraver)\n"
							"(T5  D=6 CR=0 - ZMIN=-5 - flat end mill)\n"
							"T1\nM3 S1000\nG0 X0 Y0 Z1\nG1 Z-1 F100\nG0 Z5\n"
							"T2\nM3 S1000\nG0 X0 Y0 Z1\nG1 Z-1 F100\nG0 Z5\n"
							"T3\nM3 S1000\nG0 X0 Y0 Z1\nG1 Z-1 F100\nG0 Z5\n"
							"T4\nM3 S1000\nG0 X0 Y0 Z1\nG1 Z-1 F100\nG0 Z5\n"
							"T5\nM3 S1000\nG0 X0 Y0 Z1\nG1 Z-1 F100\nG0 Z5\n");
	ASSERT_EQ(p.ops.size(), 5u);
	EXPECT_FLOAT_EQ(p.ops[0].toolAngle, 118.0f);
	EXPECT_FLOAT_EQ(p.ops[1].toolAngle, 90.0f);
	EXPECT_FLOAT_EQ(p.ops[2].toolAngle, 60.0f);
	EXPECT_FLOAT_EQ(p.ops[3].toolAngle, 90.0f); // not given: 90
	EXPECT_FLOAT_EQ(p.ops[4].toolAngle, 0.0f);	// flat
}
