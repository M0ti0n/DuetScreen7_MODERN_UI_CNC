/*
 * CncSettingsPage.cpp
 *
 * Positions are CncLayout::Settings values (PanelDue pixels) through S() / SL(), relative to the page.
 */

#include "CncSettingsPage.h"
#include "CncKeyboard.h"
#include "CncPanel.h"
#include "CncPopup.h"
#include "CncPreviewSource.h"
#include "CncSettings.h"
#include "CncState.h"
#include "CncTheme.h"
#include "version.h"
#include <algorithm>
#include <cctype>
#include <fmt/format.h>
#include <vector>

namespace UI::Cnc
{
	using namespace CncLayout;
	namespace ST = CncLayout::Settings;

	namespace
	{
		constexpr int KeyboardOwner = 5;

		const char* const PanelLabels[SettingsPage::NumPanelTiles] = {"ACCENT COLOR",
																	  "BRIGHTNESS",
																	  "INFO TIMEOUT",
																	  "SCREEN DIMMING",
																	  "SCREEN ROTATION",
																	  "SOUND",
																	  "CONNECTION",
																	  "SPLIT OPERATIONS",
																	  "ABOUT"};
		const char* const MachineLabels[SettingsPage::NumMachineTiles] = {"PROBE MODE",
																		  "TOOL HOLDER",
																		  "REMEMBER TOOL",
																		  "TOOL SETTER",
																		  "SLOW JOG F",
																		  "COOL / VAC",
																		  "CUSTOM 1",
																		  "CUSTOM 2",
																		  "CUSTOM 3",
																		  "IDLE TIMEOUT",
																		  "JOG PRESETS",
																		  "OVERRIDES"};
		const char* const SectionNames[SettingsPage::NumPages] = {"SYSTEM", "CUSTOMIZATION", "JOG DIRECTION"};
		const char* const CustomTitles[Settings::NumCustom] = {"CUSTOM 1", "CUSTOM 2", "CUSTOM 3"};

		const unsigned int BrightnessValues[] = {10, 20, 40, 60, 80, 100};
		const unsigned int InfoTimeoutValues[] = {0, 3, 6, 10};
		const unsigned int DimValues[] = {0, 60, 300, 600};
		const unsigned int IdleValues[] = {0, 5, 10, 30, 60};

		const char* const PairNames[] = {"NONE", "LEFT / RIGHT", "DOWN / UP", "225\xC2\xB0 / 45\xC2\xB0", "CCW / CW"};

		template <size_t N>
		size_t nearest(const unsigned int (&values)[N], unsigned int v)
		{
			size_t best = 0;
			for (size_t i = 1; i < N; ++i)
			{
				const auto d = [v](unsigned int x) { return (x > v) ? x - v : v - x; };
				if (d(values[i]) < d(values[best]))
				{
					best = i;
				}
			}
			return best;
		}

		void showObj(lv_obj_t* o, bool visible)
		{
			if (visible)
			{
				lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
			}
			else
			{
				lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
			}
		}

		// Labels are written into a G-code string: no quotes, no checksum or comment start
		bool labelCharOk(char c)
		{
			return c >= ' ' && c != '"' && c != '\'' && c != '*' && c != ';';
		}

		// Default label: the macro's display name in capitals, cut to 12
		std::string defaultLabel(size_t slot, const std::string& name)
		{
			std::string l;
			for (char c : name)
			{
				if (l.size() < Settings::MaxLabel && labelCharOk(c))
				{
					l += (char)std::toupper(static_cast<unsigned char>(c));
				}
			}
			return l.empty() ? std::string(CustomTitles[slot]) : l;
		}

		bool jobActive()
		{
			if (State::JobActive())
			{
				Popup::Refuse("Locked while a job is running or paused.");
				return true;
			}
			return false;
		}
	} // namespace

	SettingsPage::~SettingsPage()
	{
		if (Keyboard::IsOpenFor(KeyboardOwner))
		{
			Keyboard::Close(); // its callbacks point here
		}
		if (m_timer != nullptr)
		{
			lv_timer_delete(m_timer);
		}
	}

	// ---- Building ------------------------------------------------------------------------------

	void SettingsPage::create(lv_obj_t* parent, int32_t parentTop)
	{
		m_root = CreateBare(parent, 0, 0, ScreenW, 1);
		lv_obj_set_size(m_root, LV_PCT(100), LV_PCT(100)); // the parent is not laid out yet
		m_top = parentTop;
		auto y = [this](int32_t pdY) { return S(pdY) - m_top; };
		const int32_t smallH = lv_font_get_line_height(Font::Small());

		// IP + FACTORY RESET
		lv_obj_t* ipCard =
			CreateBare(m_root, S(ST::ColX(0)), y(ST::TopY), SL(ST::ColX(0), ST::ColW), SL(ST::TopY, ST::TopH));
		StyleCard(ipCard, Colour::Tile(), Colour::Border());
		const int32_t cardH = SL(ST::TopY, ST::TopH) - 2 * CardBorder;
		CreateLabel(ipCard,
					S(ST::IpLabelDx),
					0,
					S(ST::IpLabelW),
					cardH,
					Font::Small(),
					Colour::Muted(),
					LV_TEXT_ALIGN_LEFT,
					"IP");
		m_ipLabel = CreateLabel(ipCard,
								S(ST::IpLabelDx + ST::IpLabelW),
								0,
								S(ST::IpTextW),
								cardH,
								Font::Small(),
								Colour::Text(),
								LV_TEXT_ALIGN_RIGHT,
								m_ip.c_str());
		m_reset.create(m_root,
					   S(ST::ColX(1)),
					   y(ST::TopY),
					   SL(ST::ColX(1), ST::ColW),
					   SL(ST::TopY, ST::TopH),
					   "FACTORY RESET",
					   Font::Small());
		m_reset.setFilled(Colour::StopRed());
		m_reset.setPressedLook(false);
		m_reset.onPress([this] { factoryResetPressed(); });

		// Section + page number
		m_section = CreateLabel(m_root,
								S(ST::SectionLeftX),
								y(ST::SectionY),
								SL(ST::SectionLeftX, ST::SectionW + 60),
								smallH,
								Font::Small(),
								Colour::Muted(),
								LV_TEXT_ALIGN_LEFT,
								"");
		m_pageLabel = CreateLabel(m_root,
								  S(ST::SectionRightX),
								  y(ST::SectionY),
								  SL(ST::SectionRightX, ST::SectionW),
								  smallH,
								  Font::Small(),
								  Colour::Muted(),
								  LV_TEXT_ALIGN_RIGHT,
								  "");

		for (unsigned int p = 0; p < NumPages; ++p)
		{
			m_pageRoot[p] = CreateBare(m_root, 0, 0, ScreenW, 1);
			lv_obj_set_size(m_pageRoot[p], LV_PCT(100), LV_PCT(100));
		}
		auto tile = [&](TextButton& b, lv_obj_t* root, unsigned int i, const char* text)
		{
			b.create(root,
					 S(ST::TileX(i)),
					 y(ST::TileY(i)),
					 SL(ST::TileX(i), ST::ColW),
					 SL(ST::TileY(i), ST::TileH),
					 text,
					 Font::Small());
			b.setPressedLook(false); // popups and toggles
		};
		for (unsigned int i = 0; i < NumPanelTiles; ++i)
		{
			tile(m_panelTiles[i], m_pageRoot[0], i, PanelLabels[i]);
			m_panelTiles[i].onPress([this, i] { panelTilePressed(i); });
		}
		for (unsigned int i = 0; i < NumMachineTiles; ++i)
		{
			tile(m_machineTiles[i], m_pageRoot[1], i, MachineLabels[i]);
			m_machineTiles[i].onPress([this, i] { machineTilePressed(i); });
		}

		// Page 3: JOG DIRECTION
		lv_obj_t* p3 = m_pageRoot[2];
		m_jogLabels[0] = CreateLabel(p3,
									 S(ST::PairX + 2),
									 y(ST::JogLabelY),
									 SL(ST::PairX, ST::PairW),
									 smallH,
									 Font::Small(),
									 Colour::Muted(),
									 LV_TEXT_ALIGN_LEFT,
									 "ICONS");
		m_jogLabels[1] = CreateLabel(p3,
									 S(ST::PlusX),
									 y(ST::JogLabelY),
									 SL(ST::PlusX, ST::PlusW),
									 smallH,
									 Font::Small(),
									 Colour::Muted(),
									 LV_TEXT_ALIGN_RIGHT,
									 "+ MOVES");
		for (unsigned int r = 0; r < MaxJogRows; ++r)
		{
			JogRow& row = m_jog[r];
			const int32_t ry = y(ST::JogRowY(r)), rh = SL(ST::JogRowY(r), ST::JogRowH);
			row.letter = CreateBare(p3, S(ST::LetterX), ry, SL(ST::LetterX, ST::LetterW), rh);
			StyleCard(row.letter, Colour::Tile(), Colour::Border());
			row.letterLabel = CreateLabel(row.letter,
										  0,
										  0,
										  SL(ST::LetterX, ST::LetterW) - 2 * CardBorder,
										  rh - 2 * CardBorder,
										  Font::Large(),
										  Colour::Accent(),
										  LV_TEXT_ALIGN_CENTER,
										  "X");
			auto touchable = [&](lv_obj_t* o, lv_event_cb_t draw, void (SettingsPage::*fn)(unsigned int))
			{
				StyleCard(o, Colour::Tile(), Colour::Border());
				lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
				lv_obj_set_style_bg_color(o, Colour::Pressed(), LV_STATE_PRESSED);
				lv_obj_add_event_cb(o, draw, LV_EVENT_DRAW_MAIN_END, &row);
				struct Ctx
				{
					SettingsPage* page;
					unsigned int row;
					void (SettingsPage::*fn)(unsigned int);
				};
				auto* ctx = new Ctx{this, r, fn};
				lv_obj_add_event_cb(
					o,
					[](lv_event_t* e)
					{
						auto* c = static_cast<Ctx*>(lv_event_get_user_data(e));
						(c->page->*(c->fn))(c->row);
					},
					LV_EVENT_PRESSED,
					ctx);
				lv_obj_add_event_cb(
					o,
					[](lv_event_t* e) { delete static_cast<Ctx*>(lv_event_get_user_data(e)); },
					LV_EVENT_DELETE,
					ctx);
			};
			row.pair = CreateBare(p3, S(ST::PairX), ry, SL(ST::PairX, ST::PairW), rh);
			touchable(row.pair, drawPair, &SettingsPage::jogPairPressed);
			row.plus = CreateBare(p3, S(ST::PlusX), ry, SL(ST::PlusX, ST::PlusW), rh);
			touchable(row.plus, drawPlus, &SettingsPage::jogPlusPressed);
		}
		m_degree.create(p3,
						S(ST::LetterX),
						y(ST::DegreeY),
						SL(ST::LetterX, ST::DegreeW),
						SL(ST::DegreeY, ST::JogRowH),
						"\xC2\xB0 ON CCW / CW AXES",
						Font::Small());
		m_degree.setPressedLook(false);
		m_degree.onPress([this] { jogDegreePressed(); });

		// Page arrows
		m_up.create(m_root, S(ST::UpX), y(ST::ArrowY), SL(ST::UpX, ST::ArrowW), SL(ST::ArrowY, ST::ArrowH), true);
		m_down.create(
			m_root, S(ST::DownX), y(ST::ArrowY), SL(ST::DownX, ST::ArrowW), SL(ST::ArrowY, ST::ArrowH), false);
		m_up.onPress([this] { pagePressed(-1); });
		m_down.onPress([this] { pagePressed(1); });

		Settings::AddListener([this] { updateLooks(); });
		State::AddOffsetsListener([this] { updateJogRows(); }); // axis count / letters
		m_timer = lv_timer_create(
			[](lv_timer_t* t) { static_cast<SettingsPage*>(lv_timer_get_user_data(t))->tick(); }, 100, this);
		showPage();
		updateLooks();
	}

	// ---- Look ----------------------------------------------------------------------------------

	void SettingsPage::showPage()
	{
		for (unsigned int p = 0; p < NumPages; ++p)
		{
			showObj(m_pageRoot[p], p == m_page);
		}
		lv_label_set_text(m_section, SectionNames[m_page]);
		lv_label_set_text(m_pageLabel, fmt::format("PAGE {:d} / {:d}", m_page + 1, NumPages).c_str());
		m_up.setDisabled(m_page == 0);
		m_down.setDisabled(m_page + 1 >= NumPages);
	}

	void SettingsPage::updateLooks()
	{
		const Settings::Values& v = Settings::Get();
		m_panelTiles[PDimming].setToggle(Panel::DimTimeout() != 0);
		m_machineTiles[MRememberTool].setToggle(v.rememberTool);
		m_machineTiles[MToolSetter].setToggle(v.toolSetter);
		m_degree.setToggle(v.jogDegree);
		updateJogRows();
	}

	void SettingsPage::updateJogRows()
	{
		const size_t n = std::min<size_t>(State::AxisCount(), MaxJogRows);
		m_jogRows = (unsigned int)n;
		for (unsigned int r = 0; r < MaxJogRows; ++r)
		{
			JogRow& row = m_jog[r];
			const bool vis = r < n;
			showObj(row.letter, vis);
			showObj(row.pair, vis);
			showObj(row.plus, vis);
			if (vis)
			{
				row.axis = State::AxisLetter(r);
				const char t[2] = {row.axis, 0};
				lv_label_set_text(row.letterLabel, t);
				lv_obj_invalidate(row.pair);
				lv_obj_invalidate(row.plus);
			}
		}
	}

	// Icon pair tile: both icons, the name, the dropdown triangle
	void SettingsPage::drawPair(lv_event_t* e)
	{
		const auto* row = static_cast<const JogRow*>(lv_event_get_user_data(e));
		lv_layer_t* layer = lv_event_get_layer(e);
		lv_area_t a;
		lv_obj_get_coords(row->pair, &a);
		const int32_t cy = (a.y1 + a.y2) / 2;
		const Settings::AxisIcons icons = Settings::IconsFor(row->axis);
		const Settings::MoveLook look = Settings::MoveLookFor(row->axis);
		DrawJogIcon(layer, a.x1 + S(28), cy, 30, look.left, Colour::Text());
		DrawJogIcon(layer, a.x1 + S(64), cy, 30, look.right, Colour::Text());
		const lv_area_t t = {a.x1 + S(88), a.y1, a.x2 - S(28), a.y2};
		DrawText(layer, t, PairNames[(size_t)icons.pair], Font::Small(), Colour::Text(), LV_TEXT_ALIGN_LEFT);
		lv_draw_triangle_dsc_t tri;
		lv_draw_triangle_dsc_init(&tri);
		tri.color = Colour::Muted();
		const int32_t tx = a.x2 - S(18);
		tri.p[0] = {(lv_value_precise_t)(tx - 8), (lv_value_precise_t)(cy - 4)};
		tri.p[1] = {(lv_value_precise_t)(tx + 8), (lv_value_precise_t)(cy - 4)};
		tri.p[2] = {(lv_value_precise_t)tx, (lv_value_precise_t)(cy + 6)};
		lv_draw_triangle(layer, &tri);
	}

	// + MOVES tile: "+" and the arrow + gets
	void SettingsPage::drawPlus(lv_event_t* e)
	{
		const auto* row = static_cast<const JogRow*>(lv_event_get_user_data(e));
		lv_layer_t* layer = lv_event_get_layer(e);
		lv_area_t a;
		lv_obj_get_coords(row->plus, &a);
		const Settings::MoveLook look = Settings::MoveLookFor(row->axis);
		const lv_area_t t = {a.x1 + S(14), a.y1, a.x1 + S(50), a.y2};
		DrawText(layer, t, "+", Font::Large(), Colour::Text(), LV_TEXT_ALIGN_LEFT);
		DrawJogIcon(
			layer, a.x2 - S(36), (a.y1 + a.y2) / 2, 30, (look.leftSign > 0) ? look.left : look.right, Colour::Text());
	}

	// ---- Touches -------------------------------------------------------------------------------

	void SettingsPage::pagePressed(int direction)
	{
		if (direction < 0 && m_page > 0)
		{
			--m_page;
		}
		else if (direction > 0 && m_page + 1 < NumPages)
		{
			++m_page;
		}
		showPage();
	}

	void SettingsPage::factoryResetPressed()
	{
		if (Settings::Writing())
		{
			Popup::Refuse("The machine settings are being saved.\nTry again in a few seconds.");
			return;
		}
		Popup::Choose("FACTORY RESET",
					  {"SCREEN", "MACHINE", "BOTH"},
					  2,
					  [](size_t choice)
					  {
						  const bool screen = (choice != 1), machine = (choice != 0);
						  std::vector<std::string> lines = {"FACTORY RESET will return"};
						  lines.push_back(screen && machine ? "the screen and machine settings"
										  : screen			? "the screen settings"
															: "the machine settings");
						  lines.push_back("to the default values. Continue?");
						  Popup::Confirm("ALERT !",
										 lines,
										 [screen, machine]
										 {
											 if (machine)
											 {
												 Settings::FactoryReset();
											 }
											 if (screen)
											 {
												 Panel::SetBrightness(100);
												 Panel::SetInfoTimeout(6);
												 Panel::SetDimTimeout(0);
												 Panel::SetSoundSetting(1);
												 if (Panel::RotationFlipped())
												 {
													 Panel::SetRotationFlipped(false);
												 }
												 if (Panel::Accent() != 0)
												 {
													 Panel::SetAccent(0); // restarts the UI
												 }
											 }
										 });
					  });
	}

	// ---- Page 1: the screen ----

	void SettingsPage::panelTilePressed(unsigned int item)
	{
		switch (item)
		{
		case PAccent:
		{
			if (Settings::Writing())
			{
				Popup::Refuse("The machine settings are being saved.\nTry again in a few seconds.");
				return;
			}
			std::vector<lv_color_t> colours;
			for (unsigned int i = 0; i < Panel::NumAccents; ++i)
			{
				colours.push_back(Colour::Accent(i));
			}
			Popup::ChooseColour("ACCENT COLOR",
								colours,
								Panel::Accent(),
								[](size_t choice)
								{
									if (choice != Panel::Accent())
									{
										Panel::SetAccent((unsigned int)choice); // the UI restarts with it
									}
								});
			break;
		}
		case PBrightness:
		{
			std::vector<std::string> labels;
			for (unsigned int v : BrightnessValues)
			{
				labels.push_back(fmt::format("{:d}%", v));
			}
			Popup::Choose(
				"BRIGHTNESS",
				labels,
				nearest(BrightnessValues, Panel::Brightness()),
				[](size_t c) { Panel::SetBrightness(BrightnessValues[c]); },
				3);
			break;
		}
		case PInfoTimeout:
			Popup::Choose("INFO TIMEOUT",
						  {"0s", "3s", "6s", "10s"},
						  nearest(InfoTimeoutValues, Panel::InfoTimeout()),
						  [](size_t c) { Panel::SetInfoTimeout(InfoTimeoutValues[c]); });
			break;
		case PDimming:
			Popup::Choose("SCREEN DIMMING",
						  {"OFF", "1 MIN", "5 MIN", "10 MIN"},
						  nearest(DimValues, Panel::DimTimeout()),
						  [this](size_t c)
						  {
							  Panel::SetDimTimeout(DimValues[c]);
							  updateLooks();
						  });
			break;
		case PRotation:
			Popup::Choose("SCREEN ROTATION",
						  {"NORMAL", "UPSIDE DOWN"},
						  Panel::RotationFlipped() ? 1 : 0,
						  [](size_t c) { Panel::SetRotationFlipped(c == 1); });
			break;
		case PSound:
			Popup::Choose("SOUND",
						  {"OFF", "BUZZER", "SPEAKER 1", "SPEAKER 2", "SPEAKER 3", "SPEAKER 4", "SPEAKER 5"},
						  Panel::SoundSetting(),
						  [](size_t c) { Panel::SetSoundSetting((unsigned int)c); });
			break;
		case PConnection:
			chooseConnection();
			break;
		case PSplitOps:
			// PREVIEW: operations from names (or guessed for the same tool), or only at tool changes
			Popup::Choose("SPLIT OPERATIONS",
						  {"AUTO", "TOOL CHANGES"},
						  Panel::PreviewToolChangesOnly() ? 1 : 0,
						  [](size_t c)
						  {
							  if ((c == 1) != Panel::PreviewToolChangesOnly())
							  {
								  Panel::SetPreviewToolChangesOnly(c == 1);
								  PreviewSource::Reload(); // the job read again with it
							  }
						  });
			break;
		case PAbout:
		{
			const char* const links[] = {"UART", "USB", "NETWORK"};
			Popup::Info("ABOUT",
						fmt::format("DuetScreen {:s}\nCNC UI (PanelDue CNC port)\nConnection: {:s}",
									FIRMWARE_VERSION,
									links[(int)Panel::Connection()]));
			break;
		}
		default:
			break;
		}
	}

	void SettingsPage::chooseConnection()
	{
		if (jobActive())
		{
			return; // changing the link would cut the job's connection
		}
		Popup::Choose("CONNECTION",
					  {"UART", "USB", "NETWORK"},
					  (size_t)Panel::Connection(),
					  [this](size_t c)
					  {
						  const auto link = static_cast<Panel::Link>(c);
						  if (link != Panel::Link::Network)
						  {
							  Panel::SetConnection(link);
							  return;
						  }
						  // NETWORK: the Duet's address
						  Keyboard::Client client;
						  client.enter = [](const std::string& ip)
						  {
							  if (!ip.empty())
							  {
								  Panel::SetConnection(Panel::Link::Network, ip);
							  }
						  };
						  client.cancel = [this] { m_kbOpen = false; };
						  client.gone = [this] { m_kbOpen = false; };
						  m_kbOpen = true;
						  Keyboard::Open(Panel::DuetIp(), client, KeyboardOwner);
					  });
	}

	// ---- Page 2: the machine ----

	void SettingsPage::machineTilePressed(unsigned int item)
	{
		using Settings::Bit;
		using Settings::Global;
		const Settings::Values& v = Settings::Get();
		switch (item)
		{
		case MProbeMode:
			Popup::Choose(
				"PROBE MODE",
				{"AUTO", "SEMI-AUTO", "MANUAL"},
				v.probeMode,
				[](size_t c)
				{ Settings::Edit([c](Settings::Values& x) { x.probeMode = (uint8_t)c; }, Bit(Global::ProbeMode)); });
			break;
		case MToolHolder:
			Popup::Choose("TOOL HOLDER",
						  {"ER COLLET", "TOOL HOLDER"},
						  v.measureAfterChange ? 0 : 1,
						  [](size_t c)
						  {
							  // ER collet: the length changes with every tool
							  Settings::Edit([c](Settings::Values& x) { x.measureAfterChange = (c == 0); },
											 Bit(Global::MeasureAfterChange));
						  });
			break;
		case MRememberTool:
			Settings::Edit([](Settings::Values& x) { x.rememberTool = !x.rememberTool; }, Bit(Global::RememberTool));
			break;
		case MToolSetter:
			Settings::Edit([](Settings::Values& x) { x.toolSetter = !x.toolSetter; }, Bit(Global::ToolSetter));
			break;
		case MJogFeed:
			Settings::ChooseJogFeed(nullptr);
			break;
		case MAux:
			Popup::Choose(
				"COOL / VAC",
				{"COOLANT", "VACUUM"},
				v.auxVacuum ? 1 : 0,
				[](size_t c)
				{ Settings::Edit([c](Settings::Values& x) { x.auxVacuum = (c == 1); }, Bit(Global::AuxVacuum)); });
			break;
		case MCustom1:
		case MCustom2:
		case MCustom3:
			customPressed(item - MCustom1);
			break;
		case MIdleTimeout:
			Popup::Choose("IDLE TIMEOUT",
						  {"OFF", "5 s", "10 s", "30 s", "60 s"},
						  nearest(IdleValues, v.idleTimeout),
						  [](size_t c)
						  {
							  Settings::Edit([c](Settings::Values& x) { x.idleTimeout = IdleValues[c]; },
											 Bit(Global::IdleTimeout));
						  });
			break;
		case MJogPresets:
			choosePreset();
			break;
		case MOverrides:
			chooseOverride();
			break;
		default:
			break;
		}
	}

	// JOG PRESETS: pick one of the 6, then its value; they stay sorted
	void SettingsPage::choosePreset()
	{
		std::vector<std::string> labels;
		for (unsigned int p : Settings::Get().jogPresets)
		{
			labels.push_back(fmt::format("{:d} mm/min", p));
		}
		Popup::Choose(
			"JOG PRESETS",
			labels,
			0,
			[](size_t c)
			{
				Popup::NumpadSpec pad;
				pad.tag = fmt::format("PRESET {:d}", c + 1);
				pad.unit = "mm/min";
				pad.value = (float)Settings::Get().jogPresets[c];
				pad.min = 1.0f;
				pad.max = (float)Settings::MaxJogFeed;
				pad.onOk = [c](float value)
				{
					Settings::Edit(
						[c, value](Settings::Values& x)
						{
							x.jogPresets[c] = (unsigned int)(value + 0.5f);
							std::sort(x.jogPresets.begin(), x.jogPresets.end());
						},
						Settings::Bit(Settings::Global::JogPresets));
				};
				Popup::Numpad(pad);
			},
			2);
	}

	// OVERRIDES: the limits of the JOB STATUS overrides
	void SettingsPage::chooseOverride()
	{
		const Settings::Values& v = Settings::Get();
		Popup::Choose(
			"OVERRIDES",
			{fmt::format("FEED MIN {:d}%", v.feedMin),
			 fmt::format("FEED MAX {:d}%", v.feedMax),
			 fmt::format("SPINDLE MIN {:d}%", v.spindleMin),
			 fmt::format("SPINDLE MAX {:d}%", v.spindleMax)},
			0,
			[](size_t c)
			{
				static const char* const Tags[] = {"FEED MIN", "FEED MAX", "SPINDLE MIN", "SPINDLE MAX"};
				static const float Min[] = {1, 100, 1, 100};
				static const float Max[] = {100, 500, 100, 300};
				const Settings::Values& cur = Settings::Get();
				const int values[] = {cur.feedMin, cur.feedMax, cur.spindleMin, cur.spindleMax};
				Popup::NumpadSpec pad;
				pad.tag = Tags[c];
				pad.unit = "%";
				pad.value = (float)values[c];
				pad.min = Min[c];
				pad.max = Max[c];
				pad.onOk = [c](float value)
				{
					const int p = (int)(value + 0.5f);
					Settings::Edit(
						[c, p](Settings::Values& x)
						{
							int* fields[] = {&x.feedMin, &x.feedMax, &x.spindleMin, &x.spindleMax};
							*fields[c] = p;
						},
						Settings::Bit(Settings::Global::Overrides));
				};
				Popup::Numpad(pad);
			},
			2);
	}

	// CUSTOM n: MACRO (picked on MACROS) / LABEL (keyboard) / CLEAR; LABEL and CLEAR need a macro
	void SettingsPage::customPressed(size_t slot)
	{
		const bool hasMacro = !Settings::Get().macros[slot].empty();
		Popup::Choose(
			CustomTitles[slot],
			{"MACRO", "LABEL", "CLEAR"},
			0,
			[this, slot](size_t c)
			{
				if (c == 0)
				{
					if (!Settings::Known() || State::Status() == OM::PrinterStatus::connecting)
					{
						Settings::Edit([](Settings::Values&) {}, 0); // says why it cannot
						return;
					}
					if (m_pickMacro)
					{
						m_pickMacro(
							[slot](const std::string& path, const std::string& name)
							{
								if (path.size() > Settings::MaxMacroPath)
								{
									Popup::Refuse("The macro path is too long.");
									return;
								}
								Settings::Edit(
									[slot, path, name](Settings::Values& x)
									{
										const bool wasUsed = !x.macros[slot].empty();
										x.macros[slot] = path;
										if (!wasUsed || x.labels[slot].empty())
										{
											x.labels[slot] = defaultLabel(slot, name); // named after its macro
										}
									},
									Settings::SlotBits(slot));
							});
					}
				}
				else if (c == 1)
				{
					openLabelKeyboard(slot);
				}
				else
				{
					Settings::Edit(
						[slot](Settings::Values& x)
						{
							x.labels[slot].clear();
							x.macros[slot].clear();
						},
						Settings::SlotBits(slot));
				}
			},
			3,
			[hasMacro](size_t c) { return c == 0 || hasMacro; });
	}

	void SettingsPage::openLabelKeyboard(size_t slot)
	{
		Keyboard::Client client;
		client.changed = [](const std::string& text)
		{
			// Only the characters a label may have, at most 12
			std::string l;
			for (char ch : text)
			{
				if (l.size() < Settings::MaxLabel && labelCharOk(ch))
				{
					l += ch;
				}
			}
			if (l != text)
			{
				Keyboard::SetText(l);
			}
		};
		client.enter = [this, slot](const std::string& text)
		{
			m_kbOpen = false;
			std::string l = text;
			l.erase(0, l.find_first_not_of(' '));
			while (!l.empty() && l.back() == ' ')
			{
				l.pop_back();
			}
			if (l.empty())
			{
				// Empty: the macro's name again
				l = defaultLabel(slot,
								 MacrosPage::DisplayName(Settings::Get().macros[slot].substr(
									 Settings::Get().macros[slot].find_last_of("/:") + 1)));
			}
			Settings::Edit([slot, l](Settings::Values& x) { x.labels[slot] = l; }, Settings::SlotBits(slot));
		};
		client.cancel = [this] { m_kbOpen = false; };
		client.gone = [this] { m_kbOpen = false; };
		m_kbOpen = true;
		Keyboard::Open(Settings::Get().labels[slot], client, KeyboardOwner);
	}

	// ---- Page 3: JOG DIRECTION ----

	void SettingsPage::jogPairPressed(unsigned int row)
	{
		if (row >= m_jogRows)
		{
			return;
		}
		const char axis = m_jog[row].axis;
		const Settings::AxisIcons cur = Settings::IconsFor(axis);
		// Order of the choices: LEFT/RIGHT, DOWN/UP, 225/45, CCW/CW, NONE
		static const Settings::IconPair Order[] = {Settings::IconPair::LeftRight,
												   Settings::IconPair::DownUp,
												   Settings::IconPair::Diagonal,
												   Settings::IconPair::Rotary,
												   Settings::IconPair::None};
		size_t sel = 0;
		std::vector<std::string> labels;
		for (size_t i = 0; i < std::size(Order); ++i)
		{
			labels.push_back(PairNames[(size_t)Order[i]]);
			if (Order[i] == cur.pair)
			{
				sel = i;
			}
		}
		Popup::Choose(
			fmt::format("AXIS {:c} ICONS", axis),
			labels,
			sel,
			[axis](size_t c)
			{
				Settings::Edit(
					[axis, c](Settings::Values& x)
					{
						const Settings::AxisIcons now = Settings::IconsFor(axis);
						auto it = std::find_if(x.jogIcons.begin(),
											   x.jogIcons.end(),
											   [axis](const Settings::AxisIcons& a) { return a.letter == axis; });
						const Settings::AxisIcons set{axis, Order[c], now.plusRight};
						if (it != x.jogIcons.end())
						{
							*it = set;
						}
						else
						{
							x.jogIcons.push_back(set);
						}
					},
					Settings::Bit(Settings::Global::JogIcons));
			},
			2);
	}

	void SettingsPage::jogPlusPressed(unsigned int row)
	{
		if (row >= m_jogRows)
		{
			return;
		}
		const char axis = m_jog[row].axis;
		Settings::Edit(
			[axis](Settings::Values& x)
			{
				const Settings::AxisIcons now = Settings::IconsFor(axis);
				auto it = std::find_if(x.jogIcons.begin(),
									   x.jogIcons.end(),
									   [axis](const Settings::AxisIcons& a) { return a.letter == axis; });
				const Settings::AxisIcons set{axis, now.pair, !now.plusRight};
				if (it != x.jogIcons.end())
				{
					*it = set;
				}
				else
				{
					x.jogIcons.push_back(set);
				}
			},
			Settings::Bit(Settings::Global::JogIcons));
	}

	void SettingsPage::jogDegreePressed()
	{
		Settings::Edit([](Settings::Values& x) { x.jogDegree = !x.jogDegree; },
					   Settings::Bit(Settings::Global::JogDegree));
	}

	// ---- Machine data ----

	void SettingsPage::setShown(bool shown)
	{
		m_shown = shown;
		if (!shown && m_kbOpen && Keyboard::IsOpenFor(KeyboardOwner))
		{
			Keyboard::Close(); // label / address not changed
			m_kbOpen = false;
		}
	}

	void SettingsPage::setIp(const std::string& ip)
	{
		m_ip = ip.empty() ? std::string("-") : ip;
		lv_label_set_text(m_ipLabel, m_ip.c_str());
	}

	void SettingsPage::tick()
	{
		Settings::Tick(); // the file writer, every 100 ms
	}

	// ---- For tests ----

	std::string SettingsPage::tileText(unsigned int page, unsigned int item) const
	{
		if (page == 0 && item < NumPanelTiles)
		{
			return m_panelTiles[item].text();
		}
		if (page == 1 && item < NumMachineTiles)
		{
			return m_machineTiles[item].text();
		}
		return std::string();
	}

	bool SettingsPage::tileToggled(unsigned int page, unsigned int item) const
	{
		const TextButton* b = (page == 0 && item < NumPanelTiles)	  ? &m_panelTiles[item]
							  : (page == 1 && item < NumMachineTiles) ? &m_machineTiles[item]
																	  : nullptr;
		return b != nullptr && lv_color_eq(lv_obj_get_style_border_color(b->obj(), LV_PART_MAIN), Colour::Accent());
	}

	std::string SettingsPage::sectionText() const
	{
		return lv_label_get_text(m_section);
	}

	std::string SettingsPage::jogRowText(unsigned int row) const
	{
		if (row >= m_jogRows)
		{
			return std::string();
		}
		const char axis = m_jog[row].axis;
		const Settings::AxisIcons a = Settings::IconsFor(axis);
		return fmt::format("{:c} {:s} +{:s}", axis, PairNames[(size_t)a.pair], a.plusRight ? "R" : "L");
	}

	bool SettingsPage::degreeOn() const
	{
		return Settings::Get().jogDegree;
	}
} // namespace UI::Cnc
