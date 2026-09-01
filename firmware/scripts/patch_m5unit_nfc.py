"""Apply PaperMono NFC-A emulation transport and field-detection fixes.

M5Unit-NFC 0.1.0 uses 64-byte stack buffers in the NFC-A emulation listener.
PaperMono's tested transport uses a 253-byte command (240-byte DATA payload),
so both listener buffers are expanded to 256 bytes. The explicit length guard
also makes malformed RF frames fail closed before the FIFO read.
"""

from pathlib import Path

Import("env")


_ORIGINAL = """        _u.readFIFOSize(bytes, bits);
        rx_len = bytes;

        if (irq32 & (I_par32 | I_crc32 | I_err132 | I_err232) || rx_len <= 2) {
"""

_RX_BUFFER_ORIGINAL = "uint8_t rx[64]{};"
_RX_BUFFER_PATCHED = "uint8_t rx[256]{};"

_LEGACY_PATCHED = """        _u.readFIFOSize(bytes, bits);
        rx_len = bytes;

        // PaperMono: reject oversized RF frames before reading into rx[64].
        if (rx_len > sizeof(rx)) {
            M5_LIB_LOGE("A RX oversized %u", rx_len);
            _u.writeDirectCommand(CMD_CLEAR_FIFO);
            _u.writeDirectCommand(CMD_UNMASK_RECEIVE_DATA);
            return EmulationLayerA::State::Active;
        }

        if (irq32 & (I_par32 | I_crc32 | I_err132 | I_err232) || rx_len <= 2) {
"""

_PATCHED = """        _u.readFIFOSize(bytes, bits);
        rx_len = bytes;

        // PaperMono: reject oversized RF frames before reading into rx[256].
        if (rx_len > sizeof(rx)) {
            M5_LIB_LOGE("A RX oversized %u", rx_len);
            _u.writeDirectCommand(CMD_CLEAR_FIFO);
            _u.writeDirectCommand(CMD_UNMASK_RECEIVE_DATA);
            return EmulationLayerA::State::Active;
        }

        if (irq32 & (I_par32 | I_crc32 | I_err132 | I_err232) || rx_len <= 2) {
"""

_OFF_ORIGINAL = """    if ((get_irq(I_eon32) & I_eon32)) {
        return goto_idle();
    }
"""

_OFF_PATCHED = """    if ((get_irq(I_eon32) & I_eon32) || is_extra_field()) {
        return goto_idle();
    }
"""

_ACTIVE_EOF_ORIGINAL = """    if (is_eof(irq32)) {
        return goto_off();
    }

    uint16_t bytes{};
"""

_ACTIVE_EOF_PATCHED = """    if (is_eof(irq32)) {
        uint8_t auxiliary{};
        const bool auxiliary_ok = _u.readAuxiliaryDisplay(auxiliary);
        printf("[paper.nfc.lib] active_eof aux_ok=%u aux=%02X efd=%u\\n",
               auxiliary_ok, auxiliary, auxiliary_ok && (auxiliary & efd_o));
        return goto_off();
    }

    uint16_t bytes{};
"""

_ACTIVE_RX_ERROR_ORIGINAL = """            _u.readFIFO(actual, rx, rx_len);
            M5_LIB_LOGE("A ERR %08X %u %u %02X", irq32, _wakeup, rx_len, rx[0]);

            _u.writeDirectCommand(CMD_CLEAR_FIFO);
"""

_ACTIVE_RX_ERROR_PATCHED = """            _u.readFIFO(actual, rx, rx_len);
            printf("[paper.nfc.lib] active_rx_error irq=%08lX wakeup=%u len=%u first=%02X bits=%u\\n",
                   static_cast<unsigned long>(irq32), _wakeup, rx_len, rx[0], bits);
            M5_LIB_LOGE("A ERR %08X %u %u %02X", irq32, _wakeup, rx_len, rx[0]);

            _u.writeDirectCommand(CMD_CLEAR_FIFO);
"""

_IRQ_POLL_ORIGINAL = """    if (_u._interrupt_occurred) {  // In I2C, Unit::update acquires IRQ each time.
"""

_IRQ_POLL_PATCHED = """    // The ST25R3916 IRQ output stays high until its interrupt registers are
    // read. If the GPIO rising edge is missed, checking only the ISR flag
    // deadlocks NFC-A emulation in Ready forever. The driver's blocking
    // wait_for_interrupt() already polls the pin level for the same reason.
    if (_u._interrupt_occurred ||
        (_u._using_irq && gpio_get_level(static_cast<gpio_num_t>(_u._cfg.irq)))) {
"""

_IRQ_ORIGINAL = """        if (gpio_isr_handler_add(static_cast<gpio_num_t>(_cfg.irq), &UnitST25R3916::on_irq, this) != ESP_OK) {
"""

_IRQ_PATCHED = """        // A failed begin() can leave this instance's IRQ handler installed.
        // Remove it before a bounded startup retry registers the same handler.
        (void)gpio_isr_handler_remove(static_cast<gpio_num_t>(_cfg.irq));
        if (gpio_isr_handler_add(static_cast<gpio_num_t>(_cfg.irq), &UnitST25R3916::on_irq, this) != ESP_OK) {
"""

_TYPE_ORIGINAL = """    if (!(picc.isNTAG2() || picc.type == Type::MIFARE_Ultralight)) {
"""

_TYPE_PATCHED = """    // M5Unit-NFC has version responses and command handling for the
    // complete Ultralight family; allow those concrete types for emulation.
    if (!(picc.isNTAG2() || picc.isMifareUltralight())) {
"""

_BEGIN_RESULT_ORIGINAL = """    return !_cfg.emulation ? configureNFCMode(_cfg.mode) : configureEmulationMode(_cfg.mode);
"""

_BEGIN_RESULT_PATCHED = """    const bool configured =
        !_cfg.emulation ? configureNFCMode(_cfg.mode) : configureEmulationMode(_cfg.mode);
    if (!configured) {
        printf("[paper.nfc.lib] init_fail=configure_mode mode=%u emulation=%u\\n",
               static_cast<unsigned>(_cfg.mode), static_cast<unsigned>(_cfg.emulation));
    }
    return configured;
"""

_EMULATION_RECONFIG_ORIGINAL = """bool UnitST25R3916::configureEmulationMode(const m5::nfc::NFC mode)
{
    if (!_cfg.emulation || mode == NFC::None) {
        return false;
    }
    if (NFCMode() == mode) {
        return true;
    }
"""

_EMULATION_RECONFIG_PATCHED = """bool UnitST25R3916::configureEmulationMode(const m5::nfc::NFC mode)
{
    if (!_cfg.emulation || mode == NFC::None) {
        return false;
    }
    // PaperMono: an e-paper refresh can disturb target-mode registers while
    // the cached NFC mode remains A. Do not turn an explicit reconfiguration
    // request into a no-op; reapply the hardware settings below.
"""

_EMULATION_TX_ORIGINAL = """bool UnitST25R3916::nfcaEmulationTransmit(const uint8_t* tx, const uint16_t tx_len)
{
    if (!tx || !tx_len) {
        return false;
    }
    return writeDirectCommand(CMD_CLEAR_FIFO) &&  //
           writeFIFO(tx, tx_len) &&               //
           writeNumberOfTransmittedBytes(tx_len, 0) && writeDirectCommand(CMD_TRANSMIT_WITH_CRC);
}
"""

_EMULATION_TX_PATCHED = """bool UnitST25R3916::nfcaEmulationTransmit(const uint8_t* tx, const uint16_t tx_len)
{
    if (!tx || !tx_len) {
        return false;
    }

    // The target must answer within the NFC-A frame timing window. A single
    // transient I2C setup failure previously made the emulation layer fall
    // from Active to Idle, after which Core NFC could no longer select the
    // tag. Retry only the idempotent FIFO preparation sequence. Do not retry
    // CMD_TRANSMIT_WITH_CRC because an ambiguous I2C result could duplicate an
    // RF response that was already launched.
    for (uint8_t attempt = 0; attempt < 2; ++attempt) {
        if (!writeDirectCommand(CMD_CLEAR_FIFO)) {
            printf("[paper.nfc.lib] tx_setup_fail stage=clear attempt=%u len=%u\\n", attempt + 1, tx_len);
            continue;
        }
        if (!writeFIFO(tx, tx_len)) {
            printf("[paper.nfc.lib] tx_setup_fail stage=fifo attempt=%u len=%u\\n", attempt + 1, tx_len);
            continue;
        }
        if (!writeNumberOfTransmittedBytes(tx_len, 0)) {
            printf("[paper.nfc.lib] tx_setup_fail stage=length attempt=%u len=%u\\n", attempt + 1, tx_len);
            continue;
        }
        if (!writeDirectCommand(CMD_TRANSMIT_WITH_CRC)) {
            printf("[paper.nfc.lib] tx_start_fail len=%u\\n", tx_len);
            return false;
        }
        return true;
    }
    return false;
}
"""

_FAILURE_LOG_PATCHES = (
    ('M5_LIB_LOGE("Failed to CMD_SET_DEFAULT");',
     'printf("[paper.nfc.lib] init_fail=set_default\\n");\n        M5_LIB_LOGE("Failed to CMD_SET_DEFAULT");'),
    ('M5_LIB_LOGE("Failed to send protection command");',
     'printf("[paper.nfc.lib] init_fail=protection_command\\n");\n        M5_LIB_LOGE("Failed to send protection command");'),
    ('M5_LIB_LOGE("Failed to writeIOConfiguration");',
     'printf("[paper.nfc.lib] init_fail=io_configuration\\n");\n        M5_LIB_LOGE("Failed to writeIOConfiguration");'),
    ('M5_LIB_LOGE("Failed to TXDriver");',
     'printf("[paper.nfc.lib] init_fail=tx_driver\\n");\n        M5_LIB_LOGE("Failed to TXDriver");'),
    ('M5_LIB_LOGE("Failed to writeMaskInterrupt");',
     'printf("[paper.nfc.lib] init_fail=mask_interrupts\\n");\n        M5_LIB_LOGE("Failed to writeMaskInterrupt");'),
    ('M5_LIB_LOGE("Failed to enable_osc");',
     'printf("[paper.nfc.lib] init_fail=oscillator\\n");\n        M5_LIB_LOGE("Failed to enable_osc");'),
    ('M5_LIB_LOGE("Failed to CMD_ADJUST_REGULATORS");',
     'printf("[paper.nfc.lib] init_fail=adjust_regulators\\n");\n        M5_LIB_LOGE("Failed to CMD_ADJUST_REGULATORS");'),
)

def patch_library(*_args, **_kwargs):
    libdeps_dir = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV")
    candidates = list(
        libdeps_dir.glob(
            "M5Unit-NFC*/src/nfc/layer/a/emulation_layer_a_ST25R3916.cpp"
        )
    )
    if not candidates:
        return

    source = candidates[0]
    text = source.read_text(encoding="utf-8")
    changed = False
    rx_buffer_count = text.count(_RX_BUFFER_ORIGINAL) + text.count(_RX_BUFFER_PATCHED)
    if rx_buffer_count != 2:
        raise RuntimeError(
            "M5Unit-NFC emulation receive buffers changed; expected exactly "
            f"two buffers before building: {source}"
        )
    if _RX_BUFFER_ORIGINAL in text:
        text = text.replace(_RX_BUFFER_ORIGINAL, _RX_BUFFER_PATCHED)
        changed = True
    if _LEGACY_PATCHED in text:
        text = text.replace(_LEGACY_PATCHED, _PATCHED, 1)
        changed = True
    if _PATCHED not in text:
        if _ORIGINAL not in text:
            raise RuntimeError(
                "M5Unit-NFC receive implementation changed; review the 256-byte "
                f"emulation buffer before building: {source}"
            )
        text = text.replace(_ORIGINAL, _PATCHED, 1)
        changed = True
    if _OFF_PATCHED not in text:
        if _OFF_ORIGINAL not in text:
            raise RuntimeError(
                "M5Unit-NFC Off-state implementation changed; review external "
                f"field detection before building: {source}"
            )
        text = text.replace(_OFF_ORIGINAL, _OFF_PATCHED, 1)
        changed = True
    if _ACTIVE_EOF_PATCHED not in text:
        if _ACTIVE_EOF_ORIGINAL not in text:
            raise RuntimeError(
                "M5Unit-NFC Active EOF handling changed; review field-drop "
                f"diagnostics before building: {source}"
            )
        text = text.replace(_ACTIVE_EOF_ORIGINAL, _ACTIVE_EOF_PATCHED, 1)
        changed = True
    if _ACTIVE_RX_ERROR_PATCHED not in text:
        if _ACTIVE_RX_ERROR_ORIGINAL not in text:
            raise RuntimeError(
                "M5Unit-NFC Active receive-error handling changed; review RF "
                f"diagnostics before building: {source}"
            )
        text = text.replace(
            _ACTIVE_RX_ERROR_ORIGINAL, _ACTIVE_RX_ERROR_PATCHED, 1
        )
        changed = True
    if _IRQ_POLL_PATCHED not in text:
        if _IRQ_POLL_ORIGINAL not in text:
            raise RuntimeError(
                "M5Unit-NFC emulation IRQ polling changed; review the Ready "
                f"state interrupt path before building: {source}"
            )
        text = text.replace(_IRQ_POLL_ORIGINAL, _IRQ_POLL_PATCHED, 1)
        changed = True
    if changed:
        source.write_text(text, encoding="utf-8")
        print(f"Patched PaperMono M5Unit-NFC emulation: {source}")

    layer_candidates = list(
        libdeps_dir.glob("M5Unit-NFC*/src/nfc/layer/a/emulation_layer_a.cpp")
    )
    if not layer_candidates:
        return
    layer_source = layer_candidates[0]
    layer_text = layer_source.read_text(encoding="utf-8")
    if _TYPE_PATCHED not in layer_text:
        if _TYPE_ORIGINAL not in layer_text:
            raise RuntimeError(
                "M5Unit-NFC emulation type check changed; review Ultralight EV1 "
                f"support before building: {layer_source}"
            )
        layer_text = layer_text.replace(_TYPE_ORIGINAL, _TYPE_PATCHED, 1)
        layer_source.write_text(layer_text, encoding="utf-8")
        print(f"Patched PaperMono M5Unit-NFC Ultralight EV1 support: {layer_source}")

    unit_candidates = list(
        libdeps_dir.glob("M5Unit-NFC*/src/unit/unit_ST25R3916.cpp")
    )
    if not unit_candidates:
        return
    unit_source = unit_candidates[0]
    unit_text = unit_source.read_text(encoding="utf-8")
    unit_changed = False
    if _IRQ_PATCHED not in unit_text:
        if _IRQ_ORIGINAL not in unit_text:
            raise RuntimeError(
                "M5Unit-NFC IRQ setup changed; review startup retry before "
                f"building: {unit_source}"
            )
        unit_text = unit_text.replace(_IRQ_ORIGINAL, _IRQ_PATCHED, 1)
        unit_changed = True
    if _BEGIN_RESULT_PATCHED not in unit_text:
        if _BEGIN_RESULT_ORIGINAL not in unit_text:
            raise RuntimeError(
                "M5Unit-NFC begin result changed; review startup diagnostics "
                f"before building: {unit_source}"
            )
        unit_text = unit_text.replace(
            _BEGIN_RESULT_ORIGINAL, _BEGIN_RESULT_PATCHED, 1
        )
        unit_changed = True
    if _EMULATION_RECONFIG_PATCHED not in unit_text:
        if _EMULATION_RECONFIG_ORIGINAL not in unit_text:
            raise RuntimeError(
                "M5Unit-NFC emulation mode caching changed; review forced "
                f"target reconfiguration before building: {unit_source}"
            )
        unit_text = unit_text.replace(
            _EMULATION_RECONFIG_ORIGINAL, _EMULATION_RECONFIG_PATCHED, 1
        )
        unit_changed = True
    for original, patched in _FAILURE_LOG_PATCHES:
        if patched in unit_text:
            continue
        if original not in unit_text:
            raise RuntimeError(
                "M5Unit-NFC initialization failure path changed; review "
                f"startup diagnostics before building: {unit_source}"
            )
        unit_text = unit_text.replace(original, patched, 1)
        unit_changed = True
    if unit_changed:
        unit_source.write_text(unit_text, encoding="utf-8")
        print(f"Patched PaperMono M5Unit-NFC startup handling: {unit_source}")

    nfca_candidates = list(
        libdeps_dir.glob("M5Unit-NFC*/src/unit/unit_ST25R3916_nfca.cpp")
    )
    if not nfca_candidates:
        return
    nfca_source = nfca_candidates[0]
    nfca_text = nfca_source.read_text(encoding="utf-8")
    if _EMULATION_TX_PATCHED not in nfca_text:
        if _EMULATION_TX_ORIGINAL not in nfca_text:
            raise RuntimeError(
                "M5Unit-NFC emulation transmit implementation changed; "
                f"review retry safety before building: {nfca_source}"
            )
        nfca_text = nfca_text.replace(
            _EMULATION_TX_ORIGINAL, _EMULATION_TX_PATCHED, 1
        )
        nfca_source.write_text(nfca_text, encoding="utf-8")
        print(f"Patched PaperMono M5Unit-NFC emulation transmit: {nfca_source}")

patch_library()
env.AddPreAction("buildprog", patch_library)
