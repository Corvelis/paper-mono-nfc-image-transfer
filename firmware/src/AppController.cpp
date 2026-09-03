#include "AppController.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <M5Unified.h>
#include <utility/M5IOE1_Class.hpp>

namespace {
constexpr uint32_t kLongPressMs = 700;
constexpr uint8_t kButtonAPin = 2;
constexpr uint8_t kButtonBPin = 3;
}  // namespace

void AppController::begin() {
  Serial.begin(115200);
  delay(100);

  auto config = M5.config();
  config.internal_imu = false;
  config.internal_mic = false;
  config.internal_spk = false;
  config.internal_rtc = true;
  config.clear_display = false;
  M5.begin(config);

  M5.Display.setRotation(0);
  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  M5.Display.setBrightness(PAPER_MONO_FRONT_LIGHT_BRIGHTNESS);
  pinMode(kButtonAPin, INPUT_PULLUP);
  pinMode(kButtonBPin, INPUT_PULLUP);

  M5.Led.setAutoDisplay(false);
  M5.Led.setBrightness(0);
  M5.Led.setAllColor(0, 0, 0);
  M5.Led.display();

  if (!LittleFS.begin(true)) {
    Serial.println("[storage] LittleFS mount failed");
  }
  clock_.begin();
  steps_.begin(&preferences_);
  nfc_.setTimeSyncHandler(&RtcClock::nfcTimeSync, &clock_);
  nfc_.begin();
  initializeImu();
  configureAccelOnly();

  showDashboard(true);
  lastValuesDrawMs_ = millis();
  nextImuSampleMs_ = millis();
  Serial.printf("[app] ready version=%s board=%d display=%dx%d rtc=%d imu=%d\n",
                PAPER_MONO_RELEASE_VERSION, static_cast<int>(M5.getBoard()), M5.Display.width(),
                M5.Display.height(), clock_.valid() ? 1 : 0,
                imuReady_ ? 1 : 0);
}

void AppController::initializeImu() {
  constexpr uint8_t kAttempts = 3;
  for (uint8_t attempt = 0; attempt < kAttempts && !imuReady_; ++attempt) {
    const bool found = M5.Imu.begin(&M5.In_I2C, M5.getBoard());
    const uint32_t started = millis();
    while (found && millis() - started < 500) {
      const auto updated = M5.Imu.update();
      if ((updated & m5::IMU_Class::sensor_mask_accel) != 0) {
        imuReady_ = true;
        break;
      }
      delay(10);
    }
    if (!imuReady_) {
      delay(120);
    }
  }
}

void AppController::configureAccelOnly() {
  if (!imuReady_ || M5.Imu.getType() != m5::imu_bmi270) {
    return;
  }
  // BMI270 PWR_CTRL: bit 2 keeps the accelerometer enabled. Gyroscope,
  // temperature and auxiliary sensing are unnecessary for step detection.
  constexpr uint8_t kBmi270Address = 0x69;
  constexpr uint8_t kPowerControlRegister = 0x7D;
  constexpr uint8_t kAccelerometerOnly = 0x04;
  const bool configured = M5.In_I2C.writeRegister8(
      kBmi270Address, kPowerControlRegister, kAccelerometerOnly, 400000);
  delay(2);
  Serial.printf("[imu] accelerometer-only=%d\n", configured ? 1 : 0);
}

void AppController::updateImuAndSteps(uint32_t now) {
  if (static_cast<int32_t>(now - nextImuSampleMs_) < 0) {
    return;
  }
  nextImuSampleMs_ = now + PAPER_MONO_IMU_SAMPLE_INTERVAL_MS;
  m5::imu_data_t data = {};
  bool accelUpdated = false;
  if (imuReady_) {
    const auto updated = M5.Imu.update();
    if ((updated & m5::IMU_Class::sensor_mask_accel) != 0) {
      M5.Imu.getImuData(&data);
      accelUpdated = true;
    }
  }
  uint32_t localUnix = 0;
  const bool timeValid = clock_.localUnix(localUnix);
  steps_.update(now, data, accelUpdated, localUnix, timeValid);
}

void AppController::showDashboard(bool quality) {
  PaperMonoNfcController::StoredImage image;
  const bool hasImage = nfc_.getStoredImage(image);
  view_.drawDashboard(lowPower_, hasImage ? &image : nullptr, quality);
  screen_ = Screen::Dashboard;
  lastValuesDrawMs_ = millis();
}

uint8_t AppController::imageLibraryTotal() const {
  return static_cast<uint8_t>(nfc_.storedImageCount() + 1);
}

uint8_t AppController::imageLibraryPages() const {
  return max<uint8_t>(1, static_cast<uint8_t>((imageLibraryTotal() + 5) / 6));
}

bool AppController::fullScreenImageActive() const {
  PaperMonoNfcController::StoredImage image;
  return nfc_.getStoredImage(image) &&
         image.mode == PaperMonoNfcProtocol::ImageMode::Fullscreen;
}

void AppController::showImageLibrary() {
  const uint8_t total = imageLibraryTotal();
  libraryFocus_ = min<uint8_t>(libraryFocus_, total - 1);
  libraryPage_ = min<uint8_t>(libraryFocus_ / 6, imageLibraryPages() - 1);
  screen_ = Screen::ImageLibrary;
  view_.drawImageLibrary(nfc_, libraryPage_, libraryFocus_,
                         libraryDeleteMode_, libraryDeleteMask_);
}

void AppController::activateLibraryItem() {
  const uint8_t received = nfc_.storedImageCount();
  const bool selected = libraryFocus_ < received
                            ? nfc_.selectStoredImageAt(libraryFocus_)
                            : libraryFocus_ == received && nfc_.selectDefaultImage();
  if (selected) showDashboard(true);
  else showMessage("IMAGE LIBRARY", "COULD NOT SELECT IMAGE");
}

void AppController::toggleLibraryDeleteItem() {
  if (libraryFocus_ >= nfc_.storedImageCount()) return;
  libraryDeleteMask_ ^= 1UL << libraryFocus_;
  showImageLibrary();
}

void AppController::showMessage(const char* title, const char* message) {
  nfc_.powerDown();
  recoverTouch("message");
  view_.drawMessage(title, message, "B: BACK");
  screen_ = Screen::Message;
}

void AppController::updateButtons(uint32_t now) {
  const auto updateOne = [now](auto& button,
                               ButtonGesture& gesture,
                               auto&& onShort,
                               auto&& onLong) {
    if (button.wasPressed()) {
      gesture.pressed = true;
      gesture.longHandled = false;
      gesture.pressedAtMs = now;
    }
    if (gesture.pressed && button.isPressed() && !gesture.longHandled &&
        now - gesture.pressedAtMs >= kLongPressMs) {
      gesture.longHandled = true;
      onLong();
    }
    if (gesture.pressed && button.wasReleased()) {
      const bool wasLong = gesture.longHandled;
      gesture = {};
      if (!wasLong) {
        onShort();
      }
    }
  };
  updateOne(M5.BtnA, buttonA_, [this]() { handleAShort(); },
            [this]() { handleALong(); });
  updateOne(M5.BtnB, buttonB_, [this]() { handleBShort(); },
            [this]() { handleBLong(); });
}

void AppController::handleAShort() {
  if (lowPower_) {
    return;
  }
  switch (screen_) {
    case Screen::Menu:
      menuSelection_ = static_cast<MenuItem>(
          (static_cast<uint8_t>(menuSelection_) + 1) %
          static_cast<uint8_t>(MenuItem::Count));
      view_.drawMenu(menuSelection_);
      break;
    case Screen::Goal:
      candidateGoal_ = candidateGoal_ <= PAPER_MONO_MIN_STEP_GOAL
                           ? PAPER_MONO_MAX_STEP_GOAL
                           : candidateGoal_ - PAPER_MONO_STEP_GOAL_INCREMENT;
      view_.drawGoalEditor(candidateGoal_);
      break;
    case Screen::History:
      historyPage_ = min<uint8_t>(historyPage_ + 1,
                                  view_.historyPageCount() - 1);
      view_.drawHistory(historyPage_);
      break;
    case Screen::ResetImage:
      resetAction_ = (resetAction_ + 1) % 3;
      view_.drawResetImageMenu(resetAction_);
      break;
    case Screen::DeleteAllConfirm:
      resetSelected_ = !resetSelected_;
      view_.drawDeleteAllConfirmation(resetSelected_);
      break;
    case Screen::ImageLibrary:
      libraryFocus_ = (libraryFocus_ + 1) % imageLibraryTotal();
      libraryPage_ = libraryFocus_ / 6;
      showImageLibrary();
      break;
    default:
      break;
  }
}

void AppController::handleALong() {
  if (lowPower_) {
    return;
  }
  if (screen_ == Screen::Nfc) {
    cancelNfc("button-a-hold");
    return;
  }
  if (screen_ == Screen::ImageLibrary) {
    if (libraryDeleteMode_) {
      libraryDeleteMode_ = false;
      libraryDeleteMask_ = 0;
      showImageLibrary();
    } else {
      showDashboard();
    }
    return;
  }
  if (screen_ == Screen::Dashboard) {
    menuSelection_ = MenuItem::ReceiveImage;
    screen_ = Screen::Menu;
    view_.drawMenu(menuSelection_);
    return;
  }
  showDashboard();
}

void AppController::handleBShort() {
  if (lowPower_) {
    exitLowPower();
    return;
  }
  switch (screen_) {
    case Screen::Dashboard:
      enterLowPower();
      break;
    case Screen::Menu:
      selectMenuItem(menuSelection_);
      break;
    case Screen::Calendar:
    case Screen::Message:
      showDashboard();
      break;
    case Screen::Goal:
      candidateGoal_ = candidateGoal_ >= PAPER_MONO_MAX_STEP_GOAL
                           ? PAPER_MONO_MIN_STEP_GOAL
                           : candidateGoal_ + PAPER_MONO_STEP_GOAL_INCREMENT;
      view_.drawGoalEditor(candidateGoal_);
      break;
    case Screen::History:
      if (historyPage_ > 0) {
        --historyPage_;
      }
      view_.drawHistory(historyPage_);
      break;
    case Screen::ResetImage:
      if (resetAction_ == 0) {
        if (nfc_.selectDefaultImage()) showDashboard(true);
        else showMessage("RESET IMAGE", "COULD NOT SELECT DEFAULT");
      } else if (resetAction_ == 1) {
        libraryDeleteMode_ = true;
        libraryDeleteMask_ = 0;
        libraryFocus_ = 0;
        showImageLibrary();
      } else {
        resetSelected_ = false;
        screen_ = Screen::DeleteAllConfirm;
        view_.drawDeleteAllConfirmation(false);
      }
      break;
    case Screen::DeleteAllConfirm:
      if (!resetSelected_) {
        screen_ = Screen::ResetImage;
        view_.drawResetImageMenu(resetAction_);
      } else if (nfc_.clearStoredImage()) {
        showDashboard(true);
      } else {
        showMessage("RESET IMAGE", "COULD NOT DELETE IMAGES");
      }
      break;
    case Screen::ImageLibrary:
      if (libraryDeleteMode_) toggleLibraryDeleteItem();
      else activateLibraryItem();
      break;
    case Screen::Nfc:
      break;
  }
}

void AppController::handleBLong() {
  if (!lowPower_ && screen_ == Screen::ImageLibrary) {
    if (!libraryDeleteMode_) {
      libraryDeleteMode_ = true;
      libraryDeleteMask_ = 0;
      showImageLibrary();
    } else if (libraryDeleteMask_ != 0) {
      if (nfc_.deleteStoredImages(libraryDeleteMask_)) {
        libraryDeleteMask_ = 0;
        libraryDeleteMode_ = false;
        libraryFocus_ = 0;
        showImageLibrary();
      } else {
        showMessage("IMAGE LIBRARY", "COULD NOT DELETE IMAGES");
      }
    }
    return;
  }
  if (!lowPower_ && screen_ == Screen::Goal) {
    if (steps_.setGoalSteps(candidateGoal_, millis())) {
      showDashboard();
    } else {
      showMessage("STEP GOAL", "The selected goal is invalid.");
    }
  }
}

void AppController::selectMenuItem(MenuItem item) {
  switch (item) {
    case MenuItem::ReceiveImage:
      enterNfc(NfcPurpose::ReceiveImage);
      break;
    case MenuItem::SyncClock:
      enterNfc(NfcPurpose::SyncClock);
      break;
    case MenuItem::StepGoal:
      candidateGoal_ = steps_.goalSteps();
      screen_ = Screen::Goal;
      view_.drawGoalEditor(candidateGoal_);
      break;
    case MenuItem::StepHistory:
      historyPage_ = 0;
      screen_ = Screen::History;
      view_.drawHistory(historyPage_);
      break;
    case MenuItem::ResetImage:
      resetAction_ = 0;
      screen_ = Screen::ResetImage;
      view_.drawResetImageMenu(resetAction_);
      break;
    case MenuItem::ImageLibrary:
      libraryDeleteMode_ = false;
      libraryDeleteMask_ = 0;
      libraryFocus_ = 0;
      showImageLibrary();
      break;
    case MenuItem::Count:
      showDashboard();
      break;
  }
}

bool AppController::normalizedTouch(m5::touch_detail_t& touch) {
  touch = M5.Touch.getDetail();
  const int16_t maxX = static_cast<int16_t>(M5.Display.width() - 1);
  const int16_t maxY = static_cast<int16_t>(M5.Display.height() - 1);
  const auto normalize = [maxX, maxY](int16_t& x, int16_t& y) {
    if (x < 0 || x > maxX || y < 0 || y > maxY) {
      return;
    }
    const int32_t visibleX =
        (static_cast<int32_t>(y) * maxX + maxY / 2) / maxY;
    const int32_t visibleY =
        (static_cast<int32_t>(maxX - x) * maxY + maxX / 2) / maxX;
    x = static_cast<int16_t>(visibleX);
    y = static_cast<int16_t>(visibleY);
  };
  normalize(touch.x, touch.y);
  normalize(touch.prev_x, touch.prev_y);
  normalize(touch.base_x, touch.base_y);
  return touch.wasClicked();
}

void AppController::updateTouch() {
  if (lowPower_ || screen_ == Screen::Nfc) {
    return;
  }
  m5::touch_detail_t touch;
  const bool clicked = normalizedTouch(touch);
  if (screen_ == Screen::ImageLibrary) {
    if (touch.wasPressed()) {
      libraryTouchTracking_ = true;
      libraryTouchStartX_ = touch.x;
      libraryTouchStartY_ = touch.y;
    }
    if (libraryTouchTracking_ && touch.wasReleased()) {
      libraryTouchTracking_ = false;
      const int32_t dx = static_cast<int32_t>(touch.x) - libraryTouchStartX_;
      const int32_t dy = static_cast<int32_t>(touch.y) - libraryTouchStartY_;
      if (abs(dx) >= 70 && abs(dx) > abs(dy) * 2) {
        const uint8_t pages = imageLibraryPages();
        const int8_t direction = dx < 0 ? 1 : -1;
        libraryPage_ = static_cast<uint8_t>(
            (libraryPage_ + pages + direction) % pages);
        libraryFocus_ = min<uint8_t>(libraryPage_ * 6,
                                     imageLibraryTotal() - 1);
        showImageLibrary();
        return;
      }
    }
  }
  if (!clicked) {
    return;
  }
  switch (screen_) {
    case Screen::Dashboard:
      if (fullScreenImageActive()) break;
      if (view_.dateHit(touch.x, touch.y)) {
        if (clock_.valid()) {
          screen_ = Screen::Calendar;
          view_.drawMonthCalendar();
        } else {
          enterNfc(NfcPurpose::SyncClock);
        }
      } else if (view_.stepsHit(touch.x, touch.y)) {
        historyPage_ = 0;
        screen_ = Screen::History;
        view_.drawHistory(historyPage_);
      } else if (view_.resetHit(touch.x, touch.y)) {
        resetAction_ = 0;
        screen_ = Screen::ResetImage;
        view_.drawResetImageMenu(resetAction_);
      }
      break;
    case Screen::Menu: {
      const MenuItem item = view_.menuItemAt(touch.x, touch.y);
      if (item != MenuItem::Count) {
        menuSelection_ = item;
        selectMenuItem(item);
      }
      break;
    }
    case Screen::Calendar:
      showDashboard();
      break;
    case Screen::Goal:
      if (touch.y >= 390 && touch.y < 520) {
        if (touch.x < 240) {
          candidateGoal_ = candidateGoal_ <= PAPER_MONO_MIN_STEP_GOAL
                               ? PAPER_MONO_MAX_STEP_GOAL
                               : candidateGoal_ - PAPER_MONO_STEP_GOAL_INCREMENT;
        } else {
          candidateGoal_ = candidateGoal_ >= PAPER_MONO_MAX_STEP_GOAL
                               ? PAPER_MONO_MIN_STEP_GOAL
                               : candidateGoal_ + PAPER_MONO_STEP_GOAL_INCREMENT;
        }
        view_.drawGoalEditor(candidateGoal_);
      } else if (touch.y >= 550 && touch.y < 680 &&
                 steps_.setGoalSteps(candidateGoal_, millis())) {
        showDashboard();
      }
      break;
    case Screen::History:
      if (touch.y >= 730) {
        showDashboard();
      }
      break;
    case Screen::ResetImage: {
      const int8_t action = view_.resetImageActionAt(touch.x, touch.y);
      if (action >= 0) {
        resetAction_ = static_cast<uint8_t>(action);
        handleBShort();
      }
      break;
    }
    case Screen::DeleteAllConfirm:
      if (touch.y >= 380 && touch.y < 510) {
        resetSelected_ = touch.x >= 240;
        handleBShort();
      }
      break;
    case Screen::ImageLibrary: {
      if (!libraryDeleteMode_ && view_.imageLibraryDeleteHit(touch.x, touch.y)) {
        libraryDeleteMode_ = true;
        libraryDeleteMask_ = 0;
        showImageLibrary();
        break;
      }
      const int8_t local = view_.imageLibraryCardAt(touch.x, touch.y);
      if (local >= 0) {
        const uint8_t global = libraryPage_ * 6 + static_cast<uint8_t>(local);
        if (global < imageLibraryTotal()) {
          libraryFocus_ = global;
          if (libraryDeleteMode_) toggleLibraryDeleteItem();
          else activateLibraryItem();
        }
        break;
      }
      if (libraryDeleteMode_ && touch.y >= 648 && touch.y < 730) {
        if (touch.x < 190) {
          libraryDeleteMode_ = false;
          libraryDeleteMask_ = 0;
          showImageLibrary();
        } else if (touch.x > 290 && libraryDeleteMask_ != 0) {
          if (nfc_.deleteStoredImages(libraryDeleteMask_)) {
            libraryDeleteMode_ = false;
            libraryDeleteMask_ = 0;
            libraryFocus_ = 0;
            showImageLibrary();
          } else {
            showMessage("IMAGE LIBRARY", "COULD NOT DELETE IMAGES");
          }
        }
        break;
      }
      const int8_t direction = view_.imageLibraryPageDirectionAt(touch.x, touch.y);
      if (!libraryDeleteMode_ && direction != 0) {
        const uint8_t pages = imageLibraryPages();
        libraryPage_ = static_cast<uint8_t>(
            (libraryPage_ + pages + direction) % pages);
        libraryFocus_ = min<uint8_t>(libraryPage_ * 6,
                                     imageLibraryTotal() - 1);
        showImageLibrary();
      } else if (!libraryDeleteMode_ && touch.y >= 648 && touch.y < 730 &&
                 touch.x >= 190 && touch.x < 290) {
        const uint8_t pages = imageLibraryPages();
        const int32_t firstDot = 240 - static_cast<int32_t>(pages - 1) * 12;
        const int32_t tapped = (touch.x - firstDot + 12) / 24;
        if (tapped >= 0 && tapped < pages) {
          libraryPage_ = static_cast<uint8_t>(tapped);
          libraryFocus_ = min<uint8_t>(libraryPage_ * 6,
                                       imageLibraryTotal() - 1);
          showImageLibrary();
        }
      }
      break;
    }
    case Screen::Message:
      showDashboard();
      break;
    case Screen::Nfc:
      break;
  }
}

void AppController::enterNfc(NfcPurpose purpose) {
  nfc_.cancelActiveTransfer();
  nfc_.powerDown();
  nfcPurpose_ = purpose;
  screen_ = Screen::Nfc;
  view_.drawNfcWaiting(purpose == NfcPurpose::SyncClock);
  delay(1000);
  nfcTimeRevisionAtStart_ = nfc_.timeSyncRevision();
  nfcStartedAtMs_ = millis();
  if (!nfc_.setModeActive(true)) {
    showMessage("NFC ERROR", "Could not start the NFC listener.");
  }
}

void AppController::cancelNfc(const char* reason) {
  Serial.printf("[app] NFC cancelled reason=%s\n", reason);
  nfc_.cancelActiveTransfer();
  nfc_.powerDown();
  recoverTouch("nfc_cancel");
  showDashboard();
}

void AppController::updateNfc(uint32_t now) {
  if (screen_ != Screen::Nfc) {
    return;
  }
  if (nfcPurpose_ == NfcPurpose::SyncClock &&
      nfc_.timeSyncRevision() != nfcTimeRevisionAtStart_) {
    // Let the SET_TIME response leave the RF path before turning ST25R3916 off.
    delay(120);
    nfc_.powerDown();
    recoverTouch("time_sync");
    showDashboard();
    return;
  }
  if (nfc_.phase() == PaperMonoNfcProtocol::Phase::Displaying) {
    nfc_.powerDown();
    recoverTouch("nfc_stored");
    PaperMonoNfcController::StoredImage image;
    const bool stored = nfc_.getStoredImage(image);
    view_.drawDashboard(false, stored ? &image : nullptr, true);
    nfc_.markDisplayed(stored);
    screen_ = Screen::Dashboard;
    lastValuesDrawMs_ = millis();
    return;
  }
  if (nfc_.phase() == PaperMonoNfcProtocol::Phase::Error) {
    const char* error = nfc_.statusName();
    nfc_.powerDown();
    recoverTouch("nfc_error");
    view_.drawMessage("NFC ERROR", error, "B: BACK");
    screen_ = Screen::Message;
    return;
  }
  if (nfc_.phase() == PaperMonoNfcProtocol::Phase::Idle &&
      now - nfcStartedAtMs_ >= PAPER_MONO_NFC_WAIT_TIMEOUT_MS) {
    cancelNfc("timeout");
  }
}

void AppController::disableTouch() {
  auto* controller = M5.Display.touch();
  if (controller != nullptr) {
    controller->sleep();
  }
  M5.Touch.end();
  auto& ioe = M5.getIOExpander(0);
  constexpr auto touchEnablePin = m5::M5IOE1_Class::gpio13;
  ioe.setHighImpedance(touchEnablePin, false);
  ioe.setDirection(touchEnablePin, true);
  ioe.digitalWrite(touchEnablePin, false);
}

bool AppController::recoverTouch(const char* reason) {
  M5.Touch.end();
  auto& ioe = M5.getIOExpander(0);
  constexpr auto resetPin = m5::M5IOE1_Class::gpio6;
  constexpr auto enablePin = m5::M5IOE1_Class::gpio13;
  ioe.setHighImpedance(enablePin, false);
  ioe.setDirection(enablePin, true);
  ioe.digitalWrite(enablePin, true);
  ioe.setHighImpedance(resetPin, false);
  ioe.setDirection(resetPin, true);
  ioe.digitalWrite(resetPin, false);
  delay(12);
  ioe.digitalWrite(resetPin, true);
  delay(180);
  auto* controller = M5.Display.touch();
  bool ready = false;
  for (uint8_t attempt = 0; attempt < 3 && !ready; ++attempt) {
    ready = controller != nullptr && controller->init();
    if (!ready) {
      delay(100);
    }
  }
  if (ready) {
    controller->wakeup();
  }
  M5.Touch.begin(&M5.Display);
  for (uint8_t pass = 0; pass < 3; ++pass) {
    delay(5);
    M5.Touch.update(millis());
  }
  Serial.printf("[touch] recover reason=%s ready=%d\n", reason, ready ? 1 : 0);
  return ready;
}

void AppController::enterLowPower() {
  if (lowPower_ || screen_ != Screen::Dashboard) {
    return;
  }
  nfc_.powerDown();
  lowPower_ = true;
  steps_.saveNow(millis());
  if (!fullScreenImageActive()) view_.drawValuesPartial(true, true);
  disableTouch();
  M5.Display.setBrightness(0);
  activeCpuMhz_ = getCpuFrequencyMhz();
  if (activeCpuMhz_ > PAPER_MONO_LOW_POWER_CPU_MHZ) {
    setCpuFrequencyMhz(PAPER_MONO_LOW_POWER_CPU_MHZ);
  }
  partialRefreshes_ = 0;
  lastValuesDrawMs_ = millis();
}

void AppController::exitLowPower() {
  if (!lowPower_) {
    return;
  }
  if (activeCpuMhz_ > PAPER_MONO_LOW_POWER_CPU_MHZ) {
    setCpuFrequencyMhz(activeCpuMhz_);
  }
  lowPower_ = false;
  recoverTouch("low_power_wake");
  M5.Display.setBrightness(PAPER_MONO_FRONT_LIGHT_BRIGHTNESS);
  if (!fullScreenImageActive()) view_.drawValuesPartial(false, true);
  lastValuesDrawMs_ = millis();
}

void AppController::updateLowPower(uint32_t now) {
  if (now - lastValuesDrawMs_ >= PAPER_MONO_LOCKED_UPDATE_MS) {
    if (fullScreenImageActive()) {
      lastValuesDrawMs_ = now;
      return;
    }
    ++partialRefreshes_;
    if (partialRefreshes_ >= PAPER_MONO_FULL_REFRESH_AFTER_PARTIALS) {
      view_.drawValuesPartial(true, true);
      partialRefreshes_ = 0;
    } else {
      view_.drawValuesPartial(true);
    }
    lastValuesDrawMs_ = millis();
  }
  const uint32_t current = millis();
  if (static_cast<int32_t>(nextImuSampleMs_ - current) > 3) {
    const uint32_t sleepMs = nextImuSampleMs_ - current - 2;
    M5.Power.lightSleep(static_cast<uint64_t>(sleepMs) * 1000ULL, false);
  } else {
    delay(1);
  }
}

void AppController::update() {
  if (screen_ == Screen::Nfc && nfc_.modeActive()) {
    const uint32_t now = millis();
    nfc_.update();
    M5.BtnA.setRawState(now, digitalRead(kButtonAPin) == LOW);
    M5.BtnB.setRawState(now, digitalRead(kButtonBPin) == LOW);
    updateButtons(now);
    updateNfc(now);
    yield();
    return;
  }

  M5.update();
  const uint32_t now = millis();
  updateImuAndSteps(now);
  updateButtons(now);

  if (lowPower_) {
    updateLowPower(now);
    return;
  }

  updateTouch();
  if (screen_ == Screen::Dashboard &&
      !fullScreenImageActive() &&
      now - lastValuesDrawMs_ >= PAPER_MONO_LOCKED_UPDATE_MS) {
    ++partialRefreshes_;
    if (partialRefreshes_ >= PAPER_MONO_FULL_REFRESH_AFTER_PARTIALS) {
      view_.drawValuesPartial(false, true);
      partialRefreshes_ = 0;
    } else {
      view_.drawValuesPartial(false);
    }
    lastValuesDrawMs_ = millis();
  }
  delay(2);
}
