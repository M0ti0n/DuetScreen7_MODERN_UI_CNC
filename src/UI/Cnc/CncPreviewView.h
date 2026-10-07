/*
 * CncPreviewView.h
 *
 * The 3D view of JOB > PREVIEW: a software line renderer into a canvas (no GPU on the T113).
 *  - Stock box: visible faces flat shaded (top lightest), edges on top.
 *  - Toolpaths: the selected operation's cutting moves in the accent colour, 2 px; the other
 *    operations' cutting moves dimmed, 1 px. Rapid moves (G0 toggle) only for the selected operation,
 *    dashed. Other operations' rapids are never drawn.
 *  - Orthographic projection; ISO / TOP / FRONT presets, drag rotates (yaw, pitch), the scale is
 *    fitted to the stock when a preset is set or a drag ends.
 *  - Axis triad (X red, Y green, Z blue) in the bottom left corner.
 * Overlays (file name, buttons, stock size) are the page's, as children of obj().
 */
#pragma once

#include "CncGcode.h"
#include "lvgl/lvgl.h"
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
			Free // after a drag
		};

		~PreviewView();
		// The card at x, y (pixels in parent); insets: room kept free for the overlays when fitting
		void create(lv_obj_t* parent, int32_t x, int32_t y, int32_t w, int32_t h, int32_t insetTop, int32_t insetBottom);
		lv_obj_t* obj() const { return m_card; }

		void setProgram(std::shared_ptr<const Gcode::Program> program);	// nullptr: empty view
		void setSelected(int op);
		void setShowRapids(bool show);
		bool showRapids() const { return m_rapids; }

		void setPreset(Preset p);		// fits the scale
		Preset preset() const { return m_preset; }
		void nextPreset();				// ISO -> TOP -> FRONT -> ISO (Free -> ISO)
		void rotate(float dYawDeg, float dPitchDeg);	// a drag step (Free)
		void endRotate();				// drag released: fit again
		void onDoubleTap(std::function<void()> fn) { m_onDoubleTap = std::move(fn); }
		void onPresetChanged(std::function<void()> fn) { m_onPresetChanged = std::move(fn); }

		float yaw() const { return m_yaw; }
		float pitch() const { return m_pitch; }
		void redraw();

		// Tests
		uint32_t pixel(int32_t x, int32_t y) const;		// 0xRRGGBB at canvas x, y
		uint32_t drawnSegments() const { return m_drawn; }

		static constexpr float DragDegreesPerPixel = 0.5f;
		static constexpr uint32_t MinRedrawMs = 40;		// while dragging
		static constexpr uint32_t DoubleTapMs = 400;
		static constexpr uint32_t DragPointBudget = 120000;	// points drawn per frame while dragging (rest skipped)

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
		static void touchEvent(lv_event_t* e);

		lv_obj_t* m_card = nullptr;
		lv_obj_t* m_canvas = nullptr;
		lv_obj_t* m_axisLabels[3] = {nullptr, nullptr, nullptr};
		std::vector<uint32_t> m_buf;
		int32_t m_w = 0, m_h = 0, m_insetTop = 0, m_insetBottom = 0;
		std::shared_ptr<const Gcode::Program> m_program;
		int m_selected = 0;
		bool m_rapids = false;
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
