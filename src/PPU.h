#pragma once

#include "Bus.h"

#include <array>

namespace gb {

class PPU {
public:
    explicit PPU(Bus& memory) : bus(memory) {
        reset();
    }

    void reset();
    void tick();

    // The Windows program displays these pixels. Each stores 0x00RRGGBB.
    std::array<uint32_t, 160 * 144> pixels{};
    uint64_t frames = 0;

private:
    Bus& bus;

    int dot = 0; // Clock position within the current scanline.
    int line = 0;

    bool lcdOn = false;
    bool statLine = false;

    void updateSTAT();
    void renderLine();
    uint8_t tilePixel(uint16_t map, int x, int y, bool unsignedTiles) const;
    static uint32_t shade(uint8_t palette, uint8_t color);
};

} // namespace gb
