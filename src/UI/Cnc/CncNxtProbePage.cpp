/*
 * CncNxtProbePage.cpp
 *
 * See CncNxtProbePage.h.
 */

#include "CncNxtProbePage.h"
#include "CncPopup.h"
#include "CncState.h"
#include "CncTheme.h"
#include "Debug.h"
#include "ObjectModel/Sensor.h"
#include "UI/Components/LVGL/LvObj.h"
#include <fmt/format.h>
#include <memory>

namespace UI::Cnc
{
	using namespace UI::CncLayout;
	using namespace UI::CncLayout::Probe;

	namespace
	{
		// The grid: 11 cycles in this order, then CALIBRATE
		const Nxt::Cycle GridCycles[NxtProbePage::NumGrid - 1] = {Nxt::Cycle::Bore,
																  Nxt::Cycle::Boss,
																  Nxt::Cycle::RectPocket,
																  Nxt::Cycle::RectBlock,
																  Nxt::Cycle::Web,
																  Nxt::Cycle::Pocket,
																  Nxt::Cycle::OutCorner,
																  Nxt::Cycle::InCorner,
																  Nxt::Cycle::Surface,
																  Nxt::Cycle::ViseCorner,
																  Nxt::Cycle::Rotation};
		constexpr size_t CalibrateIndex = NxtProbePage::NumGrid - 1;

		// PanelDue units under the full DRO (as the standard page)
		constexpr int32_t X0 = 6, W = 468, ColGap = 8, RowGap = 6;
		constexpr int32_t ColW = (W - ColGap) / 2;					// 230
		constexpr int32_t GridTop = GridY;							// 344
		constexpr int32_t GridBottom = 657;
		constexpr int32_t GridRowH = (GridBottom - GridTop - 5 * RowGap) / 6;	// 47
		constexpr int32_t HeadY = LabelY - 2, HeadH = 40;
		constexpr int32_t ParTop = HeadY + HeadH + 6;
		constexpr int32_t ParBottom = BottomY - 8;
		constexpr int32_t ParRowH = (ParBottom - ParTop - 3 * RowGap) / 4;
	}

	NxtProbePage::~NxtProbePage()
	{
		if (m_timer != nullptr)
		{
			lv_timer_delete(m_timer);
		}
	}

	void NxtProbePage::create(lv_obj_t* parent, int32_t parentTop, std::function<void(bool)> onActive)
	{
		m_onActive = std::move(onActive);
		m_root = CreateBare(parent, 0, 0, ScreenW, 1);
		lv_obj_set_size(m_root, LV_PCT(100), LV_PCT(100));
		m_top = parentTop;
		auto y = [this](int32_t pdY) { return S(pdY) - m_top; };
		const int32_t smallH = lv_font_get_line_height(Font::Small());

		// ---- Step 1: the cycles ----
		m_step1 = CreateBare(m_root, 0, 0, ScreenW, 1);
		lv_obj_set_size(m_step1, LV_PCT(100), LV_PCT(100));
		CreateLabel(m_step1, S(X0 + 2), y(LabelY), SL(X0, 300), smallH, Font::Small(), Colour::Muted(), LV_TEXT_ALIGN_LEFT, "PROBING CYCLE");
		CreateLabel(m_step1, S(X0 + W - 100), y(LabelY), SL(X0 + W - 100, 100), smallH, Font::Small(), Colour::Accent(), LV_TEXT_ALIGN_RIGHT, "NXT");
		for (size_t i = 0; i < NumGrid; ++i)
		{
			const int32_t x = X0 + (int32_t)(i % 2) * (ColW + ColGap);
			const int32_t yy = GridTop + (int32_t)(i / 2) * (GridRowH + RowGap);
			lv_obj_t* t = CreateBare(m_step1, S(x), y(yy), SL(x, ColW), SL(yy, GridRowH));
			const bool calibrate = (i == CalibrateIndex);
			StyleCard(t, Colour::Tile(), calibrate ? Colour::Accent() : Colour::Border());
			lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
			lv_obj_set_style_bg_color(t, Colour::Pressed(), LV_STATE_PRESSED);
			lv_obj_add_event_cb(
				t,
				[](lv_event_t* e)
				{
					auto* self = static_cast<NxtProbePage*>(lv_event_get_user_data(e));
					lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
					for (size_t k = 0; k < NumGrid; ++k)
					{
						if (self->m_grid[k] == obj)
						{
							self->gridPressed(k);
							return;
						}
					}
				},
				LV_EVENT_CLICKED,
				this);
			const int32_t th = SL(yy, GridRowH) - 2 * CardBorder;
			const char* name = calibrate ? "CALIBRATE" : Nxt::CycleName(GridCycles[i]);
			const char* hint = calibrate ? "datum, repeatability, rotary" : Nxt::CycleHint(GridCycles[i]);
			m_gridName[i] = CreateLabel(t, 10, 2, SL(x, ColW) - 20, th / 2 + 2, Font::Small(), calibrate ? Colour::Accent() : Colour::Text(), LV_TEXT_ALIGN_LEFT, name);
			CreateLabel(t, 10, th / 2 + 2, SL(x, ColW) - 20, th / 2 - 4, Font::Tiny(), Colour::Muted(), LV_TEXT_ALIGN_LEFT, hint);
			m_grid[i] = t;
		}

		// ---- Step 2: the parameters of one cycle ----
		m_step2 = CreateBare(m_root, 0, 0, ScreenW, 1);
		lv_obj_set_size(m_step2, LV_PCT(100), LV_PCT(100));
		m_back.create(m_step2, S(X0), y(HeadY), SL(X0, 100), SL(HeadY, HeadH), "< BACK", Font::Small());
		m_back.onPress([this] { backPressed(); });
		m_cycleLabel = CreateLabel(m_step2, S(X0 + 112), y(HeadY), SL(X0 + 112, 200), SL(HeadY, HeadH), Font::Small(), Colour::Accent(), LV_TEXT_ALIGN_LEFT, "");
		m_codeLabel = CreateLabel(m_step2, S(X0 + W - 150), y(HeadY), SL(X0 + W - 150, 150), SL(HeadY, HeadH), Font::Small(), Colour::Muted(), LV_TEXT_ALIGN_RIGHT, "");
		for (size_t i = 0; i < Nxt::MaxCycleParams; ++i)
		{
			const int32_t x = X0 + (int32_t)(i % 2) * (ColW + ColGap);
			const int32_t yy = ParTop + (int32_t)(i / 2) * (ParRowH + RowGap);
			m_params[i].create(m_step2, S(x), y(yy), SL(x, ColW), SL(yy, ParRowH), "", true, false);
			m_params[i].onPress([this, i] { paramPressed(i); });
		}
		m_run.create(m_step2, S(X0), y(BottomY), SL(X0, W), SL(BottomY, BottomH), "RUN");
		m_run.onPress([this] { runPressed(); });

		State::AddProbeListener([this] { updateSignal(); });
		m_timer = lv_timer_create([](lv_timer_t* t) { static_cast<NxtProbePage*>(lv_timer_get_user_data(t))->tick(); }, 200, this);
		lv_obj_add_flag(m_root, LV_OBJ_FLAG_HIDDEN);
		showStep();
		updateSignal();
	}

	void NxtProbePage::tick()
	{
		const bool active = Nxt::Active();
		if (active != m_active)
		{
			// The signal follows NeXT's touch probe (its K number) while NXT is shown, K0 otherwise
			int32_t value = 0;
			if (OM::GetProbeValue((size_t)Nxt::UiProbeIndex(), value))
			{
				State::SetProbeValue(value);
			}
		}
		if (active == m_active)
		{
			return;
		}
		m_active = active;
		if (active)
		{
			lv_obj_remove_flag(m_root, LV_OBJ_FLAG_HIDDEN);
		}
		else
		{
			lv_obj_add_flag(m_root, LV_OBJ_FLAG_HIDDEN);
		}
		if (m_onActive)
		{
			m_onActive(active);
		}
	}

	void NxtProbePage::showStep()
	{
		if (m_inCycle)
		{
			lv_obj_add_flag(m_step1, LV_OBJ_FLAG_HIDDEN);
			lv_obj_remove_flag(m_step2, LV_OBJ_FLAG_HIDDEN);
			refreshParams();
		}
		else
		{
			lv_obj_remove_flag(m_step1, LV_OBJ_FLAG_HIDDEN);
			lv_obj_add_flag(m_step2, LV_OBJ_FLAG_HIDDEN);
		}
	}

	void NxtProbePage::refreshParams()
	{
		const auto params = Nxt::CycleParams(m_cycle);
		lv_label_set_text(m_cycleLabel, Nxt::CycleName(m_cycle));
		lv_label_set_text(m_codeLabel, fmt::format("NXT {:s}", Nxt::CycleCode(m_cycle)).c_str());
		for (size_t i = 0; i < Nxt::MaxCycleParams; ++i)
		{
			const bool vis = i < params.size();
			if (vis)
			{
				const Nxt::Param p = params[i];
				m_paramLabels[i] = Nxt::ParamLabel(p, m_cycle);
				m_params[i].setLabel(m_paramLabels[i], Nxt::ParamIsChoice(p));
				m_params[i].setValue(Nxt::ParamText(p));
			}
			m_params[i].show(vis);
		}
		m_runText = fmt::format("RUN {:s}", Nxt::CycleName(m_cycle));
		m_run.setText(m_runText);
	}

	void NxtProbePage::updateSignal()
	{
		if (!State::ProbeReported())
		{
			m_run.setSignal(ProbeButton::Signal::None);
		}
		else
		{
			m_run.setSignal(State::ProbeClosed() ? ProbeButton::Signal::Closed : ProbeButton::Signal::Open);
		}
	}

	std::string NxtProbePage::gridText(size_t index) const
	{
		return (index < NumGrid && m_gridName[index] != nullptr) ? lv_label_get_text(m_gridName[index]) : "";
	}

	std::string NxtProbePage::paramText(size_t index) const
	{
		const auto params = Nxt::CycleParams(m_cycle);
		if (!m_inCycle || index >= params.size())
		{
			return "";
		}
		return m_paramLabels[index] + " " + m_params[index].value();
	}

	// ---- Touches -------------------------------------------------------------------------------

	void NxtProbePage::gridPressed(size_t index)
	{
		if (index == CalibrateIndex)
		{
			const auto items = Nxt::CalibrationItems();
			std::vector<std::string> labels;
			for (const auto& it : items)
			{
				labels.push_back(it.label);
			}
			Popup::Choose("CALIBRATE",
						  labels,
						  0,
						  [this, items](size_t c)
						  {
							  if (c >= items.size() || refuseRun())
							  {
								  return;
							  }
							  const std::string gcode = items[c].gcode;
							  Popup::Confirm(items[c].label, {items[c].question, "NXT asks on the screen as it runs."}, [this, gcode]
											 {
												 if (!refuseRun())
												 {
													 State::Send(gcode);
												 }
											 });
						  },
						  2);
			return;
		}
		if (index >= CalibrateIndex)
		{
			return;
		}
		m_cycle = GridCycles[index];
		m_inCycle = true;
		showStep();
	}

	void NxtProbePage::backPressed()
	{
		m_inCycle = false;
		showStep();
	}

	void NxtProbePage::paramPressed(size_t index)
	{
		const auto params = Nxt::CycleParams(m_cycle);
		if (index >= params.size())
		{
			return;
		}
		const Nxt::Param p = params[index];
		if (Nxt::ParamIsChoice(p))
		{
			Popup::Choose(Nxt::ParamLabel(p, m_cycle),
						  Nxt::ParamChoices(p, m_cycle),
						  Nxt::ParamChoice(p),
						  [this, p](size_t c)
						  {
							  Nxt::SetParamChoice(p, c);
							  refreshParams();
						  },
						  (p == Nxt::Param::Corner) ? 2 : 0);
			return;
		}
		float min = 0, max = 0;
		bool minus = false;
		int decimals = 3;
		Nxt::ParamRange(p, min, max, minus, decimals);
		Popup::NumpadSpec pad;
		pad.tag = Nxt::ParamLabel(p, m_cycle);
		pad.unit = (p == Nxt::Param::Samples) ? "" : "mm";
		pad.value = Nxt::ParamValue(p);
		pad.decimals = decimals;
		pad.allowDecimal = decimals > 0;
		pad.allowMinus = minus;
		pad.min = min;
		pad.max = max;
		pad.onOk = [this, p](float v)
		{
			Nxt::SetParamValue(p, v);
			refreshParams();
		};
		Popup::Numpad(pad);
	}

	bool NxtProbePage::refuseRun()
	{
		if (State::JobActive())
		{
			Popup::Refuse("Locked while a job is running or paused.");
			return true;
		}
		if (State::Status() != OM::PrinterStatus::idle)
		{
			Popup::Refuse("Wait until the machine is idle.");
			return true;
		}
		return false;
	}

	void NxtProbePage::runPressed()
	{
		if (!m_inCycle || refuseRun())
		{
			return;
		}
		if (!State::XyzHomed())
		{
			Popup::Refuse("Home X, Y and Z first.");
			return;
		}
		if (State::ProbeClosed())
		{
			Popup::Refuse("Probe is closed (triggered).\nCheck the probe and its wiring.");
			return;
		}
		if (m_cycle != Nxt::Cycle::RotaryCentre && Nxt::ProbeToolId() >= 0 && State::ActiveTool() != Nxt::ProbeToolId())
		{
			Popup::Refuse(fmt::format("Select the touch probe first\n(T{:d}).", Nxt::ProbeToolId()));
			return;
		}
		State::Send(Nxt::Command(m_cycle));
	}
} // namespace UI::Cnc
