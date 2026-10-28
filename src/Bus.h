#pragma once

#include "Cartridge.h"

#include <array>

namespace gb {

class Bus {
public:
    explicit Bus(Cartridge& gameCartridge) : cartridge(gameCartridge) {
        reset();
    }

    void reset();

    // CPU accesses go through read/write so hardware rules are applied.
    uint8_t read(uint16_t address) const;
    void write(uint16_t address, uint8_t value);

    // DMA and debugging can read without the CPU's display restrictions.
    uint8_t peek(uint16_t address) const;

    void tick(); // Advance the timer and DMA by one clock tick.
    void setButtons(uint8_t pressed);
    uint8_t joypad() const;

    void requestInterrupt(int bit) {
        io[0x0F] |= uint8_t(1 << bit);
    }

    bool dmaActive() const {
        return dmaRemaining > 0;
    }

    std::array<uint8_t, 8192> vram{}; // Tile graphics and tile maps.
    std::array<uint8_t, 8192> wram{}; // The game's working memory.
    std::array<uint8_t, 160> oam{};   // 40 sprite entries, 4 bytes each.
    std::array<uint8_t, 128> io{};    // Hardware registers at FF00-FF7F.
    std::array<uint8_t, 127> hram{};  // Small RAM area at FF80-FFFE.

    uint8_t ie = 0; // Interrupt enable bits at FFFF.
    uint8_t ppuMode = 2;
    uint16_t divider = 0xABCC;
    bool joyEdge = false;

private:
    Cartridge& cartridge;

    // A set bit means pressed: Right, Left, Up, Down, A, B, Select, Start.
    uint8_t buttons = 0;

    int reloadDelay = 0;
    int dmaRemaining = 0;
    int dmaTicks = 0;
    uint16_t dmaSource = 0;

    bool timerSignal() const;
    void timerEdge(bool previous);
};

} // namespace gb
