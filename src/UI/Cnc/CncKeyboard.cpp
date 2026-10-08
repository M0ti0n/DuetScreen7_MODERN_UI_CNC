/*
 * CncKeyboard.cpp
 *
 * Geometry: CncLayout::Keyboard (PanelDue values inside the sheet, x 1.25).
 */

#include "CncKeyboard.h"
#include "CncGlyphs.h"
#include "CncLayout.h"
#include "CncPopup.h"
#include "CncTheme.h"
#include "CncWidgets.h"
#include "Debug.h"
#include "UI/Components/LVGL/LvObj.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace UI::Cnc::Keyboard
{
	using namespace CncLayout;
	using namespace CncLayout::Keyboard;

	namespace
	{
		// Key rows 1..3 (row 0 = digits never changes). Upper case, lower case, symbols.
		constexpr size_t NumChangingRows = 3;
		const char* const UpperRows[NumChangingRows] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
		const char* const LowerRows[NumChangingRows] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
		const char* const SymbolRows[NumChangingRows] = {"[]{}()<>=+", "_:;\"'#$%&", "*/\\^!?@"};
		const char* const Digits = "1234567890";

		Client s_client;
		int s_owner = 0;
		bool s_open = false;
		bool s_closing = false; // closed by Close() or the client's own keys: no 'gone'
		bool s_lower = false, s_symbols = false;
		bool s_upEnabled = false, s_downEnabled = false;
		std::string s_text;

		// Widgets of the open sheet
		lv_obj_t* s_textLabel = nullptr;
		std::vector<std::unique_ptr<TextButton>> s_keys;
		TextButton* s_rowKeys[NumChangingRows][10] = {};
		std::unique_ptr<GlyphButton> s_shift;
		std::unique_ptr<TextButton> s_mode;
		std::unique_ptr<ArrowButton> s_up, s_down;
		std::function<void()> s_enterHandler, s_closeHandler;
		int32_t s_hdr = 0; // px: machine prompt header above the sheet

		// Sheet coordinates (PanelDue units inside the sheet) -> pixels inside its 2 px border
		int32_t X(int32_t v)
		{
			return S(v) - CardBorder;
		}
		int32_t Y(int32_t v)
		{
			return S(TopPd + v) - S(TopPd) - CardBorder + s_hdr;
		}
		int32_t W(int32_t x, int32_t w)
		{
			return SL(x, w);
		}
		int32_t H(int32_t y, int32_t h)
		{
			return SL(TopPd + y, h);
		}

		// The end of the text (where the cursor is) when it is wider than the field
		// At most n bytes, never half a UTF-8 character
		std::string utf8Prefix(const std::string& text, size_t n)
		{
			if (text.size() <= n)
			{
				return text;
			}
			while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80)
			{
				--n;
			}
			return text.substr(0, n);
		}

		void showText()
		{
			if (s_textLabel == nullptr)
			{
				return;
			}
			const int32_t avail = S(FieldTextAvail);
			size_t p = 0;
			lv_point_t size;
			for (;;)
			{
				lv_text_get_size(&size, s_text.c_str() + p, Font::Small(), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
				if (size.x <= avail || p >= s_text.size())
				{
					break;
				}
				do
				{
					++p; // whole UTF-8 characters
				} while (p < s_text.size() && ((unsigned char)s_text[p] & 0xC0) == 0x80);
			}
			lv_label_set_text(s_textLabel, (s_text.substr(p) + "_").c_str());
		}

		void showKeys()
		{
			for (size_t r = 0; r < NumChangingRows; ++r)
			{
				const char* row = s_symbols ? SymbolRows[r] : s_lower ? LowerRows[r] : UpperRows[r];
				for (size_t i = 0; row[i] != 0 && i < 10; ++i)
				{
					if (s_rowKeys[r][i] != nullptr)
					{
						const char c[2] = {row[i], 0};
						s_rowKeys[r][i]->setText(c);
					}
				}
			}
			if (s_shift)
			{
				s_shift->setGlyph(s_symbols ? GlyphButton::Kind::Char : GlyphButton::Kind::Shift, s_symbols ? '~' : 0);
				s_shift->setActive(!s_symbols && s_lower);
			}
			if (s_mode)
			{
				s_mode->setText(s_symbols ? "ABC" : "#+=");
				s_mode->setToggle(s_symbols); // symbol page on: accent outline + text
			}
		}

		void changed()
		{
			showText();
			if (s_client.changed)
			{
				s_client.changed(s_text);
			}
		}

		size_t limit()
		{
			return std::min(s_client.maxLength, MaxText);
		}

		void typeChar(char c)
		{
			if (s_text.size() < limit())
			{
				s_text += c;
				changed();
			}
		}

		void clearWidgets()
		{
			s_textLabel = nullptr;
			s_keys.clear();
			for (auto& row : s_rowKeys)
			{
				for (TextButton*& k : row)
				{
					k = nullptr;
				}
			}
			s_shift.reset();
			s_mode.reset();
			s_up.reset();
			s_down.reset();
		}

		TextButton& key(lv_obj_t* box, int32_t x, int32_t y, int32_t w, const char* text)
		{
			s_keys.push_back(std::make_unique<TextButton>());
			TextButton& k = *s_keys.back();
			k.create(box, X(x), Y(y), W(x, w), H(y, KeyH), text, Font::Small());
			return k;
		}

		void drawIcon(lv_event_t* e, int which)
		{
			lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
			lv_area_t a;
			lv_obj_get_coords(obj, &a);
			Pen pen(lv_event_get_layer(e), a.x1 + lv_area_get_width(&a) / 2, a.y1 + lv_area_get_height(&a) / 2);
			switch (which)
			{
			case 0:
				DrawBackspaceIcon(pen); // the keyboard keeps the PanelDue bitmap
				break;
			case 1:
				DrawEnterIcon(pen, Colour::ButtonText());
				break;
			default:
				DrawCancelIcon(pen, Colour::ButtonText(), 11.0f, 4); // the size of the ENTER icon
				break;
			}
		}

		// Red X / green ENTER: acts on release (the sheet may close in the handler)
		lv_obj_t* actionKey(
			lv_obj_t* box, int32_t x, int32_t y, int32_t w, int32_t h, bool enter, std::function<void()>* handler)
		{
			lv_obj_t* b = CreateBare(box, X(x), Y(y), W(x, w), H(y, h));
			StyleCard(b, enter ? Colour::Green() : Colour::StopRed(), Colour::Border(), false);
			lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_event_cb(b,
								enter ? static_cast<lv_event_cb_t>([](lv_event_t* e) { drawIcon(e, 1); })
									  : static_cast<lv_event_cb_t>([](lv_event_t* e) { drawIcon(e, 2); }),
								LV_EVENT_DRAW_MAIN_END,
								nullptr);
			lv_obj_add_event_cb(
				b,
				[](lv_event_t* e) { Popup::InEvent(*static_cast<std::function<void()>*>(lv_event_get_user_data(e))); },
				LV_EVENT_CLICKED,
				handler);
			return b;
		}

		void build()
		{
			const bool prompt = !s_client.promptTitle.empty() || !s_client.promptText.empty();
			s_hdr = prompt ? Popup::PromptHeaderH() : 0;
			lv_obj_t* box = Popup::OpenWindowPx(WindowId,
												0,
												ScreenY - s_hdr,
												ScreenW,
												SL(TopPd, CncLayout::Keyboard::H) + s_hdr,
												prompt, // a machine prompt: nothing else reacts
												[]
												{
													clearWidgets();
													const bool gone = s_open && !s_closing;
													s_open = false;
													if (gone && s_client.gone)
													{
														s_client.gone(); // another popup replaced the keyboard
													}
												});
			s_open = true;
			if (prompt)
			{
				Popup::DrawPromptHeader(box, ScreenW - 2 * CardBorder, s_client.promptTitle, s_client.promptText);
			}

			// Top row: arrows, text field, X
			s_up = std::make_unique<ArrowButton>();
			s_up->create(box, X(UpX), Y(TopY), W(UpX, ArrowW), H(TopY, TopH), true);
			s_up->onPress([] { PressArrow(-1); });
			s_down = std::make_unique<ArrowButton>();
			s_down->create(box, X(DownX), Y(TopY), W(DownX, ArrowW), H(TopY, TopH), false);
			s_down->onPress([] { PressArrow(1); });
			lv_obj_t* field = CreateBare(box, X(FieldX), Y(TopY), W(FieldX, FieldW), H(TopY, TopH));
			StyleCard(field, Colour::Tile(), Colour::Border());
			s_textLabel = CreateLabel(box,
									  X(FieldX + FieldTextInset),
									  Y(TopY),
									  W(FieldX + FieldTextInset, FieldTextW),
									  H(TopY, TopH),
									  Font::Small(),
									  Colour::Text(),
									  LV_TEXT_ALIGN_LEFT,
									  "");
			s_closeHandler = [] { PressCancel(); };
			lv_obj_t* close = actionKey(box, CloseX, CloseY, CloseW, CloseH, false, &s_closeHandler);
			if (!s_client.cancelAllowed)
			{
				lv_obj_set_style_bg_color(close, Colour::Border(), 0); // the machine waits for the text
			}

			// Character keys
			for (size_t i = 0; i < 10; ++i)
			{
				const char c = Digits[i];
				key(box, Row0X + (int32_t)i * KeyPitchX, KeyRowY(0), KeyW, std::string(1, c).c_str())
					.onPress([c] { typeChar(c); });
			}
			static const int32_t rowX[NumChangingRows] = {Row0X, Row2X, Row3X};
			for (size_t r = 0; r < NumChangingRows; ++r)
			{
				const size_t n = std::string(UpperRows[r]).size();
				for (size_t i = 0; i < n; ++i)
				{
					TextButton& k = key(box, rowX[r] + (int32_t)i * KeyPitchX, KeyRowY((unsigned int)r + 1), KeyW, "");
					s_rowKeys[r][i] = &k;
					k.onPress(
						[r, i]
						{
							const char* row = s_symbols ? SymbolRows[r] : s_lower ? LowerRows[r] : UpperRows[r];
							typeChar(row[i]);
						});
				}
			}
			s_shift = std::make_unique<GlyphButton>();
			s_shift->create(
				box, X(Row0X), Y(KeyRowY(3)), W(Row0X, WideW), H(KeyRowY(3), KeyH), GlyphButton::Kind::Shift);
			s_shift->onPress([] { PressShift(); });
			TextButton& back = key(box, BackX, KeyRowY(3), WideW, "");
			lv_obj_add_event_cb(back.obj(), [](lv_event_t* e) { drawIcon(e, 0); }, LV_EVENT_DRAW_MAIN_END, nullptr);
			back.onPress([] { PressBackspace(); });

			// Bottom row
			s_mode = std::make_unique<TextButton>();
			s_mode->create(box, X(Row0X), Y(KeyRowY(4)), W(Row0X, WideW), H(KeyRowY(4), KeyH), "#+=", Font::Small());
			s_mode->setPressedLook(false);
			s_mode->onPress([] { PressMode(); });
			key(box, DotX, KeyRowY(4), KeyW, ".").onPress([] { typeChar('.'); });
			key(box, DotX + KeyPitchX, KeyRowY(4), KeyW, "-").onPress([] { typeChar('-'); });
			key(box, SpaceX, KeyRowY(4), SpaceW, "SPACE").onPress([] { typeChar(' '); });
			s_enterHandler = [] { PressEnter(); };
			actionKey(box, EnterX, EnterY, EnterW, EnterH, true, &s_enterHandler);

			showKeys();
			showText();
			SetArrowsEnabled(s_upEnabled, s_downEnabled);
		}
	} // namespace

	void Open(const std::string& text, Client client, int owner)
	{
		UI_LOCK();
		if (IsOpen())
		{
			// Another client takes the keyboard (e.g. a machine prompt over CONSOLE): the one before is told
			// it is gone (its own client, not the new one)
			Client before = std::move(s_client);
			s_client = Client();
			s_closing = true;
			Popup::Close();
			s_closing = false;
			s_open = false;
			if (before.gone)
			{
				before.gone();
			}
		}
		s_client = std::move(client);
		s_owner = owner;
		s_text = utf8Prefix(text, limit());
		s_symbols = false;
		s_lower = false;
		s_upEnabled = s_downEnabled = false;
		s_closing = false;
		build();
	}

	void Close()
	{
		if (!IsOpen())
		{
			return;
		}
		s_closing = true;
		Popup::Close();
		s_closing = false;
		s_client = Client(); // nothing is called back once closed
	}

	bool IsOpen()
	{
		return s_open && Popup::WindowId() == WindowId;
	}

	bool IsOpenFor(int owner)
	{
		return IsOpen() && s_owner == owner;
	}

	void SetArrowsEnabled(bool up, bool down)
	{
		s_upEnabled = up && static_cast<bool>(s_client.arrow);
		s_downEnabled = down && static_cast<bool>(s_client.arrow);
		if (s_up)
		{
			s_up->setDisabled(!s_upEnabled);
		}
		if (s_down)
		{
			s_down->setDisabled(!s_downEnabled);
		}
	}

	const std::string& Text()
	{
		return s_text;
	}

	void SetText(const std::string& text)
	{
		s_text = utf8Prefix(text, limit());
		showText();
	}

	void PressKey(char c)
	{
		if (IsOpen())
		{
			typeChar(c);
		}
	}

	void PressBackspace()
	{
		if (IsOpen() && !s_text.empty())
		{
			// a whole UTF-8 character (a machine prompt's default text may have "°" or "é")
			while (!s_text.empty() && ((unsigned char)s_text.back() & 0xC0) == 0x80)
			{
				s_text.pop_back();
			}
			if (!s_text.empty())
			{
				s_text.pop_back();
			}
			changed();
		}
	}

	void PressShift()
	{
		if (!IsOpen())
		{
			return;
		}
		if (s_symbols)
		{
			typeChar('~'); // on the symbol page the SHIFT key is '~'
			return;
		}
		s_lower = !s_lower;
		showKeys();
	}

	void PressMode()
	{
		if (IsOpen())
		{
			s_symbols = !s_symbols;
			showKeys();
		}
	}

	void PressEnter()
	{
		if (!IsOpen())
		{
			return;
		}
		const std::string text = s_text;
		const auto enter = s_client.enter; // the client may be cleared by Close()
		if (!s_client.keepOpenOnEnter)
		{
			Close();
		}
		if (enter)
		{
			enter(text);
		}
	}

	void PressCancel()
	{
		if (!IsOpen() || !s_client.cancelAllowed)
		{
			return;
		}
		auto cancel = s_client.cancel;
		Close();
		if (cancel)
		{
			cancel();
		}
	}

	void PressArrow(int dir)
	{
		if (IsOpen() && ((dir < 0) ? s_upEnabled : s_downEnabled) && s_client.arrow)
		{
			s_client.arrow(dir);
		}
	}

	bool LowerCase()
	{
		return s_lower;
	}

	bool Symbols()
	{
		return s_symbols;
	}
} // namespace UI::Cnc::Keyboard
