#include "Bus.h"

namespace gb {

void Bus::reset() {
    vram.fill(0);
    wram.fill(0);
    oam.fill(0);
    io.fill(0);
    hram.fill(0);

    ie = 0;
    buttons = 0;
    ppuMode = 2;
    divider = 0xABCC;

    reloadDelay = 0;
    dmaRemaining = 0;
    dmaTicks = 0;
    dmaSource = 0;

    joyEdge = false;

    // Selected startup values, because this emulator skips the boot ROM.
    io[0x00] = 0x30;
    io[0x0F] = 0x01;
    io[0x40] = 0x91;
    io[0x47] = 0xFC;
    io[0x48] = 0xFF;
    io[0x49] = 0xFF;

    // These sound registers only store values. No audio is generated.
    io[0x10] = 0x80;
    io[0x11] = 0xBF;
    io[0x12] = 0xF3;
    io[0x14] = 0xBF;
    io[0x16] = 0x3F;
    io[0x19] = 0xBF;
    io[0x1A] = 0x7F;
    io[0x1B] = 0xFF;
    io[0x1C] = 0x9F;
    io[0x1E] = 0xBF;
    io[0x20] = 0xFF;
    io[0x23] = 0xBF;
    io[0x24] = 0x77;
    io[0x25] = 0xF3;
    io[0x26] = 0xF1;
}

uint8_t Bus::joypad() const {
    // The Game Boy reports a pressed button as 0, so invert our button bits.
    uint8_t low = 0x0F;

    if (!(io[0x00] & 0x10)) {
        low &= uint8_t(~buttons) & 0x0F;
    }

    if (!(io[0x00] & 0x20)) {
        low &= uint8_t(~(buttons >> 4)) & 0x0F;
    }

    return 0xC0 | (io[0x00] & 0x30) | low;
}

void Bus::setButtons(uint8_t pressed) {
    const uint8_t old = joypad();

    if (pressed & uint8_t(~buttons)) {
        joyEdge = true;
    }

    buttons = pressed;

    // A selected button changing from released to pressed requests an interrupt.
    if (old & uint8_t(~joypad()) & 0x0F) {
        requestInterrupt(4);
    }
}

uint8_t Bus::peek(uint16_t address) const {
    if (address < 0x8000) {
        return cartridge.read(address);
    }

    if (address < 0xA000) {
        return vram[address - 0x8000];
    }

    if (address < 0xC000) {
        return 0xFF; // Our supported cartridges have no external RAM.
    }

    if (address < 0xE000) {
        return wram[address - 0xC000];
    }

    if (address < 0xFE00) {
        // Note: C003 and E003 both reach wram[3]. There is only one stored byte.
        return wram[address - 0xE000];
    }

    if (address < 0xFEA0) {
        return oam[address - 0xFE00];
    }

    if (address < 0xFF00) {
        return 0xFF;
    }

    if (address == 0xFFFF) {
        return ie;
    }

    if (address >= 0xFF80) {
        return hram[address - 0xFF80];
    }

    // Some registers return calculated values or have unused bits fixed at 1.
    switch (address) {
    case 0xFF00:
        return joypad();

    case 0xFF01:
    case 0xFF02:
        return 0xFF; // Serial hardware is not implemented in this version.

    case 0xFF04:
        return uint8_t(divider >> 8);

    case 0xFF07:
        return io[0x07] | 0xF8;

    case 0xFF0F:
        return io[0x0F] | 0xE0;

    case 0xFF41:
        return io[0x41] | 0x80;

    case 0xFF50:
        return 0xFF; // No boot ROM is mapped.

    default:
        return io[address - 0xFF00];
    }
}

uint8_t Bus::read(uint16_t address) const {
    const bool lcdEnabled = (io[0x40] & 0x80) != 0;

    if (lcdEnabled && address >= 0x8000 && address < 0xA000 && ppuMode == 3) {
        return 0xFF; // The PPU is currently using video RAM.
    }

    if (address >= 0xFE00 && address < 0xFEA0) {
        if (dmaActive() || (lcdEnabled && ppuMode >= 2)) {
            return 0xFF;
        }
    }

    return peek(address);
}

bool Bus::timerSignal() const {
    // TAC selects one bit of the constantly increasing internal divider.
    const int bits[] = {9, 3, 5, 7};
    const int selectedBit = bits[io[0x07] & 3];
    const bool enabled = (io[0x07] & 4) != 0;

    return enabled && (divider & (1 << selectedBit));
}

void Bus::timerEdge(bool previous) {
    // TIMA increments when that selected signal changes from 1 to 0.
    if (previous && !timerSignal() && reloadDelay == 0) {
        ++io[0x05];

        if (io[0x05] == 0) {
            reloadDelay = 4;
        }
    }
}

void Bus::write(uint16_t address, uint8_t value) {
    if (address < 0x8000) {
        return; // ROM is read-only; this version has no bank controller.
    }

    if (address < 0xA000) {
        if (!(io[0x40] & 0x80) || ppuMode != 3) {
            vram[address - 0x8000] = value;
        }

        return;
    }

    if (address < 0xC000) {
        return; // No cartridge RAM.
    }

    if (address < 0xE000) {
        wram[address - 0xC000] = value;
        return;
    }

    if (address < 0xFE00) {
        wram[address - 0xE000] = value;
        return;
    }

    if (address < 0xFEA0) {
        if (!dmaActive() && (!(io[0x40] & 0x80) || ppuMode < 2)) {
            oam[address - 0xFE00] = value;
        }

        return;
    }

    if (address < 0xFF00) {
        return;
    }

    if (address == 0xFFFF) {
        ie = value;
        return;
    }

    if (address >= 0xFF80) {
        hram[address - 0xFF80] = value;
        return;
    }

    switch (address) {
    case 0xFF00: {
        const uint8_t old = joypad();
        io[0x00] = value & 0x30;

        if (old & uint8_t(~joypad()) & 0x0F) {
            requestInterrupt(4);
        }

        return;
    }

    case 0xFF01:
    case 0xFF02:
        // Ignore writes to the unsupported serial registers.
        return;

    case 0xFF04: {
        // Any write to DIV resets it; the written value does not matter.
        const bool old = timerSignal();
        divider = 0;
        timerEdge(old);
        return;
    }

    case 0xFF05:
        io[0x05] = value;
        reloadDelay = 0;
        return;

    case 0xFF07: {
        const bool old = timerSignal();
        io[0x07] = value & 7;
        timerEdge(old);
        return;
    }

    case 0xFF0F:
        io[0x0F] = value & 0x1F;
        return;

    case 0xFF41:
        // The game controls interrupt settings, but the PPU owns status bits.
        io[0x41] = (io[0x41] & 7) | (value & 0x78);
        return;

    case 0xFF44:
        return; // LY is the current display line, controlled by the PPU.

    case 0xFF46:
        // Example: writing C0 starts a copy from C000-C09F into sprite RAM.
        io[0x46] = value;
        dmaSource = uint16_t(value) << 8;
        dmaRemaining = 160;
        dmaTicks = 0;
        return;

    case 0xFF50:
        return;

    default:
        io[address - 0xFF00] = value;
        return;
    }
}

void Bus::tick() {
    if (reloadDelay > 0) {
        --reloadDelay;

        if (reloadDelay == 0) {
            io[0x05] = io[0x06]; // Reload TIMA from TMA.
            requestInterrupt(2);
        }
    }

    const bool old = timerSignal();
    ++divider;
    timerEdge(old);

    if (dmaRemaining > 0) {
        ++dmaTicks;

        if (dmaTicks == 4) {
            dmaTicks = 0;

            const int index = 160 - dmaRemaining;
            oam[index] = peek(uint16_t(dmaSource + index));
            --dmaRemaining;
        }
    }
}

} // namespace gb
