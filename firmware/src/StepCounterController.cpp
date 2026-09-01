#include "StepCounterController.h"

#include <math.h>
#include <string.h>

namespace {
constexpr const char* kPrefsNamespace = "steps";
constexpr const char* kStateKey = "state";
constexpr uint32_t kStateMagic = 0x53544550UL; // "STEP"
constexpr uint16_t kStateVersion = 2;
constexpr uint32_t kUnknownActivityDay = 0;
constexpr uint32_t kSecondsPerDay = 24UL * 60UL * 60UL;
constexpr uint32_t kDayStartSeconds = STEP_COUNTER_DAY_START_HOUR * 60UL * 60UL;

struct PersistedStepState {
  uint32_t magic;
  uint16_t version;
  uint16_t recordCount;
  uint32_t currentActivityDay;
  uint32_t configuredGoalSteps;
  StepDayRecord records[STEP_COUNTER_HISTORY_DAYS];
  uint32_t checksum;
};

bool validActivityDay(uint32_t activityDay) {
  return activityDay != kUnknownActivityDay;
}

uint8_t stepsFromDetectedCycles(uint8_t cycles, uint16_t& remainder) {
  constexpr uint32_t kScaleDenominator =
    STEP_COUNTER_STEP_SCALE_DENOMINATOR > 0 ? STEP_COUNTER_STEP_SCALE_DENOMINATOR : 1;
  const uint32_t scaled =
    static_cast<uint32_t>(cycles) * STEP_COUNTER_STEP_SCALE_NUMERATOR + remainder;
  const uint32_t steps = scaled / kScaleDenominator;
  remainder = static_cast<uint16_t>(scaled % kScaleDenominator);
  return static_cast<uint8_t>(min<uint32_t>(steps, 255));
}
}

void StepCounterController::begin(Preferences* preferences) {
  preferences_ = preferences;
  load();
}

void StepCounterController::update(unsigned long now, const m5::imu_data_t& data, bool imuUpdated,
                                   uint32_t localUnix, bool timeValid) {
  if (timeValid) {
    const uint32_t activityDay = activityDayFromLocalUnix(localUnix);
    if (validActivityDay(activityDay)) {
      timeKnown_ = true;
      applyActivityDay(activityDay, now);
    }
  }

  if (imuUpdated) {
    const uint8_t steps = detectSteps(data, now);
    if (steps > 0) {
      StepDayRecord& record = records_[todayIndex_];
      const uint32_t before = record.steps;
      record.steps = min<uint32_t>(0xFFFFFFFEUL, record.steps + steps);
      if (record.steps != before) {
        markDirty();
      }
    }
  }

  if (dirty_ && shouldSave(now)) {
    save(now, false);
  }
}

uint32_t StepCounterController::todaySteps() const {
  return todayIndex_ < recordCount_ ? records_[todayIndex_].steps : 0;
}

uint32_t StepCounterController::goalSteps() const {
  return configuredGoalSteps_;
}

bool StepCounterController::setGoalSteps(uint32_t goalSteps, unsigned long now) {
  if (goalSteps < PAPER_MONO_MIN_STEP_GOAL ||
      goalSteps > PAPER_MONO_MAX_STEP_GOAL ||
      goalSteps % PAPER_MONO_STEP_GOAL_INCREMENT != 0) {
    return false;
  }
  if (configuredGoalSteps_ == goalSteps) {
    return true;
  }
  configuredGoalSteps_ = goalSteps;
  if (todayIndex_ < recordCount_) {
    records_[todayIndex_].goalSteps = goalSteps;
  }
  markDirty();
  save(now, true);
  return true;
}

void StepCounterController::saveNow(unsigned long now) {
  save(now, true);
}

bool StepCounterController::todayValid() const {
  return timeKnown_ && validActivityDay(currentActivityDay_);
}

uint32_t StepCounterController::currentActivityDay() const {
  return currentActivityDay_;
}

uint8_t StepCounterController::historyCount() const {
  return recordCount_;
}

const StepDayRecord* StepCounterController::recordAt(uint8_t index) const {
  return index < recordCount_ ? &records_[index] : nullptr;
}

void StepCounterController::writeHistory(JsonArray target) const {
  for (uint8_t i = 0; i < recordCount_; ++i) {
    if (!validActivityDay(records_[i].activityDay)) {
      continue;
    }
    JsonObject item = target.add<JsonObject>();
    item["activityDay"] = records_[i].activityDay;
    item["steps"] = records_[i].steps;
    item["goalSteps"] = records_[i].goalSteps;
  }
}

void StepCounterController::load() {
  recordCount_ = 0;
  todayIndex_ = 0;
  currentActivityDay_ = kUnknownActivityDay;
  configuredGoalSteps_ = PAPER_MONO_DEFAULT_STEP_GOAL;
  timeKnown_ = false;
  dirty_ = false;
  lastSaveMs_ = 0;
  lastSavedSteps_ = 0;

  if (preferences_ != nullptr) {
    preferences_->begin(kPrefsNamespace, true);
    const size_t length = preferences_->getBytesLength(kStateKey);
    if (length == sizeof(PersistedStepState)) {
      PersistedStepState state = {};
      const size_t read = preferences_->getBytes(kStateKey, &state, sizeof(state));
      const uint32_t expectedChecksum =
        checksumState(reinterpret_cast<const uint8_t*>(&state), sizeof(state) - sizeof(state.checksum));
      if (read == sizeof(state) &&
          state.magic == kStateMagic &&
          state.version == kStateVersion &&
          state.recordCount <= kHistoryDays &&
          state.checksum == expectedChecksum) {
        currentActivityDay_ = state.currentActivityDay;
        configuredGoalSteps_ = state.configuredGoalSteps;
        if (configuredGoalSteps_ < PAPER_MONO_MIN_STEP_GOAL ||
            configuredGoalSteps_ > PAPER_MONO_MAX_STEP_GOAL ||
            configuredGoalSteps_ % PAPER_MONO_STEP_GOAL_INCREMENT != 0) {
          configuredGoalSteps_ = PAPER_MONO_DEFAULT_STEP_GOAL;
        }
        recordCount_ = static_cast<uint8_t>(state.recordCount);
        for (uint8_t i = 0; i < recordCount_; ++i) {
          records_[i] = state.records[i];
          if (records_[i].goalSteps == 0) {
            records_[i].goalSteps = configuredGoalSteps_;
          }
        }
      }
    }
    preferences_->end();
  }

  if (recordCount_ == 0) {
    records_[0] = {currentActivityDay_, 0, configuredGoalSteps_};
    recordCount_ = 1;
  }

  int8_t currentIndex = findRecord(currentActivityDay_);
  if (currentIndex < 0) {
    todayIndex_ = ensureRecord(currentActivityDay_);
  } else {
    todayIndex_ = static_cast<uint8_t>(currentIndex);
  }
  lastSavedSteps_ = todaySteps();
}

void StepCounterController::save(unsigned long now, bool force) {
  if (!force && !dirty_) {
    return;
  }
  if (preferences_ == nullptr) {
    return;
  }

  PersistedStepState state = {};
  state.magic = kStateMagic;
  state.version = kStateVersion;
  state.recordCount = recordCount_;
  state.currentActivityDay = currentActivityDay_;
  state.configuredGoalSteps = configuredGoalSteps_;
  for (uint8_t i = 0; i < recordCount_; ++i) {
    state.records[i] = records_[i];
  }
  state.checksum = checksumState(reinterpret_cast<const uint8_t*>(&state),
                                 sizeof(state) - sizeof(state.checksum));

  preferences_->begin(kPrefsNamespace, false);
  const size_t written = preferences_->putBytes(kStateKey, &state, sizeof(state));
  preferences_->end();

  if (written == sizeof(state)) {
    dirty_ = false;
    lastSaveMs_ = now;
    lastSavedSteps_ = todaySteps();
  }
}

bool StepCounterController::shouldSave(unsigned long now) const {
  if (lastSaveMs_ == 0) {
    return true;
  }

  const uint32_t steps = todaySteps();
  if (steps >= lastSavedSteps_ &&
      steps - lastSavedSteps_ >= STEP_COUNTER_SAVE_STEP_DELTA) {
    return true;
  }
  if (steps < lastSavedSteps_) {
    return true;
  }
  return now - lastSaveMs_ >= STEP_COUNTER_SAVE_INTERVAL_MS;
}

void StepCounterController::markDirty() {
  dirty_ = true;
}

uint32_t StepCounterController::activityDayFromLocalUnix(uint32_t localUnix) const {
  if (localUnix < kDayStartSeconds) {
    return kUnknownActivityDay;
  }
  return (localUnix - kDayStartSeconds) / kSecondsPerDay;
}

void StepCounterController::applyActivityDay(uint32_t activityDay, unsigned long now) {
  if (activityDay == currentActivityDay_) {
    return;
  }

  if (!validActivityDay(currentActivityDay_)) {
    const int8_t unknownIndex = findRecord(kUnknownActivityDay);
    const int8_t existingIndex = findRecord(activityDay);
    currentActivityDay_ = activityDay;
    if (unknownIndex >= 0 && existingIndex < 0) {
      records_[unknownIndex].activityDay = activityDay;
      todayIndex_ = static_cast<uint8_t>(unknownIndex);
      markDirty();
      save(now, true);
      return;
    }
    if (unknownIndex >= 0 && existingIndex >= 0 && unknownIndex != existingIndex) {
      StepDayRecord& existing = records_[existingIndex];
      existing.steps = min<uint32_t>(0xFFFFFFFEUL,
                                     existing.steps + records_[unknownIndex].steps);
      records_[unknownIndex].steps = 0;
      todayIndex_ = static_cast<uint8_t>(existingIndex);
      markDirty();
      save(now, true);
      return;
    }
  }

  currentActivityDay_ = activityDay;
  todayIndex_ = ensureRecord(activityDay);
  markDirty();
  save(now, true);
}

int8_t StepCounterController::findRecord(uint32_t activityDay) const {
  for (uint8_t i = 0; i < recordCount_; ++i) {
    if (records_[i].activityDay == activityDay) {
      return static_cast<int8_t>(i);
    }
  }
  return -1;
}

uint8_t StepCounterController::ensureRecord(uint32_t activityDay) {
  const int8_t existing = findRecord(activityDay);
  if (existing >= 0) {
    return static_cast<uint8_t>(existing);
  }

  uint8_t index = recordCount_;
  if (recordCount_ < kHistoryDays) {
    ++recordCount_;
  } else {
    index = chooseEvictionIndex();
  }
  records_[index] = {activityDay, 0, configuredGoalSteps_};
  markDirty();
  return index;
}

uint8_t StepCounterController::chooseEvictionIndex() const {
  for (uint8_t i = 0; i < recordCount_; ++i) {
    if (!validActivityDay(records_[i].activityDay)) {
      return i;
    }
  }

  uint8_t oldestIndex = 0;
  for (uint8_t i = 1; i < recordCount_; ++i) {
    if (records_[i].activityDay < records_[oldestIndex].activityDay) {
      oldestIndex = i;
    }
  }
  return oldestIndex;
}

uint8_t StepCounterController::detectSteps(const m5::imu_data_t& data, unsigned long now) {
  constexpr uint8_t kWalkStartCandidates = STEP_COUNTER_WALK_START_CANDIDATES;
  constexpr uint32_t kMinStepIntervalMs = STEP_COUNTER_MIN_STEP_INTERVAL_MS;
  constexpr uint32_t kMaxStepIntervalMs = STEP_COUNTER_MAX_STEP_INTERVAL_MS;
  constexpr uint32_t kWalkingTimeoutMs = STEP_COUNTER_WALKING_TIMEOUT_MS;

  const float ax = data.accel.x;
  const float ay = data.accel.y;
  const float az = data.accel.z;
  const float mag2 = ax * ax + ay * ay + az * az;
  Detector& s = detector_;
#if STEP_COUNTER_DIAG_LOG_ENABLED
  ++s.diagSamples;
#endif
  if (!isfinite(mag2) || mag2 < 0.25f * 0.25f || mag2 > 3.5f * 3.5f) {
#if STEP_COUNTER_DIAG_LOG_ENABLED
    ++s.diagInvalidSamples;
#endif
    return 0;
  }

  const float magnitude = sqrtf(mag2);
  if (!s.initialized) {
    s.gx = ax;
    s.gy = ay;
    s.gz = az;
    s.magnitudeBaseline = magnitude;
    s.lastSampleMs = now;
#if STEP_COUNTER_DIAG_LOG_ENABLED
    s.diagStartedMs = now;
#endif
    s.initialized = true;
    return 0;
  }

  const uint32_t sampleGapMs = static_cast<uint32_t>(now - s.lastSampleMs);
  s.lastSampleMs = now;
#if STEP_COUNTER_DIAG_LOG_ENABLED
  s.diagMaxSampleGapMs = max<uint32_t>(s.diagMaxSampleGapMs, sampleGapMs);
#endif
  if (sampleGapMs > STEP_COUNTER_SAMPLE_GAP_RESET_MS) {
    // A display refresh or light sleep can leave a stale threshold crossing in
    // progress. Restart the sequence so the first sample after the pause is
    // never paired with motion from before the pause.
    s.gx = ax;
    s.gy = ay;
    s.gz = az;
    s.magnitudeBaseline = magnitude;
    s.energyLP = 0.0f;
    s.noiseLP = 0.0f;
    s.high = false;
    s.lastCandidateMs = 0;
    s.lastStepMs = 0;
    s.pendingSteps = 0;
    s.stepScaleRemainder = 0;
    s.walking = false;
    return 0;
  }

  if (s.walking && now - s.lastStepMs > kWalkingTimeoutMs) {
    s.walking = false;
    s.pendingSteps = 0;
    s.stepScaleRemainder = 0;
  }

  const float alphaG = 0.02f;
  s.gx += alphaG * (ax - s.gx);
  s.gy += alphaG * (ay - s.gy);
  s.gz += alphaG * (az - s.gz);

#if STEP_COUNTER_MAGNITUDE_RISE_ONLY
  // Paper Mono is normally carried as a relatively large flat device. Using
  // the vector delta makes a simple tilt look like acceleration, and squaring
  // both sides of a gait wave can create two or more candidates per step.
  // Magnitude is orientation-independent; keeping only its positive excursion
  // counts the impact side of the wave once and ignores the rebound.
  s.magnitudeBaseline += alphaG * (magnitude - s.magnitudeBaseline);
  const float magnitudeRise = magnitude - s.magnitudeBaseline;
  const float energy = magnitudeRise > 0.0f
                         ? magnitudeRise * magnitudeRise
                         : 0.0f;
#else
  const float dx = ax - s.gx;
  const float dy = ay - s.gy;
  const float dz = az - s.gz;
  const float energy = dx * dx + dy * dy + dz * dz;
#endif

  s.energyLP = s.energyLP * 0.70f + energy * 0.30f;
  s.noiseLP = s.noiseLP * 0.98f + s.energyLP * 0.02f;

  float hi = s.noiseLP * STEP_COUNTER_DYNAMIC_ENERGY_MULTIPLIER;
  if (hi < STEP_COUNTER_DYNAMIC_ENERGY_MIN) {
    hi = STEP_COUNTER_DYNAMIC_ENERGY_MIN;
  }
  const float lo = hi * STEP_COUNTER_LOW_THRESHOLD_RATIO;

#if STEP_COUNTER_DIAG_LOG_ENABLED
  if (s.diagStartedMs == 0) {
    s.diagStartedMs = now;
  } else if (now - s.diagStartedMs >= STEP_COUNTER_DIAG_LOG_INTERVAL_MS) {
    Serial.printf("[steps.detector] samples=%lu candidates=%u accepted=%u invalid=%u "
                  "max_gap_ms=%lu mag=%.3f energy=%.5f noise=%.5f hi=%.5f "
                  "high=%d walking=%d pending=%u\n",
                  static_cast<unsigned long>(s.diagSamples),
                  static_cast<unsigned>(s.diagCandidates),
                  static_cast<unsigned>(s.diagAcceptedSteps),
                  static_cast<unsigned>(s.diagInvalidSamples),
                  static_cast<unsigned long>(s.diagMaxSampleGapMs),
                  static_cast<double>(magnitude),
                  static_cast<double>(s.energyLP),
                  static_cast<double>(s.noiseLP),
                  static_cast<double>(hi),
                  s.high ? 1 : 0,
                  s.walking ? 1 : 0,
                  static_cast<unsigned>(s.pendingSteps));
    s.diagStartedMs = now;
    s.diagSamples = 0;
    s.diagCandidates = 0;
    s.diagAcceptedSteps = 0;
    s.diagInvalidSamples = 0;
    s.diagMaxSampleGapMs = 0;
  }
#endif

  if (!s.high && s.energyLP > hi) {
    s.high = true;
    return 0;
  }

  if (!s.high || s.energyLP >= lo) {
    return 0;
  }

  s.high = false;
#if STEP_COUNTER_DIAG_LOG_ENABLED
  ++s.diagCandidates;
#endif
  if (s.lastCandidateMs == 0) {
    s.lastCandidateMs = now;
    s.pendingSteps = 1;
    return 0;
  }

  const uint32_t interval = static_cast<uint32_t>(now - s.lastCandidateMs);
  s.lastCandidateMs = now;
  const bool plausible = interval >= kMinStepIntervalMs && interval <= kMaxStepIntervalMs;
  if (!plausible) {
    s.pendingSteps = 1;
    s.walking = false;
    s.stepScaleRemainder = 0;
    return 0;
  }

  if (!s.walking) {
    if (s.pendingSteps < 250) {
      ++s.pendingSteps;
    }
    if (s.pendingSteps >= kWalkStartCandidates) {
      const uint8_t detected = s.pendingSteps;
      s.walking = true;
      s.pendingSteps = 0;
      s.lastStepMs = now;
      const uint8_t steps = stepsFromDetectedCycles(detected, s.stepScaleRemainder);
#if STEP_COUNTER_DIAG_LOG_ENABLED
      s.diagAcceptedSteps += steps;
#endif
      return steps;
    }
    return 0;
  }

  s.lastStepMs = now;
  const uint8_t steps = stepsFromDetectedCycles(1, s.stepScaleRemainder);
#if STEP_COUNTER_DIAG_LOG_ENABLED
  s.diagAcceptedSteps += steps;
#endif
  return steps;
}

uint32_t StepCounterController::checksumState(const uint8_t* data, size_t length) const {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; ++i) {
    hash ^= data[i];
    hash *= 16777619UL;
  }
  return hash;
}
