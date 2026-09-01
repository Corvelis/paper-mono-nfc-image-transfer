#pragma once

#include <stdint.h>

#include "PaperMonoNfcController.h"
#include "RtcClock.h"
#include "StepCounterController.h"

enum class MenuItem : uint8_t {
  ReceiveImage = 0,
  SyncClock,
  StepGoal,
  StepHistory,
  ResetImage,
  Back,
  Count,
};

class DashboardView {
public:
  DashboardView(RtcClock& clock, StepCounterController& steps);

  void drawDashboard(bool locked,
                     const PaperMonoNfcController::StoredImage* storedImage,
                     bool quality = false);
  void drawValuesPartial(bool locked);
  void drawMonthCalendar();
  void drawMenu(MenuItem selected);
  void drawGoalEditor(uint32_t candidateGoal);
  void drawHistory(uint8_t page);
  void drawResetConfirmation(bool resetSelected);
  void drawNfcWaiting(bool clockOnly);
  void drawMessage(const char* title, const char* message, const char* footer);

  MenuItem menuItemAt(int32_t x, int32_t y) const;
  bool dateHit(int32_t x, int32_t y) const;
  bool stepsHit(int32_t x, int32_t y) const;
  bool resetHit(int32_t x, int32_t y) const;
  uint8_t historyPageCount() const;

private:
  bool drawImage(const PaperMonoNfcController::StoredImage* storedImage);
  void drawTopBar();
  void drawDashboardValues();
  void drawStepProgress();
  void formatSteps(uint32_t steps, char* output, size_t outputSize) const;

  RtcClock& clock_;
  StepCounterController& steps_;
};
