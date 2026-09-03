#include "PaperMonoNfcController.h"

#include "config.h"

#if STACKCHAN_DEVICE_PAPERMONO && STACKCHAN_HAS_NFC

#include <Arduino.h>
#include <LittleFS.h>
#include <M5Unified.h>
#include <M5UnitUnified.h>
#include <M5UnitUnifiedNFC.h>
#include <utility/M5IOE1_Class.hpp>
#include <wiring/m5_unit_unified_wiring.hpp>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstring>
#include <new>

namespace Protocol = PaperMonoNfcProtocol;

namespace {

constexpr char kPreferencesNamespace[] = "paper_nfc";
constexpr char kPreferencesKey[] = "active";
constexpr char kCatalogKeyA[] = "catalog_a";
constexpr char kCatalogKeyB[] = "catalog_b";
constexpr char kLegacySlotPaths[][22] = {"/nfc_image_a.jpg", "/nfc_image_b.jpg"};
constexpr uint8_t kCatalogImageSlots = 18;
constexpr uint8_t kDefaultImageSlot = 0xFF;
constexpr uint32_t kPersistSignature = 0x3143464EUL;  // "NFC1" in LE.
constexpr uint16_t kPersistVersion = 1;
constexpr uint32_t kCatalogSignature = 0x3243464EUL;  // "NFC2" in LE.
constexpr uint16_t kCatalogVersion = 2;
constexpr size_t kWorkChunkBytes = 4096;
constexpr uint8_t kPaperMonoUid[] = {0x04, 0x50, 0x41, 0x50, 0x45, 0x52, 0x01};

#pragma pack(push, 1)
struct PersistedImageRecord {
  uint32_t signature = kPersistSignature;
  uint16_t version = kPersistVersion;
  uint8_t activeSlot = 0;
  uint8_t imageMode = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t size = 0;
  uint32_t imageCrc32 = 0;
  uint32_t transferId = 0;
  uint32_t recordCrc32 = 0;
};

struct CatalogEntry {
  uint8_t valid = 0;
  uint8_t slot = 0;
  uint8_t imageMode = 0;
  uint8_t reserved = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t size = 0;
  uint32_t imageCrc32 = 0;
  uint32_t transferId = 0;
  uint32_t sequence = 0;
};

struct CatalogRecord {
  uint32_t signature = kCatalogSignature;
  uint16_t version = kCatalogVersion;
  uint8_t activeSlot = kDefaultImageSlot;
  uint8_t count = 0;
  uint32_t generation = 0;
  uint32_t nextSequence = 1;
  CatalogEntry entries[PaperMonoNfcController::kMaxReceivedImages] = {};
  uint32_t recordCrc32 = 0;
};
#pragma pack(pop)

static_assert(sizeof(PersistedImageRecord) == 28, "Unexpected NFC metadata layout");
static_assert(sizeof(CatalogEntry) == 24, "Unexpected NFC catalog entry layout");

uint32_t persistedRecordCrc(const PersistedImageRecord& record) {
  return Protocol::crc32(reinterpret_cast<const uint8_t*>(&record),
                         offsetof(PersistedImageRecord, recordCrc32));
}

uint32_t catalogRecordCrc(const CatalogRecord& record) {
  return Protocol::crc32(reinterpret_cast<const uint8_t*>(&record),
                         offsetof(CatalogRecord, recordCrc32));
}

void slotPath(uint8_t slot, char* output, size_t outputSize) {
  snprintf(output, outputSize, "/nfc_%02u.jpg", static_cast<unsigned>(slot));
}

bool isExpectedGeometry(Protocol::ImageMode mode, uint16_t width, uint16_t height) {
  return (mode == Protocol::ImageMode::Dashboard && width == 386 && height == 386) ||
         (mode == Protocol::ImageMode::Fullscreen && width == 480 && height == 800);
}

const char* phaseName(Protocol::Phase phase) {
  switch (phase) {
    case Protocol::Phase::Idle: return "IDLE";
    case Protocol::Phase::Receiving: return "RECEIVING";
    case Protocol::Phase::Verifying: return "VERIFYING";
    case Protocol::Phase::Persisting: return "PERSISTING";
    case Protocol::Phase::Stored: return "STORED";
    case Protocol::Phase::Displaying: return "DISPLAYING";
    case Protocol::Phase::Completed: return "COMPLETED";
    case Protocol::Phase::Error: return "ERROR";
  }
  return "UNKNOWN";
}

const char* statusName(Protocol::Status status) {
  switch (status) {
    case Protocol::Status::Ok: return "OK";
    case Protocol::Status::Accepted: return "ACCEPTED";
    case Protocol::Status::Busy: return "BUSY";
    case Protocol::Status::Conflict: return "CONFLICT";
    case Protocol::Status::BadMagic: return "BAD_MAGIC";
    case Protocol::Status::UnsupportedVersion: return "UNSUPPORTED_VERSION";
    case Protocol::Status::UnknownCommand: return "UNKNOWN_COMMAND";
    case Protocol::Status::InvalidLength: return "INVALID_LENGTH";
    case Protocol::Status::PayloadTooLarge: return "PAYLOAD_TOO_LARGE";
    case Protocol::Status::ImageTooLarge: return "IMAGE_TOO_LARGE";
    case Protocol::Status::BadOffset: return "BAD_OFFSET";
    case Protocol::Status::DataMismatch: return "DATA_MISMATCH";
    case Protocol::Status::CrcMismatch: return "CRC_MISMATCH";
    case Protocol::Status::InvalidJpeg: return "INVALID_JPEG";
    case Protocol::Status::UnsupportedFormat: return "UNSUPPORTED_FORMAT";
    case Protocol::Status::NotFound: return "NOT_FOUND";
    case Protocol::Status::InternalError: return "INTERNAL_ERROR";
    case Protocol::Status::Receiving: return "RECEIVING";
    case Protocol::Status::Verifying: return "VERIFYING";
    case Protocol::Status::Stored: return "STORED";
    case Protocol::Status::Displaying: return "DISPLAYING";
    case Protocol::Status::Completed: return "COMPLETED";
  }
  return "UNKNOWN";
}

const char* emulationStateName(m5::nfc::EmulationLayerA::State state) {
  switch (state) {
    case m5::nfc::EmulationLayerA::State::None: return "None";
    case m5::nfc::EmulationLayerA::State::Off: return "Off";
    case m5::nfc::EmulationLayerA::State::Idle: return "Idle";
    case m5::nfc::EmulationLayerA::State::Ready: return "Ready";
    case m5::nfc::EmulationLayerA::State::Active: return "Active";
    case m5::nfc::EmulationLayerA::State::Halt: return "Halt";
  }
  return "Unknown";
}

}  // namespace

struct PaperMonoNfcController::Impl {
  struct Transfer {
    uint32_t id = 0;
    uint8_t flags = 0;
    Protocol::ImageMode mode = Protocol::ImageMode::Dashboard;
    Protocol::ImageFormat format = Protocol::ImageFormat::JpegBaselineRgb;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t size = 0;
    uint32_t crc32 = 0;
    uint32_t nextOffset = 0;
    uint32_t lastActivityMs = 0;
  };

  class CommandEmulation final : public m5::nfc::EmulationLayerA {
  public:
    CommandEmulation(m5::unit::UnitNFC& unit, Impl& owner)
      : m5::nfc::EmulationLayerA(unit), unit_(unit), owner_(owner) {}

    State receive_callback(const uint8_t* rx, uint32_t length) override {
      owner_.lastReceivedFrameLength = length;
      owner_.lastReceivedFrameFirstByte = length > 0 ? rx[0] : 0;
      ++owner_.receivedFrameCount;

      if (length < Protocol::kCommonHeaderBytes ||
          rx[0] != Protocol::kMagic0 || rx[1] != Protocol::kMagic1) {
        // In particular, let the base layer handle HLTA normally. Core NFC
        // halts the tag after discovery, then session.connect() wakes and
        // selects it again before the first application command.
        return EmulationLayerA::receive_callback(rx, length);
      }

      uint8_t response[32] = {};
      const size_t responseLength = owner_.handleCommand(rx, length, response, sizeof(response));
      return responseLength > 0 &&
             unit_.nfcaEmulationTransmit(response, static_cast<uint16_t>(responseLength))
               ? State::Active
               : State::Idle;
    }

  private:
    m5::unit::UnitNFC& unit_;
    Impl& owner_;
  };

  m5::unit::UnitUnified units;
  m5::unit::UnitNFC unit;
  CommandEmulation emulation{unit, *this};
  m5::nfc::a::PICC picc;
  // A valid small NDEF record keeps the emulated Ultralight recognizable to
  // stock phone NFC stacks before the sender starts issuing custom commands.
  // UID/BCC bytes in pages 0-2 are overwritten during initialization.
  // Ultralight EV1 48B has 20 pages (80 bytes total) and a 48-byte user area.
  // Advertising the concrete EV1 type gives Core NFC a valid GET_VERSION
  // response; the generic Ultralight type intentionally has none in
  // M5Unit-NFC and sends iOS down an unstable Ultralight-C probe path.
  uint8_t piccMemory[80] = {
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0xA3, 0x00, 0x00,
    0xE1, 0x10, 0x06, 0x00,
    0x03, 0x10, 0xD1, 0x01,
    0x0C, 0x54, 0x02, 0x65,
    0x6E, 0x50, 0x61, 0x70,
    0x65, 0x72, 0x4D, 0x4F,
    0x4E, 0x4F, 0xFE, 0x00,
  };

  bool begun = false;
  bool unitRegistered = false;
  bool modeActive = false;
  bool hardwareReady = false;
  bool emulationActive = false;
  m5::nfc::EmulationLayerA::State lastEmulationState =
    m5::nfc::EmulationLayerA::State::None;
  uint32_t receivedFrameCount = 0;
  uint32_t loggedReceivedFrameCount = 0;
  uint32_t lastReceivedFrameLength = 0;
  uint8_t lastReceivedFrameFirstByte = 0;
  uint32_t lastWaitingFieldDiagAtMs = 0;
  Protocol::Phase phase = Protocol::Phase::Idle;
  Protocol::Status status = Protocol::Status::Ok;
  Transfer transfer;
  uint8_t* imageBuffer = nullptr;
  uint32_t verifyOffset = 0;
  uint32_t verifyCrc = 0xFFFFFFFFUL;
  uint32_t persistOffset = 0;
  uint8_t pendingSlot = 0;
  bool pendingSlotValid = false;
  File pendingFile;
  CatalogRecord catalog;
  bool catalogLoaded = false;
  PaperMonoNfcController::StoredImage stored;
  uint32_t storedTransferId = 0;
  uint32_t storedAtMs = 0;
  uint32_t displayRequestedAtMs = 0;
  uint32_t uiRevision = 1;
  PaperMonoNfcController::TimeSyncHandler timeSyncHandler = nullptr;
  void* timeSyncContext = nullptr;
  uint32_t timeSyncRevision = 0;

  ~Impl() {
    if (pendingFile) {
      pendingFile.close();
    }
    releaseBuffer();
  }

  void setResult(Protocol::Phase newPhase, Protocol::Status newStatus, bool notifyUi = false) {
    phase = newPhase;
    status = newStatus;
    if (notifyUi) {
      ++uiRevision;
    }
  }

  void releaseBuffer() {
    if (imageBuffer != nullptr) {
      heap_caps_free(imageBuffer);
      imageBuffer = nullptr;
    }
  }

  void cancelWork(bool removePendingFile) {
    const bool removeSlot = removePendingFile && pendingSlotValid;
    if (pendingFile) {
      pendingFile.close();
    }
    if (removeSlot) {
      char path[20] = {};
      slotPath(pendingSlot, path, sizeof(path));
      LittleFS.remove(path);
    }
    pendingSlotValid = false;
    releaseBuffer();
    verifyOffset = 0;
    persistOffset = 0;
  }

  void resetTransfer(Protocol::Status newStatus = Protocol::Status::Ok) {
    cancelWork(false);
    transfer = {};
    setResult(Protocol::Phase::Idle, newStatus);
  }

  bool validCatalogRecord(const CatalogRecord& record) const {
    if (record.signature != kCatalogSignature || record.version != kCatalogVersion ||
        record.count > PaperMonoNfcController::kMaxReceivedImages ||
        record.recordCrc32 != catalogRecordCrc(record)) {
      return false;
    }
    uint8_t count = 0;
    bool activeFound = record.activeSlot == kDefaultImageSlot;
    for (const auto& entry : record.entries) {
      if (!entry.valid) continue;
      if (entry.slot >= kCatalogImageSlots || entry.size == 0 ||
          entry.size > Protocol::kMaxImageBytes ||
          !isExpectedGeometry(static_cast<Protocol::ImageMode>(entry.imageMode),
                              entry.width, entry.height)) {
        return false;
      }
      ++count;
      activeFound = activeFound || entry.slot == record.activeSlot;
    }
    return count == record.count && activeFound;
  }

  bool readCatalogRecord(Preferences& preferences,
                         const char* key,
                         CatalogRecord& record) const {
    return preferences.getBytesLength(key) == sizeof(record) &&
           preferences.getBytes(key, &record, sizeof(record)) == sizeof(record) &&
           validCatalogRecord(record);
  }

  bool saveCatalog(const CatalogRecord& requested) {
    CatalogRecord next = requested;
    next.signature = kCatalogSignature;
    next.version = kCatalogVersion;
    next.generation = catalog.generation + 1;
    next.recordCrc32 = catalogRecordCrc(next);
    const char* key = (next.generation & 1U) == 0 ? kCatalogKeyA : kCatalogKeyB;
    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, false)) {
      return false;
    }
    const size_t written = preferences.putBytes(key, &next, sizeof(next));
    preferences.end();
    if (written != sizeof(next)) {
      return false;
    }
    catalog = next;
    catalogLoaded = true;
    return true;
  }

  int catalogEntryForSlot(uint8_t slot) const {
    for (uint8_t index = 0; index < PaperMonoNfcController::kMaxReceivedImages; ++index) {
      if (catalog.entries[index].valid && catalog.entries[index].slot == slot) {
        return index;
      }
    }
    return -1;
  }

  int catalogEntryAtNewestIndex(uint8_t newestIndex) const {
    uint8_t order[PaperMonoNfcController::kMaxReceivedImages] = {};
    uint8_t count = 0;
    for (uint8_t index = 0; index < PaperMonoNfcController::kMaxReceivedImages; ++index) {
      if (catalog.entries[index].valid) order[count++] = index;
    }
    for (uint8_t left = 0; left < count; ++left) {
      for (uint8_t right = left + 1; right < count; ++right) {
        if (catalog.entries[order[right]].sequence >
            catalog.entries[order[left]].sequence) {
          std::swap(order[left], order[right]);
        }
      }
    }
    return newestIndex < count ? order[newestIndex] : -1;
  }

  void populateStored(const CatalogEntry& entry) {
    stored.valid = true;
    stored.mode = static_cast<Protocol::ImageMode>(entry.imageMode);
    stored.width = entry.width;
    stored.height = entry.height;
    stored.size = entry.size;
    stored.crc32 = entry.imageCrc32;
    stored.slot = entry.slot;
    stored.sequence = entry.sequence;
    slotPath(entry.slot, stored.path, sizeof(stored.path));
    storedTransferId = entry.transferId;
  }

  void refreshStoredFromCatalog() {
    stored = {};
    storedTransferId = 0;
    if (catalog.activeSlot == kDefaultImageSlot) return;
    const int index = catalogEntryForSlot(catalog.activeSlot);
    if (index >= 0) populateStored(catalog.entries[index]);
  }

  uint8_t findFreeStorageSlot() const {
    for (uint8_t slot = 0; slot < kCatalogImageSlots; ++slot) {
      if (catalogEntryForSlot(slot) < 0 && (!pendingSlotValid || pendingSlot != slot)) {
        return slot;
      }
    }
    return kDefaultImageSlot;
  }

  int oldestCatalogEntry() const {
    int oldest = -1;
    for (uint8_t index = 0; index < PaperMonoNfcController::kMaxReceivedImages; ++index) {
      if (!catalog.entries[index].valid) continue;
      if (oldest < 0 || catalog.entries[index].sequence <
                            catalog.entries[oldest].sequence) {
        oldest = index;
      }
    }
    return oldest;
  }

  int freeCatalogEntry() const {
    for (uint8_t index = 0; index < PaperMonoNfcController::kMaxReceivedImages; ++index) {
      if (!catalog.entries[index].valid) return index;
    }
    return -1;
  }

  bool transferMatches(uint32_t id,
                       Protocol::ImageMode mode,
                       Protocol::ImageFormat format,
                       uint16_t width,
                       uint16_t height,
                       uint32_t size,
                       uint32_t crc32) const {
    return transfer.id == id && transfer.mode == mode && transfer.format == format &&
           transfer.width == width && transfer.height == height &&
           transfer.size == size && transfer.crc32 == crc32;
  }

  bool storedMatches(uint32_t id,
                     Protocol::ImageMode mode,
                     Protocol::ImageFormat format,
                     uint16_t width,
                     uint16_t height,
                     uint32_t size,
                     uint32_t crc32) const {
    return stored.valid && storedTransferId == id &&
           format == Protocol::ImageFormat::JpegBaselineRgb &&
           stored.mode == mode && stored.width == width && stored.height == height &&
           stored.size == size && stored.crc32 == crc32;
  }

  bool transferInProgress() const {
    return phase == Protocol::Phase::Receiving || phase == Protocol::Phase::Verifying ||
           phase == Protocol::Phase::Persisting || phase == Protocol::Phase::Stored ||
           phase == Protocol::Phase::Displaying;
  }

  bool transferCancellable() const {
    return phase == Protocol::Phase::Receiving || phase == Protocol::Phase::Verifying ||
           phase == Protocol::Phase::Persisting;
  }

  bool transferExpired(uint32_t now) const {
    return phase == Protocol::Phase::Receiving &&
           static_cast<uint32_t>(now - transfer.lastActivityMs) > Protocol::kTransferTtlMs;
  }

  void writeResponseHeader(uint8_t* response,
                           Protocol::Command command,
                           Protocol::Status responseStatus,
                           uint32_t transferId,
                           uint32_t nextOffset) const {
    response[0] = Protocol::kMagic0;
    response[1] = Protocol::kMagic1;
    response[2] = Protocol::kVersion;
    response[3] = static_cast<uint8_t>(command) | 0x80U;
    response[4] = static_cast<uint8_t>(responseStatus);
    Protocol::writeLe32(response + 5, transferId);
    Protocol::writeLe32(response + 9, nextOffset);
  }

  Protocol::Status transferWireStatus() const {
    if (phase == Protocol::Phase::Error) {
      return status;
    }
    switch (phase) {
      case Protocol::Phase::Receiving: return Protocol::Status::Receiving;
      case Protocol::Phase::Verifying:
      case Protocol::Phase::Persisting: return Protocol::Status::Verifying;
      case Protocol::Phase::Stored: return Protocol::Status::Stored;
      case Protocol::Phase::Displaying: return Protocol::Status::Displaying;
      case Protocol::Phase::Completed: return Protocol::Status::Completed;
      case Protocol::Phase::Idle:
      case Protocol::Phase::Error: break;
    }
    return status;
  }

  size_t simpleResponse(uint8_t* response,
                        Protocol::Command command,
                        Protocol::Status responseStatus,
                        uint32_t transferId = 0) const {
    writeResponseHeader(response, command, responseStatus, transferId, transfer.nextOffset);
    return Protocol::kResponseHeaderBytes;
  }

  size_t handleBegin(const uint8_t* request, uint32_t length, uint8_t* response) {
    if (length != Protocol::kBeginRequestBytes) {
      return simpleResponse(response, Protocol::Command::Begin, Protocol::Status::InvalidLength);
    }

    const uint32_t id = Protocol::readLe32(request + 4);
    const uint8_t flags = request[8];
    const auto mode = static_cast<Protocol::ImageMode>(request[9]);
    const auto format = static_cast<Protocol::ImageFormat>(request[10]);
    const uint16_t width = Protocol::readLe16(request + 11);
    const uint16_t height = Protocol::readLe16(request + 13);
    const uint32_t size = Protocol::readLe32(request + 15);
    const uint32_t crc32 = Protocol::readLe32(request + 19);
    const bool replace = (flags & Protocol::BeginReplace) != 0;

    if (size > Protocol::kMaxImageBytes) {
      return simpleResponse(response, Protocol::Command::Begin,
                            Protocol::Status::ImageTooLarge, id);
    }
    if (format != Protocol::ImageFormat::JpegBaselineRgb) {
      return simpleResponse(response, Protocol::Command::Begin,
                            Protocol::Status::UnsupportedFormat, id);
    }
    if (id == 0 || size == 0 || !isExpectedGeometry(mode, width, height) ||
        (flags & ~Protocol::BeginReplace) != 0) {
      return simpleResponse(response, Protocol::Command::Begin,
                            Protocol::Status::InvalidArgument, id);
    }

    const uint32_t now = millis();
    if (transferExpired(now)) {
      resetTransfer();
    }

    // A COMMIT response can be lost while the E-paper refresh temporarily
    // stops card emulation. Treat the persisted transfer as authoritative so
    // the sender can reconnect and finish without uploading the image again.
    if (storedTransferId == id && stored.valid) {
      if (storedMatches(id, mode, format, width, height, size, crc32)) {
        writeResponseHeader(response, Protocol::Command::Begin,
                            Protocol::Status::Completed, id, stored.size);
        return Protocol::kResponseHeaderBytes;
      }
      if (!replace) {
        return simpleResponse(response, Protocol::Command::Begin,
                              Protocol::Status::Conflict, id);
      }
    }

    if (transferInProgress()) {
      if (transferMatches(id, mode, format, width, height, size, crc32)) {
        transfer.lastActivityMs = now;
        return simpleResponse(response, Protocol::Command::Begin,
                              Protocol::Status::Ok, id);
      }
      if (transfer.id == id && !replace) {
        return simpleResponse(response, Protocol::Command::Begin,
                              Protocol::Status::Conflict, id);
      }
      if (!replace) {
        return simpleResponse(response, Protocol::Command::Begin,
                              Protocol::Status::Busy, id);
      }
      if (!transferCancellable()) {
        return simpleResponse(response, Protocol::Command::Begin,
                              Protocol::Status::Busy, id);
      }
      cancelWork(true);
    } else if (phase == Protocol::Phase::Completed && transfer.id == id) {
      if (transferMatches(id, mode, format, width, height, size, crc32)) {
        return simpleResponse(response, Protocol::Command::Begin,
                              Protocol::Status::Ok, id);
      }
      if (!replace) {
        return simpleResponse(response, Protocol::Command::Begin,
                              Protocol::Status::Conflict, id);
      }
    }

    releaseBuffer();
    imageBuffer = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (imageBuffer == nullptr) {
      imageBuffer = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_8BIT));
    }
    if (imageBuffer == nullptr) {
      setResult(Protocol::Phase::Error, Protocol::Status::NoMemory, true);
      return simpleResponse(response, Protocol::Command::Begin,
                            Protocol::Status::NoMemory, id);
    }

    transfer = {id, flags, mode, format, width, height, size, crc32, 0, now};
    setResult(Protocol::Phase::Receiving, Protocol::Status::Accepted);
    Serial.printf("[paper.nfc] BEGIN id=%lu mode=%u size=%lu crc=%08lX\n",
                  static_cast<unsigned long>(id), static_cast<unsigned>(mode),
                  static_cast<unsigned long>(size), static_cast<unsigned long>(crc32));
    return simpleResponse(response, Protocol::Command::Begin,
                          Protocol::Status::Accepted, id);
  }

  size_t handleData(const uint8_t* request, uint32_t length, uint8_t* response) {
    if (length < Protocol::kDataHeaderBytes || length > Protocol::kMaxCommandBytes) {
      return simpleResponse(response, Protocol::Command::Data,
                            Protocol::Status::InvalidLength);
    }
    const uint32_t id = Protocol::readLe32(request + 4);
    const uint32_t offset = Protocol::readLe32(request + 8);
    const uint8_t payloadLength = request[12];
    if (payloadLength > Protocol::kMaxDataPayloadBytes) {
      return simpleResponse(response, Protocol::Command::Data,
                            Protocol::Status::PayloadTooLarge, id);
    }
    if (payloadLength == 0 || length != Protocol::kDataHeaderBytes + payloadLength) {
      return simpleResponse(response, Protocol::Command::Data,
                            Protocol::Status::InvalidLength, id);
    }
    if (phase != Protocol::Phase::Receiving || imageBuffer == nullptr || id != transfer.id) {
      return simpleResponse(response, Protocol::Command::Data,
                            Protocol::Status::NotFound, id);
    }
    if (offset > transfer.size || payloadLength > transfer.size - offset) {
      return simpleResponse(response, Protocol::Command::Data,
                            Protocol::Status::InvalidArgument, id);
    }
    if (offset > transfer.nextOffset) {
      return simpleResponse(response, Protocol::Command::Data,
                            Protocol::Status::BadOffset, id);
    }
    if (offset < transfer.nextOffset) {
      if (payloadLength > transfer.nextOffset - offset) {
        return simpleResponse(response, Protocol::Command::Data,
                              Protocol::Status::BadOffset, id);
      }
      if (memcmp(imageBuffer + offset, request + Protocol::kDataHeaderBytes,
                 payloadLength) != 0) {
        return simpleResponse(response, Protocol::Command::Data,
                              Protocol::Status::DataMismatch, id);
      }
    } else {
      memcpy(imageBuffer + offset, request + Protocol::kDataHeaderBytes, payloadLength);
      transfer.nextOffset += payloadLength;
    }
    transfer.lastActivityMs = millis();
    status = Protocol::Status::Ok;
    return simpleResponse(response, Protocol::Command::Data, Protocol::Status::Ok, id);
  }

  size_t handleStatus(const uint8_t* request, uint32_t length, uint8_t* response) {
    if (length != Protocol::kTransferRequestBytes) {
      return simpleResponse(response, Protocol::Command::Status,
                            Protocol::Status::InvalidLength);
    }
    const uint32_t id = Protocol::readLe32(request + 4);
    const bool activeTransfer = id != 0 && id == transfer.id;
    const bool completedTransfer = stored.valid && id != 0 && id == storedTransferId;
    const Protocol::Status current = activeTransfer
                                       ? transferWireStatus()
                                       : (completedTransfer
                                            ? Protocol::Status::Completed
                                            : Protocol::Status::NotFound);
    writeResponseHeader(response, Protocol::Command::Status, current, id,
                        activeTransfer ? transfer.nextOffset
                                       : (completedTransfer ? stored.size : 0));
    if (id == transfer.id && phase == Protocol::Phase::Stored &&
        displayRequestedAtMs == 0) {
      // Let this STORED response leave the RF path before starting an e-paper
      // refresh, which can block the application loop for a noticeable time.
      displayRequestedAtMs = millis();
    }
    return Protocol::kResponseHeaderBytes;
  }

  size_t handleCommit(const uint8_t* request, uint32_t length, uint8_t* response) {
    if (length != Protocol::kTransferRequestBytes + sizeof(uint32_t)) {
      return simpleResponse(response, Protocol::Command::Commit,
                            Protocol::Status::InvalidLength);
    }
    const uint32_t id = Protocol::readLe32(request + 4);
    const uint32_t requestedCrc = Protocol::readLe32(request + 8);
    if (stored.valid && id != 0 && id == storedTransferId) {
      return simpleResponse(response, Protocol::Command::Commit,
                            requestedCrc == stored.crc32
                              ? Protocol::Status::Completed
                              : Protocol::Status::CrcMismatch,
                            id);
    }
    if (phase == Protocol::Phase::Completed && id == transfer.id) {
      return simpleResponse(response, Protocol::Command::Commit,
                            requestedCrc == transfer.crc32
                              ? Protocol::Status::Completed
                              : Protocol::Status::CrcMismatch,
                            id);
    }
    if (phase != Protocol::Phase::Receiving || id != transfer.id || imageBuffer == nullptr) {
      return simpleResponse(response, Protocol::Command::Commit,
                            Protocol::Status::NotFound, id);
    }
    if (requestedCrc != transfer.crc32) {
      return simpleResponse(response, Protocol::Command::Commit,
                            Protocol::Status::CrcMismatch, id);
    }
    if (transfer.nextOffset != transfer.size) {
      return simpleResponse(response, Protocol::Command::Commit,
                            Protocol::Status::BadOffset, id);
    }
    verifyOffset = 0;
    verifyCrc = 0xFFFFFFFFUL;
    setResult(Protocol::Phase::Verifying, Protocol::Status::Accepted, true);
    return simpleResponse(response, Protocol::Command::Commit,
                          Protocol::Status::Accepted, id);
  }

  size_t handleAbort(const uint8_t* request, uint32_t length, uint8_t* response) {
    if (length != Protocol::kTransferRequestBytes) {
      return simpleResponse(response, Protocol::Command::Abort,
                            Protocol::Status::InvalidLength);
    }
    const uint32_t id = Protocol::readLe32(request + 4);
    if (id != transfer.id || !transferCancellable()) {
      return simpleResponse(response, Protocol::Command::Abort,
                            Protocol::Status::NotFound, id);
    }
    cancelWork(true);
    transfer = {};
    setResult(Protocol::Phase::Idle, Protocol::Status::Ok, true);
    return simpleResponse(response, Protocol::Command::Abort, Protocol::Status::Ok, id);
  }

  size_t handleSetTime(const uint8_t* request, uint32_t length, uint8_t* response) {
    if (length != Protocol::kSetTimeRequestBytes) {
      return simpleResponse(response, Protocol::Command::SetTime,
                            Protocol::Status::InvalidLength);
    }
    const uint64_t unixSeconds = Protocol::readLe64(request + 4);
    const int16_t utcOffsetMinutes =
      static_cast<int16_t>(Protocol::readLe16(request + 12));
    const uint8_t flags = request[14];
    constexpr uint64_t kUnix2023 = 1672531200ULL;
    constexpr uint64_t kUnix2100 = 4102444800ULL;
    if (flags != 0 || unixSeconds < kUnix2023 || unixSeconds >= kUnix2100 ||
        utcOffsetMinutes < -840 || utcOffsetMinutes > 840 ||
        timeSyncHandler == nullptr) {
      return simpleResponse(response, Protocol::Command::SetTime,
                            Protocol::Status::InvalidArgument);
    }
    const bool applied = timeSyncHandler(unixSeconds, utcOffsetMinutes,
                                         timeSyncContext);
    if (applied) {
      ++timeSyncRevision;
      ++uiRevision;
      Serial.printf("[paper.nfc] TIME_SET unix=%llu offset=%d\n",
                    static_cast<unsigned long long>(unixSeconds),
                    static_cast<int>(utcOffsetMinutes));
    }
    return simpleResponse(response, Protocol::Command::SetTime,
                          applied ? Protocol::Status::Ok
                                  : Protocol::Status::InternalError);
  }

  size_t handleCommand(const uint8_t* request,
                       uint32_t length,
                       uint8_t* response,
                       size_t responseCapacity) {
    if (responseCapacity < 24 || length < Protocol::kCommonHeaderBytes ||
        length > Protocol::kMaxCommandBytes) {
      return 0;
    }
    const auto command = static_cast<Protocol::Command>(request[3]);
    if (request[0] != Protocol::kMagic0 || request[1] != Protocol::kMagic1) {
      return simpleResponse(response, command, Protocol::Status::BadMagic);
    }
    if (request[2] != Protocol::kVersion) {
      return simpleResponse(response, command, Protocol::Status::UnsupportedVersion);
    }
    if (!modeActive) {
      return simpleResponse(response, command, Protocol::Status::Busy);
    }

    switch (command) {
      case Protocol::Command::Hello:
        if (length != Protocol::kCommonHeaderBytes) {
          return simpleResponse(response, command, Protocol::Status::InvalidLength);
        }
        writeResponseHeader(response, command, Protocol::Status::Ok, transfer.id,
                            transfer.nextOffset);
        Protocol::writeLe16(response + 13, Protocol::kMaxFifoFrameBytes);
        Protocol::writeLe16(response + 15, Protocol::kMaxCommandBytes);
        Protocol::writeLe16(response + 17, Protocol::kMaxDataPayloadBytes);
        Protocol::writeLe32(response + 19, Protocol::kMaxImageBytes);
        response[23] = 0x07;  // Baseline JPEG + TIME_SET + full-screen images.
        return 24;
      case Protocol::Command::Begin: return handleBegin(request, length, response);
      case Protocol::Command::Data: return handleData(request, length, response);
      case Protocol::Command::Status: return handleStatus(request, length, response);
      case Protocol::Command::Commit: return handleCommit(request, length, response);
      case Protocol::Command::Abort: return handleAbort(request, length, response);
      case Protocol::Command::SetTime: return handleSetTime(request, length, response);
    }
    return simpleResponse(response, command, Protocol::Status::UnknownCommand);
  }

  bool validateJpeg() const {
    if (imageBuffer == nullptr || transfer.size < 4 || imageBuffer[0] != 0xFF ||
        imageBuffer[1] != 0xD8 || imageBuffer[transfer.size - 2] != 0xFF ||
        imageBuffer[transfer.size - 1] != 0xD9) {
      return false;
    }

    bool sawSof0 = false;
    uint32_t offset = 2;
    while (offset + 1 < transfer.size) {
      if (imageBuffer[offset] != 0xFF) {
        return false;
      }
      while (offset < transfer.size && imageBuffer[offset] == 0xFF) {
        ++offset;
      }
      if (offset >= transfer.size) {
        return false;
      }
      const uint8_t marker = imageBuffer[offset++];
      if (marker == 0xD9) {
        break;
      }
      if (marker == 0xDA) {
        return sawSof0;  // Entropy-coded data continues until the validated EOI.
      }
      if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
        continue;
      }
      if (offset + 2 > transfer.size) {
        return false;
      }
      const uint16_t segmentLength =
        (static_cast<uint16_t>(imageBuffer[offset]) << 8) | imageBuffer[offset + 1];
      if (segmentLength < 2 || offset + segmentLength > transfer.size) {
        return false;
      }
      if ((marker >= 0xE1 && marker <= 0xEF) || marker == 0xFE) {
        return false;  // v1 requires EXIF/APP metadata and comments to be stripped.
      }
      if (marker >= 0xC1 && marker <= 0xCF && marker != 0xC4 &&
          marker != 0xC8 && marker != 0xCC) {
        return false;  // Progressive and non-baseline SOF markers are not accepted.
      }
      if (marker == 0xC0) {
        if (segmentLength < 8 || imageBuffer[offset + 2] != 8) {
          return false;
        }
        const uint16_t height =
          (static_cast<uint16_t>(imageBuffer[offset + 3]) << 8) | imageBuffer[offset + 4];
        const uint16_t width =
          (static_cast<uint16_t>(imageBuffer[offset + 5]) << 8) | imageBuffer[offset + 6];
        const uint8_t components = imageBuffer[offset + 7];
        if (width != transfer.width || height != transfer.height || components != 3) {
          return false;
        }
        sawSof0 = true;
      }
      offset += segmentLength;
    }
    return false;
  }

  void failTransfer(Protocol::Status failure) {
    const bool removeSlot = pendingSlotValid;
    if (pendingFile) {
      pendingFile.close();
    }
    if (removeSlot) {
      char path[20] = {};
      slotPath(pendingSlot, path, sizeof(path));
      LittleFS.remove(path);
    }
    pendingSlotValid = false;
    releaseBuffer();
    setResult(Protocol::Phase::Error, failure, true);
    Serial.printf("[paper.nfc] transfer failed id=%lu status=%s\n",
                  static_cast<unsigned long>(transfer.id), ::statusName(failure));
  }

  void updateVerification() {
    const uint32_t remaining = transfer.size - verifyOffset;
    const size_t chunk = std::min<size_t>(remaining, kWorkChunkBytes);
    verifyCrc = Protocol::crc32Update(verifyCrc, imageBuffer + verifyOffset, chunk);
    verifyOffset += chunk;
    if (verifyOffset < transfer.size) {
      return;
    }
    const uint32_t finalCrc = verifyCrc ^ 0xFFFFFFFFUL;
    if (finalCrc != transfer.crc32) {
      failTransfer(Protocol::Status::CrcMismatch);
      return;
    }
    if (!validateJpeg()) {
      failTransfer(Protocol::Status::InvalidJpeg);
      return;
    }

    pendingSlot = findFreeStorageSlot();
    if (pendingSlot == kDefaultImageSlot) {
      failTransfer(Protocol::Status::StorageError);
      return;
    }
    pendingSlotValid = true;
    char pendingPath[20] = {};
    slotPath(pendingSlot, pendingPath, sizeof(pendingPath));
    LittleFS.remove(pendingPath);
    pendingFile = LittleFS.open(pendingPath, "w");
    if (!pendingFile) {
      failTransfer(Protocol::Status::StorageError);
      return;
    }
    persistOffset = 0;
    setResult(Protocol::Phase::Persisting, Protocol::Status::Accepted);
  }

  void updatePersistence() {
    const uint32_t remaining = transfer.size - persistOffset;
    const size_t chunk = std::min<size_t>(remaining, kWorkChunkBytes);
    if (pendingFile.write(imageBuffer + persistOffset, chunk) != chunk) {
      failTransfer(Protocol::Status::StorageError);
      return;
    }
    persistOffset += chunk;
    if (persistOffset < transfer.size) {
      return;
    }
    pendingFile.flush();
    pendingFile.close();

    char pendingPath[20] = {};
    slotPath(pendingSlot, pendingPath, sizeof(pendingPath));
    File check = LittleFS.open(pendingPath, "r");
    const bool sizeMatches = check && check.size() == transfer.size;
    if (check) {
      check.close();
    }
    if (!sizeMatches) {
      failTransfer(Protocol::Status::StorageError);
      return;
    }

    CatalogRecord next = catalog;
    int target = freeCatalogEntry();
    int evicted = -1;
    if (target < 0) {
      target = oldestCatalogEntry();
      evicted = target;
    }
    if (target < 0) {
      failTransfer(Protocol::Status::StorageError);
      return;
    }
    const uint8_t evictedSlot = evicted >= 0 ? next.entries[evicted].slot
                                             : kDefaultImageSlot;
    CatalogEntry entry;
    entry.valid = 1;
    entry.slot = pendingSlot;
    entry.imageMode = static_cast<uint8_t>(transfer.mode);
    entry.width = transfer.width;
    entry.height = transfer.height;
    entry.size = transfer.size;
    entry.imageCrc32 = transfer.crc32;
    entry.transferId = transfer.id;
    entry.sequence = next.nextSequence++;
    next.entries[target] = entry;
    if (next.count < PaperMonoNfcController::kMaxReceivedImages) ++next.count;
    next.activeSlot = pendingSlot;
    if (!saveCatalog(next)) {
      failTransfer(Protocol::Status::StorageError);
      return;
    }
    if (evictedSlot != kDefaultImageSlot) {
      char evictedPath[20] = {};
      slotPath(evictedSlot, evictedPath, sizeof(evictedPath));
      LittleFS.remove(evictedPath);
    }
    populateStored(entry);
    storedAtMs = millis();
    displayRequestedAtMs = 0;
    pendingSlotValid = false;
    releaseBuffer();
    setResult(Protocol::Phase::Stored, Protocol::Status::Ok);
    Serial.printf("[paper.nfc] stored id=%lu path=%s size=%lu\n",
                  static_cast<unsigned long>(transfer.id), stored.path,
                  static_cast<unsigned long>(stored.size));
  }

  void loadStoredImage() {
    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, true)) {
      return;
    }
    CatalogRecord first;
    CatalogRecord second;
    const bool firstValid = readCatalogRecord(preferences, kCatalogKeyA, first);
    const bool secondValid = readCatalogRecord(preferences, kCatalogKeyB, second);
    PersistedImageRecord legacy;
    const size_t legacyRead = preferences.getBytes(kPreferencesKey, &legacy, sizeof(legacy));
    preferences.end();

    if (firstValid || secondValid) {
      catalog = !secondValid || (firstValid && first.generation >= second.generation)
                  ? first
                  : second;
      catalogLoaded = true;
      CatalogRecord cleaned = catalog;
      bool changed = false;
      for (auto& entry : cleaned.entries) {
        if (!entry.valid) continue;
        char path[20] = {};
        slotPath(entry.slot, path, sizeof(path));
        File file = LittleFS.open(path, "r");
        const bool validFile = file && file.size() == entry.size;
        if (file) file.close();
        if (!validFile) {
          if (cleaned.activeSlot == entry.slot) cleaned.activeSlot = kDefaultImageSlot;
          entry = {};
          if (cleaned.count > 0) --cleaned.count;
          changed = true;
        }
      }
      if (changed) saveCatalog(cleaned);
      refreshStoredFromCatalog();
      return;
    }

    catalog = {};
    catalogLoaded = true;
    bool migratedLegacyFile = false;
    const bool legacyValid = legacyRead == sizeof(legacy) &&
        legacy.signature == kPersistSignature && legacy.version == kPersistVersion &&
        legacy.activeSlot <= 1 && legacy.recordCrc32 == persistedRecordCrc(legacy) &&
        legacy.size > 0 && legacy.size <= Protocol::kMaxImageBytes &&
        isExpectedGeometry(static_cast<Protocol::ImageMode>(legacy.imageMode),
                           legacy.width, legacy.height);
    if (legacyValid) {
      File file = LittleFS.open(kLegacySlotPaths[legacy.activeSlot], "r");
      const bool fileValid = file && file.size() == legacy.size;
      if (file) file.close();
      if (fileValid) {
        char migratedPath[20] = {};
        slotPath(0, migratedPath, sizeof(migratedPath));
        LittleFS.remove(migratedPath);
        if (LittleFS.rename(kLegacySlotPaths[legacy.activeSlot], migratedPath)) {
          migratedLegacyFile = true;
          CatalogEntry& entry = catalog.entries[0];
          entry.valid = 1;
          entry.slot = 0;
          entry.imageMode = legacy.imageMode;
          entry.width = legacy.width;
          entry.height = legacy.height;
          entry.size = legacy.size;
          entry.imageCrc32 = legacy.imageCrc32;
          entry.transferId = legacy.transferId;
          entry.sequence = 1;
          catalog.count = 1;
          catalog.activeSlot = 0;
          catalog.nextSequence = 2;
        }
      }
    }
    if (saveCatalog(catalog)) {
      for (const auto* path : kLegacySlotPaths) LittleFS.remove(path);
    } else if (migratedLegacyFile) {
      char migratedPath[20] = {};
      slotPath(0, migratedPath, sizeof(migratedPath));
      LittleFS.rename(migratedPath, kLegacySlotPaths[legacy.activeSlot]);
      catalog = {};
    }
    refreshStoredFromCatalog();
  }

  bool clearStoredImage() {
    cancelWork(true);
    bool filesCleared = true;
    for (uint8_t slot = 0; slot < kCatalogImageSlots; ++slot) {
      char path[20] = {};
      slotPath(slot, path, sizeof(path));
      if (LittleFS.exists(path) && !LittleFS.remove(path)) {
        filesCleared = false;
        Serial.printf("[paper.nfc] clear file failed path=%s\n", path);
      }
    }
    for (const auto* path : kLegacySlotPaths) LittleFS.remove(path);

    CatalogRecord empty;
    const bool metadataCleared = saveCatalog(empty);

    transfer = {};
    stored = {};
    storedTransferId = 0;
    storedAtMs = 0;
    displayRequestedAtMs = 0;
    verifyOffset = 0;
    verifyCrc = 0xFFFFFFFFUL;
    persistOffset = 0;
    pendingSlot = 0;
    pendingSlotValid = false;
    refreshStoredFromCatalog();

    const bool success = filesCleared && metadataCleared;
    setResult(success ? Protocol::Phase::Idle : Protocol::Phase::Error,
              success ? Protocol::Status::Ok : Protocol::Status::StorageError,
              true);
    Serial.printf("[paper.nfc] stored image cleared success=%d files=%d metadata=%d\n",
                  success ? 1 : 0, filesCleared ? 1 : 0, metadataCleared ? 1 : 0);
    return success;
  }

  bool getStoredImageAt(uint8_t newestIndex,
                        PaperMonoNfcController::StoredImage& image) const {
    const int index = catalogEntryAtNewestIndex(newestIndex);
    if (index < 0) {
      image = {};
      return false;
    }
    const CatalogEntry& entry = catalog.entries[index];
    image.valid = true;
    image.mode = static_cast<Protocol::ImageMode>(entry.imageMode);
    image.width = entry.width;
    image.height = entry.height;
    image.size = entry.size;
    image.crc32 = entry.imageCrc32;
    image.slot = entry.slot;
    image.sequence = entry.sequence;
    slotPath(entry.slot, image.path, sizeof(image.path));
    return true;
  }

  bool selectStoredImageAt(uint8_t newestIndex) {
    const int index = catalogEntryAtNewestIndex(newestIndex);
    if (index < 0) return false;
    CatalogRecord next = catalog;
    next.activeSlot = next.entries[index].slot;
    if (!saveCatalog(next)) return false;
    refreshStoredFromCatalog();
    ++uiRevision;
    return true;
  }

  bool selectDefaultImage() {
    if (catalog.activeSlot == kDefaultImageSlot) return true;
    CatalogRecord next = catalog;
    next.activeSlot = kDefaultImageSlot;
    if (!saveCatalog(next)) return false;
    refreshStoredFromCatalog();
    ++uiRevision;
    return true;
  }

  bool deleteStoredImages(uint32_t newestIndexMask) {
    if (newestIndexMask == 0) return true;
    CatalogRecord next = catalog;
    uint8_t slotsToRemove[PaperMonoNfcController::kMaxReceivedImages] = {};
    uint8_t removeCount = 0;
    for (uint8_t newest = 0; newest < PaperMonoNfcController::kMaxReceivedImages; ++newest) {
      if ((newestIndexMask & (1UL << newest)) == 0) continue;
      const int index = catalogEntryAtNewestIndex(newest);
      if (index < 0 || !next.entries[index].valid) continue;
      const uint8_t slot = next.entries[index].slot;
      slotsToRemove[removeCount++] = slot;
      if (next.activeSlot == slot) next.activeSlot = kDefaultImageSlot;
      next.entries[index] = {};
      if (next.count > 0) --next.count;
    }
    if (removeCount == 0) return true;
    if (!saveCatalog(next)) return false;
    for (uint8_t index = 0; index < removeCount; ++index) {
      char path[20] = {};
      slotPath(slotsToRemove[index], path, sizeof(path));
      LittleFS.remove(path);
    }
    refreshStoredFromCatalog();
    ++uiRevision;
    return true;
  }

  static uint8_t bcc(const uint8_t* data, size_t length, uint8_t initial = 0) {
    uint8_t value = initial;
    while (length-- > 0) {
      value ^= *data++;
    }
    return value;
  }

  bool initializeHardware() {
    if (hardwareReady) {
      return true;
    }

    Serial.println("[paper.nfc] init: enable control pin");
    auto& ioe = M5.getIOExpander(0);
    constexpr auto nfcEnablePin = m5::M5IOE1_Class::gpio4;
    ioe.setHighImpedance(nfcEnablePin, false);
    ioe.setDirection(nfcEnablePin, true);
    // Follow the PaperMono board demo's warm-start sequence. The IO expander
    // survives an ESP restart, so merely writing HIGH can leave ST25R3916 in
    // the half-configured state from the previous boot. Give PYB_NFC_EN one
    // deterministic low/high reset before the first UnitST25R3916::begin().
    ioe.digitalWrite(nfcEnablePin, false);
    delay(20);
    ioe.digitalWrite(nfcEnablePin, true);
    delay(100);
    Serial.printf("[paper.nfc] init: NFC_EN IO4 output=%d\n",
                  ioe.getWriteValue(nfcEnablePin) ? 1 : 0);

    Serial.println("[paper.nfc] init: configure ST25R3916");
    auto config = unit.config();
    config.emulation = true;
    config.mode = m5::nfc::NFC::A;
    // PaperMono's official UserDemo drives the built-in ST25R3916 through
    // UnitUnified with IRQ disabled. Polling the interrupt registers over the
    // internal I2C bus avoids depending on a GPIO6 edge while entering passive
    // target states (the failing trace was Ready -> Idle before tag discovery).
    config.using_irq = false;
    config.irq = 6;
    unit.config(config);

    const bool newlyRegistered = !unitRegistered;
    if (newlyRegistered) {
      Serial.println("[paper.nfc] init: register I2C unit");
      if (!m5::unit::wiring::i2cClass(units, unit, M5.In_I2C)) {
        setResult(Protocol::Phase::Error, Protocol::Status::HardwareError, true);
        Serial.println("[paper.nfc] UnitUnified add failed");
        return false;
      }
      unitRegistered = true;
    }

    bool initialized = false;
    for (uint8_t attempt = 1; attempt <= 3 && !initialized; ++attempt) {
      if (attempt > 1) {
        // A failed UnitST25R3916::begin() can leave both the IC and IRQ line in
        // a partial state. Reset only for a bounded startup retry; never while
        // listening.
        ioe.digitalWrite(nfcEnablePin, false);
        delay(10);
        ioe.digitalWrite(nfcEnablePin, true);
        delay(100);
      }
      Serial.printf("[paper.nfc] init: begin I2C unit attempt=%u\n",
                    static_cast<unsigned>(attempt));
      // UnitUnified owns first-time wiring. After a deliberate NFC power-off,
      // rerun the concrete unit's begin() so ST25R3916 registers are restored
      // even if UnitUnified already considers its component registered.
      initialized = newlyRegistered && attempt == 1 ? units.begin() : unit.begin();
      if (!initialized) {
        uint8_t type = 0;
        uint8_t revision = 0;
        const bool identityRead = unit.readICIdentity(type, revision);
        Serial.printf("[paper.nfc] init failed attempt=%u identity_read=%d type=%02X rev=%02X\n",
                      static_cast<unsigned>(attempt), identityRead ? 1 : 0,
                      static_cast<unsigned>(type), static_cast<unsigned>(revision));
      }
    }
    if (!initialized) {
      setResult(Protocol::Phase::Error, Protocol::Status::HardwareError, true);
      Serial.println("[paper.nfc] ST25R3916 initialization failed");
      return false;
    }

    Serial.println("[paper.nfc] init: configure emulated PICC");
    // PaperMono's compact antenna does not always reach the stock collision
    // detector's 205 mV activation level (0x13). Use 105 mV for both peer and
    // collision activation, with 75 mV deactivation (0x11/0x00). This retains
    // 30 mV hysteresis on both detectors. The prior Ready-state lockup at this
    // setting was caused by a missed level-high IRQ, now handled by the
    // M5Unit-NFC build patch; do not use 0x00/0x00, which has no hysteresis.
    if (!unit.writeExternalFieldDetectorActivationThreshold(0x11) ||
        !unit.writeExternalFieldDetectorDeactivationThreshold(0x00)) {
      setResult(Protocol::Phase::Error, Protocol::Status::HardwareError, true);
      Serial.println("[paper.nfc] external field threshold setup failed");
      return false;
    }
    uint8_t activationThreshold = 0;
    uint8_t deactivationThreshold = 0;
    if (unit.readExternalFieldDetectorActivationThreshold(activationThreshold) &&
        unit.readExternalFieldDetectorDeactivationThreshold(deactivationThreshold)) {
      Serial.printf("[paper.nfc] field thresholds activation=%02X deactivation=%02X\n",
                    static_cast<unsigned>(activationThreshold),
                    static_cast<unsigned>(deactivationThreshold));
    }
    if (!picc.emulate(m5::nfc::a::Type::MIFARE_Ultralight_EV1_1,
                      kPaperMonoUid, sizeof(kPaperMonoUid))) {
      setResult(Protocol::Phase::Error, Protocol::Status::HardwareError, true);
      return false;
    }
    memcpy(piccMemory, kPaperMonoUid, 3);
    piccMemory[3] = bcc(kPaperMonoUid, 3, 0x88);
    memcpy(piccMemory + 4, kPaperMonoUid + 3, 4);
    piccMemory[8] = bcc(kPaperMonoUid + 3, 4);
    piccMemory[9] = 0xA3;
    piccMemory[12] = 0xE1;
    piccMemory[13] = 0x10;
    piccMemory[14] = 0x06;
    hardwareReady = true;
    if (phase == Protocol::Phase::Error &&
        status == Protocol::Status::HardwareError) {
      setResult(Protocol::Phase::Idle, Protocol::Status::Ok, true);
    }
    Serial.println("[paper.nfc] ST25R3916 ready");
    return true;
  }

  bool setActive(bool active) {
    if (!active) {
      if (modeActive && emulationActive) {
        // Do not stop/restart the listener before the blocking e-paper update:
        // the panel and ST25R3916 share the board power/I2C environment. Pause
        // servicing here, then perform one controlled end + forced target-mode
        // reconfiguration after the display is quiet in setActive(true).
        Serial.printf("[paper.nfc] card emulation paused state=%s\n",
                      ::emulationStateName(emulation.state()));
      }
      modeActive = false;
      return true;
    }
    modeActive = true;
    if (!initializeHardware()) {
      modeActive = false;
      return false;
    }
    if (emulationActive) {
      // The official PaperMono demo powers the built-in ST25R3916 and reruns
      // UnitST25R3916::begin() whenever NFC is reused. Do the same after an
      // e-paper refresh: target-mode registers and even an in-flight I2C stop
      // can be invalid at this point. EmulationLayerA::end() sets its software
      // state to None even when the hardware stop fails, so a failed end is a
      // reason to power-cycle the IC, not a reason to leave NFC permanently
      // disabled.
      const auto pausedState = emulation.state();
      const bool ended = emulation.end();
      emulationActive = false;

      auto& ioe = M5.getIOExpander(0);
      constexpr auto nfcEnablePin = m5::M5IOE1_Class::gpio4;
      ioe.setHighImpedance(nfcEnablePin, false);
      ioe.setDirection(nfcEnablePin, true);
      ioe.digitalWrite(nfcEnablePin, false);
      delay(20);
      ioe.digitalWrite(nfcEnablePin, true);
      delay(120);

      bool unitRestarted = false;
      for (uint8_t attempt = 1; attempt <= 3 && !unitRestarted; ++attempt) {
        unitRestarted = unit.begin();
        if (!unitRestarted && attempt < 3) {
          ioe.digitalWrite(nfcEnablePin, false);
          delay(20);
          ioe.digitalWrite(nfcEnablePin, true);
          delay(120);
        }
      }
      const bool modeConfigured =
        unitRestarted && unit.configureEmulationMode(m5::nfc::NFC::A);
      const bool thresholdsConfigured =
        modeConfigured && unit.writeExternalFieldDetectorActivationThreshold(0x11) &&
        unit.writeExternalFieldDetectorDeactivationThreshold(0x00);
      Serial.printf("[paper.nfc] card emulation rearm previous=%s end=%d unit=%d mode=%d thresholds=%d\n",
                    ::emulationStateName(pausedState), ended ? 1 : 0,
                    unitRestarted ? 1 : 0, modeConfigured ? 1 : 0,
                    thresholdsConfigured ? 1 : 0);
      if (!unitRestarted || !modeConfigured || !thresholdsConfigured) {
        modeActive = false;
        setResult(Protocol::Phase::Error, Protocol::Status::HardwareError, true);
        return false;
      }
    }
    if (!emulationActive) {
      emulationActive = emulation.begin(picc, piccMemory, sizeof(piccMemory));
      if (!emulationActive) {
        modeActive = false;
        setResult(Protocol::Phase::Error, Protocol::Status::HardwareError, true);
        Serial.println("[paper.nfc] card emulation begin failed");
      } else {
        lastEmulationState = emulation.state();
        const auto& activePicc = emulation.emulatePICC();
        Serial.printf("[paper.nfc] card emulation ready uid=%s atqa=%04X sak=%u state=%s\n",
                      activePicc.uidAsString().c_str(), activePicc.atqa, activePicc.sak,
                      ::emulationStateName(lastEmulationState));
      }
    }
    return emulationActive;
  }

  void powerDown() {
    modeActive = false;
    if (emulationActive) {
      emulation.end();
      emulationActive = false;
    }
    auto& ioe = M5.getIOExpander(0);
    constexpr auto nfcEnablePin = m5::M5IOE1_Class::gpio4;
    ioe.setHighImpedance(nfcEnablePin, false);
    ioe.setDirection(nfcEnablePin, true);
    ioe.digitalWrite(nfcEnablePin, false);
    hardwareReady = false;
    lastEmulationState = m5::nfc::EmulationLayerA::State::None;
    Serial.println("[paper.nfc] hardware powered down");
  }

  void update() {
    if (modeActive && hardwareReady && emulationActive) {
      units.update();
      emulation.update();
      const auto currentState = emulation.state();
      const uint32_t now = millis();
      if (currentState != lastEmulationState) {
        const bool detectorChatter =
          (lastEmulationState == m5::nfc::EmulationLayerA::State::Off &&
           currentState == m5::nfc::EmulationLayerA::State::Idle) ||
          (lastEmulationState == m5::nfc::EmulationLayerA::State::Idle &&
           currentState == m5::nfc::EmulationLayerA::State::Off);
        if (!detectorChatter) {
          Serial.printf("[paper.nfc] state %s -> %s\n",
                        ::emulationStateName(lastEmulationState),
                        ::emulationStateName(currentState));
        }
        lastEmulationState = currentState;
      }

      // While no reader is detected, expose the two facts that distinguish a
      // phone/antenna problem from a listener-state problem: whether NFC_EN is
      // still asserted and whether ST25R3916's external-field detector is high.
      // Stop this diagnostic immediately once selection begins so serial I/O
      // cannot affect NFC-A response timing.
      if (currentState == m5::nfc::EmulationLayerA::State::Off &&
          static_cast<uint32_t>(now - lastWaitingFieldDiagAtMs) >= 2000) {
        lastWaitingFieldDiagAtMs = now;
        uint8_t auxiliary = 0;
        const bool auxiliaryRead = unit.readAuxiliaryDisplay(auxiliary);
        auto& ioe = M5.getIOExpander(0);
        constexpr auto nfcEnablePin = m5::M5IOE1_Class::gpio4;
        Serial.printf("[paper.nfc] waiting field aux_ok=%d aux=%02X efd=%d nfc_en=%d irq=%d\n",
                      auxiliaryRead ? 1 : 0,
                      static_cast<unsigned>(auxiliary),
                      auxiliaryRead && (auxiliary & 0x40U) ? 1 : 0,
                      ioe.getWriteValue(nfcEnablePin) ? 1 : 0,
                      digitalRead(6));
      }

      if (receivedFrameCount != loggedReceivedFrameCount) {
        loggedReceivedFrameCount = receivedFrameCount;
        // Serial output is intentionally sparse here: every print delays the
        // passive-target loop before the reader's next command.
        if (receivedFrameCount <= 8 || (receivedFrameCount & 0x7FU) == 0) {
          Serial.printf("[paper.nfc] rx frame count=%lu len=%lu first=%02X\n",
                        static_cast<unsigned long>(receivedFrameCount),
                        static_cast<unsigned long>(lastReceivedFrameLength),
                        static_cast<unsigned>(lastReceivedFrameFirstByte));
        }
      }

    }
    if (phase == Protocol::Phase::Receiving && transferExpired(millis())) {
      cancelWork(true);
      transfer = {};
      setResult(Protocol::Phase::Idle, Protocol::Status::Ok, true);
      return;
    }
    if (phase == Protocol::Phase::Verifying) {
      updateVerification();
    } else if (phase == Protocol::Phase::Persisting) {
      updatePersistence();
    } else if (phase == Protocol::Phase::Stored &&
               ((displayRequestedAtMs != 0 &&
                 // STORED is the durable success boundary. Give the sender
                 // time to invalidate its reader session and drop the RF
                 // field before the blocking e-paper refresh begins.
                 static_cast<uint32_t>(millis() - displayRequestedAtMs) >= 250) ||
                // If the terminal STATUS response itself is lost, iOS first
                // invalidates Core NFC and then opens a new reader session.
                // Keep target mode alive long enough for that reconnect; a
                // matching BEGIN then returns COMPLETED without re-uploading.
                static_cast<uint32_t>(millis() - storedAtMs) >= 5000)) {
      setResult(Protocol::Phase::Displaying, Protocol::Status::Ok, true);
    }
  }
};

PaperMonoNfcController::PaperMonoNfcController() : impl_(new (std::nothrow) Impl()) {}

PaperMonoNfcController::~PaperMonoNfcController() {
  delete impl_;
}

void PaperMonoNfcController::begin() {
  if (impl_ == nullptr || impl_->begun) {
    return;
  }
  impl_->begun = true;
  impl_->loadStoredImage();
  // This standalone firmware has no network, audio, or animation workload.
  // Keep ST25R3916 fully off until the user explicitly opens an NFC screen.
  // The waiting screen is committed before setModeActive(true) initializes it.
  Serial.println("[paper.nfc] metadata loaded; hardware remains off until requested");
}

void PaperMonoNfcController::setTimeSyncHandler(TimeSyncHandler handler,
                                                void* context) {
  if (impl_ != nullptr) {
    impl_->timeSyncHandler = handler;
    impl_->timeSyncContext = context;
  }
}

bool PaperMonoNfcController::setModeActive(bool active) {
  return impl_ != nullptr && impl_->setActive(active);
}

void PaperMonoNfcController::cancelActiveTransfer() {
  if (impl_ != nullptr && impl_->transferCancellable()) {
    impl_->cancelWork(true);
    impl_->transfer = {};
    impl_->setResult(Protocol::Phase::Idle, Protocol::Status::Ok, true);
  }
}

void PaperMonoNfcController::powerDown() {
  if (impl_ != nullptr) {
    impl_->powerDown();
  }
}

void PaperMonoNfcController::update() {
  if (impl_ != nullptr) {
    impl_->update();
  }
}

bool PaperMonoNfcController::modeActive() const {
  return impl_ != nullptr && impl_->modeActive;
}

bool PaperMonoNfcController::hardwareReady() const {
  return impl_ != nullptr && impl_->hardwareReady && impl_->emulationActive;
}

bool PaperMonoNfcController::getStoredImage(StoredImage& image) const {
  if (impl_ == nullptr || !impl_->stored.valid) {
    image = {};
    return false;
  }
  image = impl_->stored;
  return true;
}

uint8_t PaperMonoNfcController::storedImageCount() const {
  return impl_ != nullptr ? impl_->catalog.count : 0;
}

bool PaperMonoNfcController::getStoredImageAt(uint8_t newestIndex,
                                              StoredImage& image) const {
  return impl_ != nullptr && impl_->getStoredImageAt(newestIndex, image);
}

bool PaperMonoNfcController::selectStoredImageAt(uint8_t newestIndex) {
  return impl_ != nullptr && impl_->selectStoredImageAt(newestIndex);
}

bool PaperMonoNfcController::selectDefaultImage() {
  return impl_ != nullptr && impl_->selectDefaultImage();
}

bool PaperMonoNfcController::defaultImageSelected() const {
  return impl_ == nullptr || impl_->catalog.activeSlot == kDefaultImageSlot;
}

bool PaperMonoNfcController::deleteStoredImageAt(uint8_t newestIndex) {
  return newestIndex < kMaxReceivedImages &&
         deleteStoredImages(1UL << newestIndex);
}

bool PaperMonoNfcController::deleteStoredImages(uint32_t newestIndexMask) {
  if (impl_ == nullptr) return false;
  impl_->setActive(false);
  return impl_->deleteStoredImages(newestIndexMask);
}

bool PaperMonoNfcController::clearStoredImage() {
  if (impl_ == nullptr) {
    return false;
  }
  impl_->setActive(false);
  return impl_->clearStoredImage();
}

Protocol::Phase PaperMonoNfcController::phase() const {
  return impl_ != nullptr ? impl_->phase : Protocol::Phase::Error;
}

Protocol::Status PaperMonoNfcController::status() const {
  return impl_ != nullptr ? impl_->status : Protocol::Status::InternalError;
}

uint32_t PaperMonoNfcController::receivedBytes() const {
  return impl_ != nullptr ? impl_->transfer.nextOffset : 0;
}

uint32_t PaperMonoNfcController::totalBytes() const {
  return impl_ != nullptr ? impl_->transfer.size : 0;
}

uint32_t PaperMonoNfcController::uiRevision() const {
  return impl_ != nullptr ? impl_->uiRevision : 0;
}

uint32_t PaperMonoNfcController::timeSyncRevision() const {
  return impl_ != nullptr ? impl_->timeSyncRevision : 0;
}

void PaperMonoNfcController::markDisplayed(bool success) {
  if (impl_ == nullptr ||
      (impl_->phase != Protocol::Phase::Stored &&
       impl_->phase != Protocol::Phase::Displaying)) {
    return;
  }
  impl_->setResult(success ? Protocol::Phase::Completed : Protocol::Phase::Error,
                   success ? Protocol::Status::Ok : Protocol::Status::InternalError,
                   true);
}

const char* PaperMonoNfcController::phaseName() const {
  return ::phaseName(phase());
}

const char* PaperMonoNfcController::statusName() const {
  return ::statusName(status());
}

#else

struct PaperMonoNfcController::Impl {};

PaperMonoNfcController::PaperMonoNfcController() = default;
PaperMonoNfcController::~PaperMonoNfcController() = default;
void PaperMonoNfcController::begin() {}
void PaperMonoNfcController::setTimeSyncHandler(TimeSyncHandler, void*) {}
bool PaperMonoNfcController::setModeActive(bool) { return false; }
void PaperMonoNfcController::cancelActiveTransfer() {}
void PaperMonoNfcController::powerDown() {}
void PaperMonoNfcController::update() {}
bool PaperMonoNfcController::modeActive() const { return false; }
bool PaperMonoNfcController::hardwareReady() const { return false; }
bool PaperMonoNfcController::getStoredImage(StoredImage& image) const {
  image = {};
  return false;
}
uint8_t PaperMonoNfcController::storedImageCount() const { return 0; }
bool PaperMonoNfcController::getStoredImageAt(uint8_t, StoredImage& image) const {
  image = {};
  return false;
}
bool PaperMonoNfcController::selectStoredImageAt(uint8_t) { return false; }
bool PaperMonoNfcController::selectDefaultImage() { return false; }
bool PaperMonoNfcController::defaultImageSelected() const { return true; }
bool PaperMonoNfcController::deleteStoredImageAt(uint8_t) { return false; }
bool PaperMonoNfcController::deleteStoredImages(uint32_t) { return false; }
bool PaperMonoNfcController::clearStoredImage() { return false; }
PaperMonoNfcProtocol::Phase PaperMonoNfcController::phase() const {
  return PaperMonoNfcProtocol::Phase::Idle;
}
PaperMonoNfcProtocol::Status PaperMonoNfcController::status() const {
  return PaperMonoNfcProtocol::Status::HardwareError;
}
uint32_t PaperMonoNfcController::receivedBytes() const { return 0; }
uint32_t PaperMonoNfcController::totalBytes() const { return 0; }
uint32_t PaperMonoNfcController::uiRevision() const { return 0; }
uint32_t PaperMonoNfcController::timeSyncRevision() const { return 0; }
void PaperMonoNfcController::markDisplayed(bool) {}
const char* PaperMonoNfcController::phaseName() const { return "IDLE"; }
const char* PaperMonoNfcController::statusName() const { return "HARDWARE_ERROR"; }

#endif
