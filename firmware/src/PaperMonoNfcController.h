#pragma once

#include <stdint.h>

#include "PaperMonoNfcProtocol.h"

class PaperMonoNfcController {
public:
  static constexpr uint8_t kMaxReceivedImages = 17;

  using TimeSyncHandler = bool (*)(uint64_t unixSeconds,
                                   int16_t utcOffsetMinutes,
                                   void* context);

  struct StoredImage {
    bool valid = false;
    PaperMonoNfcProtocol::ImageMode mode =
      PaperMonoNfcProtocol::ImageMode::Dashboard;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t size = 0;
    uint32_t crc32 = 0;
    char path[20] = {};
    uint8_t slot = 0;
    uint32_t sequence = 0;
  };

  PaperMonoNfcController();
  ~PaperMonoNfcController();

  // Reads the last committed slot. NFC hardware is initialized lazily when
  // setModeActive(true) is first called.
  void begin();
  void setTimeSyncHandler(TimeSyncHandler handler, void* context);
  bool setModeActive(bool active);
  void cancelActiveTransfer();
  void powerDown();
  void update();

  bool modeActive() const;
  bool hardwareReady() const;
  bool getStoredImage(StoredImage& image) const;
  uint8_t storedImageCount() const;
  bool getStoredImageAt(uint8_t newestIndex, StoredImage& image) const;
  bool selectStoredImageAt(uint8_t newestIndex);
  bool selectDefaultImage();
  bool defaultImageSelected() const;
  bool deleteStoredImageAt(uint8_t newestIndex);
  bool deleteStoredImages(uint32_t newestIndexMask);
  // Stops reception, removes the committed image and resets the transfer
  // state. The caller may then explicitly re-enable reception.
  bool clearStoredImage();
  PaperMonoNfcProtocol::Phase phase() const;
  PaperMonoNfcProtocol::Status status() const;
  uint32_t receivedBytes() const;
  uint32_t totalBytes() const;
  uint32_t uiRevision() const;
  uint32_t timeSyncRevision() const;

  // The display owner calls this after rendering a newly stored image.
  void markDisplayed(bool success);

  const char* phaseName() const;
  const char* statusName() const;

private:
  struct Impl;
  Impl* impl_ = nullptr;
};
