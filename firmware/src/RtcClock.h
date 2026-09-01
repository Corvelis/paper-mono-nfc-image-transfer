#pragma once

#include <stdint.h>
#include <time.h>

class RtcClock {
public:
  struct LocalTime {
    tm value = {};
    bool valid = false;
  };

  void begin();
  bool sync(uint64_t unixSeconds, int16_t utcOffsetMinutes);
  LocalTime localTime() const;
  bool localUnix(uint32_t& value) const;
  bool valid() const;
  int16_t utcOffsetMinutes() const;
  void formatZone(char* output, size_t outputSize) const;

  static bool nfcTimeSync(uint64_t unixSeconds,
                          int16_t utcOffsetMinutes,
                          void* context);

private:
  bool restoreSystemTimeFromRtc();
  bool valid_ = false;
  int16_t utcOffsetMinutes_ = 540;
};
