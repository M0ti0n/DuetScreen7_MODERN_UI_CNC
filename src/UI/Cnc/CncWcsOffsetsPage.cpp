/*
 * CncWcsOffsetsPage.cpp
 *
 * Positions are PanelDue CncLayout::Offsets values through S() / SL(), relative to the page area.
 */

#include "CncWcsOffsetsPage.h"
#include "CncLayout.h"
#include "CncPopup.h"
#include "CncState.h"
#include "CncTheme.h"
#include <cmath>
#include <fmt/format.h>
#include <vector>

namespace UI::Cnc
{
	using namespace CncLayout;
	using namespace CncLayout::Offsets;

	namespace
	{
		const char* const WcsNames[WcsOffsetsPage::NumWcs] = {"G54", "G55", "G56", "G57", "G58", "G59"};
		const float Steps[WcsOffsetsPage::NumSteps] = {0.005f, 0.02f, 0.1f, 0.5f, 1.0f};
		const char* const StepNames[WcsOffsetsPage::NumSteps] = {"0.005", "0.02", "0.1", "0.5", "1"};

		std::string format3(float v)
		{
			return fmt::format("{:.3f}", (std::fabs(v) < 0.0005f) ? 0.0f : v); // no "-0.000"
		}

		void show(lv_obj_t* o, bool visible)
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

		bool stepAllowed(size_t i)
		{
			return i < WcsOffsetsPage::FirstLargeStep || !State::MachineBusy(); // large steps only idle / paused
		}

		constexpr const char* LargeStepRefusal = "0.5 and 1 mm steps: not while a job is running.";
	} // namespace

	// ---- Building ------------------------------------------------------------------------------

	void WcsOffsetsPage::create(lv_obj_t* parent, int32_t parentTop)
	{
		m_root = CreateBare(parent, 0, 0, ScreenW, 1);
		lv_obj_set_size(m_root, LV_PCT(100), LV_PCT(100)); // the parent is not laid out yet
		m_top = parentTop;
		auto y = [this](int32_t pdY) { return S(pdY) - m_top; };
		const int32_t smallH = lv_font_get_line_height(Font::Small());
		const lv_color_t accent = Colour::Accent();

		auto label = [&](int32_t pdY, int32_t pdX, int32_t pdW, const char* text, lv_text_align_t align)
		{
			return CreateLabel(
				m_root, S(pdX), y(pdY), SL(pdX, pdW), smallH, Font::Small(), Colour::Muted(), align, text);
		};
		auto button = [&](TextButton& b,
						  int32_t pdY,
						  int32_t pdX,
						  int32_t pdW,
						  int32_t pdH,
						  const char* text,
						  const lv_font_t* font)
		{ b.create(m_root, S(pdX), y(pdY), SL(pdX, pdW), SL(pdY, pdH), text, font); };

		// Labels
		label(LabelWcsY, LeftX + 2, 200, "WORK OFFSET", LV_TEXT_ALIGN_LEFT);
		label(LabelWcsY, RightX - 6, RightW + 12, "OFFSET", LV_TEXT_ALIGN_CENTER);
		m_gridLabel = label(LabelGridY, LeftX + 2, 250, "", LV_TEXT_ALIGN_LEFT);

		// G54..G59
		{
			const int32_t w = (LeftW - (int32_t)(NumWcs - 1) * WcsGap) / (int32_t)NumWcs;
			for (size_t i = 0; i < NumWcs; ++i)
			{
				button(m_wcs[i], RowWcsY, LeftX + (int32_t)i * (w + WcsGap), w, RowWcsH, WcsNames[i], Font::Small());
				m_wcs[i].onPress([this, i] { wcsPressed(i); });
			}
		}

		// 2 x 2 offset grid: X Y / Z A. Letter (accent) left of a card with the value (right-aligned)
		for (size_t a = 0; a < MaxAxes; ++a)
		{
			const int32_t cx = LeftX + (int32_t)(a % 2) * (CellW + CellGap);
			const int32_t cy = GridY + (int32_t)(a / 2) * (CellH + CellGap);
			const int32_t top = y(cy), h = SL(cy, CellH);
			const char letter[2] = {State::AxisLetter(a), 0};
			m_cellLetter[a] = CreateLabel(
				m_root, S(cx), top, SL(cx, CellLetterW), h, Font::Large(), accent, LV_TEXT_ALIGN_CENTER, letter);
			m_cellCard[a] = CreateBare(m_root, S(cx + CellLetterW), top, SL(cx + CellLetterW, CellW - CellLetterW), h);
			StyleCard(m_cellCard[a], Colour::Tile(), Colour::Border());
			m_cellValue[a] = CreateLabel(m_root,
										 S(cx + CellLetterW + 6),
										 top,
										 SL(cx + CellLetterW + 6, CellW - CellLetterW - 18),
										 h,
										 Font::Small(),
										 Colour::Text(),
										 LV_TEXT_ALIGN_RIGHT,
										 "0.000");
			// The whole cell (letter and card) opens the numpad; no pressed look (PanelDue touch area)
			m_cellTouch[a] = CreateBare(m_root, S(cx), top, SL(cx, CellW), h);
			lv_obj_add_flag(m_cellTouch[a], LV_OBJ_FLAG_CLICKABLE);
			lv_obj_add_event_cb(
				m_cellTouch[a],
				[](lv_event_t* e)
				{
					auto* page = static_cast<WcsOffsetsPage*>(lv_event_get_user_data(e));
					lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_current_target(e));
					for (size_t i = 0; i < MaxAxes; ++i)
					{
						if (page->m_cellTouch[i] == target)
						{
							page->offsetPressed(i);
						}
					}
				},
				LV_EVENT_PRESSED,
				this);
		}

		// Actions, 2 rows of 3
		{
			const int32_t w = (LeftW - 2 * 8) / 3;
			const int32_t y2 = ActY + ActH + ActGap;
			button(m_activate, ActY, LeftX, w, ActH, "ACTIVATE", Font::Small());
			button(m_copy, ActY, LeftX + w + 8, w, ActH, "COPY TO", Font::Small());
			button(m_save, ActY, LeftX + 2 * (w + 8), w, ActH, "SAVE", Font::Small());
			button(m_reset, y2, LeftX, w, ActH, "RESET", Font::Small());
			button(m_shift, y2, LeftX + w + 8, w, ActH, "SHIFT", Font::Small());
			button(m_apply, y2, LeftX + 2 * (w + 8), w, ActH, "APPLY |->|", Font::Small());
			m_activate.onPress([this] { activatePressed(); });
			m_copy.onPress([this] { copyPressed(); });
			m_save.onPress([this] { savePressed(); });
			m_reset.onPress([this] { resetPressed(); });
			m_shift.onPress([this] { shiftPressed(); });
			m_apply.onPress([this] { applyPressed(); });
			for (TextButton* b : {&m_copy, &m_reset, &m_shift, &m_apply})
			{
				b->setPressedLook(false); // they open a popup
			}
		}

		// Vertical accent separator
		lv_obj_t* sep =
			CreateBare(m_root, S(SepX), y(LabelWcsY), SL(SepX, SeparatorW), SL(LabelWcsY, Bottom - LabelWcsY));
		lv_obj_set_style_bg_color(sep, accent, 0);
		lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);

		// OFFSET column (offset step)
		button(m_zPlus, StepUpY, RightX, RightW, StepBtnH, "Z+", Font::Large());
		lv_obj_t* totalCard =
			CreateBare(m_root, S(RightX), y(StepTotalY), SL(RightX, RightW), SL(StepTotalY, StepTotalH));
		StyleCard(totalCard, Colour::Tile(), Colour::Border());
		m_total = CreateLabel(m_root,
							  S(RightX + 4),
							  y(StepTotalY),
							  SL(RightX + 4, RightW - 8),
							  SL(StepTotalY, StepTotalH),
							  Font::Small(),
							  accent,
							  LV_TEXT_ALIGN_CENTER,
							  "0.000");
		button(m_zMinus, StepDownY, RightX, RightW, StepBtnH, "Z-", Font::Large());
		label(StepSizeLabelY, RightX, RightW, "STEP", LV_TEXT_ALIGN_CENTER);
		button(m_stepSize, StepSizeY, RightX, RightW, StepSizeH, StepNames[m_stepIndex], Font::Small());
		button(m_clear, StepClearY, RightX, RightW, StepClearH, "CLEAR", Font::Small());
		m_zPlus.onPress([this] { stepPressed(1); });
		m_zMinus.onPress([this] { stepPressed(-1); });
		m_stepSize.onPress([this] { stepSizePressed(); });
		m_clear.onPress([this] { clearPressed(); });
		m_stepSize.setPressedLook(false);
		m_clear.setPressedLook(false);

		// Shared state
		State::AddOffsetsListener([this] { refresh(); });
		State::AddJobListener(
			[this]
			{
				m_jobRunning = State::MachineBusy();
				m_jobActive = State::JobActive();
				updateLockedLook();
			});
		m_jobRunning = State::MachineBusy();
		m_jobActive = State::JobActive();
		refresh();
	}

	// ---- Look ----------------------------------------------------------------------------------

	void WcsOffsetsPage::refresh()
	{
		const size_t active = State::ActiveWorkplace();
		for (size_t i = 0; i < NumWcs; ++i)
		{
			m_wcs[i].setSelected(i == active);	 // active: accent fill
			m_wcs[i].setOutlined(i == m_viewed); // viewed: accent outline
		}
		lv_label_set_text(m_gridLabel, fmt::format("OFFSET {:s}", WcsNames[m_viewed]).c_str());

		// The A cell only with a visible A axis (the slot stays empty otherwise)
		const size_t axes = State::AxisCount();
		for (size_t a = 0; a < MaxAxes; ++a)
		{
			const char letter[2] = {State::AxisLetter(a), 0};
			lv_label_set_text(m_cellLetter[a], letter);
			lv_label_set_text(m_cellValue[a], format3(State::WorkplaceOffset(a, m_viewed)).c_str());
			for (lv_obj_t* o : {m_cellLetter[a], m_cellCard[a], m_cellValue[a], m_cellTouch[a]})
			{
				show(o, a < axes);
			}
		}
		lv_label_set_text(m_total, format3(State::BabystepZ()).c_str());
		m_save.setOutlined(State::OffsetsUnsaved());
		updateLockedLook();
	}

	// During a job only the active WCS matters to the cut: it is locked, the other five stay editable
	bool WcsOffsetsPage::editable(size_t wcs) const
	{
		return !m_jobActive || wcs != State::ActiveWorkplace();
	}

	void WcsOffsetsPage::refuseJobWcs()
	{
		const size_t active = State::ActiveWorkplace();
		Popup::Refuse(fmt::format("{:s} is the job's work offset.\nLocked until the job ends.",
								  (active < NumWcs) ? WcsNames[active] : "The active WCS"));
	}

	void WcsOffsetsPage::updateLockedLook()
	{
		const bool canEdit = editable(m_viewed);
		m_activate.setLocked(m_jobActive || m_viewed == State::ActiveWorkplace()); // no-op when already active
		m_apply.setLocked(m_jobRunning); // fine while paused: the net position does not change
		m_reset.setLocked(!canEdit);
		m_shift.setLocked(!canEdit);
		for (lv_obj_t* v : m_cellValue)
		{
			lv_obj_set_style_text_color(v, canEdit ? Colour::Text() : Colour::Muted(), 0); // readable, not editable
		}
	}

	// ---- Touches -------------------------------------------------------------------------------

	void WcsOffsetsPage::wcsPressed(size_t wcs)
	{
		if (wcs < NumWcs)
		{
			m_viewed = wcs;
			refresh();
		}
	}

	void WcsOffsetsPage::activatePressed()
	{
		if (m_jobActive)
		{
			Popup::Refuse("Locked while a job is running or paused.");
			return;
		}
		if (m_viewed == State::ActiveWorkplace())
		{
			Popup::Refuse(fmt::format("{:s} is already active.", WcsNames[m_viewed]));
			return;
		}
		State::Send(fmt::format("{:s}\n", WcsNames[m_viewed]));
	}

	void WcsOffsetsPage::savePressed()
	{
		State::Send("M500 P10\n"); // P10: tool offsets too
		State::MarkOffsetsSaved();
	}

	// ---- Offset value (numpad) ----

	void WcsOffsetsPage::offsetPressed(size_t axis)
	{
		if (axis >= State::AxisCount())
		{
			return;
		}
		if (!editable(m_viewed))
		{
			refuseJobWcs();
			return;
		}
		const size_t wcs = m_viewed;
		Popup::NumpadSpec pad;
		pad.tag = fmt::format("{:s} {:c}", WcsNames[wcs], State::AxisLetter(axis));
		pad.unit = (axis == 3) ? "deg" : "mm";
		pad.value = State::WorkplaceOffset(axis, wcs);
		pad.decimals = 3;
		pad.allowDecimal = true;
		pad.allowMinus = true;
		pad.min = -99999.0f;
		pad.max = 99999.0f;
		// POS: the offset that makes the current position 0 in the viewed WCS. From the work position the
		// DRO shows (tool offset and babystep included, as RRF does), relative to the active WCS.
		pad.pos = [axis, wcs]
		{
			const size_t active = State::ActiveWorkplace();
			if (active >= NumWcs)
			{
				return State::WorkplaceOffset(axis, wcs); // G59.1..G59.3 active: its offsets are not known here
			}
			return State::WorkplaceOffset(axis, active) + State::WorkPosition(axis);
		};
		pad.onOk = [this, axis](float value) { offsetEntered(axis, value); };
		Popup::Numpad(pad);
	}

	void WcsOffsetsPage::offsetEntered(size_t axis, float value)
	{
		if (!editable(m_viewed))
		{
			refuseJobWcs(); // a job started while the numpad was open
			return;
		}
		State::Send(fmt::format("G10 L2 P{:d} {:c}{:.3f}\n", m_viewed + 1, State::AxisLetter(axis), value));
		State::MarkOffsetsUnsaved();
	}

	// ---- RESET ----

	void WcsOffsetsPage::resetPressed()
	{
		if (!editable(m_viewed))
		{
			refuseJobWcs();
			return;
		}
		const size_t wcs = m_viewed;
		Popup::Confirm(
			"RESET", {fmt::format("Set all {:s} offsets to 0?", WcsNames[wcs])}, [this, wcs] { doReset(wcs); });
	}

	void WcsOffsetsPage::doReset(size_t wcs)
	{
		if (!editable(wcs))
		{
			refuseJobWcs();
			return;
		}
		std::string cmd = fmt::format("G10 L2 P{:d}", wcs + 1);
		for (size_t a = 0; a < State::AxisCount(); ++a)
		{
			cmd += fmt::format(" {:c}0", State::AxisLetter(a));
		}
		State::Send(cmd + "\n");
		State::MarkOffsetsUnsaved();
	}

	// ---- Offset step (RRF babystep) ----

	void WcsOffsetsPage::stepPressed(int direction)
	{
		if (!stepAllowed(m_stepIndex))
		{
			Popup::Refuse(LargeStepRefusal); // chosen while idle, the job started since
			return;
		}
		State::Send(fmt::format("M290 Z{:.3f}\n", Steps[m_stepIndex] * (float)direction));
	}

	void WcsOffsetsPage::stepSizePressed()
	{
		Popup::Choose(
			"STEP SIZE",
			std::vector<std::string>(std::begin(StepNames), std::end(StepNames)),
			m_stepIndex,
			[this](size_t choice)
			{
				if (!stepAllowed(choice))
				{
					Popup::Refuse(LargeStepRefusal);
					return;
				}
				m_stepIndex = choice;
				m_stepSize.setText(StepNames[choice]);
			},
			0,
			stepAllowed);
	}

	void WcsOffsetsPage::clearPressed()
	{
		const float z = State::BabystepZ();
		if (std::fabs(z) < 0.0005f)
		{
			Popup::Refuse("No offset step to clear.");
			return;
		}
		// Z moves by the whole offset step: confirm first (also live during a job)
		Popup::Confirm("CLEAR",
					   {"Clear offset?", fmt::format("Z will move {:.3f} mm.", std::fabs(z))},
					   [] { State::Send("M290 R0 Z0\n"); });
	}

	// ---- APPLY OFFSET ----

	void WcsOffsetsPage::applyPressed()
	{
		if (m_jobRunning)
		{
			Popup::Refuse("Pause the job to apply the offset step.");
			return;
		}
		const float z = State::BabystepZ();
		if (std::fabs(z) < 0.0005f)
		{
			Popup::Refuse("No offset step to apply.");
			return;
		}
		const size_t active = State::ActiveWorkplace();
		Popup::Confirm("APPLY OFFSET",
					   {fmt::format("Add Z offset step {:+.3f}", z),
						fmt::format("to the {:s} Z offset", (active < NumWcs) ? WcsNames[active] : "active"),
						"and clear the offset step?"},
					   [this] { doApply(); });
	}

	void WcsOffsetsPage::doApply()
	{
		if (m_jobRunning)
		{
			Popup::Refuse("Pause the job to apply the offset step.");
			return;
		}
		// Fold the offset step into the active WCS Z on the Duet, from its own current values
		State::Send("G10 L2 P{move.workplaceNumber + 1} "
					"Z{move.axes[2].workplaceOffsets[move.workplaceNumber] + move.axes[2].babystep}\n");
		State::Send("M290 R0 Z0\n");
		State::MarkOffsetsUnsaved();
	}

	// ---- COPY TO ----

	bool WcsOffsetsPage::hasOffsets(size_t wcs, uint16_t axes) const
	{
		for (size_t a = 0; a < State::AxisCount(); ++a)
		{
			if ((axes & (1u << a)) != 0 && State::WorkplaceOffset(a, wcs) != 0.0f)
			{
				return true;
			}
		}
		return false;
	}

	std::string WcsOffsetsPage::axisWords(size_t wcs, uint16_t axes, float add) const
	{
		std::string words;
		for (size_t a = 0; a < State::AxisCount(); ++a)
		{
			if ((axes & (1u << a)) != 0)
			{
				words += fmt::format(" {:c}{:.3f}", State::AxisLetter(a), State::WorkplaceOffset(a, wcs) + add);
			}
		}
		return words;
	}

	void WcsOffsetsPage::copyPressed()
	{
		const size_t source = m_viewed;
		const size_t active = State::ActiveWorkplace();
		const size_t axes = State::AxisCount();
		uint16_t disabled = (uint16_t)(1u << source);
		if (m_jobActive && active < NumWcs)
		{
			disabled |= (uint16_t)(1u << active); // the job's WCS cannot be overwritten
		}
		const uint16_t allAxes = (uint16_t)((1u << axes) - 1);
		uint16_t marked = 0;
		for (size_t w = 0; w < NumWcs; ++w)
		{
			if (hasOffsets(w, allAxes))
			{
				marked |= (uint16_t)(1u << w);
			}
		}
		// Preselect the next free-to-use WCS after the source
		uint16_t target = 0;
		for (size_t k = 1; k < NumWcs && target == 0; ++k)
		{
			const size_t w = (source + k) % NumWcs;
			if ((disabled & (1u << w)) == 0)
			{
				target = (uint16_t)(1u << w);
			}
		}

		Popup::FormGroup to;
		to.label = "TO";
		to.items.assign(std::begin(WcsNames), std::end(WcsNames));
		to.selected = target;
		to.disabled = disabled;
		to.marked = marked;
		Popup::FormGroup axesGroup;
		axesGroup.label = "AXES";
		for (size_t a = 0; a < axes; ++a)
		{
			axesGroup.items.emplace_back(1, State::AxisLetter(a));
		}
		axesGroup.multi = true;
		axesGroup.selected = allAxes;
		Popup::Form(fmt::format("COPY {:s}", WcsNames[source]),
					{to, axesGroup},
					[this, source](const std::vector<uint16_t>& sel)
					{
						size_t t = 0;
						while (t < NumWcs && (sel[0] & (1u << t)) == 0)
						{
							++t;
						}
						copyChosen(source, t, sel[1]);
					});
	}

	void WcsOffsetsPage::copyChosen(size_t source, size_t target, uint16_t axes)
	{
		if (target >= NumWcs || target == source || !editable(target))
		{
			refuseJobWcs();
			return;
		}
		if (!hasOffsets(target, axes))
		{
			doCopy(source, target, axes); // empty target: nothing to lose
			return;
		}

		// Overwrite: old -> new for every copied axis
		std::vector<std::string> lines;
		lines.push_back(fmt::format("{:s} already has offsets", WcsNames[target]));
		for (size_t a = 0; a < State::AxisCount(); ++a)
		{
			if ((axes & (1u << a)) != 0)
			{
				lines.push_back(fmt::format("{:c}  {:+.3f} -> {:+.3f}",
											State::AxisLetter(a),
											State::WorkplaceOffset(a, target),
											State::WorkplaceOffset(a, source)));
			}
		}
		lines.push_back(fmt::format("Copy {:s} into {:s}?", WcsNames[source], WcsNames[target]));
		Popup::Confirm("OVERWRITE", lines, [this, source, target, axes] { doCopy(source, target, axes); });
	}

	void WcsOffsetsPage::doCopy(size_t source, size_t target, uint16_t axes)
	{
		if (!editable(target))
		{
			refuseJobWcs(); // the target became the active WCS of a job
			return;
		}
		State::Send(fmt::format("G10 L2 P{:d}{:s}\n", target + 1, axisWords(source, axes, 0.0f)));
		State::MarkOffsetsUnsaved();
	}

	// ---- SHIFT ----

	void WcsOffsetsPage::shiftPressed()
	{
		if (!editable(m_viewed))
		{
			refuseJobWcs();
			return;
		}
		const size_t wcs = m_viewed;
		Popup::FormGroup axesGroup;
		axesGroup.label = "AXES";
		for (size_t a = 0; a < State::AxisCount(); ++a)
		{
			axesGroup.items.emplace_back(1, State::AxisLetter(a));
		}
		axesGroup.multi = true; // nothing picked yet
		Popup::Form(fmt::format("SHIFT {:s}", WcsNames[wcs]),
					{axesGroup},
					[this, wcs](const std::vector<uint16_t>& sel) { shiftAxesChosen(wcs, sel[0]); });
	}

	void WcsOffsetsPage::shiftAxesChosen(size_t wcs, uint16_t axes)
	{
		Popup::NumpadSpec pad;
		pad.tag = fmt::format("{:s} +", WcsNames[wcs]);
		pad.unit = (axes == (1u << 3)) ? "deg" : "mm"; // degrees only when A is the only picked axis
		pad.value = 0.0f;
		pad.decimals = 3;
		pad.allowDecimal = true;
		pad.allowMinus = true;
		pad.min = -9999.0f;
		pad.max = 9999.0f;
		pad.onOk = [this, wcs, axes](float amount) { shiftEntered(wcs, axes, amount); };
		Popup::Numpad(pad);
	}

	void WcsOffsetsPage::shiftEntered(size_t wcs, uint16_t axes, float amount)
	{
		if (!editable(wcs))
		{
			refuseJobWcs();
			return;
		}
		State::Send(fmt::format("G10 L2 P{:d}{:s}\n", wcs + 1, axisWords(wcs, axes, amount)));
		State::MarkOffsetsUnsaved();
	}
} // namespace UI::Cnc
