#include "PPU.h"

#include <algorithm>
#include <vector>

namespace gb {

uint32_t PPU::shade(uint8_t palette, uint8_t color) {
    const uint32_t colors[] = {0xE0F0C0, 0x88B068, 0x346856, 0x102838};

    // Each two-bit field in a palette chooses one of the four shades.
    const int shadeIndex = (palette >> (color * 2)) & 3;
    return colors[shadeIndex];
}

void PPU::reset() {
    dot = 0;
    line = 0;
    frames = 0;

    statLine = false;
    lcdOn = (bus.io[0x40] & 0x80) != 0;

    bus.io[0x44] = 0;
    bus.ppuMode = lcdOn ? 2 : 0;

    pixels.fill(shade(0, 0));
    updateSTAT();
}

void PPU::updateSTAT() {
    const bool match = bus.io[0x44] == bus.io[0x45];

    // Keep the game's interrupt settings and replace the PPU-owned status bits.
    bus.io[0x41] = uint8_t((bus.io[0x41] & 0x78) | (match ? 4 : 0) | bus.ppuMode);
    const uint8_t stat = bus.io[0x41];

    const bool compareInterrupt = (stat & 0x40) && match;
    const bool hblankInterrupt = (stat & 0x08) && bus.ppuMode == 0;
    const bool vblankInterrupt = (stat & 0x10) && bus.ppuMode == 1;
    const bool oamInterrupt = (stat & 0x20) && bus.ppuMode == 2;

    const bool signal =
        lcdOn && (compareInterrupt || hblankInterrupt || vblankInterrupt || oamInterrupt);

    if (signal && !statLine) {
        bus.requestInterrupt(1);
    }

    statLine = signal;
}

uint8_t PPU::tilePixel(uint16_t map, int x, int y, bool unsignedTiles) const {
    // A map is a 32 by 32 grid of tile numbers. Each tile is 8 by 8 pixels.
    const int tileX = (x / 8) & 31;
    const int tileY = (y / 8) & 31;
    const int mapOffset = map - 0x8000 + tileY * 32 + tileX;
    const int tile = bus.vram[mapOffset];

    int address;

    if (unsignedTiles) {
        address = tile * 16;
    } else {
        const int signedTile = (tile < 128) ? tile : tile - 256;
        address = 0x1000 + signedTile * 16;
    }

    // Two bytes describe one row. One bit from each byte gives a color 0-3.
    address += (y & 7) * 2;
    const int bit = 7 - (x & 7);
    const int low = (bus.vram[address] >> bit) & 1;
    const int high = (bus.vram[address + 1] >> bit) & 1;

    return uint8_t(low | (high << 1));
}

void PPU::renderLine() {
    const uint8_t lcd = bus.io[0x40];
    std::array<uint8_t, 160> backgroundColors{};

    const bool backgroundEnabled = (lcd & 1) != 0;
    const bool unsignedTiles = (lcd & 0x10) != 0;

    // Draw the scrolling background first. This version has no window layer.
    for (int x = 0; x < 160; ++x) {
        uint8_t color = 0;

        if (backgroundEnabled) {
            const int backgroundX = (x + bus.io[0x43]) & 255;
            const int backgroundY = (line + bus.io[0x42]) & 255;
            const uint16_t map = (lcd & 8) ? 0x9C00 : 0x9800;

            color = tilePixel(map, backgroundX, backgroundY, unsignedTiles);
        }

        // Keep the color number too; sprites use it for background priority.
        backgroundColors[x] = color;

        if (backgroundEnabled) {
            pixels[line * 160 + x] = shade(bus.io[0x47], color);
        } else {
            pixels[line * 160 + x] = shade(0, 0);
        }
    }

    if (!(lcd & 2)) {
        return; // Sprites are disabled.
    }

    // Select up to ten sprites that overlap this line. All sprites are 8x8 here.
    std::vector<int> selected;

    for (int i = 0; i < 40 && selected.size() < 10; ++i) {
        const int spriteY = int(bus.oam[i * 4]) - 16;

        if (line >= spriteY && line < spriteY + 8) {
            selected.push_back(i);
        }
    }

    // Smaller X wins. A stable sort preserves OAM order when X is equal.
    std::stable_sort(selected.begin(), selected.end(), [&](int a, int b) {
        return bus.oam[a * 4 + 1] < bus.oam[b * 4 + 1];
    });

    for (int x = 0; x < 160; ++x) {
        for (int sprite : selected) {
            const int entry = sprite * 4;
            const int spriteX = int(bus.oam[entry + 1]) - 8;
            const int spriteY = int(bus.oam[entry]) - 16;

            if (x < spriteX || x >= spriteX + 8) {
                continue;
            }

            const uint8_t flags = bus.oam[entry + 3];
            int row = line - spriteY;
            int column = x - spriteX;

            if (flags & 0x40) {
                row = 7 - row;
            }

            if (flags & 0x20) {
                column = 7 - column;
            }

            const int tile = bus.oam[entry + 2];
            const int address = tile * 16 + row * 2;
            const int bit = 7 - column;
            const int low = (bus.vram[address] >> bit) & 1;
            const int high = (bus.vram[address + 1] >> bit) & 1;
            const uint8_t color = uint8_t(low | (high << 1));

            if (color == 0) {
                continue; // Sprite color 0 is transparent.
            }

            if (!(flags & 0x80) || backgroundColors[x] == 0) {
                const uint8_t palette = bus.io[(flags & 0x10) ? 0x49 : 0x48];
                pixels[line * 160 + x] = shade(palette, color);
            }

            // This sprite wins over later sprites, even if the background hides it.
            break;
        }
    }
}

void PPU::tick() {
    const bool enabled = (bus.io[0x40] & 0x80) != 0;

    if (!enabled) {
        if (lcdOn) {
            pixels.fill(shade(0, 0));
        }

        lcdOn = false;
        dot = 0;
        line = 0;

        bus.io[0x44] = 0;
        bus.ppuMode = 0;
        updateSTAT();
        return;
    }

    if (!lcdOn) {
        lcdOn = true;
        dot = 0;
        line = 0;
        bus.ppuMode = 2;
    }

    ++dot;

    // Fixed timing per visible line: 80 ticks OAM, 172 drawing, 204 HBlank.
    // Rendering a whole line at once is simpler than a real pixel pipeline.
    if (line < 144) {
        if (dot == 80) {
            bus.ppuMode = 3;
        }

        if (dot == 252) {
            renderLine();
            bus.ppuMode = 0;
        }
    }

    if (dot == 456) {
        dot = 0;
        ++line;

        if (line == 144) {
            bus.ppuMode = 1;
            ++frames;
            bus.requestInterrupt(0);
        } else if (line == 154) {
            line = 0;
            bus.ppuMode = 2;
        } else if (line < 144) {
            bus.ppuMode = 2;
        }

        bus.io[0x44] = uint8_t(line);
    }

    updateSTAT();
}

} // namespace gb
