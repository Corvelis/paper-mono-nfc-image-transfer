#include "RtcClock.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <sys/time.h>

namespace {
constexpr char kPreferencesNamespace[] = "paper_time";
constexpr char kOffsetKey[] = "offset";
constexpr int64_t kUnix2023 = 1672531200LL;
constexpr int64_t kUnix2100 = 4102444800LL;

bool validRtcFields(const m5::rtc_date_t& date, const m5::rtc_time_t& time) {
  return date.year >= 2023 && date.year <= 2099 &&
         date.month >= 1 && date.month <= 12 &&
         date.date >= 1 && date.date <= 31 &&
         time.hours <= 23 && time.minutes <= 59 && time.seconds <= 59;
}
}  // namespace

void RtcClock::begin() {
  Preferences preferences;
  if (preferences.begin(kPreferencesNamespace, true)) {
    const int32_t stored = preferences.getInt(kOffsetKey, 540);
    if (stored >= -840 && stored <= 840) {
      utcOffsetMinutes_ = static_cast<int16_t>(stored);
    }
    preferences.end();
  }
  valid_ = restoreSystemTimeFromRtc();
}

bool RtcClock::restoreSystemTimeFromRtc() {
  if (!M5.Rtc.isEnabled() || M5.Rtc.getVoltLow()) {
    return false;
  }
  m5::rtc_date_t date;
  m5::rtc_time_t rtcTime;
  if (!M5.Rtc.getDateTime(&date, &rtcTime) || !validRtcFields(date, rtcTime)) {
    return false;
  }
  tm utc = {};
  utc.tm_year = date.year - 1900;
  utc.tm_mon = date.month - 1;
  utc.tm_mday = date.date;
  utc.tm_hour = rtcTime.hours;
  utc.tm_min = rtcTime.minutes;
  utc.tm_sec = rtcTime.seconds;
  setenv("TZ", "UTC0", 1);
  tzset();
  const time_t unixSeconds = mktime(&utc);
  if (unixSeconds < kUnix2023 || unixSeconds >= kUnix2100) {
    return false;
  }
  timeval value = {};
  value.tv_sec = unixSeconds;
  return settimeofday(&value, nullptr) == 0;
}

bool RtcClock::sync(uint64_t unixSeconds, int16_t utcOffsetMinutes) {
  if (!M5.Rtc.isEnabled() || unixSeconds < static_cast<uint64_t>(kUnix2023) ||
      unixSeconds >= static_cast<uint64_t>(kUnix2100) ||
      utcOffsetMinutes < -840 || utcOffsetMinutes > 840) {
    return false;
  }

  const time_t raw = static_cast<time_t>(unixSeconds);
  tm utc = {};
  gmtime_r(&raw, &utc);
  M5.Rtc.setDateTime(&utc);

  timeval value = {};
  value.tv_sec = raw;
  if (settimeofday(&value, nullptr) != 0) {
    return false;
  }

  Preferences preferences;
  if (!preferences.begin(kPreferencesNamespace, false)) {
    return false;
  }
  const size_t written = preferences.putInt(kOffsetKey, utcOffsetMinutes);
  preferences.end();
  if (written != sizeof(int32_t)) {
    return false;
  }
  utcOffsetMinutes_ = utcOffsetMinutes;
  valid_ = true;
  return true;
}

RtcClock::LocalTime RtcClock::localTime() const {
  LocalTime result;
  if (!valid_) {
    return result;
  }
  const time_t utc = time(nullptr);
  if (utc < kUnix2023 || utc >= kUnix2100) {
    return result;
  }
  const time_t adjusted = utc + static_cast<time_t>(utcOffsetMinutes_) * 60;
  gmtime_r(&adjusted, &result.value);
  result.valid = true;
  return result;
}

bool RtcClock::localUnix(uint32_t& value) const {
  if (!valid_) {
    value = 0;
    return false;
  }
  const int64_t adjusted = static_cast<int64_t>(time(nullptr)) +
                           static_cast<int64_t>(utcOffsetMinutes_) * 60LL;
  if (adjusted <= 0 || adjusted > 0xFFFFFFFFLL) {
    value = 0;
    return false;
  }
  value = static_cast<uint32_t>(adjusted);
  return true;
}

bool RtcClock::valid() const {
  return valid_;
}

int16_t RtcClock::utcOffsetMinutes() const {
  return utcOffsetMinutes_;
}

void RtcClock::formatZone(char* output, size_t outputSize) const {
  if (output == nullptr || outputSize == 0) {
    return;
  }
  if (utcOffsetMinutes_ == 540) {
    strlcpy(output, "JST", outputSize);
    return;
  }
  const int offset = utcOffsetMinutes_;
  const char sign = offset < 0 ? '-' : '+';
  const int absolute = offset < 0 ? -offset : offset;
  snprintf(output, outputSize, "UTC%c%d:%02d", sign, absolute / 60,
           absolute % 60);
}

bool RtcClock::nfcTimeSync(uint64_t unixSeconds,
                           int16_t utcOffsetMinutes,
                           void* context) {
  auto* clock = static_cast<RtcClock*>(context);
  return clock != nullptr && clock->sync(unixSeconds, utcOffsetMinutes);
}
