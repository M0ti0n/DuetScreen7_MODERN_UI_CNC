/*
 * CncLayout.h
 *
 * Geometry of the portrait CNC UI on DuetScreen (600 x 1024 after the 90 degree rotation).
 *
 * The values are the PanelDue CNC UI's (PanelDue_Modern_UI_CNC, src/UI/Cnc/CncLayout.hpp,
 * 480 x 800) scaled by 1.25. Edges are scaled, not sizes, so neighbouring tiles keep their exact
 * gaps. The panel is 24 px taller than 800 x 1.25: the nav bar stays anchored to the bottom and
 * the page content gets the extra height.
 *
 * Rule (as on PanelDue): page code uses these names, never raw numbers.
 */
#pragma once

#include <cstdint>

namespace UI::CncLayout
{
	constexpr int32_t ScreenW = 600, ScreenH = 1024;

	// PanelDue x / top-area y -> DuetScreen pixels (x 1.25, rounded)
	constexpr int32_t S(int32_t v) { return (v * 5 + 2) / 4; }
	// Width / height of a span [v, v + len) after scaling its edges
	constexpr int32_t SL(int32_t v, int32_t len) { return S(v + len) - S(v); }
	// PanelDue y in the bottom area (nav bar, sub-tabs): anchored to the bottom of the panel
	constexpr int32_t SB(int32_t v) { return ScreenH - S(800 - v); }

	constexpr int32_t Extra = ScreenH - S(800);		// 24 px: goes to the page content

	constexpr int32_t Margin = S(6);				// 8
	constexpr int32_t ContentW = S(474) - Margin;	// 585

	// Cards: PanelDue draws a 2 px border with UTFT's small rounded corner
	constexpr int32_t CardBorder = 2, CardRadius = 3;

	// Full DRO (CONTROL, WCS): PanelDue values, scaled where used
	namespace Dro
	{
		constexpr int32_t HeaderY = 6;
		constexpr int32_t RowsTop = 28;
		constexpr int32_t RowH3 = 68, Gap3 = 7;		// 3 axes
		constexpr int32_t RowH4 = 50, Gap4 = 6;		// 4 axes
		constexpr int32_t AxisLabelX = 6, AxisLabelW = 40;
		constexpr int32_t WorkX = 50, WorkW = 180;
		constexpr int32_t MachX = 238, MachW = 140;
		constexpr int32_t ZeroX = 386, ZeroW = 88;
		constexpr int32_t TsfY = 253, TsfH = 50;
		constexpr int32_t ToolX = 6, ToolW = 104;
		constexpr int32_t SpindleX = 118, SpindleW = 178;
		constexpr int32_t FeedX = 304, FeedW = 170;
		constexpr int32_t SeparatorY = 311, SeparatorH = 3;
	}

	// Page content (full DRO pages): from under the separator to above the sub-tabs / nav bar
	constexpr int32_t ContentTop = S(Dro::SeparatorY + Dro::SeparatorH + 7);	// 401

	// Bottom navigation: 5 page tiles + STOP, square tiles, symmetric margins
	namespace Nav
	{
		constexpr int32_t Y = SB(721), Tile = S(73);		// 925, 91
		constexpr unsigned int Count = 6;
		constexpr int32_t X(unsigned int i) { return Margin + (int32_t)((i * (ContentW - Tile) * 2 + (Count - 1)) / (2 * (Count - 1))); }
	}

	// CONTROL page, PanelDue values (scale with S / SL where used)
	namespace Control
	{
		constexpr int32_t ContentTopPd = Dro::SeparatorY + Dro::SeparatorH + 7;	// 321
		constexpr int32_t LabelH = 23;				// label row incl. 2 px gap
		constexpr int32_t RowH = 52;				// button rows
		constexpr int32_t GroupGap = 8;				// between a row and the next label
		constexpr int32_t ActionGap = 8;			// between the two ACTION rows
		constexpr int32_t LeftX = 6, LeftW = 358;	// left block
		constexpr int32_t ColGap = 10;				// gap between buttons in a row
		constexpr int32_t HomeX = Dro::ZeroX, HomeW = Dro::ZeroW;	// home column under the DRO ZERO column
		constexpr int32_t SeparatorW = Dro::SeparatorH;
		constexpr int32_t SepX = LeftX + LeftW + (HomeX - LeftX - LeftW - SeparatorW) / 2;	// vertical accent line

		constexpr int32_t LabelAxisY = ContentTopPd;					// 321
		constexpr int32_t RowAxisY = LabelAxisY + LabelH;				// 344
		constexpr int32_t LabelStepY = RowAxisY + RowH + GroupGap;		// 404
		constexpr int32_t RowStepY = LabelStepY + LabelH;				// 427
		constexpr int32_t LabelMoveY = RowStepY + RowH + GroupGap;		// 487
		constexpr int32_t RowMoveY = LabelMoveY + LabelH;				// 510
		constexpr int32_t LabelActY = RowMoveY + RowH + GroupGap;		// 570
		constexpr int32_t RowAct1Y = LabelActY + LabelH;				// 593
		constexpr int32_t RowAct2Y = RowAct1Y + RowH + ActionGap;		// 653
		constexpr int32_t Bottom = RowAct2Y + RowH;						// 705

		// Home column: 5 buttons evenly spaced from the AXIS row to the bottom of the last ACTION row
		constexpr int32_t HomePitch = (Bottom - RowH - RowAxisY) / 4;	// 77
	}

	// WCS > OFFSETS page, PanelDue values (scale with S / SL where used); same split as CONTROL
	namespace Offsets
	{
		constexpr int32_t ContentTopPd = Control::ContentTopPd;				// 321
		constexpr int32_t ContentBottomPd = 657;							// above the sub-tabs
		constexpr int32_t LabelH = Control::LabelH, Gap = 8;
		constexpr int32_t LeftX = Control::LeftX, LeftW = Control::LeftW;
		constexpr int32_t RightX = Control::HomeX, RightW = Control::HomeW;
		constexpr int32_t SepX = Control::SepX, SeparatorW = Control::SeparatorW;

		constexpr int32_t LabelWcsY = ContentTopPd;							// 321
		constexpr int32_t RowWcsY = LabelWcsY + LabelH;						// 344
		constexpr int32_t RowWcsH = 52, WcsGap = 6;
		constexpr int32_t LabelGridY = RowWcsY + RowWcsH + Gap;				// 404
		constexpr int32_t GridY = LabelGridY + LabelH;						// 427
		constexpr int32_t CellH = 53, CellGap = 8;
		constexpr int32_t CellW = (LeftW - CellGap) / 2;					// 175
		constexpr int32_t CellLetterW = 26;
		constexpr int32_t ActY = GridY + 2 * CellH + CellGap + Gap;			// 549
		constexpr int32_t Bottom = ContentBottomPd;							// 657
		constexpr int32_t ActGap = 8;
		constexpr int32_t ActH = (Bottom - ActY - ActGap) / 2;				// 50

		// OFFSET column (offset step): Z+, total, Z-, STEP (size), CLEAR
		constexpr int32_t StepUpY = RowWcsY, StepBtnH = 52;					// Z+ 344, level with the G5x row
		constexpr int32_t StepTotalY = StepUpY + StepBtnH + 8, StepTotalH = 44;
		constexpr int32_t StepDownY = StepTotalY + StepTotalH + 8;			// Z-
		constexpr int32_t StepSizeLabelY = StepDownY + StepBtnH + 8;
		constexpr int32_t StepSizeY = StepSizeLabelY + LabelH, StepSizeH = 44;
		constexpr int32_t StepClearY = Bottom - ActH, StepClearH = ActH;	// CLEAR, level with the last action row
	}

	// WCS > TOOLS page, PanelDue values
	namespace Tools
	{
		constexpr int32_t LabelY = Control::ContentTopPd;					// 321: TOOLS / TOOL NAME / Z OFFSET
		constexpr int32_t RowsY = LabelY + Control::LabelH;					// 344
		constexpr int32_t RowH = 54, RowGap = 8;
		constexpr unsigned int NumRows = 4;
		constexpr int32_t RowY(unsigned int r) { return RowsY + (int32_t)r * (RowH + RowGap); }	// 344 406 468 530
		constexpr int32_t NumX = 6, NumW = 60;								// T# button
		constexpr int32_t NameX = NumX + NumW + 8, NameW = 204;				// 74
		constexpr int32_t ZX = NameX + NameW + 8, ZW = 122;					// 286
		constexpr int32_t ArrowX = ZX + ZW + 8, ArrowW = 480 - 6 - ArrowX;	// 416, 58
		constexpr int32_t ArrowH = 2 * RowH + RowGap;						// 116: two rows tall
		constexpr int32_t ActH = 52, ActY = Offsets::ContentBottomPd - ActH;	// 605
		constexpr int32_t ActGap = 10, ActW = (468 - 2 * ActGap) / 3;		// 149
	}

	// WCS > PROBE page, PanelDue values
	namespace Probe
	{
		constexpr int32_t LabelY = Control::ContentTopPd;					// 321: ORIGIN / MODE
		constexpr int32_t GridY = LabelY + Control::LabelH;					// 344
		constexpr int32_t BtnW = 66, BtnH = 52, BtnGap = 6, BigBtnH = 72;
		constexpr int32_t GridX = 6;
		constexpr int32_t BigRowY = GridY + 3 * (BtnH + BtnGap);			// 518: BORE BOSS Z TOP
		constexpr int32_t PickBottom = BigRowY + BigBtnH;					// 590
		constexpr int32_t SeparatorW = Control::SeparatorW;
		constexpr int32_t SepX = GridX + 3 * BtnW + 2 * BtnGap + 9;			// 225
		constexpr int32_t ParX = SepX + SeparatorW + 12, ParW = 480 - 6 - ParX;	// 240, 234
		constexpr int32_t TileGap = 8;
		constexpr int32_t AutoTileW = (ParW - TileGap) / 2;					// 113
		constexpr int32_t AutoTileH = (PickBottom - GridY - 2 * TileGap) / 3;	// 76
		constexpr int32_t RowTileH = 52, RowTileGap = 6;					// SEMI / MANUAL parameter list tiles
		constexpr int32_t BottomY = 601, BottomH = Offsets::ContentBottomPd - BottomY;	// 56
		constexpr int32_t RowW = 468;								// the content width (PanelDue ContentW)

		// Popups under the full DRO (auto probe progress, jog prompt), 460 x 396 at (10, 318)
		constexpr int32_t PopX = 10, PopY = 318, PopW = 460, PopH = 396;
		constexpr int32_t TitleX = 105, TitleY = 14, TitleW = 250, TitleH = 46;
		constexpr int32_t PopBtnW = 110, PopBtnH = 78, PopBtnY = PopH - 14 - PopBtnH;	// 304
		constexpr int32_t PopCancelX = (PopW - 2 * PopBtnW - 20) / 2, PopOkX = PopCancelX + PopBtnW + 20;
		constexpr int32_t StopW = 3 * PopBtnW;								// wide emergency STOP
	}

	// Compact DRO (JOB, MACROS, SYSTEM), read-only: PanelDue values, scaled where used
	//   3 axes: X Y Z in row 1, T S F in row 2
	//   4 axes: X Y Z in row 1, A (under X) T S F in row 2 (layout B)
	namespace Compact
	{
		constexpr int32_t HeaderY = 4, HeaderX = 32, HeaderW = 200;
		constexpr int32_t Row1Y = 25, Row2Y = 77, RowH = 44;
		constexpr int32_t LetterW = 26, AxisTileW = 123, AxisPitch = 159;
		constexpr int32_t AxisX(unsigned int i) { return 7 + (int32_t)i * AxisPitch; }	// letter x; tile at +26
		constexpr int32_t T3X = 7, T3W = 102, S3X = 119, S3W = 172, F3X = 301, F3W = 172;		// row 2, 3 axes
		constexpr int32_t T4X = 164, T4W = 62, S4X = 234, S4W = 116, F4X = 358, F4W = 116;	// row 2, 4 axes
		constexpr int32_t SeparatorY = 130, SeparatorH = 3;
	}

	// Page content of the compact pages
	constexpr int32_t CompactContentTop = S(Compact::SeparatorY + 12);	// 178 (PanelDue 142)

	// JOB > JOB STATUS page, PanelDue values (under the compact DRO)
	//   file name + progress / result, ELAPSED / LEFT, FEED WORK and SPINDLE override rows,
	//   COOLANT / CUSTOM 2 / CUSTOM 3, tool change row (only when needed), PAUSE / ABORT
	namespace Job
	{
		constexpr int32_t Gap = 8;
		constexpr int32_t NameY = Compact::SeparatorY + 14, NameH = 56;			// 144
		constexpr int32_t NameX = 6, NameW = 350;
		constexpr int32_t ResultX = NameX + NameW + 8, ResultW = 474 - ResultX;	// 364, 110
		constexpr int32_t TimesY = NameY + NameH + Gap, TimesH = 50;			// 208
		constexpr int32_t TimeW = (468 - 8) / 2;								// 230
		constexpr int32_t ElapsedX = 6, LeftX = 6 + TimeW + 8;					// 6, 244
		constexpr int32_t TimeLabelX = 12, TimeLabelW = 96;						// inside the card
		constexpr int32_t TimeValueX = 100, TimeValueInset = 12;
		constexpr int32_t LabelH = 22;
		constexpr int32_t OvrH = 60;
		constexpr int32_t Label1Y = TimesY + TimesH + Gap;						// 266 FEED WORK
		constexpr int32_t Ovr1Y = Label1Y + LabelH;								// 288
		constexpr int32_t Label2Y = Ovr1Y + OvrH + Gap;							// 356 SPINDLE
		constexpr int32_t Ovr2Y = Label2Y + LabelH;								// 378
		constexpr int32_t MinusX = 6, PmW = 50;
		constexpr int32_t ValueX = MinusX + PmW + 6, ValueW = 80;				// 62
		constexpr int32_t PlusX = ValueX + ValueW + 6;							// 148
		constexpr int32_t ActualX = PlusX + PmW + 8, ActualW = 112;				// 206
		constexpr int32_t StepX = ActualX + ActualW + 8, StepW = 46, StepPitch = 50;	// 326
		constexpr int32_t BtnY = Ovr2Y + OvrH + Gap, BtnH = 58;					// 446 COOLANT CUSTOM 2 CUSTOM 3
		constexpr int32_t BtnW = (468 - 16) / 3;								// 150
		constexpr int32_t BtnX(unsigned int i) { return 6 + (int32_t)i * (BtnW + 8); }
		constexpr int32_t TcY = BtnY + BtnH + Gap, TcH = 76;					// 512 tool change row
		constexpr int32_t TcX = 6, TcW = 468;
		constexpr int32_t TcOkW = 96, TcOkH = 60;
		constexpr int32_t TcOkX = 474 - 8 - TcOkW, TcOkY = TcY + (TcH - TcOkH) / 2;	// 370
		constexpr int32_t PauseY = 598, PauseH = 56;
		constexpr int32_t PauseX = 6, PauseW = 230;
		constexpr int32_t AbortW = 150, AbortX = 474 - AbortW;					// 324
	}

	// JOB > JOB LIST page, PanelDue values (under the compact DRO)
	namespace JobList
	{
		constexpr int32_t LabelY = Compact::SeparatorY + 12;					// 142
		constexpr int32_t RowsY = 165, RowH = 40, RowPitch = 47;
		constexpr unsigned int NumRows = 10;									// last row 588 .. 627
		constexpr unsigned int NumRowsKeyboard = 3;								// rows above the keyboard sheet
		constexpr int32_t RowY(unsigned int r) { return RowsY + (int32_t)r * RowPitch; }
		constexpr int32_t RowX = 6, RowW = 400;
		constexpr int32_t SideW = 54, SideX = 480 - 6 - SideW;					// 420
		constexpr int32_t LabelW = RowW;										// the path ends above the rows
		constexpr int32_t SearchY = RowsY, SearchH = 87;						// same size as SD
		constexpr int32_t UpY = SearchY + SearchH + 7, ArrowH = 134;			// 259
		constexpr int32_t DownY = UpY + ArrowH + 7;								// 400
		constexpr int32_t SdY = DownY + ArrowH + 7, SdH = SearchH;				// 541 .. 627
		constexpr int32_t EmptyY = RowY(2);										// "Loading...", "No files": in row 2
		// label row: left word (FILES / SEARCH), query (search only), right text (path / counts)
		constexpr int32_t LabelLeftX = RowX + 2, LabelLeftW = 84;
		constexpr int32_t QueryX = LabelLeftX + LabelLeftW + 2, QueryW = 128;	// 94
		constexpr int32_t QueryTextW = QueryW - 30;								// room for the quotes and ".."
		constexpr int32_t RightX = QueryX, RightW = RowX + LabelW - RightX;		// FILES: path from x 94
		constexpr int32_t RightSearchX = QueryX + QueryW + 4, RightSearchW = RowX + LabelW - RightSearchX;
	}

	// SYSTEM > SETTINGS, PanelDue values: IP + FACTORY RESET, section label + "PAGE n / 3", 2 x 6 tiles,
	// page arrows. Page 3 (JOG DIRECTION, new): one row per axis (letter, icon pair, + side) and the "°" tile
	namespace Settings
	{
		constexpr int32_t TopY = Compact::SeparatorY + 12, TopH = 44;			// 142 .. 185
		constexpr int32_t ColW = 229, ColGap = 10;
		constexpr int32_t ColX(unsigned int c) { return 6 + (int32_t)c * (ColW + ColGap); }	// 6, 245
		constexpr int32_t IpLabelDx = 12, IpLabelW = 40;						// "IP" muted, left
		constexpr int32_t IpTextW = ColW - IpLabelDx - IpLabelW - 12;			// address, right aligned
		constexpr int32_t SectionY = 195, SectionW = 220;
		constexpr int32_t SectionLeftX = 6 + 2, SectionRightX = 480 - 6 - 2 - SectionW;
		constexpr int32_t TilesY = 218, TileH = 56, TilePitch = 62;			// rows 218 .. 528
		constexpr unsigned int NumRows = 6, NumTiles = 2 * NumRows;
		constexpr int32_t TileY(unsigned int i) { return TilesY + (int32_t)(i / 2) * TilePitch; }
		constexpr int32_t TileX(unsigned int i) { return ColX(i % 2); }
		constexpr int32_t ArrowY = 596, ArrowH = 56, ArrowW = 150;
		constexpr int32_t UpX = 80, DownX = 250;
		// Page 3
		constexpr int32_t JogLabelY = 226;										// ICONS / + MOVES
		constexpr int32_t JogRowY0 = 250, JogRowH = 56, JogRowPitch = 62;
		constexpr int32_t JogRowY(unsigned int i) { return JogRowY0 + (int32_t)i * JogRowPitch; }
		constexpr int32_t LetterX = 6, LetterW = 53;
		constexpr int32_t PairX = LetterX + LetterW + 8, PairW = 350 - PairX;	// 67 .. 350
		constexpr int32_t PlusX = 360, PlusW = 474 - PlusX;					// 360 .. 474
		constexpr unsigned int MaxJogRows = 4;
		constexpr int32_t DegreeY = JogRowY(MaxJogRows), DegreeW = 468;		// 498: "°" on CCW / CW axes
	}

	// SYSTEM > CONSOLE, PanelDue values (under the compact DRO): label row, log card with 15 lines
	// (age + text), input line and KEYBOARD button
	namespace Console
	{
		constexpr int32_t LabelY = Compact::SeparatorY + 12;					// 142: CONSOLE / NEWEST FIRST
		constexpr int32_t LabelW = 200, LabelLeftX = 6 + 2, LabelRightX = 480 - 6 - 2 - LabelW;
		constexpr int32_t LogX = 6 + 1, LogW = 468 - 2;							// 7 .. 472
		constexpr int32_t LogY = 167, LogH = 413;								// .. 579
		constexpr unsigned int NumLines = 15;
		constexpr int32_t LineY0 = 175, LinePitch = 27;						// text tops (21 px font)
		constexpr int32_t LineY(unsigned int i) { return LineY0 + (int32_t)i * LinePitch; }	// last 553
		constexpr int32_t TimeX = LogX + 2, TimeW = 64;							// age ("59m59"), ends at 72
		constexpr int32_t TextX = LogX + 76, TextW = LogX + LogW - 8 - TextX;	// 83 .. 464
		constexpr unsigned int LinesAboveKeyboard = 7;							// PanelDue 6; the sheet sits 24 px lower here
		constexpr int32_t InputY = 602, InputH = 54;
		constexpr int32_t InputX = LogX, InputW = 386;							// "Tap to enter G-code..."
		constexpr int32_t KbButtonX = InputX + InputW + 10, KbButtonW = LogX + LogW - KbButtonX;	// 403, 70
	}

	// SYSTEM > ALERT, PanelDue values (under the compact DRO): label row, 10 rows per page, page bar
	// (PanelDue: 11 rows on one page; here 5 pages of 10, the bar uses the room of row 11)
	namespace Alerts
	{
		constexpr int32_t LabelY = Compact::SeparatorY + 12;					// 142: ALERTS / "n / 50   NEWEST FIRST"
		constexpr int32_t LabelLeftX = 6 + 2, LabelLeftW = 100;
		constexpr int32_t LabelRightW = 300, LabelRightX = 480 - 6 - 2 - LabelRightW;
		constexpr unsigned int NumRows = 10, NumPages = 5;
		constexpr unsigned int MaxAlerts = NumRows * NumPages;					// 50
		constexpr int32_t RowX = 6 + 1, RowW = 468 - 2;							// 7 .. 472
		constexpr int32_t RowY0 = 165, RowH = 38, RowPitch = 44;				// last row 561 .. 598
		constexpr int32_t RowY(unsigned int i) { return RowY0 + (int32_t)i * RowPitch; }
		constexpr int32_t BadgeDx = 5, BadgeW = 68, BadgeH = 26;				// ERR / WARN badge
		constexpr int32_t AgeX = RowX + BadgeDx + BadgeW + 4, AgeW = 64;		// "59m59"
		constexpr int32_t TextDx = AgeX + AgeW + 8 - RowX;						// text from x 156
		constexpr int32_t DotDx = RowW - 11, DotR = 4;							// unread dot
		constexpr int32_t EmptyY = RowY(2);										// "No alerts"
		constexpr int32_t BarY = RowY(NumRows) + 2, BarH = 48;					// 607 .. 654: page bar
		constexpr int32_t ArrowW = 104;											// ^ at the left, v at the right
		constexpr int32_t UpX = RowX, DownX = RowX + RowW - ArrowW;
	}

	// MACROS page, PanelDue values (under the compact DRO, no sub-tabs): label row, list (11 rows) or
	// grid (2 x 5 tiles) on the left, side column SEARCH / up / down / LIST-GRID (as JOB LIST)
	namespace Macros
	{
		constexpr int32_t LabelY = JobList::LabelY;								// 142: MACROS + path, or SEARCH "text" + counts
		constexpr int32_t LabelLeftX = 6 + 2, LabelLeftW = 92;					// "MACROS" is 89 px
		constexpr int32_t QueryX = LabelLeftX + LabelLeftW + 2, QueryW = 128, QueryTextW = QueryW - 30;
		constexpr int32_t PathX = QueryX, PathW = JobList::RowX + JobList::LabelW - PathX;
		constexpr int32_t CountX = QueryX + QueryW + 4, CountW = JobList::RowX + JobList::LabelW - CountX;
		constexpr int32_t AreaX = JobList::RowX, AreaW = JobList::RowW, AreaY = JobList::RowsY;	// 6, 400, 165
		constexpr unsigned int ListRows = 11;									// last row 635 .. 674
		constexpr int32_t ListRowH = JobList::RowH, ListPitch = JobList::RowPitch;
		constexpr unsigned int GridCols = 2, GridRows = 5;						// 10 tiles
		constexpr int32_t GridGap = 10, GridTileW = (AreaW - GridGap) / 2, GridTileH = 94, GridPitchY = 104;	// 195 wide
		constexpr unsigned int NumCells = ListRows;								// rows or tiles
		constexpr int32_t SideX = JobList::SideX, SideW = JobList::SideW;
		constexpr int32_t SearchY = JobList::SearchY, SearchH = JobList::SearchH;
		constexpr int32_t ToggleY = JobList::SdY, ToggleH = JobList::SdH;		// LIST / GRID: in the SD slot
		constexpr unsigned int ListRowsKeyboard = 3, GridRowsKeyboard = 1;		// above the keyboard sheet
		constexpr int32_t UpY = JobList::UpY, DownY = JobList::DownY, ArrowH = JobList::ArrowH;
		constexpr int32_t EmptyY = AreaY + 2 * ListPitch;						// message in row 2 (list)
		constexpr int32_t EmptyGridY = AreaY + GridPitchY;						// tile row 1 (grid)
		constexpr int32_t EmptyGridKbY = AreaY;									// grid while typing: tile row 0
	}

	// Shared keyboard sheet (JOB LIST search, SYSTEM > CONSOLE), PanelDue values inside the sheet.
	// The sheet ends 6 px above the nav bar, so it is anchored to the bottom of the panel like the nav.
	namespace Keyboard
	{
		constexpr int32_t H = 374, TopPd = 721 - 6 - H;							// 341 .. 714
		constexpr int32_t TopY = 11, TopH = 50;									// arrows, text field, X
		constexpr int32_t UpX = 11, DownX = 59, ArrowW = 42;
		constexpr int32_t FieldX = 107, FieldW = 298;
		constexpr int32_t FieldTextInset = 11, FieldTextW = FieldW - 16;		// text inside the field
		constexpr int32_t FieldTextAvail = FieldW - 24;							// text width left for the cursor
		constexpr int32_t CloseX = 412, CloseY = 10, CloseW = 58, CloseH = 52;
		constexpr int32_t KeysY = 73, KeyH = 50, KeyPitchY = 57;
		constexpr int32_t KeyW = 40, KeyPitchX = 47;
		constexpr int32_t KeyRowY(unsigned int r) { return KeysY + (int32_t)r * KeyPitchY; }	// 73 130 187 244 301
		constexpr int32_t Row0X = 11, Row2X = 34, Row3X = 82;					// digits/QWERTY, ASDF, ZXCV
		constexpr int32_t WideW = 64;											// shift, backspace, #+=
		constexpr int32_t BackX = 412;
		constexpr int32_t DotX = 82, SpaceX = 176, SpaceW = 206;
		constexpr int32_t EnterX = 388, EnterW = 88, EnterY = 300, EnterH = 52;	// 1 px taller each side (mock-up)
		constexpr int32_t ScreenY = SB(TopPd);									// top of the sheet on the panel
	}

	// Sub-tab strip above the nav bar (WCS, JOB, SYSTEM)
	constexpr int32_t SubTabY = SB(665), SubTabH = S(50);
	constexpr int32_t SubTabXPd(unsigned int i, unsigned int n) { return 6 + (int32_t)((i * 468) / n); }	// PanelDue SubTabX
	constexpr int32_t ContentBottom = SubTabY - S(8);			// pages with sub-tabs
	constexpr int32_t ContentBottomNoTabs = SB(710);			// pages without

	// JOB > PREVIEW (new with DuetScreen, no PanelDue page): screen pixels inside the content area
	// (CompactContentTop .. ContentBottom = 585 x 667). 3D view left, operations right, info below.
	namespace Preview
	{
		constexpr int32_t Left = 8, Right = ScreenW - 8, Gap = 8;
		constexpr int32_t InfoH = 120;
		constexpr int32_t Height = ContentBottom - CompactContentTop;			// 667
		constexpr int32_t ViewX = Left, ViewW = 474;
		constexpr int32_t ViewH = Height - InfoH - Gap;							// 539
		constexpr int32_t StripX = ViewX + ViewW + Gap, StripW = Right - StripX;	// 490, 102
		constexpr int32_t ArrowH = 60;
		constexpr int32_t NumOpButtons = 3;
		constexpr int32_t OpH = (ViewH - 2 * ArrowH - 4 * Gap) / NumOpButtons;	// 129
		constexpr int32_t InfoY = ViewH + Gap, InfoW = Right - Left;			// 547, 584
		// overlays in the view (inside its border)
		constexpr int32_t OverlayInset = 12;
		constexpr int32_t ButtonW = 90, ButtonH = 46, ButtonGap = 8;			// ISO / TOP / FRONT, G0
		constexpr int32_t InsetTop = 70, InsetBottom = 44;						// kept free when fitting
	}
}
