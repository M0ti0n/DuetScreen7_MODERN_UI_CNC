/*
 * CncControlPage.cpp
 *
 * The logic is the PanelDue CncControlPage.cpp, line for line where it can be; positions are
 * CncLayout::Control (PanelDue units) through S() / SL().
 */

#include "CncControlPage.h"
#include "CncLayout.h"
#include "CncPopup.h"
#include "CncSettings.h"
#include "CncState.h"
#include "CncTheme.h"
#include <fmt/format.h>
#include <iterator>
#include <vector>

namespace UI::Cnc
{
	using namespace CncLayout;
	using namespace CncLayout::Control;

	namespace
	{
		const char AxisLetters[ControlPage::MaxAxes] = {'X', 'Y', 'Z', 'A'};
		const char* const AxisNames[ControlPage::MaxAxes] = {"X", "Y", "Z", "A"};
		const float StepValues[ControlPage::NumSteps] = {0.01f, 0.1f, 1.0f, 10.0f, 100.0f};
		const char* const StepNames[ControlPage::NumSteps] = {"0.01", "0.1", "1", "10", "100"};
		constexpr size_t FirstGuardedStep = 3; // 10 and 100: no queued jogs

		// Home column order: ALL, Z, X, Y, A. Value = axis index, -1 = all
		const int HomeAxis[ControlPage::NumHome] = {-1, 2, 0, 1, 3};
		const char* const HomeNames[ControlPage::NumHome] = {"ALL", "Z", "X", "Y", "A"};

		const char* const LockedJob = "Locked while a job is running.";
	} // namespace

	// ---- Building ------------------------------------------------------------------------------

	void ControlPage::create(lv_obj_t* parent, int32_t parentTop)
	{
		m_root = CreateBare(parent, 0, 0, ScreenW, 1);
		lv_obj_set_size(m_root, LV_PCT(100), LV_PCT(100)); // the parent is not laid out yet
		m_top = parentTop;
		auto y = [this](int32_t pdY) { return S(pdY) - m_top; };
		const int32_t smallH = lv_font_get_line_height(Font::Small());

		// Labels (muted, 2 px right of the block)
		auto label = [&](int32_t pdY, int32_t pdX, int32_t pdW, const char* text, lv_text_align_t align)
		{
			return CreateLabel(
				m_root, S(pdX), y(pdY), SL(pdX, pdW), smallH, Font::Small(), Colour::Muted(), align, text);
		};
		label(LabelAxisY, LeftX + 2, 250, "AXIS", LV_TEXT_ALIGN_LEFT);
		label(LabelAxisY, HomeX, HomeW, "HOME", LV_TEXT_ALIGN_CENTER);
		label(LabelStepY, LeftX + 2, 250, "STEP DISTANCE", LV_TEXT_ALIGN_LEFT);
		label(LabelMoveY, LeftX + 2, 120, "MOVE", LV_TEXT_ALIGN_LEFT);
		label(LabelActY, LeftX + 2, 120, "ACTION", LV_TEXT_ALIGN_LEFT);

		auto button = [&](TextButton& b,
						  int32_t pdY,
						  int32_t pdX,
						  int32_t pdW,
						  int32_t pdH,
						  const char* text,
						  const lv_font_t* font)
		{ b.create(m_root, S(pdX), y(pdY), SL(pdX, pdW), SL(pdY, pdH), text, font); };

		// AXIS row (positions set by layoutAxisRow)
		for (size_t i = 0; i < MaxAxes; ++i)
		{
			button(m_axis[i], RowAxisY, LeftX, 100, RowH, AxisNames[i], Font::Large());
			m_axis[i].onPress([this, i] { axisPressed(i); });
		}

		// STEP DISTANCE row
		{
			const int32_t w = (LeftW - (int32_t)(NumSteps - 1) * ColGap) / (int32_t)NumSteps;
			for (size_t i = 0; i < NumSteps; ++i)
			{
				button(m_step[i], RowStepY, LeftX + (int32_t)i * (w + ColGap), w, RowH, StepNames[i], Font::Small());
				m_step[i].onPress([this, i] { stepPressed(i); });
			}
		}

		// MOVE row
		{
			const int32_t w = (LeftW - ColGap) / 2;
			button(m_minus, RowMoveY, LeftX, w, RowH, "-", Font::Large());
			button(m_plus, RowMoveY, LeftX + w + ColGap, w, RowH, "+", Font::Large());
			// The left button has the left / down / 225 / CCW arrow; its sign follows JOG DIRECTION
			m_minus.onPress([this] { movePressed(m_leftSign); });
			m_plus.onPress([this] { movePressed(-m_leftSign); });
		}

		// ACTION grid 3 x 2: RAPID / SLOW is one tall toggle over both rows of the first column
		{
			const int32_t w = (LeftW - 2 * ColGap) / 3;
			m_feedMode.create(
				m_root, S(LeftX), y(RowAct1Y), SL(LeftX, w), SL(RowAct1Y, 2 * RowH + ActionGap), "RAPID", "SLOW");
			m_feedMode.onPress([this] { feedModePressed(); });
			button(m_safeZ, RowAct1Y, LeftX + w + ColGap, w, RowH, "SAFE Z", Font::Small());
			button(m_xy0, RowAct1Y, LeftX + 2 * (w + ColGap), w, RowH, "XY0", Font::Small());
			button(m_aux, RowAct2Y, LeftX + w + ColGap, w, RowH, State::AuxLabel(), Font::Small());
			button(m_custom, RowAct2Y, LeftX + 2 * (w + ColGap), w, RowH, "", Font::Small());
			m_safeZ.onPress([this] { safeZPressed(); });
			m_xy0.onPress([this] { xy0Pressed(); });
			m_aux.onPress([this] { auxPressed(); });
			m_custom.onPress([this] { customPressed(); });
			m_aux.setPressedLook(false); // a toggle: its look is on / off
		}

		// Vertical accent line between the left block and the HOME column
		lv_obj_t* sep =
			CreateBare(m_root, S(SepX), y(LabelAxisY), SL(SepX, SeparatorW), SL(LabelAxisY, Bottom - LabelAxisY));
		lv_obj_set_style_bg_color(sep, Colour::Accent(), 0);
		lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

		// HOME column
		for (size_t i = 0; i < NumHome; ++i)
		{
			button(m_home[i],
				   RowAxisY + (int32_t)i * HomePitch,
				   HomeX,
				   HomeW,
				   RowH,
				   HomeNames[i],
				   (i == 0) ? Font::Small() : Font::Large());
			m_home[i].setPressedLook(false); // it opens the confirmation
			m_home[i].onPress([this, i] { homePressed(i); });
		}

		// Shared state
		State::AddCustomListener([this] { updateCustom(); });
		State::AddAuxListener([this] { updateAux(); });
		State::AddHomedListener([this] { updateHomeColours(); });
		Settings::AddListener([this] { updateMoveLabels(); }); // JOG DIRECTION changed
		State::AddJobListener(
			[this]
			{
				const bool active = State::JobActive();
				const bool running = State::MachineBusy();
				if (running != m_lockAll || active != m_lockHome)
				{
					m_lockAll = running;
					m_lockHome = active;
					updateLockedLook();
				}
			});

		// Initial state: no axis chosen, step 0.1, RAPID
		m_step[DefaultStep].setSelected(true);
		layoutAxisRow();
		updateMoveLabels();
		updateCustom();
		updateAux();
		updateHomeColours();

		m_timer = lv_timer_create(
			[](lv_timer_t* t) { static_cast<ControlPage*>(lv_timer_get_user_data(t))->tick(); }, 100, this);
	}

	ControlPage::~ControlPage()
	{
		if (m_timer != nullptr)
		{
			lv_timer_delete(m_timer);
		}
	}

	void ControlPage::show(bool visible)
	{
		if (visible)
		{
			lv_obj_remove_flag(m_root, LV_OBJ_FLAG_HIDDEN);
		}
		else
		{
			lv_obj_add_flag(m_root, LV_OBJ_FLAG_HIDDEN);
		}
	}

	// X, Y, Z get equal widths; with a visible A the row is split in four
	void ControlPage::layoutAxisRow()
	{
		const int32_t n = (m_visibleAxes > 3) ? 4 : 3;
		const int32_t w = (LeftW - (n - 1) * ColGap) / n;
		for (size_t i = 0; i < MaxAxes; ++i)
		{
			if ((int32_t)i < n)
			{
				const int32_t x = LeftX + (int32_t)i * (w + ColGap);
				m_axis[i].setGeometry(S(x), SL(x, w));
			}
			m_axis[i].show((int32_t)i < n);
		}
		m_home[4].show(m_visibleAxes > 3);
		if (m_axisChosen && m_currentAxis >= (size_t)n)
		{
			clearAxisSelection(); // the chosen axis is not on the page any more
		}
	}

	void ControlPage::updateMoveLabels()
	{
		// Arrows and "°" for the chosen axis (SETTINGS > JOG DIRECTION); none while no axis is chosen
		const Settings::MoveLook look =
			m_axisChosen ? Settings::MoveLookFor(AxisLetters[m_currentAxis]) : Settings::MoveLook();
		m_leftSign = look.leftSign;
		m_minusText = Settings::MoveText(m_leftSign, StepNames[m_currentStep], look.degree);
		m_plusText = Settings::MoveText(-m_leftSign, StepNames[m_currentStep], look.degree);
		m_minus.setText(m_minusText.c_str());
		m_plus.setText(m_plusText.c_str());
		m_minus.setIcon(look.left, false);
		m_plus.setIcon(look.right, true);
	}

	void ControlPage::updateHomeColours()
	{
		bool allHomed = true;
		for (size_t i = 0; i < NumHome; ++i)
		{
			const int axis = HomeAxis[i];
			if (axis < 0)
			{
				continue;
			}
			const bool homed = State::Homed((size_t)axis);
			if ((size_t)axis < m_visibleAxes && !homed)
			{
				allHomed = false;
			}
			m_home[i].setColours(m_lockHome ? Colour::Border() : Colour::Text(),
								 homed ? Colour::Tile() : Colour::NotHomed());
		}
		m_home[0].setColours(m_lockHome ? Colour::Border() : Colour::Text(),
							 allHomed ? Colour::Tile() : Colour::NotHomed());
	}

	void ControlPage::updateLockedLook()
	{
		for (TextButton& b : m_axis)
		{
			b.setLocked(m_lockAll);
		}
		for (TextButton& b : m_step)
		{
			b.setLocked(m_lockAll);
		}
		m_custom.setLocked(m_lockAll);
		m_minus.setLocked(m_lockAll);
		m_plus.setLocked(m_lockAll);
		m_safeZ.setLocked(m_lockAll);
		m_xy0.setLocked(m_lockAll);
		updateAux();
		m_feedMode.setLocked(m_lockAll);
		updateHomeColours();
	}

	void ControlPage::updateCustom()
	{
		m_custom.setText(State::CustomLabel(0));
		m_custom.show(State::CustomUsed(0));
	}

	void ControlPage::updateAux()
	{
		m_aux.setText(State::AuxLabel());
		m_aux.setToggle(!m_lockAll && State::AuxOn()); // locked look replaces the on / off look during a job
		m_aux.setLocked(m_lockAll);
	}

	// ---- Selection -----------------------------------------------------------------------------

	void ControlPage::selectAxis(size_t axis)
	{
		for (size_t i = 0; i < MaxAxes; ++i)
		{
			m_axis[i].setSelected(i == axis);
		}
	}

	void ControlPage::selectStep(size_t step)
	{
		for (size_t i = 0; i < NumSteps; ++i)
		{
			m_step[i].setSelected(i == step);
		}
	}

	void ControlPage::clearAxisSelection()
	{
		for (TextButton& b : m_axis)
		{
			b.setSelected(false);
		}
		m_axisChosen = false;
		updateMoveLabels(); // no arrows without an axis
	}

	// Idle timeout: no axis, default step
	void ControlPage::resetSelection()
	{
		clearAxisSelection();
		if (m_currentStep != DefaultStep)
		{
			m_currentStep = DefaultStep;
			selectStep(DefaultStep);
			updateMoveLabels();
		}
	}

	void ControlPage::tick()
	{
		const uint32_t timeout = Settings::Get().idleTimeout * 1000u;
		if (timeout != 0 && (m_axisChosen || m_currentStep != DefaultStep) && State::Now() - m_lastActivity >= timeout)
		{
			resetSelection();
		}
	}

	// ---- Jogging -------------------------------------------------------------------------------

	// Large steps: refuse while the previous large jog may still be moving
	bool ControlPage::jogAllowed()
	{
		if (m_currentStep < FirstGuardedStep)
		{
			return true; // small steps may queue, they are short
		}
		return State::GuardedJogAllowed(m_lastGuardedJog);
	}

	void ControlPage::jog(int direction)
	{
		const char axis = AxisLetters[m_currentAxis];
		const float distance = StepValues[m_currentStep] * (float)direction;
		if (m_feedMode.secondActive())
		{
			State::Send(fmt::format("G91\nG1 {:c}{:.3f} F{:d}\nG90\n", axis, distance, State::JogFeed()));
		}
		else
		{
			State::Send(fmt::format("G91\nG0 {:c}{:.3f}\nG90\n", axis, distance));
		}
	}

	// ---- Touches -------------------------------------------------------------------------------

	bool ControlPage::refuseLocked(bool isHome)
	{
		if (m_lockAll || (isHome && m_lockHome))
		{
			Popup::Refuse(m_lockAll ? "Locked while a job is running.\nPause the job to use CONTROL."
									: "HOME is locked until the job ends.");
			return true;
		}
		return false;
	}

	void ControlPage::axisPressed(size_t axis)
	{
		if (refuseLocked(false))
		{
			return;
		}
		m_currentAxis = axis;
		m_axisChosen = true;
		m_lastActivity = State::Now();
		selectAxis(axis);
		updateMoveLabels(); // the axis's arrows
	}

	void ControlPage::stepPressed(size_t step)
	{
		if (refuseLocked(false))
		{
			return;
		}
		m_currentStep = step;
		m_lastActivity = State::Now();
		selectStep(step);
		updateMoveLabels();
	}

	void ControlPage::feedModePressed()
	{
		if (refuseLocked(false))
		{
			return;
		}
		m_feedMode.setSecondActive(!m_feedMode.secondActive());
	}

	void ControlPage::movePressed(int direction)
	{
		if (refuseLocked(false))
		{
			return;
		}
		if (State::MachineBusy())
		{
			Popup::Refuse(LockedJob);
			return;
		}
		if (!m_axisChosen)
		{
			Popup::Refuse("Select an axis first.");
			return;
		}
		if (!jogAllowed())
		{
			Popup::Refuse("Wait until the last move has finished.");
			return;
		}
		m_lastActivity = State::Now();
		jog(direction);
	}

	void ControlPage::safeZPressed()
	{
		if (refuseLocked(false))
		{
			return;
		}
		if (State::MachineBusy())
		{
			Popup::Refuse(LockedJob);
			return;
		}
		if (!State::XyzHomed())
		{
			Popup::Refuse("Home X, Y and Z first.");
			return;
		}
		State::Send("M98 P\"0:/macros/safe_z.g\"\n");
	}

	void ControlPage::xy0Pressed()
	{
		if (refuseLocked(false))
		{
			return;
		}
		if (State::MachineBusy())
		{
			Popup::Refuse(LockedJob);
			return;
		}
		if (!State::XyzHomed())
		{
			Popup::Refuse("Home X, Y and Z first.");
			return;
		}
		State::Send("M98 P\"0:/macros/goto_xy0.g\"\n");
	}

	void ControlPage::askHome(int axis)
	{
		const std::string title = (axis < 0) ? "HOME ALL" : fmt::format("HOME {:c}", AxisLetters[axis]);
		const std::string line = (axis < 0) ? "Home all axes?" : fmt::format("Home {:c} axis?", AxisLetters[axis]);
		Popup::Confirm(title,
					   {line, "Tool clear of stock and clamps?"},
					   [axis]
					   {
						   if (State::JobActive())
						   {
							   Popup::Refuse("HOME is locked until the job ends."); // a job started meanwhile
							   return;
						   }
						   State::Send((axis < 0) ? std::string("G28\n")
												  : fmt::format("G28 {:c}\n", AxisLetters[axis]));
					   });
	}

	void ControlPage::homePressed(size_t index)
	{
		if (refuseLocked(true))
		{
			return;
		}
		if (State::MachineBusy())
		{
			Popup::Refuse(LockedJob);
			return;
		}
		askHome(HomeAxis[index]);
	}

	void ControlPage::customPressed()
	{
		if (refuseLocked(false))
		{
			return;
		}
		if (State::MachineBusy())
		{
			Popup::Refuse(LockedJob);
			return;
		}
		State::RunCustomMacro(0); // '!' macro: confirmation first
	}

	void ControlPage::auxPressed()
	{
		if (refuseLocked(false))
		{
			return;
		}
		State::ToggleAux(); // the listener updates the look here and on JOB
	}

	// ---- Machine state -------------------------------------------------------------------------

	void ControlPage::setAxisCount(size_t visibleAxes)
	{
		const size_t n = (visibleAxes > 3) ? 4 : 3;
		if (n != m_visibleAxes)
		{
			m_visibleAxes = n;
			layoutAxisRow();
			updateHomeColours();
		}
	}

	void ControlPage::setJogFeed(unsigned int mmPerMin)
	{
		State::SetJogFeed(mmPerMin);
	}

	void ControlPage::selectSlow()
	{
		m_feedMode.setSecondActive(true); // a feed only applies to SLOW moves
	}

	// ---- DRO tiles (PanelDue CncControl: T, S and F tiles of the full DRO) ----

	namespace
	{
		constexpr int32_t UnknownMaxRpm = 100000; // upper limit while RRF has not reported the spindle max

		// fromToolTile: no speed was set yet, start the spindle once it is entered
		void openSpindleNumpadFor(bool fromToolTile)
		{
			Popup::NumpadSpec pad;
			pad.tag = "S";
			pad.unit = "rpm";
			pad.value = (float)State::SpindleRpm();
			pad.decimals = 0;
			pad.allowDecimal = false;
			pad.allowMinus = false;
			pad.min = 1.0f;
			pad.max = (float)((State::SpindleMaxRpm() > 0) ? State::SpindleMaxRpm() : UnknownMaxRpm);
			pad.onOk = [fromToolTile](float value)
			{
				if (State::MachineBusy())
				{
					Popup::Refuse("Locked while a job is running."); // a job started while the numpad was open
					return;
				}
				State::SetSpindleRpm((int32_t)(value + 0.5f)); // only a speed that is used
				if (State::SpindleRunning() || fromToolTile)
				{
					State::SendSpindleOn(); // new speed for the running spindle, or start it
				}
			};
			Popup::Numpad(pad);
		}
	} // namespace

	// T tile: spindle on (at the set speed) / standby (M5: 0 rpm, the set speed stays on the S tile)
	void ControlPage::toolTilePressed()
	{
		if (State::MachineBusy())
		{
			Popup::Refuse("Locked while a job is running.");
			return;
		}
		if (State::SpindleRunning())
		{
			State::Send("M5\n");
		}
		else if (State::SpindleRpm() > 0)
		{
			State::SendSpindleOn();
		}
		else
		{
			openSpindleNumpadFor(true); // no speed yet: ask, then start
		}
	}

	// S tile: set the spindle speed (applied at once while the spindle runs)
	void ControlPage::spindleTilePressed()
	{
		if (State::MachineBusy())
		{
			Popup::Refuse("Locked while a job is running.\nUse the spindle override on JOB.");
			return;
		}
		openSpindleNumpadFor(false);
	}

	// F tile: the SLOW JOG FEEDRATE presets (same popup as SETTINGS); a choice also selects SLOW
	void ControlPage::feedTilePressed()
	{
		Settings::ChooseJogFeed([this] { selectSlow(); }); // the presets of SETTINGS > JOG PRESETS
	}
} // namespace UI::Cnc
