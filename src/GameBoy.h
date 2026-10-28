#pragma once

#include "CPU.h"
#include "PPU.h"

namespace gb {

class GameBoy {
public:
    GameBoy() : bus(cartridge), ppu(bus), cpu(bus) {}

    // The components hold references to each other, so do not copy this object.
    GameBoy(const GameBoy&) = delete;
    GameBoy& operator=(const GameBoy&) = delete;

    void load(const std::filesystem::path& path) {
        cartridge.load(path);
        reset();
    }

    void reset() {
        bus.reset();
        ppu.reset();
        cpu.reset();

        ticks = 0;
        frameDebt = 0;
    }

    int step() {
        int cycles;

        // Simplification: pause the whole CPU while DMA copies sprite data.
        if (bus.dmaActive()) {
            cycles = 4;
        } else {
            cycles = cpu.step();
        }

        // Note: these are Game Boy clock ticks, not milliseconds on the PC.
        // Give the timer and PPU the same amount of time as the CPU instruction.
        for (int i = 0; i < cycles; ++i) {
            bus.tick();
            ppu.tick();
        }

        ticks += cycles;
        return cycles;
    }

    void runFrame() {
        // One frame lasts 154 lines * 456 ticks = 70,224 ticks.
        // Carry any instruction overshoot into the next frame's budget.
        frameDebt += 70224;

        while (frameDebt > 0) {
            frameDebt -= step();
        }
    }

    // Declaration order matters: each dependency is constructed first.
    Cartridge cartridge;
    Bus bus;
    PPU ppu;
    CPU cpu;
    uint64_t ticks = 0;

private:
    int frameDebt = 0;
};

} // namespace gb
