/*
 * CncPreviewView.h
 *
 * The 3D view of JOB > PREVIEW: a software line renderer into a canvas (no GPU on the T113).
 *  - Stock box: visible faces flat shaded (top lightest), edges on top.
 *  - Toolpaths: the selected operation's cutting moves in the accent colour, 2 px; the other
 *    operations' cutting moves dimmed, 1 px. Rapid moves (G0 toggle) only for the selected operation,
 *    dashed. Other operations' rapids are never drawn.
 *  - Orthographic projection; ISO / TOP / FRONT / SIDE presets, drag rotates (yaw, pitch), the scale is
 *    fitted to the stock when a preset is set or a drag ends.
 *  - Axis triad (X red, Y green, Z blue) in the bottom left corner.
 *  - PART (setPart): the stock simulated up to the selected operation (StockSim) instead of the box:
 *    ISO / 3D see-through (a shaded part over the background at GlassOpacity, its edges on top), TOP
 *    shaded by depth, FRONT / SIDE as a drawing (outline solid, each hidden feature's outline dashed).
 *  - Cut point (a tap on a cutting move, setCutPoint): a crosshair; FRONT and SIDE become sections
 *    through it (CUT X: along X at its Y; CUT Y: along Y at its X): the cut face hatched, the part
 *    behind faint, the selected operation's moves near the cut, a dot where a move passes through it.
 *    Sections draw the simulated part whether PART is on or not.
 *  - A single tap waits DoubleTapMs (a double tap is ISO); then it picks the nearest cutting move
 *    within PickPx: onPick(op, x, y). A tap away from every move: onPick(-1, ...).
 * Overlays (file name, buttons, stock size) are the page's, as children of obj().
 */
#pragma once

#include "CncGcode.h"
#include "CncStockSim.h"
#include "lvgl/lvgl.h"
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace UI::Cnc
{
	class PreviewView
	{
	  public:
		enum class Preset
		{
			Iso,
			Top,
			Front,
			Side,
			Free // after a drag
		};

		~PreviewView();
		// The card at x, y (pixels in parent); insets: room kept free for the overlays when fitting
		// insetRight: room kept free at the right (the button column)
		void create(lv_obj_t* parent,
					int32_t x,
					int32_t y,
					int32_t w,
					int32_t h,
					int32_t insetTop,
					int32_t insetBottom,
					int32_t insetRight = 0);
		lv_obj_t* obj() const { return m_card; }

		void setProgram(std::shared_ptr<const Gcode::Program> program);	// nullptr: empty view
		void setSelected(int op);
		void setShowRapids(bool show);
		bool showRapids() const { return m_rapids; }

		void setPreset(Preset p);		// fits the scale
		Preset preset() const { return m_preset; }
		void nextPreset();								// ISO -> TOP -> FRONT -> SIDE -> ISO (Free -> ISO)
		void rotate(float dYawDeg, float dPitchDeg);	// a drag step (Free)
		void endRotate();				// drag released: fit again
		void onDoubleTap(std::function<void()> fn) { m_onDoubleTap = std::move(fn); }
		void onPresetChanged(std::function<void()> fn) { m_onPresetChanged = std::move(fn); }

		void setPart(bool on);
		bool part() const { return m_part; }
		void setCutPoint(float x, float y);
		void clearCutPoint();
		bool hasCutPoint() const { return m_cutSet; }
		float cutX() const { return m_cutX; }
		float cutY() const { return m_cutY; }
		bool sectionShown() const { return m_cutSet && (m_preset == Preset::Front || m_preset == Preset::Side); }
		void onPick(std::function<void(int op, float x, float y)> fn) { m_onPick = std::move(fn); }
		void tick(); // a single tap waiting for a possible second one (every 100 ms)
		// The machine's tool (work coordinates of the program): a small marker over the drawing, moved
		// without drawing the view again
		void setTool(bool show, float x, float y, float z);
		// A running job: points before `points` are done (the selected operation's drawn in the done colour;
		// only the newly done moves are drawn when it grows). 0: none.
		void setDone(uint32_t points);
		uint32_t done() const { return m_done; }
		// PART up to this operation instead of the selected one (a running job: the operations finished);
		// NoPartLimit: the selected one
		static constexpr int NoPartLimit = -2;
		void setPartLimit(int lastOp);
		int partOp() const { return (m_partLimit != NoPartLimit) ? m_partLimit : m_selected; }
		bool toolShown() const;
		lv_point_t toolTip() const; // canvas point of the marker's tip (tests)

		float yaw() const { return m_yaw; }
		float pitch() const { return m_pitch; }
		void redraw();
		const std::vector<uint32_t>& pixels() const { return m_buf; } // tests

		// Tests
		uint32_t pixel(int32_t x, int32_t y) const;		// 0xRRGGBB at canvas x, y
		uint32_t drawnSegments() const { return m_drawn; }
		void tapAt(int32_t x, int32_t y);						 // a single tap at canvas x, y (no waiting)
		lv_point_t canvasPoint(float x, float y, float z) const; // where a point of the job is drawn
		const StockSim::Map& stockMap() const { return m_map; }
		uint32_t simMs() const { return m_simMs; } // the last simulation's time

		static constexpr float DragDegreesPerPixel = 0.5f;
		static constexpr uint32_t MinRedrawMs = 40;		// while dragging
		static constexpr uint32_t DoubleTapMs = 400;
		static constexpr uint32_t DragPointBudget = 120000;	// points drawn per frame while dragging (rest skipped)
		static constexpr int32_t PickPx = 28;				// a tap this close to a cutting move picks it
		static constexpr float GlassOpacity = 0.42f;		// the see-through part over the background

	  private:
		struct Proj
		{
			float x, y;
		};
		Proj project(float x, float y, float z) const;
		float depthOf(float nx, float ny, float nz) const;	// view depth of a direction (< 0: towards the viewer)
		void fit();
		void clear(uint32_t colour);
		void line(float x0, float y0, float x1, float y1, uint32_t colour, int width, bool dashed);
		void fillQuad(const Proj q[4], uint32_t colour);
		void plot(int32_t x, int32_t y, uint32_t colour);
		void drawStock();
		void drawPaths();
		void drawTriad();
		bool ensureMap(); // the simulation up to the selected operation
		void drawGlass();
		void drawTopDepth();
		void drawDrawing(bool alongX);
		void drawSection(bool alongX);
		void drawCutMarker();
		void drawRims(uint32_t colour);
		void blend(int32_t x, int32_t y, uint32_t colour, float a);
		void fillRect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t colour);
		void pick(int32_t x, int32_t y);
		static void touchEvent(lv_event_t* e);

		lv_obj_t* m_card = nullptr;
		lv_obj_t* m_canvas = nullptr;
		lv_obj_t* m_tool = nullptr; // marker: shank and tip
		bool m_toolWanted = false;
		uint32_t m_done = 0;
		int m_partLimit = NoPartLimit;
		float m_toolX = 0, m_toolY = 0, m_toolZ = 0;
		void placeTool();
		lv_obj_t* m_axisLabels[3] = {nullptr, nullptr, nullptr};
		std::vector<uint32_t> m_buf;
		int32_t m_w = 0, m_h = 0, m_insetTop = 0, m_insetBottom = 0, m_insetRight = 0;
		std::shared_ptr<const Gcode::Program> m_program;
		int m_selected = 0;
		bool m_rapids = false;
		bool m_part = false;
		bool m_cutSet = false;
		float m_cutX = 0, m_cutY = 0, m_cutZ = NAN; // m_cutZ: the tapped move's height (NaN: not known)
		bool m_rotating = false;					// a drag is turning the view (coarser PART)
		StockSim::Map m_map;
		std::shared_ptr<const Gcode::Program> m_mapProgram; // the program m_map was made for
		std::vector<StockSim::Feature> m_features;
		int m_featuresFor = -2; // m_map.lastOp they were found for
		uint32_t m_simMs = 0;
		std::vector<uint32_t> m_layer; // the part, before it is laid over the background
		std::function<void(int, float, float)> m_onPick;
		bool m_tapPending = false;
		lv_point_t m_tapAt{0, 0};
		Preset m_preset = Preset::Iso;
		float m_yaw = -35.0f, m_pitch = 32.0f;
		float m_cosY = 1, m_sinY = 0, m_cosP = 1, m_sinP = 0;
		float m_scale = 1, m_cx = 0, m_cy = 0, m_cz = 0, m_ox = 0, m_oy = 0;
		uint32_t m_drawn = 0;
		bool m_fast = false;	// dragging a large job: draw fewer points
		uint32_t m_dashPhase = 0;
		// touch
		lv_point_t m_last{0, 0}, m_downAt{0, 0};
		bool m_dragging = false;
		uint32_t m_lastRedraw = 0, m_lastTap = 0;
		std::function<void()> m_onDoubleTap, m_onPresetChanged;
	};
}
