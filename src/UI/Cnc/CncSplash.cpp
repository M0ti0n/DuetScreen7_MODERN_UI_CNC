/*
 * CncSplash.cpp
 *
 * Start-up splash (see CncSplash.h). The mark is drawn with LVGL draw calls in a draw event, so it
 * takes the accent colour and needs no image file.
 */

#include "CncSplash.h"
#include "CncTheme.h"
#include "Debug.h"
#include "UI/Styles/Styles.h" // ASSETS_FOLDER
#include "version.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace UI::Cnc::Splash
{
	const char* const LogoFiles[NumLogos] = {"logo_duet3d.png", "logo_rrf.png", "logo_millennium.png"};

	namespace
	{
		lv_obj_t* s_obj = nullptr;
		lv_timer_t* s_timer = nullptr;
		lv_font_t* s_titleFont = nullptr;
		lv_font_t* s_wordFont = nullptr;
		lv_font_t* s_versionFont = nullptr;
		int s_logos = 0;
		std::string s_marker;

		// ---- Layout (screen px, portrait 600 x 1024) ----
		constexpr int32_t Cx = 300;
		// Block with a pocket
		constexpr int32_t BlockL = 150, BlockR = 450, BlockTop = 420, BlockBottom = 480;
		constexpr int32_t PocketL = 248, PocketR = 352, PocketFloor = 448; // inner edges / floor line
		constexpr int32_t Line = 3;										   // outline width
		// End mill: collet nut, shank, fluted body; the body ends on the pocket floor
		constexpr int32_t NutTop = 208, NutMid = 240, ShankTop = 270, FluteTop = 345;
		constexpr int32_t Tip = PocketFloor - 2; // last body row, touching the floor line
		constexpr int32_t ShankW = 34, FluteW = 30;
		constexpr float FluteRise = 10.0f; // drop of a flute across the body
		constexpr int FluteCount = 7;	   // laid out from the tip; the last one ends in the middle of it
		// Title
		constexpr int32_t TitleY = 560, WordY = 648, RuleY = 688, VersionY = 724;
		constexpr int32_t LogoY = 868, LogoPitch = 195;

		std::string markerPath()
		{
			if (!s_marker.empty())
			{
				return s_marker;
			}
			std::error_code ec;
			const std::filesystem::path tmp = std::filesystem::temp_directory_path(ec);
			return ((ec ? std::filesystem::path("/tmp") : tmp) / "duetscreen_cnc_skip_splash").string();
		}

		// LVGL here has integer draw coordinates
		lv_value_precise_t px(float v)
		{
			return (lv_value_precise_t)std::lround(v);
		}

		void rect(lv_layer_t* layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, lv_color_t c, int32_t radius = 0)
		{
			lv_draw_rect_dsc_t dsc;
			lv_draw_rect_dsc_init(&dsc);
			dsc.bg_color = c;
			dsc.bg_opa = LV_OPA_COVER;
			dsc.radius = radius;
			const lv_area_t a = {x1, y1, x2, y2};
			lv_draw_rect(layer, &dsc, &a);
		}

		void triangle(lv_layer_t* layer, float x1, float y1, float x2, float y2, float x3, float y3, lv_color_t c)
		{
			lv_draw_triangle_dsc_t dsc;
			lv_draw_triangle_dsc_init(&dsc);
			dsc.color = c;
			dsc.opa = LV_OPA_COVER;
			dsc.p[0] = {px(x1), px(y1)};
			dsc.p[1] = {px(x2), px(y2)};
			dsc.p[2] = {px(x3), px(y3)};
			lv_draw_triangle(layer, &dsc);
		}

		void line(lv_layer_t* layer, float x1, float y1, float x2, float y2, int32_t width, lv_color_t c)
		{
			lv_draw_line_dsc_t dsc;
			lv_draw_line_dsc_init(&dsc);
			dsc.color = c;
			dsc.width = width;
			dsc.p1 = {px(x1), px(y1)};
			dsc.p2 = {px(x2), px(y2)};
			lv_draw_line(layer, &dsc);
		}

		void drawMark(lv_event_t* e)
		{
			lv_layer_t* layer = lv_event_get_layer(e);
			lv_area_t at;
			lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_current_target(e)), &at);
			const int32_t ox = at.x1, oy = at.y1;
			const lv_color_t accent = Colour::Accent();
			const lv_color_t bg = Colour::PageBg();
			const lv_color_t border = Colour::Border();

			// Block, pocket cut out of its top
			rect(layer, ox + BlockL, oy + BlockTop, ox + BlockR, oy + BlockBottom, Colour::Tile());
			rect(layer, ox + PocketL, oy + BlockTop, ox + PocketR, oy + PocketFloor - 1, bg);
			// Outline (3 px): top left and right, pocket walls and floor, sides, bottom
			rect(layer, ox + BlockL, oy + BlockTop, ox + PocketL + Line - 1, oy + BlockTop + Line - 1, border);
			rect(layer, ox + PocketR - Line + 1, oy + BlockTop, ox + BlockR, oy + BlockTop + Line - 1, border);
			rect(layer, ox + PocketL, oy + BlockTop, ox + PocketL + Line - 1, oy + PocketFloor + Line - 1, border);
			rect(layer, ox + PocketR - Line + 1, oy + BlockTop, ox + PocketR, oy + PocketFloor + Line - 1, border);
			rect(layer, ox + PocketL, oy + PocketFloor, ox + PocketR, oy + PocketFloor + Line - 1, border);
			rect(layer, ox + BlockL, oy + BlockTop, ox + BlockL + Line - 1, oy + BlockBottom, border);
			rect(layer, ox + BlockR - Line + 1, oy + BlockTop, ox + BlockR, oy + BlockBottom, border);
			rect(layer, ox + BlockL, oy + BlockBottom - Line + 1, ox + BlockR, oy + BlockBottom, border);

			// Shank and fluted body
			rect(layer,
				 ox + Cx - ShankW / 2,
				 oy + ShankTop,
				 ox + Cx + ShankW / 2 - 1,
				 oy + FluteTop,
				 lv_color_hex(0x8A94A2),
				 4);
			const lv_area_t body = {ox + Cx - FluteW / 2, oy + FluteTop, ox + Cx + FluteW / 2 - 1, oy + Tip};
			rect(layer, body.x1, body.y1, body.x2, body.y2, accent);

			// Flutes: equal pitch, the gap above the top one equal too; the lowest one ends at the bottom
			// edge in the middle. Clipped to the body so they finish cleanly on its edges.
			const lv_area_t savedClip = layer->_clip_area;
			lv_area_t clip;
			clip.x1 = LV_MAX(savedClip.x1, body.x1);
			clip.y1 = LV_MAX(savedClip.y1, body.y1);
			clip.x2 = LV_MIN(savedClip.x2, body.x2);
			clip.y2 = LV_MIN(savedClip.y2, body.y2);
			if (clip.x1 <= clip.x2 && clip.y1 <= clip.y2)
			{
				layer->_clip_area = clip;
				const float bottom = (float)(oy + Tip + 1);
				const float half = FluteRise / 2.0f;
				const float pitch = ((float)(Tip + 1 - FluteTop) - half) / (float)FluteCount;
				const float slope = FluteRise / (float)FluteW;
				const float l = (float)body.x1 - 3.0f, r = (float)body.x2 + 4.0f; // past the edges (clipped)
				const float mid = (float)(ox + Cx);
				for (int k = 0; k < FluteCount; ++k)
				{
					const float c = bottom - (float)k * pitch; // where the flute crosses the centre line
					line(layer, l, c + (mid - l) * slope, r, c - (r - mid) * slope, Line, bg);
				}
				layer->_clip_area = savedClip;
			}

			// Collet nut: a rectangle with a sloped triangle each side (no diagonal seam across it)
			rect(layer, ox + Cx - 30, oy + NutMid, ox + Cx + 30, oy + ShankTop - 1, border);
			const float nl = (float)(ox + Cx - 46), nr = (float)(ox + Cx + 46);
			const float il = (float)(ox + Cx - 30), ir = (float)(ox + Cx + 31);
			const float nt = (float)(oy + NutMid), nb = (float)(oy + ShankTop);
			triangle(layer, nl, nt, il + 1.0f, nt, il + 1.0f, nb, border);
			triangle(layer, nr, nt, ir - 1.0f, nt, ir - 1.0f, nb, border);
			rect(layer, ox + Cx - 46, oy + NutTop, ox + Cx + 46, oy + NutMid - 1, Colour::Pressed());

			// Chips
			static const int32_t chips[][3] = {{-78, -22, 5}, {-96, -44, 4}, {84, -30, 5}, {104, -10, 3}};
			for (const auto& ch : chips)
			{
				const int32_t x = ox + Cx + ch[0], y = oy + BlockTop + ch[1], rr = ch[2];
				rect(layer, x - rr, y - rr, x + rr, y + rr, accent, LV_RADIUS_CIRCLE);
			}
		}

		lv_obj_t* centredLabel(int32_t centreY, int32_t h, const lv_font_t* font, lv_color_t colour, const char* text)
		{
			return CreateLabel(s_obj, 0, centreY - h / 2, 600, h, font, colour, LV_TEXT_ALIGN_CENTER, text);
		}

		bool addLogo(const std::string& path, int32_t centreX)
		{
			std::error_code ec;
			if (!std::filesystem::is_regular_file(path, ec))
			{
				return false;
			}
			const std::string src = "A:" + path;
			lv_image_header_t header;
			if (lv_image_decoder_get_info(src.c_str(), &header) != LV_RESULT_OK)
			{
				LOG_WARN("CNC splash: cannot read {:s}", path);
				return false;
			}
			lv_obj_t* img = lv_image_create(s_obj);
			lv_obj_remove_style_all(img);
			lv_obj_set_pos(img, centreX - LogoW / 2, LogoY);
			lv_obj_set_size(img, LogoW, LogoH);
			lv_image_set_src(img, src.c_str());
			// Larger than the slot: scaled down to fit; smaller: centred at its own size
			const bool tooLarge = (int32_t)header.w > LogoW || (int32_t)header.h > LogoH;
			lv_image_set_inner_align(img, tooLarge ? LV_IMAGE_ALIGN_CONTAIN : LV_IMAGE_ALIGN_CENTER);
			return true;
		}

		void releaseFonts()
		{
			Font::Release(s_titleFont);
			Font::Release(s_wordFont);
			Font::Release(s_versionFont);
			s_titleFont = s_wordFont = s_versionFont = nullptr;
		}
	} // namespace

	bool Show(const std::string& logoDir, const std::string& version)
	{
		std::error_code ec;
		const std::string marker = markerPath();
		if (std::filesystem::exists(marker, ec))
		{
			std::filesystem::remove(marker, ec);
			return false; // back from our own restart
		}
		Close();

		s_titleFont = Font::Create(118);
		s_wordFont = Font::Create(40);
		s_versionFont = Font::Create(26);
		auto font = [](lv_font_t* f) { return f ? static_cast<const lv_font_t*>(f) : Font::Large(); };

		s_obj = CreateBare(lv_layer_top(),
						   0,
						   0,
						   lv_display_get_horizontal_resolution(nullptr),
						   lv_display_get_vertical_resolution(nullptr));
		lv_obj_set_style_bg_color(s_obj, Colour::PageBg(), 0);
		lv_obj_set_style_bg_opa(s_obj, LV_OPA_COVER, 0);
		lv_obj_add_flag(s_obj, LV_OBJ_FLAG_CLICKABLE);
		lv_obj_add_event_cb(s_obj, drawMark, LV_EVENT_DRAW_MAIN, nullptr);
		lv_obj_add_event_cb(
			s_obj,
			[](lv_event_t*) { lv_async_call([](void*) { Close(); }, nullptr); }, // not inside its own event
			LV_EVENT_PRESSED,
			nullptr);

		centredLabel(TitleY, 140, font(s_titleFont), Colour::Text(), "CNC");
		lv_obj_t* word = centredLabel(WordY, 50, font(s_wordFont), Colour::Accent(), "MODERN UI");
		lv_obj_set_style_text_letter_space(word, 10, 0);
		lv_obj_t* rule = CreateBare(s_obj, Cx - 120, RuleY, 241, 4);
		lv_obj_set_style_bg_color(rule, Colour::Accent(), 0);
		lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
		centredLabel(
			VersionY, 34, font(s_versionFont), Colour::Muted(), version.empty() ? FIRMWARE_VERSION : version.c_str());

		const std::string dir = logoDir.empty() ? std::string(ASSETS_FOLDER "cnc/") : logoDir;
		s_logos = 0;
		for (int i = 0; i < NumLogos; ++i)
		{
			if (addLogo(dir + LogoFiles[i], Cx + (i - 1) * LogoPitch))
			{
				++s_logos;
			}
		}

		s_timer = lv_timer_create(
			[](lv_timer_t*)
			{
				lv_timer_delete(s_timer);
				s_timer = nullptr;
				Close();
			},
			ShowMs,
			nullptr);
		return true;
	}

	void Close()
	{
		if (s_timer != nullptr)
		{
			lv_timer_delete(s_timer);
			s_timer = nullptr;
		}
		if (s_obj != nullptr)
		{
			lv_obj_delete(s_obj);
			s_obj = nullptr;
		}
		releaseFonts(); // after the labels that used them
	}

	bool IsOpen()
	{
		return s_obj != nullptr;
	}

	lv_obj_t* Object()
	{
		return s_obj;
	}

	int LogosShown()
	{
		return s_obj ? s_logos : 0;
	}

	void SkipNextStart()
	{
		std::ofstream(markerPath()) << "1\n";
	}

	void SetMarkerPath(const std::string& path)
	{
		s_marker = path;
	}
} // namespace UI::Cnc::Splash
