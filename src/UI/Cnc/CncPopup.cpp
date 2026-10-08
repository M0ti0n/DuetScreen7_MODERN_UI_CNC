/*
 * CncPopup.cpp
 *
 * PanelDue geometry, inside each popup (x 1.25 where used):
 *
 * Standard popup, 460 x 480 at (10, 160):
 *   title tile  y 28, h 60   short title: x 105 w 250, long (more than 9 characters): x 25 w 410
 *   info tile   y 108, h 248, x 25 w 410, lines 36 px apart, the block centred
 *   choices     same area as the info tile, buttons 60 px high, 10 / 12 px gaps, rows centred
 *   X / check   110 x 78 at y 376, x 110 / 240 (ALERT: X alone, centred)
 *
 * Numpad, 460 x 396 at (10, 318), inner margin 17:
 *   value tile  322 x 52 (value left, unit right), tag tile 102 x 52 with accent outline
 *   keys        102 x 70, 8 px column gap, 6 px row gap, from y 81: 1 2 3 / 4 5 6 / 7 8 9 / . 0 -
 *   side column backspace, X, check, POS (optional)
 */

#include "CncPopup.h"
#include "CncGlyphs.h"
#include "CncLayout.h"
#include "CncTheme.h"
#include "CncWidgets.h"
#include "Debug.h"
#include "UI/Components/LVGL/LvObj.h"
#include <cstdlib>
#include <fmt/format.h>
#include <memory>

namespace UI::Cnc::Popup
{
	using namespace CncLayout;

	namespace
	{
		enum class Mode
		{
			None,
			Confirm,
			Alert,
			Choose,
			Form,
			Numpad,
			Message,  // machine message (M291) in the standard popup
			Response, // reply from the machine
			Window	  // a page's own window (probe progress, jog prompt)
		};

		// ---- PanelDue values ----
		// Standard popup
		constexpr int32_t StdX = 10, StdY = 160, StdW = 460, StdH = 480;
		constexpr int32_t TitleY = 28, TitleH = 60;
		constexpr int32_t TitleShortX = 105, TitleShortW = 250, TitleLongX = 25, TitleLongW = 410;
		constexpr int32_t InfoX = 25, InfoY = 108, InfoW = 410, InfoH = 248;
		constexpr int32_t LinePitch = 36, LineH = 21;
		constexpr int32_t ActW = 110, ActH = 78, ActY = 376;
		constexpr int32_t CancelX = 110, OkX = 240, AloneX = (StdW - ActW) / 2;
		// ConfirmWithTrash (Modern UI START PRINT): trash, X, check; three 110 x 78 tiles
		constexpr int32_t TrashGap = 35;
		constexpr int32_t TrashX = (StdW - (3 * ActW + 2 * TrashGap)) / 2; // 30
		constexpr int32_t TrashCancelX = TrashX + ActW + TrashGap, TrashOkX = TrashCancelX + ActW + TrashGap;
		constexpr int32_t WrapWidth = InfoW - 20; // 390 px of text in the info tile
		constexpr size_t MaxLines = 6;
		constexpr size_t MaxChoices = 9;
		constexpr int32_t ChoiceH = 60, ChoiceGapX = 10, ChoiceGapY = 12;
		// Form: up to 2 groups, each a small label + rows of 50 px buttons, the block centred
		constexpr size_t MaxFormGroups = 2, MaxFormItems = 6;
		constexpr int32_t FormBtnH = 50, FormGapX = 10, FormGapY = 8, FormLabelH = 22, FormGroupGap = 14;

		// Numpad (under the full DRO)
		constexpr int32_t PadX = 10, PadY = 318, PadW = 460, PadH = 396;
		constexpr int32_t NM = 17; // inner margin
		constexpr int32_t KW = 102, KH = 70, KGX = 8, KGY = 6;
		constexpr int32_t ValH = 52, ValGap = 12;
		constexpr int32_t ValW = 3 * KW + 2 * KGX;	  // 322
		constexpr int32_t KeysY = NM + ValH + ValGap; // 81
		constexpr size_t MaxValueChars = 9;
		constexpr size_t MaxDecimals = 3;

		// Machine prompt header (PanelDue units): title tile, text tile of up to 3 lines
		constexpr int32_t HdrM = 12, HdrTitleH = 50, HdrGap = 8, HdrPitch = 26, HdrPad = 8;
		constexpr size_t HdrLines = 3;
		constexpr int32_t HdrTextH = (int32_t)(HdrLines - 1) * HdrPitch + LineH + 2 * HdrPad; // 89
		constexpr int32_t HdrH = HdrM + HdrTitleH + HdrGap + HdrTextH;						  // 159

		// Choice prompt (M291 S4): header, up to 4 rows of choices, action row with < > in the corners
		constexpr int32_t CpY = 100, CpH = 590;
		constexpr int32_t CpAreaY = HdrH + 12, CpActY = CpH - ActH - 26; // 171, 486
		constexpr int32_t CpAreaH = CpActY - 12 - CpAreaY;				 // 303
		constexpr int32_t CpRowH = 60, CpGapY = 12, CpRows = 4;
		constexpr int32_t CpPageW = 80, CpPrevX = 15, CpNextX = StdW - 15 - CpPageW;
		constexpr int32_t KeyX(int32_t col)
		{
			return NM + col * (KW + KGX);
		}
		constexpr int32_t KeyY(int32_t row)
		{
			return KeysY + row * (KH + KGY);
		}
		constexpr int32_t CncPopupKeyY(int32_t row)
		{
			return KeyY(row);
		}

		// ---- State ----
		Mode s_mode = Mode::None;
		lv_obj_t* s_blocker = nullptr; // full screen: swallows touches, passes STOP on
		lv_obj_t* s_box = nullptr;
		int32_t s_px = StdX, s_py = StdY; // PanelDue position of the open window

		std::string s_title;
		std::vector<std::string> s_lines;
		Handler s_onOk;
		Handler s_onTrash;				  // ConfirmWithTrash: the trash can
		bool s_okGrey = false;			  // ConfirmWithTrash: the check is grey (not available)
		bool s_okGreyOpen = false;		  // ... in the popup that is open
		bool s_warningOpen = false;		  // the popup that is open has the warning look
		bool s_machineBoxOpen = false;	  // the popup that is open shows the machine's message box
		uint32_t s_openCount = 0;
		Handler s_onCancel;				  // Message: X (M292 P1)
		bool s_messageWithCancel = false; // Message: X and check, else the check alone, centred
		bool s_messageWaits = false;	  // Message: the machine waits for an answer given in DWC
		bool s_messagePassive = false;	  // Message: M291 S1, its check mark only closes it
		int s_windowId = 0;				  // Window: who owns it
		Handler s_onWindowClosed;		  // Window: told when it closes

		std::vector<std::string> s_choices;
		std::vector<std::unique_ptr<TextButton>> s_choiceButtons;
		size_t s_pending = 0;
		std::function<void(size_t)> s_onChoice;
		std::function<bool(size_t)> s_choiceAllowed;

		struct FormItem
		{
			lv_obj_t* obj;
			lv_obj_t* label;
			lv_obj_t* dot;
			size_t group, index;
		};
		std::vector<FormGroup> s_formGroups;
		std::vector<uint16_t> s_formSelected;
		std::vector<FormItem> s_formItems;
		std::function<void(const std::vector<uint16_t>&)> s_onForm;
		lv_obj_t* s_okButton = nullptr;

		NumpadSpec s_pad;
		std::string s_value;
		bool s_fresh = true;			  // the first key replaces the shown value
		bool s_alertBackToNumpad = false; // X on the ALERT reopens the numpad, typed value kept
		lv_obj_t* s_valueLabel = nullptr;
		std::vector<std::unique_ptr<TextButton>> s_keys;

		bool s_inEvent = false;		// closing from a popup button: delete after the event
		bool s_warningLook = false; // Warning(): amber title outline with a triangle, amber text

		lv_timer_t* s_responseTimer = nullptr; // Response: closes it after the INFO TIMEOUT
		bool s_machinePrompt = false;		   // the window has a prompt header (M291 S4..S7)

		// Choice prompt
		bool s_choicePrompt = false;
		std::string s_choiceText;
		Handler s_choiceCancel;
		size_t s_choicePage = 0, s_choiceCols = 1;
		lv_obj_t* s_choiceArea = nullptr;
		std::unique_ptr<TextButton> s_pagePrev, s_pageNext;

		lv_area_t s_stopArea = {0, 0, -1, -1};
		Handler s_onStop;

		// Position inside the open window (PanelDue units) -> pixels inside its bordered box
		int32_t X(int32_t v)
		{
			return S(s_px + v) - S(s_px) - CardBorder;
		}
		int32_t Y(int32_t v)
		{
			return S(s_py + v) - S(s_py) - CardBorder;
		}
		int32_t W(int32_t x, int32_t w)
		{
			return SL(s_px + x, w);
		}
		int32_t H(int32_t y, int32_t h)
		{
			return SL(s_py + y, h);
		}

		void deleteWindow()
		{
			if (s_box == nullptr)
			{
				return;
			}
			UI_LOCK();
			if (s_inEvent)
			{
				lv_obj_delete_async(s_box); // the event of one of its buttons is still running
				if (s_blocker != nullptr)
				{
					lv_obj_delete_async(s_blocker);
				}
			}
			else
			{
				lv_obj_delete(s_box);
				if (s_blocker != nullptr)
				{
					lv_obj_delete(s_blocker);
				}
			}
			s_box = s_blocker = nullptr;
			s_valueLabel = nullptr;
			if (s_responseTimer != nullptr)
			{
				lv_timer_delete(s_responseTimer);
				s_responseTimer = nullptr;
			}
			s_machinePrompt = false;
			s_choicePrompt = false;
			s_choiceArea = nullptr;
			s_pagePrev.reset();
			s_pageNext.reset();
			s_choiceButtons.clear(); // their LVGL objects go with the box
			s_onTrash = nullptr;
			s_formItems.clear();
			s_okButton = nullptr;
			s_keys.clear();
			s_mode = Mode::None;
			if (s_onWindowClosed)
			{
				Handler h = std::move(s_onWindowClosed); // a page window closed (by its owner or replaced)
				s_onWindowClosed = nullptr;
				h();
			}
		}

		// Blocker (modal windows) and window: page background, accent double border. Screen pixels.
		void openWindowPx(Mode mode, int32_t x, int32_t y, int32_t w, int32_t h, bool modal)
		{
			deleteWindow();
			s_okGreyOpen = false;
			s_warningOpen = s_warningLook;
			s_machineBoxOpen = false;
			++s_openCount;
			s_mode = mode;
			lv_obj_t* top = lv_layer_top();
			if (modal)
			{
				s_blocker = CreateBare(top, 0, 0, ScreenW, ScreenH);
				lv_obj_add_flag(s_blocker, LV_OBJ_FLAG_CLICKABLE);
				lv_obj_add_event_cb(
					s_blocker,
					[](lv_event_t*)
					{
						lv_point_t p;
						lv_indev_get_point(lv_indev_active(), &p);
						if (s_onStop && p.x >= s_stopArea.x1 && p.x <= s_stopArea.x2 && p.y >= s_stopArea.y1 &&
							p.y <= s_stopArea.y2)
						{
							s_onStop(); // STOP acts over any popup
						}
					},
					LV_EVENT_PRESSED,
					nullptr);
			}
			s_box = CreateBare(top, x, y, w, h);
			StyleCard(s_box, Colour::PageBg(), Colour::Accent());
		}

		// The same at a PanelDue position (x 1.25)
		void openWindow(Mode mode, int32_t px, int32_t py, int32_t pw, int32_t ph)
		{
			openWindowPx(mode, S(px), S(py), SL(px, pw), SL(py, ph), true);
			s_px = px;
			s_py = py;
		}

		lv_obj_t* tile(int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t border)
		{
			lv_obj_t* t = CreateBare(s_box, X(x), Y(y), W(x, w), H(y, h));
			StyleCard(t, Colour::Tile(), border);
			return t;
		}

		lv_obj_t* label(int32_t x,
						int32_t y,
						int32_t w,
						int32_t h,
						const lv_font_t* font,
						lv_color_t colour,
						lv_text_align_t align,
						const char* text)
		{
			return CreateLabel(s_box, X(x), Y(y), W(x, w), H(y, h), font, colour, align, text);
		}

		// ---- Red X / green check ----

		void drawActionIcon(lv_event_t* e)
		{
			lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
			lv_area_t a;
			lv_obj_get_coords(obj, &a);
			Pen pen(lv_event_get_layer(e), a.x1 + lv_area_get_width(&a) / 2, a.y1 + lv_area_get_height(&a) / 2);
			if (lv_event_get_user_data(e) == (void*)2)
			{
				DrawOkIcon(pen, Colour::Muted()); // not available
			}
			else if (lv_event_get_user_data(e) != nullptr)
			{
				DrawOkIcon(pen, Colour::ButtonText());
			}
			else
			{
				DrawCancelIcon(pen, Colour::ButtonText());
			}
		}

		lv_obj_t* actionButton(int32_t x, int32_t y, int32_t w, int32_t h, bool ok)
		{
			lv_obj_t* b = CreateBare(s_box, X(x), Y(y), W(x, w), H(y, h));
			const bool grey = ok && s_okGrey;
			StyleCard(b, grey ? Colour::Tile() : ok ? Colour::Green() : Colour::StopRed(), Colour::Border(), grey);
			lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_event_cb(b, drawActionIcon, LV_EVENT_DRAW_MAIN_END, grey ? (void*)2 : ok ? (void*)1 : nullptr);
			lv_obj_add_event_cb(
				b,
				[](lv_event_t* e)
				{
					s_inEvent = true;
					(lv_event_get_user_data(e) != nullptr) ? PressOk() : PressCancel();
					s_inEvent = false;
				},
				LV_EVENT_CLICKED,
				ok ? (void*)1 : nullptr);
			return b;
		}

		// ConfirmWithTrash's trash button: cyan with a dark trash can (Modern UI START PRINT)
		lv_obj_t* trashButton(int32_t x, int32_t y, int32_t w, int32_t h)
		{
			lv_obj_t* b = CreateBare(s_box, X(x), Y(y), W(x, w), H(y, h));
			StyleCard(b, Colour::Cyan(), Colour::Border(), false);
			lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_event_cb(
				b,
				[](lv_event_t* e)
				{
					lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
					lv_area_t a;
					lv_obj_get_coords(obj, &a);
					Pen pen(lv_event_get_layer(e), a.x1 + lv_area_get_width(&a) / 2, a.y1 + lv_area_get_height(&a) / 2);
					DrawTrashIcon(pen, Colour::ButtonText(), Colour::Cyan());
				},
				LV_EVENT_DRAW_MAIN_END,
				nullptr);
			lv_obj_add_event_cb(
				b,
				[](lv_event_t*)
				{
					s_inEvent = true;
					PressTrash();
					s_inEvent = false;
				},
				LV_EVENT_CLICKED,
				nullptr);
			return b;
		}

		// ---- Standard popup ----

		int32_t textWidth(const std::string& text, const lv_font_t* font)
		{
			lv_point_t size;
			lv_text_get_size(&size, text.c_str(), font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
			return size.x;
		}

		// Word wrap to the info tile width (or maxW px); '\n' starts a new line
		std::vector<std::string> wrap(std::string_view text, int32_t maxW = S(WrapWidth), size_t maxLines = MaxLines)
		{
			const lv_font_t* font = Font::Small();
			std::vector<std::string> out;
			size_t p = 0;
			while (p < text.size() && out.size() < maxLines)
			{
				while (p < text.size() && text[p] == ' ')
				{
					++p;
				}
				if (p < text.size() && text[p] == '\n')
				{
					out.emplace_back();
					++p;
					continue;
				}
				size_t end = p, fit = p;
				while (end < text.size() && text[end] != '\n')
				{
					const size_t before = end;
					do
					{
						++end; // a whole UTF-8 character
					} while (end < text.size() && ((unsigned char)text[end] & 0xC0) == 0x80);
					const bool wordEnd = end == text.size() || text[end] == ' ' || text[end] == '\n';
					const std::string candidate(text.substr(p, end - p));
					if (textWidth(candidate, font) > maxW)
					{
						if (fit == p)
						{
							fit = (before > p) ? before : end; // one over-long word: cut it where it fits
						}
						break;
					}
					if (wordEnd)
					{
						fit = end;
					}
				}
				if (fit == p)
				{
					fit = end;
				}
				out.emplace_back(text.substr(p, fit - p));
				p = fit;
				if (p < text.size() && text[p] == '\n')
				{
					++p; // the break ends this line (PanelDue WrapText)
				}
			}
			return out;
		}

		void titleTile(std::string_view title)
		{
			s_title = title;
			if (s_title.empty())
			{
				return; // replies have no title tile (Modern UI)
			}
			const bool longTitle = s_title.size() > 9 || s_warningLook;
			const int32_t tx = longTitle ? TitleLongX : TitleShortX, tw = longTitle ? TitleLongW : TitleShortW;
			lv_obj_t* t = tile(tx, TitleY, tw, TitleH, s_warningLook ? Colour::Amber() : Colour::Accent());
			if (s_warningLook)
			{
				// Warning: the triangle at the left of the title, the text centred in the rest
				lv_obj_add_event_cb(
					t,
					[](lv_event_t* e)
					{
						lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
						lv_area_t a;
						lv_obj_get_coords(obj, &a);
						DrawWarningIcon(lv_event_get_layer(e),
										a.x1 + S(34),
										a.y1 + lv_area_get_height(&a) / 2,
										S(38),
										Colour::Amber(),
										Colour::DarkText());
					},
					LV_EVENT_DRAW_MAIN_END,
					nullptr);
				label(tx + 50,
					  TitleY,
					  tw - 50,
					  TitleH,
					  Font::Large(),
					  Colour::Text(),
					  LV_TEXT_ALIGN_CENTER,
					  s_title.c_str());
				return;
			}
			label(tx, TitleY, tw, TitleH, Font::Large(), Colour::Text(), LV_TEXT_ALIGN_CENTER, s_title.c_str());
		}

		void openStandard(Mode mode,
						  std::string_view title,
						  const std::vector<std::string>& lines,
						  Handler onOk,
						  Handler onTrash = nullptr)
		{
			UI_LOCK();
			openWindow(mode, StdX, StdY, StdW, StdH);
			s_onTrash = std::move(onTrash);
			s_lines.assign(lines.begin(), lines.begin() + (std::ptrdiff_t)std::min(lines.size(), MaxLines));
			if (s_lines.empty())
			{
				s_lines.emplace_back();
			}
			s_onOk = std::move(onOk);
			titleTile(title);

			// Information tile, lines centred as a block
			tile(InfoX, InfoY, InfoW, InfoH, Colour::Border());
			const int32_t n = (int32_t)s_lines.size();
			const int32_t firstTop = InfoY + (InfoH - ((n - 1) * LinePitch + LineH)) / 2;
			for (int32_t i = 0; i < n; ++i)
			{
				label(InfoX + 10,
					  firstTop + i * LinePitch,
					  InfoW - 20,
					  LineH,
					  Font::Small(),
					  s_warningLook ? Colour::Amber() : Colour::Text(),
					  LV_TEXT_ALIGN_CENTER,
					  s_lines[(size_t)i].c_str());
			}

			if (mode == Mode::Confirm && s_onTrash)
			{
				trashButton(TrashX, ActY, ActW, ActH);
				actionButton(TrashCancelX, ActY, ActW, ActH, false);
				actionButton(TrashOkX, ActY, ActW, ActH, true);
			}
			else if (mode == Mode::Confirm || (mode == Mode::Message && s_messageWithCancel))
			{
				actionButton(CancelX, ActY, ActW, ActH, false);
				actionButton(OkX, ActY, ActW, ActH, true);
			}
			else if (mode == Mode::Message)
			{
				actionButton(AloneX, ActY, ActW, ActH, true); // single check mark, centred
			}
			else
			{
				actionButton(AloneX, ActY, ActW, ActH, false);
			}
		}

		// ---- Form ----

		bool formValid()
		{
			for (uint16_t sel : s_formSelected)
			{
				if (sel == 0)
				{
					return false;
				}
			}
			return true;
		}

		// PanelDue CncChoiceButton: selected = accent fill and outline, dark text; disabled = no outline,
		// muted text; a dot (top right) marks an item that already holds something
		void showFormItems()
		{
			const lv_color_t accent = Colour::Accent();
			for (const FormItem& it : s_formItems)
			{
				const FormGroup& g = s_formGroups[it.group];
				const bool disabled = (g.disabled & (1u << it.index)) != 0;
				const bool selected = !disabled && (s_formSelected[it.group] & (1u << it.index)) != 0;
				lv_obj_set_style_bg_color(it.obj, selected ? accent : Colour::Tile(), 0);
				// disabled: no visible outline (same colour as the fill, so the text stays where it is)
				lv_obj_set_style_border_color(it.obj,
											  selected	 ? accent
											  : disabled ? Colour::Tile()
														 : Colour::Border(),
											  0);
				lv_obj_set_style_text_color(it.label,
											disabled   ? Colour::Border()
											: selected ? Colour::DarkText()
													   : Colour::Text(),
											0);
				lv_obj_set_style_bg_color(it.dot, selected ? Colour::DarkText() : accent, 0);
				if ((g.marked & (1u << it.index)) != 0 && !disabled)
				{
					lv_obj_remove_flag(it.dot, LV_OBJ_FLAG_HIDDEN);
				}
				else
				{
					lv_obj_add_flag(it.dot, LV_OBJ_FLAG_HIDDEN);
				}
			}
			// The check mark is greyed until every group has a selection
			if (s_okButton != nullptr)
			{
				lv_obj_set_style_bg_color(s_okButton, formValid() ? Colour::Green() : Colour::Border(), 0);
			}
		}

		size_t formItemsPerRow(size_t n)
		{
			return (n <= 4) ? n : 3;
		}

		std::vector<lv_color_t> s_choiceColours; // ChooseColour: the swatches
		size_t s_choiceFirst = 0;				 // choice of the first button (choice prompt pages)

		void showChoiceSelection()
		{
			for (size_t i = 0; i < s_choiceButtons.size(); ++i)
			{
				if (i < s_choiceColours.size())
				{
					// A swatch: the chosen one gets a thick light outline
					TextButton& b = *s_choiceButtons[i];
					b.setFilled(s_choiceColours[i]);
					b.setPressedLook(false);
					lv_obj_set_style_border_width(b.obj(), (i == s_pending) ? S(4) : CardBorder, 0);
					lv_obj_set_style_border_color(b.obj(), (i == s_pending) ? Colour::Text() : s_choiceColours[i], 0);
				}
				else
				{
					s_choiceButtons[i]->setSelected(i + s_choiceFirst == s_pending);
				}
			}
		}

		// ---- Numpad ----

		void showValue()
		{
			if (s_valueLabel != nullptr)
			{
				lv_label_set_text(s_valueLabel, s_value.c_str());
			}
		}

		std::string formatValue(float v, unsigned int decimals)
		{
			if (decimals == 0)
			{
				return fmt::format("{:d}", (long)((v < 0.0f) ? v - 0.5f : v + 0.5f));
			}
			return fmt::format("{:.{}f}", v, decimals);
		}

		// False if the text is not a complete number
		bool parseValue(float& out)
		{
			if (s_value.empty() || s_value == "-")
			{
				return false;
			}
			char* end = nullptr;
			out = std::strtof(s_value.c_str(), &end);
			return end != nullptr && *end == 0;
		}

		void key(char c)
		{
			if (s_fresh)
			{
				s_value.clear();
				s_fresh = false;
			}

			if (c == '-')
			{
				if (!s_value.empty() && s_value[0] == '-')
				{
					s_value.erase(0, 1);
				}
				else if (s_value.size() < MaxValueChars)
				{
					s_value.insert(0, 1, '-');
				}
				else
				{
					return; // full: the key does nothing
				}
				showValue();
				return;
			}

			if (s_value.size() >= MaxValueChars)
			{
				return; // full: the key does nothing
			}
			const size_t dot = s_value.find('.');
			if (c != '.' && dot != std::string::npos && s_value.size() - dot > MaxDecimals)
			{
				return; // at most MaxDecimals decimals (what is sent and saved)
			}

			if (c == '.')
			{
				if (s_value.find('.') != std::string::npos)
				{
					return; // only one decimal point
				}
				if (s_value.empty() || s_value == "-")
				{
					s_value += '0';
				}
				s_value += '.';
			}
			else if (s_value == "0")
			{
				s_value = c; // no leading zeros
			}
			else if (s_value == "-0")
			{
				s_value = "-";
				s_value += c;
			}
			else
			{
				s_value += c;
			}
			showValue();
		}

		void backspace()
		{
			s_fresh = false;
			if (!s_value.empty())
			{
				s_value.pop_back();
				showValue();
			}
		}

		void posKey()
		{
			if (s_pad.pos)
			{
				s_value = formatValue(s_pad.pos(), 3);
				s_fresh = true;
				showValue();
			}
		}

		bool numpadHasPrompt()
		{
			return !s_pad.promptTitle.empty() || !s_pad.promptText.empty();
		}

		TextButton& padKey(int32_t col, int32_t row, const char* text, const lv_font_t* font)
		{
			s_keys.push_back(std::make_unique<TextButton>());
			TextButton& k = *s_keys.back();
			const int32_t y = (numpadHasPrompt() ? HdrH : 0) + KeyY(row);
			k.create(s_box, X(KeyX(col)), Y(y), W(KeyX(col), KW), H(y, KH), text, font);
			k.setBorderVisible(false); // PanelDue: the keys have no outline
			return k;
		}

		void drawBackspace(lv_event_t* e)
		{
			lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
			lv_area_t a;
			lv_obj_get_coords(obj, &a);
			Pen pen(lv_event_get_layer(e), a.x1 + lv_area_get_width(&a) / 2, a.y1 + lv_area_get_height(&a) / 2);
			DrawNumpadBackspaceIcon(pen, Colour::Text());
		}

		// Builds the numpad window from s_pad / s_value / s_fresh (also when coming back from its ALERT).
		// A machine prompt: the window grows upwards by the header, the numpad stays where it is.
		void openNumpadWindow()
		{
			UI_LOCK();
			const int32_t hdr = numpadHasPrompt() ? HdrH : 0;
			openWindow(Mode::Numpad, PadX, PadY - hdr, PadW, PadH + hdr);
			const lv_color_t accent = Colour::Accent();
			if (hdr != 0)
			{
				DrawPromptHeader(s_box, SL(PadX, PadW) - 2 * CardBorder, s_pad.promptTitle, s_pad.promptText);
			}
			const int32_t top = hdr + NM;
			auto KeyY = [hdr](int32_t row) { return hdr + CncPopupKeyY(row); };

			// Value tile: value left, unit right
			tile(NM, top, ValW, ValH, Colour::Border());
			s_valueLabel = label(NM + 16, top, 196, ValH, Font::Large(), Colour::Text(), LV_TEXT_ALIGN_LEFT, "");
			label(NM + ValW - 14 - 90,
				  top,
				  90,
				  ValH,
				  Font::Small(),
				  Colour::Muted(),
				  LV_TEXT_ALIGN_RIGHT,
				  s_pad.unit.c_str());

			// Tag tile: what is being set, accent text and outline
			tile(KeyX(3), top, KW, ValH, accent);
			label(KeyX(3) + 3, top, KW - 6, ValH, Font::Small(), accent, LV_TEXT_ALIGN_CENTER, s_pad.tag.c_str());

			// Digits 1-9, then . 0 -
			static const char* const digits[10] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
			for (int d = 1; d <= 9; ++d)
			{
				const int i = d - 1;
				const char c = (char)('0' + d);
				padKey(i % 3, i / 3, digits[d], Font::Large()).onPress([c] { key(c); });
			}
			auto optionalKey = [](int32_t col, const char* text, bool enabled, char c)
			{
				TextButton& k = padKey(col, 3, text, Font::Large());
				if (enabled)
				{
					k.onPress([c] { key(c); });
				}
				else
				{
					k.setColours(Colour::Muted(), Colour::Tile()); // inactive: muted, no reaction
					k.setPressedLook(false);
				}
			};
			optionalKey(0, ".", s_pad.allowDecimal, '.');
			padKey(1, 3, "0", Font::Large()).onPress([] { key('0'); });
			optionalKey(2, "-", s_pad.allowMinus, '-');

			// Side column: backspace, X, check, POS
			TextButton& back = padKey(3, 0, "", Font::Small());
			lv_obj_add_event_cb(back.obj(), drawBackspace, LV_EVENT_DRAW_MAIN_END, nullptr);
			back.onPress([] { backspace(); });
			lv_obj_t* cancel = actionButton(KeyX(3), KeyY(1), KW, KH, false);
			if (!s_pad.cancelAllowed)
			{
				lv_obj_set_style_bg_color(cancel, Colour::Border(), 0); // the machine waits for a value
			}
			actionButton(KeyX(3), KeyY(2), KW, KH, true);
			if (s_pad.pos)
			{
				padKey(3, 3, "POS", Font::Small()).onPress([] { posKey(); });
			}
			showValue();
		}

		void numpadOk()
		{
			float v = 0.0f;
			if (!parseValue(v) || v < s_pad.min || v > s_pad.max)
			{
				// ALERT, and its X brings the numpad back with the typed value
				const int dec = (int)s_pad.decimals;
				const bool prompt = s_machinePrompt;
				openStandard(
					Mode::Alert,
					"ALERT !",
					wrap(fmt::format("Enter a value from {:.{}f} to {:.{}f}.", s_pad.min, dec, s_pad.max, dec)),
					nullptr);
				s_alertBackToNumpad = true;
				s_machinePrompt = prompt; // still the machine's prompt: nothing replaces it
				return;
			}
			std::function<void(float)> h = s_pad.onOk;
			Close(); // close first: the handler may open the next popup
			if (h)
			{
				h(v);
			}
		}

		// ---- Choice prompt ----

		size_t choicesPerPage()
		{
			return s_choiceCols * (size_t)CpRows;
		}

		size_t choicePages()
		{
			return s_choices.empty() ? 1 : (s_choices.size() + choicesPerPage() - 1) / choicesPerPage();
		}

		// Columns: 3, 2 or 1, as many as the longest choice fits in (4 choices: 2 x 2)
		size_t choiceColumns(const std::vector<std::string>& choices)
		{
			int32_t widest = 0;
			for (const std::string& c : choices)
			{
				widest = std::max(widest, textWidth(c, Font::Small()));
			}
			size_t cols = 3;
			while (cols > 1 && widest + S(24) > S((InfoW - (int32_t)(cols - 1) * ChoiceGapX) / (int32_t)cols))
			{
				--cols;
			}
			if (cols == 3 && choices.size() == 4)
			{
				cols = 2; // 2 x 2 rather than 3 + 1
			}
			return std::max<size_t>(1, std::min(cols, choices.size()));
		}

		void showChoicePageButtons()
		{
			if (s_pagePrev)
			{
				s_pagePrev->setLocked(s_choicePage == 0);
			}
			if (s_pageNext)
			{
				s_pageNext->setLocked(s_choicePage + 1 >= choicePages());
			}
		}

		// The buttons of the shown page, centred in the choice area
		void buildChoicePage()
		{
			if (s_choiceArea != nullptr)
			{
				lv_obj_delete(s_choiceArea);
			}
			s_choiceButtons.clear();
			s_choiceArea = CreateBare(s_box, 0, 0, W(0, StdW), H(0, CpH));
			lv_obj_remove_flag(s_choiceArea, LV_OBJ_FLAG_CLICKABLE);
			const size_t cols = s_choiceCols;
			s_choiceFirst = s_choicePage * choicesPerPage();
			const size_t count =
				std::min(choicesPerPage(), s_choices.size() - std::min(s_choiceFirst, s_choices.size()));
			if (count > 0)
			{
				const size_t rows = (count + cols - 1) / cols;
				const int32_t w = (InfoW - (int32_t)(cols - 1) * ChoiceGapX) / (int32_t)cols;
				const int32_t blockH = (int32_t)rows * CpRowH + (int32_t)(rows - 1) * CpGapY;
				const int32_t top = CpAreaY + (CpAreaH - blockH) / 2;
				for (size_t j = 0; j < count; ++j)
				{
					const size_t row = j / cols;
					const size_t inRow = (row + 1 < rows) ? cols : count - row * cols;
					const int32_t rowW = (int32_t)inRow * w + (int32_t)(inRow - 1) * ChoiceGapX;
					const int32_t x = InfoX + (InfoW - rowW) / 2 + (int32_t)(j % cols) * (w + ChoiceGapX);
					const int32_t y = top + (int32_t)row * (CpRowH + CpGapY);
					const size_t i = s_choiceFirst + j;
					s_choiceButtons.push_back(std::make_unique<TextButton>());
					TextButton& b = *s_choiceButtons.back();
					b.create(s_choiceArea, X(x), Y(y), W(x, w), H(y, CpRowH), s_choices[i].c_str(), Font::Small());
					b.onPress([i] { PressChoice(i); });
				}
			}
			showChoiceSelection();
			showChoicePageButtons();
		}
	} // namespace

	// ---- Standard popup ----

	void Confirm(std::string_view title, const std::vector<std::string>& lines, Handler onOk)
	{
		s_alertBackToNumpad = false;
		openStandard(Mode::Confirm, title, lines, std::move(onOk));
	}

	void ConfirmWithTrash(
		std::string_view title, const std::vector<std::string>& lines, Handler onTrash, Handler onOk, bool okAvailable)
	{
		s_alertBackToNumpad = false;
		s_okGrey = !okAvailable;
		openStandard(Mode::Confirm, title, lines, std::move(onOk), std::move(onTrash));
		s_okGrey = false;
		s_okGreyOpen = !okAvailable;
	}

	void ConfirmWarning(std::string_view title, const std::vector<std::string>& lines, Handler onOk)
	{
		s_alertBackToNumpad = false;
		s_warningLook = true;
		openStandard(Mode::Confirm, title, lines, std::move(onOk));
		s_warningLook = false;
	}

	void Alert(std::string_view text)
	{
		s_alertBackToNumpad = false;
		openStandard(Mode::Alert, "ALERT !", wrap(text), nullptr);
	}

	void Info(std::string_view title, std::string_view text)
	{
		s_alertBackToNumpad = false;
		openStandard(Mode::Alert, title, wrap(text), nullptr);
	}

	void Warning(std::string_view title, std::string_view text)
	{
		s_alertBackToNumpad = false;
		s_warningLook = true;
		openStandard(Mode::Alert, title, wrap(text), nullptr);
		s_warningLook = false;
	}

	void Choose(std::string_view title,
				const std::vector<std::string>& choices,
				size_t selected,
				std::function<void(size_t)> onOk,
				size_t columns,
				std::function<bool(size_t)> allowed)
	{
		UI_LOCK();
		s_alertBackToNumpad = false;
		openWindow(Mode::Choose, StdX, StdY, StdW, StdH);
		titleTile(title);
		s_lines.clear();
		s_choiceColours.clear();
		s_choiceFirst = 0;
		s_choices.assign(choices.begin(), choices.begin() + (std::ptrdiff_t)std::min(choices.size(), MaxChoices));
		s_onChoice = std::move(onOk);
		s_choiceAllowed = std::move(allowed);
		const size_t n = s_choices.size();
		s_pending = (selected < n) ? selected : 0;

		// Up to 3 per row (5 choices: 3 + 2), 4 in one row; or the given number of columns
		if (n > 0)
		{
			const size_t perRow = (columns != 0 && columns <= n) ? columns : ((n <= 4) ? n : 3);
			const size_t rows = (n + perRow - 1) / perRow;
			const int32_t w = (InfoW - (int32_t)(perRow - 1) * ChoiceGapX) / (int32_t)perRow;
			const int32_t blockH = (int32_t)rows * ChoiceH + (int32_t)(rows - 1) * ChoiceGapY;
			const int32_t top = InfoY + (InfoH - blockH) / 2;
			for (size_t i = 0; i < n; ++i)
			{
				const size_t row = i / perRow;
				const size_t inRow = (row + 1 < rows) ? perRow : n - row * perRow;
				const int32_t rowW = (int32_t)inRow * w + (int32_t)(inRow - 1) * ChoiceGapX;
				const int32_t x = InfoX + (InfoW - rowW) / 2 + (int32_t)(i % perRow) * (w + ChoiceGapX);
				const int32_t y = top + (int32_t)row * (ChoiceH + ChoiceGapY);
				s_choiceButtons.push_back(std::make_unique<TextButton>());
				TextButton& b = *s_choiceButtons.back();
				b.create(s_box, X(x), Y(y), W(x, w), H(y, ChoiceH), s_choices[i].c_str(), Font::Small());
				b.onPress([i] { PressChoice(i); });
				if (s_choiceAllowed && !s_choiceAllowed(i))
				{
					b.setColours(Colour::Border(), Colour::Tile()); // greyed: a tap does nothing
					b.setPressedLook(false);
				}
			}
		}
		showChoiceSelection();
		actionButton(CancelX, ActY, ActW, ActH, false);
		actionButton(OkX, ActY, ActW, ActH, true);
	}

	void ChooseColour(std::string_view title,
					  const std::vector<lv_color_t>& colours,
					  size_t selected,
					  std::function<void(size_t)> onOk)
	{
		Choose(title, std::vector<std::string>(colours.size()), selected, std::move(onOk), 4);
		s_choiceColours = colours;
		showChoiceSelection();
	}

	// ---- Form ----

	// question: FormQuestion (one group, no label): a tile with the question, the answers at the bottom
	static void formImpl(std::string_view title,
						 const std::vector<FormGroup>& groups,
						 std::function<void(const std::vector<uint16_t>&)> onOk,
						 const std::string* question)
	{
		UI_LOCK();
		s_alertBackToNumpad = false;
		openWindow(Mode::Form, StdX, StdY, StdW, StdH);
		titleTile(title);
		s_lines.clear();
		s_formGroups.assign(groups.begin(), groups.begin() + (std::ptrdiff_t)std::min(groups.size(), MaxFormGroups));
		s_onForm = std::move(onOk);
		s_formSelected.clear();
		s_formItems.reserve(MaxFormGroups * MaxFormItems);

		// Height of the whole block, to centre it in the info area
		int32_t blockH = 0;
		for (size_t g = 0; g < s_formGroups.size(); ++g)
		{
			FormGroup& grp = s_formGroups[g];
			if (grp.items.size() > MaxFormItems)
			{
				grp.items.resize(MaxFormItems);
			}
			const size_t n = grp.items.size();
			const size_t rows = (n + formItemsPerRow(n) - 1) / formItemsPerRow(n);
			blockH += (question ? 0 : FormLabelH) + (int32_t)rows * FormBtnH + (int32_t)(rows - 1) * FormGapY;
			if (g + 1 < s_formGroups.size())
			{
				blockH += FormGroupGap;
			}
		}
		int32_t y = question ? InfoY + InfoH - blockH : InfoY + ((blockH < InfoH) ? (InfoH - blockH) / 2 : 0);
		if (question)
		{
			// Question tile from the info area top down to a gap above the answers; the text centred
			// across it, 20 px above its middle (PanelDue FormQuestion)
			const int32_t tileH = y - FormGroupGap - InfoY;
			tile(InfoX, InfoY, InfoW, tileH, Colour::Border());
			const int32_t mid = (tileH - LineH) / 2;
			label(InfoX + 10,
				  InfoY + ((mid > 20) ? mid - 20 : 0),
				  InfoW - 20,
				  LineH,
				  Font::Small(),
				  Colour::Text(),
				  LV_TEXT_ALIGN_CENTER,
				  question->c_str());
			s_lines = {*question};
		}

		for (size_t g = 0; g < s_formGroups.size(); ++g)
		{
			const FormGroup& grp = s_formGroups[g];
			s_formSelected.push_back((uint16_t)(grp.selected & ~grp.disabled));
			if (!question)
			{
				label(InfoX + 2,
					  y,
					  InfoW - 4,
					  FormLabelH,
					  Font::Small(),
					  Colour::Muted(),
					  LV_TEXT_ALIGN_LEFT,
					  grp.label.c_str());
				y += FormLabelH;
			}

			const size_t n = grp.items.size();
			const size_t perRow = formItemsPerRow(n);
			const int32_t w = (InfoW - (int32_t)(perRow - 1) * FormGapX) / (int32_t)perRow;
			for (size_t i = 0; i < n; ++i)
			{
				const int32_t bx = InfoX + (int32_t)(i % perRow) * (w + FormGapX);
				const int32_t by = y + (int32_t)(i / perRow) * (FormBtnH + FormGapY);
				FormItem it{};
				it.group = g;
				it.index = i;
				it.obj = tile(bx, by, w, FormBtnH, Colour::Border());
				lv_obj_add_flag(it.obj, LV_OBJ_FLAG_CLICKABLE);
				it.label = CreateLabel(it.obj,
									   SL(s_px + bx, 4) - CardBorder,
									   -CardBorder,
									   SL(s_px + bx + 4, w - 8),
									   H(by, FormBtnH),
									   Font::Small(),
									   Colour::Text(),
									   LV_TEXT_ALIGN_CENTER,
									   grp.items[i].c_str());
				// Dot: radius 3, centred 9 px from the top right corner
				it.dot = CreateBare(it.obj,
									SL(s_px + bx, w - 13) - CardBorder,
									SL(s_py + by, 6) - CardBorder,
									SL(s_px + bx + w - 13, 7),
									SL(s_py + by + 6, 7));
				lv_obj_set_style_bg_opa(it.dot, LV_OPA_COVER, 0);
				lv_obj_set_style_radius(it.dot, LV_RADIUS_CIRCLE, 0);
				s_formItems.push_back(it);
				lv_obj_add_event_cb(
					it.obj,
					[](lv_event_t* e)
					{
						const FormItem* fi = static_cast<const FormItem*>(lv_event_get_user_data(e));
						PressFormItem(fi->group, fi->index);
					},
					LV_EVENT_PRESSED,
					&s_formItems.back());
			}
			const size_t rows = (n + perRow - 1) / perRow;
			y += (int32_t)rows * FormBtnH + (int32_t)(rows - 1) * FormGapY + FormGroupGap;
		}
		actionButton(CancelX, ActY, ActW, ActH, false);
		s_okButton = actionButton(OkX, ActY, ActW, ActH, true);
		showFormItems();
	}

	void Form(std::string_view title,
			  const std::vector<FormGroup>& groups,
			  std::function<void(const std::vector<uint16_t>&)> onOk)
	{
		formImpl(title, groups, std::move(onOk), nullptr);
	}

	void FormQuestion(std::string_view title,
					  std::string_view question,
					  const std::vector<std::string>& answers,
					  std::function<void(size_t)> onOk)
	{
		FormGroup g;
		g.items = answers; // no answer selected yet
		const std::string q(question);
		formImpl(
			title,
			{g},
			[onOk = std::move(onOk)](const std::vector<uint16_t>& sel)
			{
				size_t i = 0;
				while (i < 16 && (sel[0] & (1u << i)) == 0)
				{
					++i;
				}
				if (onOk)
				{
					onOk(i);
				}
			},
			&q);
	}

	// ---- Numpad ----

	void Numpad(const NumpadSpec& spec)
	{
		s_pad = spec;
		s_value = formatValue(spec.value, spec.decimals);
		s_fresh = true;
		s_alertBackToNumpad = false;
		openNumpadWindow();
	}

	// ---- Common ----

	bool IsOpen()
	{
		return s_box != nullptr;
	}

	void Close()
	{
		deleteWindow();
	}

	void SetStopTarget(int32_t x, int32_t y, int32_t w, int32_t h, Handler onStop)
	{
		s_stopArea = {x, y, x + w - 1, y + h - 1};
		s_onStop = std::move(onStop);
	}

	std::string Title()
	{
		return (s_mode == Mode::Confirm || s_mode == Mode::Alert || s_mode == Mode::Choose || s_mode == Mode::Form ||
				s_mode == Mode::Message || s_mode == Mode::Response || s_machinePrompt)
				   ? s_title
				   : std::string();
	}

	std::vector<std::string> Lines()
	{
		return (s_mode == Mode::Confirm || s_mode == Mode::Alert || s_mode == Mode::Form || s_mode == Mode::Message ||
				s_mode == Mode::Response || s_machinePrompt)
				   ? s_lines
				   : std::vector<std::string>();
	}

	bool HasOk()
	{
		return s_mode == Mode::Confirm || s_mode == Mode::Choose || s_mode == Mode::Form || s_mode == Mode::Numpad ||
			   s_mode == Mode::Message;
	}

	void PressOk()
	{
		switch (s_mode)
		{
		case Mode::Confirm:
		case Mode::Message:
		{
			Handler h = std::move(s_onOk);
			Close(); // close first: the handler may open the next popup
			if (h)
			{
				h();
			}
			break;
		}
		case Mode::Choose:
		{
			std::function<void(size_t)> h = std::move(s_onChoice);
			const size_t choice = s_pending;
			Close();
			if (h)
			{
				h(choice);
			}
			break;
		}
		case Mode::Form:
		{
			if (!formValid())
			{
				break; // greyed check mark: nothing happens
			}
			auto h = std::move(s_onForm);
			const std::vector<uint16_t> selected = s_formSelected;
			Close();
			if (h)
			{
				h(selected);
			}
			break;
		}
		case Mode::Numpad:
			numpadOk();
			break;
		default:
			break;
		}
	}

	void PressCancel()
	{
		if (s_mode == Mode::Alert && s_alertBackToNumpad)
		{
			s_alertBackToNumpad = false;
			openNumpadWindow(); // same value, same limits
			return;
		}
		if ((s_mode == Mode::Choose && s_choicePrompt && !s_choiceCancel) ||
			(s_mode == Mode::Numpad && !s_pad.cancelAllowed))
		{
			return; // the machine waits for an answer: no X
		}
		Handler h = (s_mode == Mode::Message && s_messageWithCancel) ? std::move(s_onCancel)
					: (s_mode == Mode::Choose && s_choicePrompt)	 ? std::move(s_choiceCancel)
					: (s_mode == Mode::Numpad)						 ? s_pad.onCancel
																	 : nullptr;
		Close();
		if (h)
		{
			h();
		}
	}

	bool IsChooseOpen()
	{
		return s_mode == Mode::Choose;
	}

	size_t ChoiceSelected()
	{
		return s_pending;
	}

	bool HasTrash()
	{
		return s_mode == Mode::Confirm && s_onTrash != nullptr;
	}

	uint32_t OpenCount()
	{
		return s_openCount;
	}

	void MarkMachineBox()
	{
		s_machineBoxOpen = s_mode != Mode::None;
	}

	bool IsMachineBoxOpen()
	{
		return s_mode != Mode::None && s_machineBoxOpen;
	}

	bool WarningLook()
	{
		return s_mode != Mode::None && s_warningOpen;
	}

	bool OkGreyed()
	{
		return s_mode == Mode::Confirm && s_okGreyOpen;
	}

	void PressTrash()
	{
		if (!HasTrash())
		{
			return;
		}
		Handler h = std::move(s_onTrash);
		Close(); // close first: the handler may open the next page or popup
		h();
	}

	void PressChoice(size_t index)
	{
		if (s_mode != Mode::Choose || index >= s_choices.size() || (s_choiceAllowed && !s_choiceAllowed(index)))
		{
			return; // greyed choice: nothing happens
		}
		s_pending = index;
		showChoiceSelection();
	}

	bool IsNumpadOpen()
	{
		return s_mode == Mode::Numpad;
	}

	std::string NumpadTag()
	{
		return IsNumpadOpen() ? s_pad.tag : std::string();
	}

	std::string NumpadValue()
	{
		return IsNumpadOpen() ? s_value : std::string();
	}

	void NumpadKey(char k)
	{
		if (!IsNumpadOpen())
		{
			return;
		}
		if (k == '\b')
		{
			backspace();
		}
		else if (k == 'P')
		{
			posKey();
		}
		else if ((k == '.' && !s_pad.allowDecimal) || (k == '-' && !s_pad.allowMinus))
		{
			return; // inactive key
		}
		else if ((k >= '0' && k <= '9') || k == '.' || k == '-')
		{
			key(k);
		}
	}

	bool IsFormOpen()
	{
		return s_mode == Mode::Form;
	}

	uint16_t FormSelection(size_t group)
	{
		return (IsFormOpen() && group < s_formSelected.size()) ? s_formSelected[group] : 0;
	}

	bool FormOkEnabled()
	{
		return IsFormOpen() && formValid();
	}

	void PressFormItem(size_t group, size_t index)
	{
		if (!IsFormOpen() || group >= s_formGroups.size() || index >= s_formGroups[group].items.size() ||
			(s_formGroups[group].disabled & (1u << index)) != 0)
		{
			return; // greyed item: nothing happens
		}
		const uint16_t bit = (uint16_t)(1u << index);
		if (s_formGroups[group].multi)
		{
			s_formSelected[group] ^= bit;
		}
		else
		{
			s_formSelected[group] = bit;
		}
		showFormItems();
	}

	// ---- Machine messages ----

	void Message(std::string_view title,
				 std::string_view text,
				 bool withCancel,
				 Handler onOk,
				 Handler onCancel,
				 bool waits,
				 bool passive)
	{
		s_alertBackToNumpad = false;
		s_messageWithCancel = withCancel;
		s_messageWaits = waits;
		s_messagePassive = passive;
		s_onCancel = std::move(onCancel);
		openStandard(Mode::Message, title, wrap(text), std::move(onOk));
	}

	bool IsMessageOpen()
	{
		return s_mode == Mode::Message;
	}

	bool IsBlockingMessageOpen()
	{
		return s_machinePrompt || (s_mode == Mode::Message &&
								   (s_messageWaits || s_messageWithCancel || (s_onOk != nullptr && !s_messagePassive)));
	}

	bool CanShowPassive()
	{
		return s_mode == Mode::None || s_mode == Mode::Response ||
			   (s_mode == Mode::Message && !IsBlockingMessageOpen());
	}

	// ---- Replies ----

	void Response(std::string_view text, bool isError, uint32_t timeoutMs, bool force)
	{
		if (!CanShowPassive() && (!force || IsBlockingMessageOpen()))
		{
			return; // a popup in use, or a prompt the machine waits for
		}
		UI_LOCK();
		s_alertBackToNumpad = false;
		openStandard(Mode::Response, isError ? "ALERT !" : "", wrap(text), nullptr);
		if (!isError && timeoutMs != 0)
		{
			s_responseTimer = lv_timer_create(
				[](lv_timer_t*)
				{
					s_responseTimer = nullptr; // deletes itself after this run
					if (s_mode == Mode::Response)
					{
						Close();
					}
				},
				timeoutMs,
				nullptr);
			lv_timer_set_repeat_count(s_responseTimer, 1);
		}
	}

	void CloseResponse()
	{
		if (s_mode == Mode::Response)
		{
			Close();
		}
	}

	bool IsResponseOpen()
	{
		return s_mode == Mode::Response;
	}

	// ---- Machine prompts ----

	int32_t PromptHeaderH()
	{
		return S(HdrH);
	}

	void DrawPromptHeader(lv_obj_t* box, int32_t innerW, std::string_view title, std::string_view text)
	{
		UI_LOCK();
		s_title = title;
		const bool longTitle = s_title.size() > 9;
		const int32_t tw = longTitle ? innerW - 2 * S(TitleLongX) : S(TitleShortW);
		const int32_t ty = S(HdrM), th = S(HdrTitleH);
		lv_obj_t* t = CreateBare(box, (innerW - tw) / 2, ty, tw, th);
		StyleCard(t, Colour::Tile(), Colour::Accent());
		CreateLabel(
			box, (innerW - tw) / 2, ty, tw, th, Font::Large(), Colour::Text(), LV_TEXT_ALIGN_CENTER, s_title.c_str());

		const int32_t x = S(InfoX) - CardBorder, w = innerW - 2 * x;
		const int32_t y = ty + th + S(HdrGap), h = S(HdrTextH);
		lv_obj_t* textTile = CreateBare(box, x, y, w, h);
		StyleCard(textTile, Colour::Tile(), Colour::Border());
		s_lines = wrap(text, w - S(20), HdrLines);
		const int32_t n = (int32_t)s_lines.size();
		const int32_t first = y + (h - ((n - 1) * S(HdrPitch) + S(LineH))) / 2;
		for (int32_t i = 0; i < n; ++i)
		{
			CreateLabel(box,
						x + S(10),
						first + i * S(HdrPitch),
						w - S(20),
						S(LineH),
						Font::Small(),
						Colour::Text(),
						LV_TEXT_ALIGN_CENTER,
						s_lines[(size_t)i].c_str());
		}
		s_machinePrompt = true;
	}

	bool IsMachinePromptOpen()
	{
		return s_machinePrompt;
	}

	void ChoosePrompt(std::string_view title,
					  std::string_view text,
					  const std::vector<std::string>& choices,
					  size_t selected,
					  std::function<void(size_t)> onOk,
					  Handler onCancel)
	{
		UI_LOCK();
		s_alertBackToNumpad = false;
		openWindow(Mode::Choose, StdX, CpY, StdW, CpH);
		DrawPromptHeader(s_box, W(0, StdW) - 2 * CardBorder, title, text);
		s_choicePrompt = true;
		s_choiceText = text;
		s_choiceColours.clear();
		s_choiceAllowed = nullptr;
		s_choices = choices;
		s_onChoice = std::move(onOk);
		s_choiceCancel = std::move(onCancel);
		s_choiceCols = choiceColumns(s_choices);
		s_pending = (selected < s_choices.size()) ? selected : 0;
		s_choicePage = s_pending / choicesPerPage();
		if (choicePages() > 1)
		{
			s_pagePrev = std::make_unique<TextButton>();
			s_pagePrev->create(s_box, X(CpPrevX), Y(CpActY), W(CpPrevX, CpPageW), H(CpActY, ActH), "<", Font::Large());
			s_pagePrev->onPress([] { PressChoicePage(-1); });
			s_pageNext = std::make_unique<TextButton>();
			s_pageNext->create(s_box, X(CpNextX), Y(CpActY), W(CpNextX, CpPageW), H(CpActY, ActH), ">", Font::Large());
			s_pageNext->onPress([] { PressChoicePage(1); });
		}
		buildChoicePage();
		if (s_choiceCancel)
		{
			actionButton(CancelX, CpActY, ActW, ActH, false);
			actionButton(OkX, CpActY, ActW, ActH, true);
		}
		else
		{
			actionButton(AloneX, CpActY, ActW, ActH, true);
		}
	}

	size_t ChoicePage()
	{
		return s_choicePage;
	}

	size_t ChoicePages()
	{
		return (s_mode == Mode::Choose && s_choicePrompt) ? choicePages() : 1;
	}

	void PressChoicePage(int dir)
	{
		if (s_mode != Mode::Choose || !s_choicePrompt)
		{
			return;
		}
		if ((dir < 0 && s_choicePage == 0) || (dir > 0 && s_choicePage + 1 >= choicePages()))
		{
			return; // first / last page: the arrow is muted
		}
		UI_LOCK();
		s_choicePage = (dir < 0) ? s_choicePage - 1 : s_choicePage + 1;
		buildChoicePage(); // the selection stays where it was (it may be on another page)
	}

	// ---- Page windows ----

	lv_obj_t* OpenWindow(int id, int32_t pdX, int32_t pdY, int32_t pdW, int32_t pdH)
	{
		UI_LOCK();
		s_alertBackToNumpad = false;
		openWindow(Mode::Window, pdX, pdY, pdW, pdH);
		s_windowId = id;
		return s_box;
	}

	lv_obj_t* OpenWindowPx(int id, int32_t x, int32_t y, int32_t w, int32_t h, bool modal, Handler onClosed)
	{
		UI_LOCK();
		s_alertBackToNumpad = false;
		openWindowPx(Mode::Window, x, y, w, h, modal);
		s_windowId = id;
		s_onWindowClosed = std::move(onClosed);
		return s_box;
	}

	int WindowId()
	{
		return (s_mode == Mode::Window) ? s_windowId : 0;
	}

	void EmergencyStop()
	{
		if (s_onStop)
		{
			s_onStop();
		}
	}

	void InEvent(const Handler& fn)
	{
		const bool was = s_inEvent;
		s_inEvent = true;
		fn();
		s_inEvent = was;
	}
} // namespace UI::Cnc::Popup
