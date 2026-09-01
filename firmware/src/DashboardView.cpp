#include "DashboardView.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <M5Unified.h>

#include "DefaultImageData.h"

namespace {
constexpr int32_t kDeckTop = 480;
constexpr int32_t kResetX = 372;
constexpr int32_t kResetY = 752;
constexpr int32_t kResetW = 92;
constexpr int32_t kResetH = 34;
constexpr int32_t kValuesTop = 497;
constexpr int32_t kLiveTop = 550;
constexpr int32_t kLiveBottom = 663;
constexpr int32_t kValuesBottom = 739;
constexpr int32_t kProgressTop = 748;
constexpr int32_t kProgressBottom = 789;

class DisplayBatch {
public:
  explicit DisplayBatch(epd_mode_t mode) {
    M5.Display.setEpdMode(mode);
    M5.Display.startWrite();
  }
  ~DisplayBatch() {
    M5.Display.endWrite();
    M5.Display.setEpdMode(epd_mode_t::epd_fast);
  }
  DisplayBatch(const DisplayBatch&) = delete;
  DisplayBatch& operator=(const DisplayBatch&) = delete;
};

int32_t dayKey(const RtcClock::LocalTime& local) {
  return local.valid ? (local.value.tm_year + 1900) * 366 + local.value.tm_yday
                     : -1;
}
}  // namespace

DashboardView::DashboardView(RtcClock& clock, StepCounterController& steps)
    : clock_(clock), steps_(steps) {}

void DashboardView::formatSteps(uint32_t steps,
                                char* output,
                                size_t outputSize) const {
  if (output == nullptr || outputSize == 0) {
    return;
  }
  char raw[16] = {};
  snprintf(raw, sizeof(raw), "%lu", static_cast<unsigned long>(steps));
  const size_t length = strlen(raw);
  size_t writeIndex = 0;
  for (size_t index = 0; index < length && writeIndex + 1 < outputSize; ++index) {
    if (index > 0 && (length - index) % 3 == 0 && writeIndex + 2 < outputSize) {
      output[writeIndex++] = ',';
    }
    output[writeIndex++] = raw[index];
  }
  output[writeIndex] = '\0';
}

bool DashboardView::drawImage(
    const PaperMonoNfcController::StoredImage* storedImage) {
  constexpr float scale = 480.0f / 386.0f;
  if (storedImage != nullptr && storedImage->valid && storedImage->path != nullptr) {
    File file = LittleFS.open(storedImage->path, "r");
    if (file) {
      const bool drawn = M5.Display.drawJpg(&file, 0, 0, 480, 480, 0, 0,
                                            scale, scale, datum_t::top_left);
      file.close();
      if (drawn) {
        return true;
      }
    }
  }
  return M5.Display.drawJpg(kPaperMonoDefaultImage,
                            static_cast<uint32_t>(kPaperMonoDefaultImageSize),
                            0, 0, 480, 480, 0, 0, scale, scale,
                            datum_t::top_left);
}

void DashboardView::drawTopBar(bool locked) {
  const int32_t width = M5.Display.width();
  M5.Display.fillRect(0, 0, width, 42, TFT_WHITE);
  M5.Display.drawFastHLine(0, 41, width, TFT_BLACK);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextSize(1);
  M5.Display.setTextDatum(middle_left);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.drawString("PAPER MONO", 14, 20);

  char status[24] = {};
  const int battery = constrain(M5.Power.getBatteryLevel(), 0, 100);
  if (locked) {
    snprintf(status, sizeof(status), "LOW POWER  %d%%", battery);
  } else {
    snprintf(status, sizeof(status), "%d%%", battery);
  }
  M5.Display.setTextDatum(middle_right);
  M5.Display.drawString(status, width - 14, 20);
  M5.Display.setTextDatum(top_left);
}

void DashboardView::drawDashboardValues() {
  const int32_t width = M5.Display.width();
  const auto local = clock_.localTime();
  M5.Display.fillRect(0, kValuesTop, width,
                      kValuesBottom - kValuesTop + 1, TFT_WHITE);
  M5.Display.setTextSize(1);

  static const char* kWeekdays[] = {"SUN", "MON", "TUE", "WED",
                                     "THU", "FRI", "SAT"};
  static const char* kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                  "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  char dateText[32] = "---  -- --- ----";
  if (local.valid) {
    snprintf(dateText, sizeof(dateText), "%s  %02d %s %04d",
             kWeekdays[local.value.tm_wday], local.value.tm_mday,
             kMonths[local.value.tm_mon], local.value.tm_year + 1900);
  }

  M5.Display.fillRoundRect(18, 507, 64, 30, 6, TFT_BLACK);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString("TODAY", 50, 522);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(middle_left);
  M5.Display.drawString(dateText, 96, 522);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextDatum(middle_right);
  M5.Display.drawString(local.valid ? "TAP FOR MONTH" : "SYNC CLOCK",
                        width - 18, 522);
  M5.Display.drawFastHLine(18, 546, width - 36, TFT_BLACK);

  M5.Display.fillRect(0, kLiveTop, width, kLiveBottom - kLiveTop + 1, TFT_WHITE);
  char timeText[8] = "--:--";
  if (local.valid) {
    snprintf(timeText, sizeof(timeText), "%02d:%02d", local.value.tm_hour,
             local.value.tm_min);
  }
  char zone[16] = "RTC";
  if (local.valid) {
    clock_.formatZone(zone, sizeof(zone));
  }
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(middle_left);
  const int32_t zoneW = strlen(zone) <= 3 ? 62 : 118;
  M5.Display.fillRoundRect(18, 552, zoneW, 28, 8, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(zone, 18 + zoneW / 2, 566);

  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setFont(&fonts::FreeSansBold24pt7b);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(timeText, 170, 618);
  M5.Display.setTextSize(1);

  char stepText[20] = {};
  formatSteps(steps_.todaySteps(), stepText, sizeof(stepText));
  constexpr int32_t stepCardX = 336;
  constexpr int32_t stepCardY = 554;
  constexpr int32_t stepCardW = 128;
  constexpr int32_t stepCardH = 105;
  M5.Display.fillRoundRect(stepCardX, stepCardY, stepCardW, stepCardH, 10, TFT_BLACK);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString("TODAY / STEPS", stepCardX + stepCardW / 2,
                        stepCardY + 17);
  M5.Display.setFont(&fonts::FreeSansBold24pt7b);
  if (M5.Display.textWidth(stepText) > stepCardW - 14) {
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  }
  M5.Display.drawString(stepText, stepCardX + stepCardW / 2, stepCardY + 59);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.drawString("TAP FOR HISTORY", stepCardX + stepCardW / 2,
                        stepCardY + 91);

  M5.Display.drawFastHLine(18, 670, width - 36, TFT_BLACK);
  const int32_t weekLeft = 18;
  const int32_t weekWidth = width - weekLeft * 2;
  const int32_t cellWidth = weekWidth / 7;
  for (int column = 0; column < 7; ++column) {
    const int32_t centerX = weekLeft + column * cellWidth + cellWidth / 2;
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(kWeekdays[column], centerX, 687);
    char dayText[4] = "--";
    bool today = false;
    if (local.valid) {
      tm day = local.value;
      const int offset = column - local.value.tm_wday;
      day.tm_mday += offset;
      day.tm_hour = 12;
      day.tm_min = 0;
      day.tm_sec = 0;
      setenv("TZ", "UTC0", 1);
      tzset();
      mktime(&day);
      snprintf(dayText, sizeof(dayText), "%d", day.tm_mday);
      today = offset == 0;
    }
    if (today) {
      M5.Display.fillCircle(centerX, 718, 18, TFT_BLACK);
      M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
      M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    }
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.drawString(dayText, centerX, 718);
  }
  M5.Display.setTextDatum(top_left);
  (void)dayKey(local);
}

void DashboardView::drawStepProgress() {
  constexpr int32_t segmentCount = 20;
  constexpr int32_t segmentW = 14;
  constexpr int32_t segmentGap = 2;
  const uint32_t goal = max<uint32_t>(steps_.goalSteps(), 1);
  const uint32_t current = steps_.todaySteps();
  const int32_t filled = min<int32_t>(
      segmentCount,
      static_cast<int32_t>((static_cast<uint64_t>(current) * segmentCount) / goal));

  M5.Display.fillRect(0, kProgressTop, kResetX - 8,
                      kProgressBottom - kProgressTop + 1, TFT_WHITE);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(middle_left);
  M5.Display.drawString("DAILY GOAL", 18, 758);
  char goalText[12] = {};
  if (goal % 1000 == 0) {
    snprintf(goalText, sizeof(goalText), "%luK",
             static_cast<unsigned long>(goal / 1000));
  } else {
    formatSteps(goal, goalText, sizeof(goalText));
  }
  M5.Display.setTextDatum(middle_right);
  M5.Display.drawString(goalText, 348, 758);
  for (int32_t segment = 0; segment < segmentCount; ++segment) {
    const int32_t x = 18 + segment * (segmentW + segmentGap);
    if (segment < filled) {
      M5.Display.fillRect(x, 773, segmentW, 10, TFT_BLACK);
    } else {
      M5.Display.drawRect(x, 773, segmentW, 10, TFT_BLACK);
    }
  }
  M5.Display.setTextDatum(top_left);
}

void DashboardView::drawDashboard(
    bool locked,
    const PaperMonoNfcController::StoredImage* storedImage,
    bool quality) {
  DisplayBatch batch(quality ? epd_mode_t::epd_quality : epd_mode_t::epd_fast);
  M5.Display.fillScreen(TFT_WHITE);
  const bool received = storedImage != nullptr && storedImage->valid &&
                        drawImage(storedImage);
  if (!received) {
    drawImage(nullptr);
  }
  drawTopBar(locked);
  M5.Display.fillRect(0, kDeckTop, M5.Display.width(),
                      M5.Display.height() - kDeckTop, TFT_WHITE);
  M5.Display.fillRect(0, kDeckTop, M5.Display.width(), 8, TFT_BLACK);
  drawDashboardValues();
  M5.Display.drawFastHLine(18, 744, M5.Display.width() - 36, TFT_BLACK);
  drawStepProgress();
  if (!locked) {
    M5.Display.fillRoundRect(kResetX, kResetY, kResetW, kResetH, 6, TFT_BLACK);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("RESET IMAGE", kResetX + kResetW / 2,
                          kResetY + kResetH / 2);
  }
  M5.Display.setTextDatum(top_left);
}

void DashboardView::drawValuesPartial(bool locked) {
  DisplayBatch batch(epd_mode_t::epd_fastest);
  drawTopBar(locked);
  drawDashboardValues();
  M5.Display.drawFastHLine(18, 744, M5.Display.width() - 36, TFT_BLACK);
  drawStepProgress();
  if (!locked) {
    M5.Display.fillRoundRect(kResetX, kResetY, kResetW, kResetH, 6, TFT_BLACK);
    M5.Display.setFont(&fonts::Font0);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("RESET IMAGE", kResetX + kResetW / 2,
                          kResetY + kResetH / 2);
  }
  M5.Display.setTextDatum(top_left);
}

void DashboardView::drawMonthCalendar() {
  DisplayBatch batch(epd_mode_t::epd_fast);
  const auto local = clock_.localTime();
  const int32_t width = M5.Display.width();
  M5.Display.fillRect(0, kDeckTop, width, M5.Display.height() - kDeckTop, TFT_WHITE);
  M5.Display.fillRect(0, kDeckTop, width, 8, TFT_BLACK);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextDatum(middle_left);
  char monthText[24] = "SYNC CLOCK";
  if (local.valid) {
    snprintf(monthText, sizeof(monthText), "%04d / %02d",
             local.value.tm_year + 1900, local.value.tm_mon + 1);
  }
  M5.Display.drawString(monthText, 18, 524);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.setTextDatum(middle_center);
  static const char* kWeekdays[] = {"S", "M", "T", "W", "T", "F", "S"};
  const int32_t cellW = width / 7;
  for (int day = 0; day < 7; ++day) {
    M5.Display.drawString(kWeekdays[day], day * cellW + cellW / 2, 554);
  }
  if (local.valid) {
    tm first = local.value;
    first.tm_mday = 1;
    first.tm_hour = 12;
    first.tm_min = 0;
    first.tm_sec = 0;
    setenv("TZ", "UTC0", 1);
    tzset();
    mktime(&first);
    static const uint8_t kDaysInMonth[] = {31, 28, 31, 30, 31, 30,
                                           31, 31, 30, 31, 30, 31};
    int days = kDaysInMonth[local.value.tm_mon];
    const int year = local.value.tm_year + 1900;
    if (local.value.tm_mon == 1 &&
        ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
      days = 29;
    }
    for (int day = 1; day <= days; ++day) {
      const int slot = first.tm_wday + day - 1;
      const int column = slot % 7;
      const int row = slot / 7;
      const int32_t cx = column * cellW + cellW / 2;
      const int32_t cy = 584 + row * 29;
      if (day == local.value.tm_mday) {
        M5.Display.fillCircle(cx, cy, 13, TFT_BLACK);
        M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
      } else {
        M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
      }
      M5.Display.drawNumber(day, cx, cy);
    }
  }
  M5.Display.fillRect(0, 752, width, 48, TFT_BLACK);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString("TAP OR HOLD A: BACK", width / 2, 768);
  M5.Display.setTextDatum(top_left);
}

MenuItem DashboardView::menuItemAt(int32_t x, int32_t y) const {
  if (y < 82 || y > 694 || x < 18 || x >= M5.Display.width() - 18) {
    return MenuItem::Count;
  }
  constexpr int32_t gap = 12;
  constexpr int32_t margin = 18;
  const int32_t cardW = (M5.Display.width() - margin * 2 - gap) / 2;
  const int32_t column = x < margin + cardW ? 0 : 1;
  const int32_t row = min<int32_t>(2, (y - 82) / 208);
  return static_cast<MenuItem>(row * 2 + column);
}

void DashboardView::drawMenu(MenuItem selected) {
  DisplayBatch batch(epd_mode_t::epd_fast);
  struct Card {
    const char* index;
    const char* title;
  };
  constexpr Card cards[] = {
      {"01", "RECEIVE IMAGE"},
      {"02", "SYNC CLOCK"},
      {"03", "STEP GOAL"},
      {"04", "STEP HISTORY"},
      {"05", "RESET IMAGE"},
      {"06", "BACK"},
  };
  constexpr int32_t margin = 18;
  constexpr int32_t gap = 12;
  constexpr int32_t topY = 82;
  constexpr int32_t cardH = 196;
  constexpr int32_t rowGap = 12;
  const int32_t width = M5.Display.width();
  const int32_t cardW = (width - margin * 2 - gap) / 2;
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.fillRect(0, 0, width, 64, TFT_BLACK);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextDatum(middle_left);
  M5.Display.drawString("PAPER MONO MENU", 18, 32);
  for (size_t index = 0; index < sizeof(cards) / sizeof(cards[0]); ++index) {
    const int32_t column = index % 2;
    const int32_t row = index / 2;
    const int32_t x = margin + column * (cardW + gap);
    const int32_t y = topY + row * (cardH + rowGap);
    const bool isSelected = static_cast<size_t>(selected) == index;
    const uint16_t fill = isSelected ? TFT_BLACK : M5.Display.color565(196, 196, 196);
    const uint16_t text = isSelected ? TFT_WHITE : TFT_BLACK;
    M5.Display.fillRoundRect(x, y, cardW, cardH, 12, fill);
    M5.Display.drawRoundRect(x, y, cardW, cardH, 12, TFT_BLACK);
    if (isSelected) {
      M5.Display.drawRoundRect(x + 5, y + 5, cardW - 10, cardH - 10, 9, TFT_WHITE);
    }
    M5.Display.setFont(&fonts::FreeSansBold24pt7b);
    M5.Display.setTextDatum(top_left);
    M5.Display.setTextColor(text, fill);
    M5.Display.drawString(cards[index].index, x + 16, y + 20);
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(cards[index].title, x + cardW / 2, y + 116);
    if (isSelected) {
      M5.Display.drawFastHLine(x + 34, y + 148, cardW - 68, TFT_WHITE);
      M5.Display.drawString("SELECTED", x + cardW / 2, y + 174);
    }
  }
  M5.Display.fillRect(0, 708, width, 92, TFT_BLACK);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString("A: NEXT     B: OPEN", width / 2, 734);
  M5.Display.drawString("HOLD A: CLOSE", width / 2, 776);
  M5.Display.setTextDatum(top_left);
}

void DashboardView::drawGoalEditor(uint32_t candidateGoal) {
  DisplayBatch batch(epd_mode_t::epd_fast);
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.fillRect(0, 0, M5.Display.width(), 64, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextDatum(middle_left);
  M5.Display.drawString("STEP GOAL", 18, 32);
  char value[20] = {};
  formatSteps(candidateGoal, value, sizeof(value));
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setFont(&fonts::FreeSansBold24pt7b);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(value, M5.Display.width() / 2, 250);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::Font0);
  M5.Display.drawString("STEPS / DAY", M5.Display.width() / 2, 320);
  M5.Display.drawRoundRect(36, 390, 188, 112, 12, TFT_BLACK);
  M5.Display.drawRoundRect(256, 390, 188, 112, 12, TFT_BLACK);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.drawString("- 1,000", 130, 446);
  M5.Display.drawString("+ 1,000", 350, 446);
  M5.Display.fillRoundRect(90, 570, 300, 84, 12, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString("SAVE", 240, 612);
  M5.Display.fillRect(0, 700, M5.Display.width(), 100, TFT_BLACK);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString("A: -1K       B: +1K", 240, 730);
  M5.Display.drawString("HOLD B: SAVE     HOLD A: BACK", 240, 772);
  M5.Display.setTextDatum(top_left);
}

uint8_t DashboardView::historyPageCount() const {
  uint8_t valid = 0;
  for (uint8_t index = 0; index < steps_.historyCount(); ++index) {
    const StepDayRecord* record = steps_.recordAt(index);
    if (record != nullptr && record->activityDay != 0) {
      ++valid;
    }
  }
  return max<uint8_t>(1, static_cast<uint8_t>((valid + 6) / 7));
}

void DashboardView::drawHistory(uint8_t page) {
  DisplayBatch batch(epd_mode_t::epd_fast);
  const StepDayRecord* records[STEP_COUNTER_HISTORY_DAYS] = {};
  uint8_t count = 0;
  for (uint8_t index = 0; index < steps_.historyCount(); ++index) {
    const StepDayRecord* record = steps_.recordAt(index);
    if (record != nullptr && record->activityDay != 0) {
      records[count++] = record;
    }
  }
  for (uint8_t i = 0; i < count; ++i) {
    for (uint8_t j = i + 1; j < count; ++j) {
      if (records[j]->activityDay > records[i]->activityDay) {
        const StepDayRecord* swap = records[i];
        records[i] = records[j];
        records[j] = swap;
      }
    }
  }
  const uint8_t pages = max<uint8_t>(1, static_cast<uint8_t>((count + 6) / 7));
  page = min<uint8_t>(page, pages - 1);
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.fillRect(0, 0, M5.Display.width(), 64, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextDatum(middle_left);
  M5.Display.drawString("STEP HISTORY", 18, 32);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextDatum(middle_right);
  char pageText[16] = {};
  snprintf(pageText, sizeof(pageText), "%u / %u", page + 1, pages);
  M5.Display.drawString(pageText, M5.Display.width() - 18, 32);

  for (uint8_t row = 0; row < 7; ++row) {
    const uint8_t index = page * 7 + row;
    const int32_t y = 102 + row * 84;
    M5.Display.drawFastHLine(18, y + 66, M5.Display.width() - 36, TFT_BLACK);
    if (index >= count) {
      continue;
    }
    const StepDayRecord& record = *records[index];
    const time_t raw = static_cast<time_t>(record.activityDay) * 86400;
    tm date = {};
    gmtime_r(&raw, &date);
    static const char* months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                   "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    char dateText[16] = {};
    snprintf(dateText, sizeof(dateText), "%s %02d", months[date.tm_mon],
             date.tm_mday);
    char stepText[20] = {};
    formatSteps(record.steps, stepText, sizeof(stepText));
    const uint32_t goal = max<uint32_t>(record.goalSteps, 1);
    const uint32_t percent = min<uint32_t>(999, (record.steps * 100ULL) / goal);
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setTextDatum(middle_left);
    M5.Display.drawString(dateText, 20, y + 24);
    M5.Display.setTextDatum(middle_right);
    M5.Display.drawString(stepText, 232, y + 24);
    char percentText[12] = {};
    snprintf(percentText, sizeof(percentText), "%lu%%",
             static_cast<unsigned long>(percent));
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.drawString(percentText, 462, y + 24);
    constexpr int32_t barX = 264;
    constexpr int32_t barW = 150;
    M5.Display.drawRect(barX, y + 17, barW, 16, TFT_BLACK);
    M5.Display.fillRect(barX, y + 17,
                        min<int32_t>(barW, (barW * percent) / 100), 16, TFT_BLACK);
  }

  uint64_t monthTotal = 0;
  uint8_t monthDays = 0;
  int anchorYear = -1;
  int anchorMonth = -1;
  if (count > 0) {
    const uint8_t anchorIndex = min<uint8_t>(page * 7, count - 1);
    const time_t anchorRaw =
        static_cast<time_t>(records[anchorIndex]->activityDay) * 86400;
    tm anchorDate = {};
    gmtime_r(&anchorRaw, &anchorDate);
    anchorYear = anchorDate.tm_year;
    anchorMonth = anchorDate.tm_mon;
    for (uint8_t index = 0; index < count; ++index) {
      const time_t raw =
          static_cast<time_t>(records[index]->activityDay) * 86400;
      tm date = {};
      gmtime_r(&raw, &date);
      if (date.tm_year == anchorYear && date.tm_mon == anchorMonth) {
        monthTotal += records[index]->steps;
        ++monthDays;
      }
    }
  }
  char averageText[20] = "--";
  if (monthDays > 0) {
    formatSteps(static_cast<uint32_t>(monthTotal / monthDays), averageText,
                sizeof(averageText));
  }
  static const char* months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                 "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  char summary[48] = {};
  snprintf(summary, sizeof(summary), "%s AVG  %s STEPS",
           anchorMonth >= 0 ? months[anchorMonth] : "MONTH", averageText);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(summary, M5.Display.width() / 2, 706);
  M5.Display.fillRect(0, 728, M5.Display.width(), 72, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString("A: OLDER     B: NEWER", M5.Display.width() / 2, 750);
  M5.Display.drawString("HOLD A: BACK", M5.Display.width() / 2, 782);
  M5.Display.setTextDatum(top_left);
}

void DashboardView::drawResetConfirmation(bool resetSelected) {
  DisplayBatch batch(epd_mode_t::epd_fast);
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.fillRect(0, 0, M5.Display.width(), 64, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextDatum(middle_left);
  M5.Display.drawString("RESET IMAGE", 18, 32);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString("RESTORE THE DEFAULT IMAGE?", 240, 150);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextDatum(middle_left);
  M5.Display.drawString("RECEIVED IMAGE", 44, 250);
  M5.Display.drawString("CLOCK + STEPS", 44, 315);
  M5.Display.drawString("HISTORY + GOAL", 44, 380);
  M5.Display.setTextDatum(middle_center);
  M5.Display.fillRoundRect(300, 228, 136, 44, 10, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString("DELETE", 368, 250);
  M5.Display.drawRoundRect(300, 293, 136, 44, 10, TFT_BLACK);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.drawString("KEEP", 368, 315);
  M5.Display.drawRoundRect(300, 358, 136, 44, 10, TFT_BLACK);
  M5.Display.drawString("KEEP", 368, 380);
  const auto button = [&](int32_t x, const char* text, bool selected) {
    const uint16_t fill = selected ? TFT_BLACK : TFT_WHITE;
    const uint16_t color = selected ? TFT_WHITE : TFT_BLACK;
    M5.Display.fillRoundRect(x, 430, 180, 90, 12, fill);
    M5.Display.drawRoundRect(x, 430, 180, 90, 12, TFT_BLACK);
    M5.Display.setTextColor(color, fill);
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.drawString(text, x + 90, 475);
  };
  button(40, "CANCEL", !resetSelected);
  button(260, "RESET", resetSelected);
  M5.Display.fillRect(0, 700, M5.Display.width(), 100, TFT_BLACK);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString("A: CHANGE     B: OK", 240, 730);
  M5.Display.drawString("HOLD A: BACK", 240, 772);
  M5.Display.setTextDatum(top_left);
}

void DashboardView::drawNfcWaiting(bool clockOnly) {
  DisplayBatch batch(epd_mode_t::epd_fast);
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.fillRect(0, 0, M5.Display.width(), 64, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextDatum(middle_left);
  M5.Display.drawString(clockOnly ? "SYNC CLOCK" : "RECEIVE IMAGE", 18, 32);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setFont(&fonts::FreeSansBold24pt7b);
  M5.Display.drawString("NFC READY", 240, 210);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.drawString(clockOnly ? "1. OPEN SYNC CLOCK IN APP"
                                  : "1. SELECT IMAGE IN APP",
                        240, 320);
  M5.Display.drawString("2. HOLD PHONE NEAR NFC AREA", 240, 370);
  M5.Display.fillRoundRect(48, 440, 384, 112, 12, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString("KEEP PHONE STILL", 240, 474);
  M5.Display.drawString("UNTIL TRANSFER COMPLETES", 240, 520);
  M5.Display.fillRect(0, 710, M5.Display.width(), 90, TFT_BLACK);
  M5.Display.drawString("B: CANCEL", 240, 755);
  M5.Display.setTextDatum(top_left);
}

void DashboardView::drawMessage(const char* title,
                                const char* message,
                                const char* footer) {
  DisplayBatch batch(epd_mode_t::epd_fast);
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.fillRect(0, 0, M5.Display.width(), 64, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextDatum(middle_left);
  M5.Display.drawString(title != nullptr ? title : "PAPER MONO", 18, 32);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(message != nullptr ? message : "", 240, 330);
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.drawString(footer != nullptr ? footer : "", 240, 758);
  M5.Display.setTextDatum(top_left);
}

bool DashboardView::dateHit(int32_t x, int32_t y) const {
  return x >= 0 && x < M5.Display.width() && y >= 497 && y < 548;
}

bool DashboardView::stepsHit(int32_t x, int32_t y) const {
  return x >= 330 && x < 475 && y >= 548 && y < 665;
}

bool DashboardView::resetHit(int32_t x, int32_t y) const {
  return x >= kResetX - 8 && x < kResetX + kResetW + 8 &&
         y >= kResetY - 8 && y < kResetY + kResetH + 8;
}
