#pragma once

#include <Preferences.h>

#include "DashboardView.h"
#include "PaperMonoNfcController.h"
#include "RtcClock.h"
#include "StepCounterController.h"

class AppController {
public:
  void begin();
  void update();

private:
  enum class Screen : uint8_t {
    Dashboard,
    Menu,
    Calendar,
    Goal,
    History,
    ResetImage,
    DeleteAllConfirm,
    ImageLibrary,
    Nfc,
    Message,
  };

  enum class NfcPurpose : uint8_t {
    ReceiveImage,
    SyncClock,
  };

  struct ButtonGesture {
    bool pressed = false;
    bool longHandled = false;
    uint32_t pressedAtMs = 0;
  };

  void initializeImu();
  void configureAccelOnly();
  void updateImuAndSteps(uint32_t now);
  void updateButtons(uint32_t now);
  void handleAShort();
  void handleALong();
  void handleBShort();
  void handleBLong();
  void updateTouch();
  void selectMenuItem(MenuItem item);
  void showDashboard(bool quality = false);
  void showImageLibrary();
  void activateLibraryItem();
  void toggleLibraryDeleteItem();
  uint8_t imageLibraryTotal() const;
  uint8_t imageLibraryPages() const;
  bool fullScreenImageActive() const;
  void enterNfc(NfcPurpose purpose);
  void updateNfc(uint32_t now);
  void cancelNfc(const char* reason);
  void enterLowPower();
  void exitLowPower();
  void updateLowPower(uint32_t now);
  void disableTouch();
  bool recoverTouch(const char* reason);
  void showMessage(const char* title, const char* message);
  bool normalizedTouch(m5::touch_detail_t& touch);

  Preferences preferences_;
  RtcClock clock_;
  StepCounterController steps_;
  PaperMonoNfcController nfc_;
  DashboardView view_{clock_, steps_};

  Screen screen_ = Screen::Dashboard;
  NfcPurpose nfcPurpose_ = NfcPurpose::ReceiveImage;
  MenuItem menuSelection_ = MenuItem::ReceiveImage;
  ButtonGesture buttonA_;
  ButtonGesture buttonB_;
  bool lowPower_ = false;
  bool resetSelected_ = false;
  uint8_t resetAction_ = 0;
  uint8_t libraryPage_ = 0;
  uint8_t libraryFocus_ = 0;
  bool libraryDeleteMode_ = false;
  uint32_t libraryDeleteMask_ = 0;
  bool libraryTouchTracking_ = false;
  int16_t libraryTouchStartX_ = 0;
  int16_t libraryTouchStartY_ = 0;
  bool imuReady_ = false;
  uint32_t candidateGoal_ = PAPER_MONO_DEFAULT_STEP_GOAL;
  uint8_t historyPage_ = 0;
  uint8_t partialRefreshes_ = 0;
  uint32_t lastValuesDrawMs_ = 0;
  uint32_t nextImuSampleMs_ = 0;
  uint32_t nfcStartedAtMs_ = 0;
  uint32_t nfcTimeRevisionAtStart_ = 0;
  uint32_t activeCpuMhz_ = 240;
};
