#pragma once

#include <stdint.h>

// Paper Mono's PMIC exposes battery voltage in millivolts.  Smooth the value
// before converting it to a percentage so that small ADC/PMIC fluctuations do
// not make the e-paper status jump between adjacent values.
class BatteryLevelEstimator {
public:
  static constexpr int32_t kEmptyMv = 3300;
  static constexpr int32_t kFullMv = 4200;
  static constexpr int32_t kDeadbandMv = 3;
  static constexpr int32_t kFilterDivisor = 4;

  // Invalid reads do not erase the last known good value.
  bool update(int32_t millivolts) {
    if (millivolts <= 0) {
      return false;
    }

    if (!valid_) {
      filteredMillivolts_ = millivolts;
      valid_ = true;
      return true;
    }

    const int32_t difference = millivolts - filteredMillivolts_;
    const int32_t absoluteDifference = difference < 0 ? -difference : difference;
    if (absoluteDifference > kDeadbandMv) {
      filteredMillivolts_ += difference / kFilterDivisor;
    }
    return true;
  }

  bool valid() const { return valid_; }
  int32_t millivolts() const { return filteredMillivolts_; }

  uint8_t percentage() const {
    if (!valid_ || filteredMillivolts_ <= kEmptyMv) {
      return 0;
    }
    if (filteredMillivolts_ >= kFullMv) {
      return 100;
    }
    return static_cast<uint8_t>(
        (filteredMillivolts_ - kEmptyMv) * 100 / (kFullMv - kEmptyMv));
  }

private:
  int32_t filteredMillivolts_ = 0;
  bool valid_ = false;
};
