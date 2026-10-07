/*
 * CncHomePresenter.h
 *
 * Logic of the CNC frame: live DRO, tool, spindle and feed from the object model; ZERO and
 * page switching. Same behaviour as the PanelDue CNC UI (CncUserInterface.cpp).
 */
#pragma once

#include "ObjectModel/Alert.h"
#include "ObjectModel/PrinterStatus.h"
#include <string>
#include "UI/Core/Presenter.h"

namespace UI
{
	class CncHomeView;

	class CncHomePresenter : public Presenter<CncHomeView>
	{
	  public:
		PRESENTER_CONSTRUCTOR(CncHomePresenter, CncHomeView);

		static constexpr size_t MaxDroAxes = 4;		// X Y Z + A

		// Actions
		void zeroAxis(size_t row);					// ZERO button of a DRO row
		void showPage(unsigned int page);			// nav tile: CONTROL WCS JOB MACROS SYSTEM
		void emergencyStop();

		// Observers
		void newAxesData();
		void newWorkplaceNumber();
		void newCurrentTool();
		void newToolData();
		void newAlert(const OM::Alert& alert);
		void newResponse(ResponseType type, const std::string& text);
		void newProbeData();
		void newSpindleData();
		void newRequestedSpeed();
		void newStatus(OM::PrinterStatus status);
		void newNextTool();
		void newJobFileName(const std::string& name);
		void newJobProgress();
		void newJobDuration();
		void newJobTimeLeft();
		void newSpeedFactor();
		void newIpAddress(const std::string& ip);

	  protected:
		void onInit() override;
		void onActivate() override;
		void onDeactivate() override {}

		void onConnect() override {}
		void onDisconnect() override {}

	  private:
		char m_rowLetters[MaxDroAxes] = {'X', 'Y', 'Z', 'A'};
		size_t m_rowCount = 3;
		size_t m_zAxisIndex = 2;					// RRF axis number of Z (tool offsets are by axis)
	};
} // namespace UI
