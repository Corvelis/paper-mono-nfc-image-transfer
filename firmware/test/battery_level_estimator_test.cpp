#include <cassert>

#include "BatteryLevelEstimator.h"

int main() {
  BatteryLevelEstimator unavailable;
  assert(!unavailable.update(0));
  assert(!unavailable.valid());

  BatteryLevelEstimator empty;
  assert(empty.update(3300));
  assert(empty.percentage() == 0);

  BatteryLevelEstimator half;
  assert(half.update(3750));
  assert(half.percentage() == 50);

  BatteryLevelEstimator full;
  assert(full.update(4200));
  assert(full.percentage() == 100);

  BatteryLevelEstimator filtered;
  assert(filtered.update(3600));
  assert(filtered.update(4000));
  assert(filtered.millivolts() == 3700);
  assert(filtered.percentage() == 44);

  assert(filtered.update(3703));
  assert(filtered.millivolts() == 3700);

  assert(!filtered.update(-1));
  assert(filtered.valid());
  assert(filtered.millivolts() == 3700);
  assert(filtered.percentage() == 44);
}
