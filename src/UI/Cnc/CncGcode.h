/*
 * CncGcode.h
 *
 * G-code reader for JOB > PREVIEW: the toolpath as 3D points and the job split into operations, with
 * what an operator checks before running each one (tool, depth, feed, speed, coolant, time).
 *
 * Understood: G0 / G1 / G2 / G3 (arcs in G17 / G18 / G19, I J K or R, full circles), G20 / G21,
 * G90 / G91, G90.1 / G91.1, G53 (machine moves: not drawn, the position is unknown after them),
 * G54..G59.3, T (a tool change, with or without M6), S, F, M3 / M4 / M5, M7 / M8 / M9.
 * Comments in ( ) and after ;. RRF meta commands (if, var, echo, ...) are skipped.
 *
 * Operations:
 *  - FreeCAD:  (Begin operation: Name) ... (Finish operation: Name)
 *  - Fusion 360 and most others: a comment line with the operation's name before its code. A comment
 *    line after cutting moves starts the next operation at its first move.
 *  - Own marker: ;CNC_OP Roughing (one line per operation, e.g. added to a post-processor).
 *  - A comment repeated 3 times or more is not a name (a post's "(Retract)" between moves).
 *  - A tool change always starts a new operation; with no name it is called "T3".
 *  - No names in the file at all: the same tool is split where an operation probably starts (the
 *    spindle started again or a work offset given again after cutting; or, after a retract to the
 *    operation's highest Z, a feed or spindle speed it has not cut with before). Pieces: "T2 (1)", "T2 (2)".
 *    Options::toolChangesOnly: none of this, operations only at tool changes.
 *  - An "operation" without cutting moves (a retract at the end, a tool change) joins its neighbour.
 *
 * Tools (diameter, type), from comments anywhere in the file:
 *  - Fusion 360:  (T1  D=6 CR=0 - ZMIN=-5 - flat end mill)
 *  - own:         ;CNC_TOOL T1 D=6 TYPE=FLAT
 *  - generic:     (T3: 6mm Endmill)    (TC: 6 mm ball) before the tool change
 *
 * Stock: ;CNC_STOCK X=120 Y=80 Z=20 [X0=0 Y0=0 Z0=0] (size, then optionally the corner at minimum X / Y
 * and the top Z; default 0 0 0). Without it: the box around the cutting moves (stockFromFile false).
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace UI::Cnc::Gcode
{
	enum class PointKind : uint8_t
	{
		Start, // no line to this point (first point, after a G53 move or an unknown position)
		Cut,   // G1 / G2 / G3 to this point
		Rapid  // G0 to this point
	};

	struct Point
	{
		float x, y, z;
		PointKind kind;
	};

	struct Box
	{
		float minX = 0, minY = 0, minZ = 0, maxX = 0, maxY = 0, maxZ = 0;
		bool empty = true;
		void add(float x, float y, float z);
		void add(const Box& other);
		float sizeX() const { return maxX - minX; }
		float sizeY() const { return maxY - minY; }
		float sizeZ() const { return maxZ - minZ; }
	};

	struct Operation
	{
		std::string name;			// as in the file ("2D Adaptive1"); "T3" when the file has none
		int tool = -1;				// -1: no tool selected
		float toolDiameter = 0;		// 0: not known
		std::string toolType;		// short, upper case: FLAT, BALL, DRILL, FACE MILL, ... ("" not known)
		float toolAngle = 0;		// pointed tools (V-BIT, CHAMFER, ENGRAVE, DRILL, SPOT DRILL): included tip angle,
									// degrees (from the file, else 90; drills 118); 0: not pointed
		unsigned int wcs = 1;		// 1 = G54 .. 6 = G59, 7..9 = G59.1..G59.3
		float spindle = 0;			// highest S while cutting (rpm)
		bool spindleCcw = false;	// M4
		float feed = 0;				// the feed of most of the cutting length (mm/min)
		float zMin = 0, zMax = 0;	// cutting moves (top: of the moves along XY, plunges start above the stock)
		bool coolant = false;		// M7 / M8 while cutting
		double cutLength = 0, rapidLength = 0;	// mm
		double seconds = 0;			// estimate: feeds as programmed, rapids at the rapid speed
		Box bounds;					// cutting moves
		Box travel;					// every move, rapids too (machine travel check)
		float plungeFeed = 0;		// straight down cutting moves (0: none)
		bool cutsWithSpindleOff = false;	// a cutting move without M3 / M4 before it
		std::string toolText;		// the tool's description as written ("flat end mill", "6mm Endmill")
		uint32_t firstPoint = 0, pointCount = 0;
		uint32_t fileStart = 0;		// byte offset of its first line (live highlight from job.filePosition)
	};

	struct Program
	{
		std::vector<Point> points;
		std::vector<Operation> ops;
		Box cutBounds;				// cutting moves only
		Box stock;
		bool stockFromFile = false;
		size_t toolCount = 0;		// different tools used
		double seconds = 0;
		size_t lines = 0;
		bool truncated = false;		// too many points: small moves were merged
		std::vector<uint32_t> pointOffsets; // per point: byte offset of the line it comes from (rising)
		std::vector<int> definedTools;		// tools the file creates itself (M563 P<n>), sorted
		bool definesTool(int tool) const;
	};

	struct Options
	{
		float rapidSpeed = 5000.0f;	// mm/min, for the time estimate
		size_t maxPoints = 400000;	// above this, moves shorter than a growing tolerance are merged
		bool toolChangesOnly = false;	// SETTINGS > SPLIT OPERATIONS: TOOL CHANGES (no other splits)
	};

	Program Parse(std::string_view text, const Options& options = Options());

	// The operation button: one word per row, 2 rows, each at most 7 letters (common CAM words
	// shortened, trailing numbers dropped, a 2D / 3D prefix dropped when there are 3 words or more)
	std::array<std::string, 2> ShortName(std::string_view name);
	constexpr size_t MaxShortLetters = 7;

	// "1:05" / "12:40" / "1:02:03"
	std::string FormatTime(double seconds);
	// The operation that contains this file position (job.filePosition), -1 if none
	int OperationAt(const Program& program, uint32_t filePosition);
	// How many points come from lines before this file position (job.filePosition): the ones already run
	uint32_t PointsBefore(const Program& program, uint32_t filePosition);
}
