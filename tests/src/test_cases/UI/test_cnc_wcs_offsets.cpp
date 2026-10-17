/*
 * test_cnc_wcs_offsets.cpp
 *
 * WCS > OFFSETS of the CNC UI: the G-code it sends, its popups, what it refuses during a job, and
 * screenshots (compare with the PanelDue CNC UI references x 1.25).
 *
 * Machine: G54 active, offsets X 100 / Y 100 / Z -25, work position X 12.345 / Y -5.5 / Z 3.2.
 */

#include "Debug.h"
#include "UI/Cnc/CncHomeView.h"
#include "UI/Cnc/CncLayout.h"
#include "UI/Cnc/CncPopup.h"
#include "UI/Cnc/CncState.h"
#include "test_utils/UiTestSuite.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace UI::Cnc;
using Lines = std::vector<std::string>;

class TestCncWcsOffsets : public UiTestSuite
{
  protected:
	TestCncWcsOffsets()
	{
		ZoneScoped;
		lv_display_set_resolution(lv_display_get_default(), UI::CncLayout::ScreenW, UI::CncLayout::ScreenH);
		lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
		State::Reset();
		State::SetSendHook([this](std::string_view g) { sent.emplace_back(g); });
	}

	~TestCncWcsOffsets() override { Popup::Close(); }

	// The frame on WCS > OFFSETS with machine data, idle
	std::unique_ptr<UI::CncHomeView> makeView(bool fourAxes = false)
	{
		load_model_data_from_file(fourAxes ? "tests/object_model/cnc/model_move_4axis.json"
										   : "tests/object_model/cnc/model_move_3axis.json");
		load_model_data_from_file("tests/object_model/cnc/model_spindles.json");
		load_model_data_from_file("tests/object_model/cnc/model_tools.json");
		load_model_data_from_file("tests/object_model/cnc/model_state_idle.json");
		auto v = std::make_unique<UI::CncHomeView>();
		v->show();
		v->showPage(UI::CncHomeView::PageWcs);
		State::StatusChanged(OM::PrinterStatus::idle);
		return v;
	}

	void type(const std::string& keys)
	{
		for (char c : keys)
		{
			Popup::NumpadKey(c);
		}
	}

	std::string last() const { return sent.empty() ? std::string() : sent.back(); }

	std::vector<std::string> sent;
};

// ---- Viewing and the numpad --------------------------------------------------------------------

TEST_F(TestCncWcsOffsets, ViewOtherWcs)
{
	auto view = makeView();
	auto& page = view->getWcsOffsetsPage();
	page.wcsPressed(1);
	EXPECT_EQ(page.viewedWcs(), 1u);
	EXPECT_TRUE(sent.empty()); // viewing only
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_offsets_g55_viewed.png"));
}

TEST_F(TestCncWcsOffsets, OffsetNumpad)
{
	auto view = makeView();
	view->getWcsOffsetsPage().offsetPressed(0);
	ASSERT_TRUE(Popup::IsNumpadOpen());
	EXPECT_EQ(Popup::NumpadTag(), "G54 X");
	EXPECT_EQ(Popup::NumpadValue(), "100.000");
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_numpad_offset.png"));
	type("-12.5");
	Popup::PressOk();
	EXPECT_EQ(last(), "G10 L2 P1 X-12.500\n");
	EXPECT_TRUE(State::OffsetsUnsaved());
}

TEST_F(TestCncWcsOffsets, OffsetNumpadPosMakesCurrentPointZero)
{
	auto view = makeView();
	auto& page = view->getWcsOffsetsPage();
	page.wcsPressed(2); // set G56 from where the tool is now
	page.offsetPressed(2);
	Popup::NumpadKey('P');
	EXPECT_EQ(Popup::NumpadValue(), "-21.800"); // G54 Z -25 + work Z 3.2
	Popup::PressOk();
	EXPECT_EQ(last(), "G10 L2 P3 Z-21.800\n");
}

// ---- ACTIVATE, SAVE, RESET ---------------------------------------------------------------------

TEST_F(TestCncWcsOffsets, ActivateAndSave)
{
	auto view = makeView();
	auto& page = view->getWcsOffsetsPage();
	page.activatePressed();
	EXPECT_EQ(Popup::Lines(), Lines({"G54 is already active."}));
	Popup::Close();
	page.wcsPressed(2);
	page.activatePressed();
	EXPECT_EQ(last(), "G56\n");

	State::MarkOffsetsUnsaved();
	page.savePressed();
	EXPECT_EQ(last(), "M500 P10\n");
	EXPECT_FALSE(State::OffsetsUnsaved());
}

TEST_F(TestCncWcsOffsets, ResetAsksFirst)
{
	auto view = makeView(true);
	auto& page = view->getWcsOffsetsPage();
	page.wcsPressed(1);
	page.resetPressed();
	EXPECT_EQ(Popup::Title(), "RESET");
	EXPECT_EQ(Popup::Lines(), Lines({"Set all G55 offsets to 0?"}));
	EXPECT_TRUE(sent.empty());
	Popup::PressOk();
	EXPECT_EQ(last(), "G10 L2 P2 X0 Y0 Z0 A0\n"); // 4 axes
}

// ---- Offset step (babystep), STEP, CLEAR, APPLY ------------------------------------------------

TEST_F(TestCncWcsOffsets, OffsetStepAndStepSize)
{
	auto view = makeView();
	auto& page = view->getWcsOffsetsPage();
	page.stepPressed(1);
	EXPECT_EQ(last(), "M290 Z0.020\n");
	page.stepPressed(-1);
	EXPECT_EQ(last(), "M290 Z-0.020\n");

	page.stepSizePressed();
	EXPECT_EQ(Popup::Title(), "STEP SIZE");
	EXPECT_EQ(Popup::ChoiceSelected(), 1u);
	Popup::PressChoice(4);
	Popup::PressOk();
	EXPECT_EQ(page.stepIndex(), 4u);
	page.stepPressed(1);
	EXPECT_EQ(last(), "M290 Z1.000\n");
}

TEST_F(TestCncWcsOffsets, LargeStepsNotDuringJob)
{
	auto view = makeView();
	auto& page = view->getWcsOffsetsPage();
	page.stepSizePressed();
	Popup::PressChoice(3);
	Popup::PressOk(); // 0.5 mm, chosen while idle
	State::StatusChanged(OM::PrinterStatus::processing);
	sent.clear();
	page.stepPressed(1);
	EXPECT_EQ(Popup::Lines(), Lines({"0.5 and 1 mm steps: not while a job is", "running."})); // wrapped
	EXPECT_TRUE(sent.empty());

	page.stepSizePressed(); // 0.5 and 1 greyed: a tap does nothing
	Popup::PressChoice(4);
	EXPECT_EQ(Popup::ChoiceSelected(), 3u);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_step_size_job.png"));
	Popup::PressChoice(0);
	Popup::PressOk();
	page.stepPressed(-1); // small steps work live in the cut
	EXPECT_EQ(last(), "M290 Z-0.005\n");
}

TEST_F(TestCncWcsOffsets, ClearAndApplyOffsetStep)
{
	auto view = makeView();
	auto& page = view->getWcsOffsetsPage();
	page.clearPressed();
	EXPECT_EQ(Popup::Lines(), Lines({"No offset step to clear."}));
	page.applyPressed();
	EXPECT_EQ(Popup::Lines(), Lines({"No offset step to apply."}));

	State::SetBabystepZ(0.04f);
	State::CommitOffsets();
	page.clearPressed();
	EXPECT_EQ(Popup::Title(), "CLEAR");
	EXPECT_EQ(Popup::Lines(), Lines({"Clear offset?", "Z will move 0.040 mm."}));
	Popup::PressOk();
	EXPECT_EQ(last(), "M290 R0 Z0\n");

	sent.clear();
	page.applyPressed();
	EXPECT_EQ(Popup::Title(), "APPLY OFFSET");
	EXPECT_EQ(Popup::Lines(), Lines({"Add Z offset step +0.040", "to the G54 Z offset", "and clear the offset step?"}));
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_apply_offset_confirm.png"));
	Popup::PressOk();
	ASSERT_EQ(sent.size(), 2u);
	EXPECT_EQ(sent[0],
			  "G10 L2 P1 Z{move.axes[2].workplaceOffsets[0] + move.axes[2].babystep}\n");
	EXPECT_EQ(sent[1], "M290 R0 Z0\n");
	EXPECT_TRUE(State::OffsetsUnsaved());
}

// ---- COPY TO, SHIFT ----------------------------------------------------------------------------

TEST_F(TestCncWcsOffsets, CopyToEmptyTarget)
{
	auto view = makeView();
	view->getWcsOffsetsPage().copyPressed();
	ASSERT_TRUE(Popup::IsFormOpen());
	EXPECT_EQ(Popup::Title(), "COPY G54");
	EXPECT_EQ(Popup::FormSelection(0), 1u << 1); // the next WCS, G55
	EXPECT_EQ(Popup::FormSelection(1), 0x7u);	 // all axes
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_copy_form.png"));

	Popup::PressFormItem(0, 0); // the source itself: greyed, nothing happens
	EXPECT_EQ(Popup::FormSelection(0), 1u << 1);
	for (size_t a = 0; a < 3; ++a)
	{
		Popup::PressFormItem(1, a); // no axis: the check mark does nothing
	}
	EXPECT_FALSE(Popup::FormOkEnabled());
	Popup::PressOk();
	EXPECT_TRUE(Popup::IsFormOpen());
	Popup::PressFormItem(1, 0);
	Popup::PressFormItem(1, 2);
	Popup::PressOk();
	EXPECT_EQ(last(), "G10 L2 P2 X100.000 Z-25.000\n"); // empty target: no question
}

TEST_F(TestCncWcsOffsets, CopyToOverwriteAsks)
{
	auto view = makeView();
	State::SetWorkplaceOffset(0, 2, 5.0f); // G56 X already set
	State::CommitOffsets();
	view->getWcsOffsetsPage().copyPressed();
	Popup::PressFormItem(0, 2);
	Popup::PressOk();
	EXPECT_EQ(Popup::Title(), "OVERWRITE");
	EXPECT_EQ(Popup::Lines(),
			  Lines({"G56 already has offsets",
					 "X  +5.000 -> +100.000",
					 "Y  +0.000 -> +100.000",
					 "Z  +0.000 -> -25.000",
					 "Copy G54 into G56?"}));
	EXPECT_TRUE(sent.empty());
	Popup::PressOk();
	EXPECT_EQ(last(), "G10 L2 P3 X100.000 Y100.000 Z-25.000\n");
}

TEST_F(TestCncWcsOffsets, ShiftPicksAxesThenAmount)
{
	auto view = makeView();
	view->getWcsOffsetsPage().shiftPressed();
	EXPECT_EQ(Popup::Title(), "SHIFT G54");
	EXPECT_FALSE(Popup::FormOkEnabled()); // nothing picked yet
	Popup::PressFormItem(0, 0);
	Popup::PressFormItem(0, 2);
	Popup::PressOk();
	ASSERT_TRUE(Popup::IsNumpadOpen());
	EXPECT_EQ(Popup::NumpadTag(), "G54 +");
	type("2");
	Popup::PressOk();
	EXPECT_EQ(last(), "G10 L2 P1 X102.000 Z-23.000\n");
}

// ---- During a job ------------------------------------------------------------------------------

TEST_F(TestCncWcsOffsets, JobLocksOnlyTheActiveWcs)
{
	auto view = makeView();
	auto& page = view->getWcsOffsetsPage();
	State::StatusChanged(OM::PrinterStatus::processing);
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_offsets_job_running.png"));

	const Lines jobWcs = {"G54 is the job's work offset.", "Locked until the job ends."};
	page.offsetPressed(0);
	EXPECT_EQ(Popup::Lines(), jobWcs);
	page.resetPressed();
	EXPECT_EQ(Popup::Lines(), jobWcs);
	page.shiftPressed();
	EXPECT_EQ(Popup::Lines(), jobWcs);
	page.activatePressed();
	EXPECT_EQ(Popup::Lines(), Lines({"Locked while a job is running or paused."}));
	State::SetBabystepZ(0.02f);
	State::CommitOffsets();
	page.applyPressed();
	EXPECT_EQ(Popup::Lines(), Lines({"Pause the job to apply the offset step."}));
	EXPECT_TRUE(sent.empty());

	// The other five stay editable (set up the next fixture while cutting)
	page.wcsPressed(1);
	page.offsetPressed(0);
	EXPECT_TRUE(Popup::IsNumpadOpen());
	Popup::Close();
	page.copyPressed(); // the job's WCS cannot be a target
	Popup::PressFormItem(0, 0);
	EXPECT_EQ(Popup::FormSelection(0), 1u << 2);
	Popup::Close();

	// Paused: APPLY works (the net position does not change)
	State::StatusChanged(OM::PrinterStatus::paused);
	page.applyPressed();
	EXPECT_EQ(Popup::Title(), "APPLY OFFSET");
}

TEST_F(TestCncWcsOffsets, ChangesAfterInitialLoadMarkUnsaved)
{
	auto view = makeView();
	EXPECT_FALSE(State::OffsetsUnsaved()); // the first offsets are the initial load
	State::SetWorkplaceOffset(1, 0, 101.0f);
	EXPECT_FALSE(State::OffsetsUnsaved());
	lv_tick_inc(9000);
	State::SetWorkplaceOffset(1, 0, 102.0f); // e.g. ZERO, probing or DWC later on
	State::CommitOffsets();
	EXPECT_TRUE(State::OffsetsUnsaved());
	EXPECT_TRUE(lv_test_screenshot_compare("cnc/wcs_offsets_unsaved.png"));
}
