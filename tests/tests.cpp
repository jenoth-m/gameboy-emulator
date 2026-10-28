#include "GameBoy.h"

#include <algorithm>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

void check(bool passed, const char* message) {
    if (!passed) {
        throw std::runtime_error(message);
    }
}

void loadProgram(gb::GameBoy& game, std::initializer_list<uint8_t> instructions) {
    // A tiny ROM lets us test a few CPU instructions without a whole game.
    std::vector<uint8_t> rom(32768, 0);
    std::copy(instructions.begin(), instructions.end(), rom.begin() + 0x0100);

    game.cartridge.loadBytes(std::move(rom));
    game.reset();
    game.bus.write(0xFF0F, 0);
    game.cpu.F = 0;
}

void testFetchAndMemory() {
    gb::GameBoy game;
    loadProgram(game, {0x3E, 0x2A, 0xEA, 0x03, 0xC0});

    // LD A, 2A consumes both the opcode and its one-byte operand.
    check(game.cpu.step() == 8, "LD A,d8 timing");
    check(game.cpu.A == 0x2A, "LD A,d8 result");
    check(game.cpu.PC == 0x0102, "PC skips the operand byte");

    // LD (C003),A: the address is stored in the ROM as 03 followed by C0.
    check(game.cpu.step() == 16, "LD (a16),A timing");
    check(game.bus.read(0xC003) == 0x2A, "Store into working RAM");
    check(game.bus.read(0xE003) == 0x2A, "Echo RAM shares the same byte");

    game.bus.write(0x0100, 0xFF);
    check(game.bus.read(0x0100) == 0x3E, "ROM cannot be overwritten by the CPU");
    game.bus.write(0xA000, 0x12);
    check(game.bus.read(0xA000) == 0xFF, "No cartridge RAM in this version");
}

void testArithmetic() {
    gb::GameBoy game;
    loadProgram(game, {0x3E, 0xFF, 0xC6, 0x01, 0xD6, 0x01, 0xFE, 0xFF});

    game.cpu.step(); // LD A,FF
    game.cpu.step(); // ADD A,1
    check(game.cpu.A == 0, "Addition wraps at 256");
    check(game.cpu.F == 0xB0, "Addition sets zero, half-carry and carry");

    game.cpu.step(); // SUB 1
    check(game.cpu.A == 0xFF, "Subtraction wraps below zero");
    check(game.cpu.F == 0x70, "Subtraction sets subtract, half-borrow and borrow");

    game.cpu.step(); // CP FF
    check(game.cpu.A == 0xFF, "Comparison leaves A unchanged");
    check(game.cpu.F == 0xC0, "Equal comparison sets Z and N");
}

void testBitInstructions() {
    gb::GameBoy game;
    loadProgram(game, {0x06, 0x80, 0xCB, 0x00, 0xCB, 0x40, 0xCB, 0x80});

    game.cpu.step(); // LD B,80
    check(game.cpu.step() == 8, "CB register instruction timing");
    check(game.cpu.B == 1 && game.cpu.F == 0x10, "RLC wraps the high bit");

    game.cpu.step(); // BIT 0,B
    check(game.cpu.B == 1 && game.cpu.F == 0x30, "BIT tests without modifying B");

    game.cpu.step(); // RES 0,B
    check(game.cpu.B == 0 && game.cpu.F == 0x30, "RES clears a bit and keeps flags");
}

void testJumpsAndStack() {
    gb::GameBoy game;
    loadProgram(game, {0xAF, 0x20, 0x02, 0x28, 0x02, 0x00, 0x00, 0x76});

    game.cpu.step(); // XOR A makes A zero, setting Z.
    check(game.cpu.step() == 8, "Untaken JR NZ takes 8 ticks");
    check(game.cpu.PC == 0x0103, "Untaken branch still consumes its operand");
    check(game.cpu.step() == 12, "Taken JR Z takes 12 ticks");
    check(game.cpu.PC == 0x0107, "Relative jump uses PC after the operand");

    loadProgram(game, {0xCD, 0x06, 0x01, 0x76, 0x00, 0x00, 0x3E, 0x42, 0xC9});
    check(game.cpu.step() == 24, "CALL timing");
    check(game.cpu.PC == 0x0106 && game.cpu.SP == 0xFFFC, "CALL jumps and pushes");
    check(game.bus.read(0xFFFC) == 0x03, "Return address low byte");
    check(game.bus.read(0xFFFD) == 0x01, "Return address high byte");

    game.cpu.step(); // LD A,42
    check(game.cpu.step() == 16, "RET timing");
    check(game.cpu.PC == 0x0103 && game.cpu.SP == 0xFFFE, "RET restores PC and SP");
    check(game.cpu.A == 0x42, "The called function ran");
}

void testInterrupts() {
    gb::GameBoy game;
    loadProgram(game, {0xFB, 0x00, 0x00}); // EI, NOP, NOP
    game.bus.write(0xFFFF, 1);
    game.bus.write(0xFF0F, 1);

    game.cpu.step();
    check(!game.cpu.ime, "EI is delayed");
    game.cpu.step();
    check(game.cpu.ime, "EI takes effect after the next instruction");
    check(game.cpu.step() == 20, "Interrupt entry timing");
    check(game.cpu.PC == 0x0040, "VBlank interrupt vector");
    check(!game.cpu.ime && !(game.bus.io[0x0F] & 1), "Interrupt is acknowledged");

    // HALT waits without consuming the next instruction.
    loadProgram(game, {0x76, 0x3E, 0x2A});
    check(game.cpu.step() == 4 && game.cpu.halted, "HALT enters the waiting state");
    check(game.cpu.step() == 4 && game.cpu.PC == 0x0101, "HALT leaves PC alone");

    game.bus.write(0xFFFF, 1);
    game.bus.write(0xFF0F, 1);
    check(game.cpu.step() == 8 && !game.cpu.halted, "An enabled request wakes HALT");
    check(game.cpu.A == 0x2A && game.cpu.PC == 0x0103, "Normal fetching resumes");

    // The simplified HALT also fetches normally if a request was already pending.
    loadProgram(game, {0x76, 0x3E, 0x2A});
    game.bus.write(0xFFFF, 1);
    game.bus.write(0xFF0F, 1);
    game.cpu.step();
    game.cpu.step();
    check(game.cpu.A == 0x2A && game.cpu.PC == 0x0103, "No repeated opcode after HALT");

    // With IME enabled, waking from HALT enters the interrupt handler instead.
    loadProgram(game, {0x76, 0x00});
    game.cpu.ime = true;
    game.cpu.step();
    game.bus.write(0xFFFF, 1);
    game.bus.write(0xFF0F, 1);
    check(game.cpu.step() == 20 && !game.cpu.halted, "Interrupt service wakes HALT");
    check(game.cpu.PC == 0x0040, "HALT wake enters the right handler");
}

void testButtonsAndTimer() {
    gb::GameBoy game;
    loadProgram(game, {0x00});
    gb::Bus& bus = game.bus;

    bus.write(0xFF00, 0x20); // Select directional buttons.
    bus.setButtons(1);       // Press Right.
    check((bus.read(0xFF00) & 0x0F) == 0x0E, "Pressed button reads as zero");
    check(bus.io[0x0F] & 0x10, "New button press requests interrupt");

    bus.write(0xFF04, 0);
    bus.write(0xFF06, 0x77);
    bus.write(0xFF05, 0xFF);
    bus.write(0xFF07, 0x05); // Timer on, using divider bit 3.

    for (int i = 0; i < 16; ++i) {
        bus.tick();
    }

    check(bus.read(0xFF05) == 0, "TIMA overflows before reload");
    check(!(bus.io[0x0F] & 4), "Timer interrupt waits for reload");

    for (int i = 0; i < 4; ++i) {
        bus.tick();
    }

    check(bus.read(0xFF05) == 0x77, "TIMA reloads from TMA after four ticks");
    check(bus.io[0x0F] & 4, "Timer reload requests interrupt");
}

void testDMA() {
    gb::GameBoy game;
    gb::Bus& bus = game.bus;

    for (int i = 0; i < 160; ++i) {
        bus.write(uint16_t(0xC000 + i), uint8_t(i));
    }

    bus.write(0xFF46, 0xC0);
    check(bus.dmaActive(), "DMA starts on the control-register write");

    for (int i = 0; i < 640; ++i) {
        bus.tick();
    }

    check(!bus.dmaActive(), "DMA completes after 640 ticks");

    for (int i = 0; i < 160; ++i) {
        check(bus.oam[i] == i, "DMA copies the source byte into sprite RAM");
    }
}

void testPPU() {
    gb::GameBoy game;
    gb::Bus& bus = game.bus;

    bus.write(0xFF40, 0); // Turn LCD off so the CPU can write tile data.
    bus.write(0xFF47, 0xE4);

    for (int row = 0; row < 8; ++row) {
        bus.write(uint16_t(0x8000 + row * 2), 0xFF);
        bus.write(uint16_t(0x8001 + row * 2), 0x00);
    }

    bus.write(0xFF40, 0x91);
    game.ppu.reset();
    bus.write(0xFF0F, 0);

    for (int i = 0; i < 80; ++i) {
        game.ppu.tick();
    }

    check(bus.ppuMode == 3, "PPU starts drawing after OAM scanning");
    check(bus.read(0x8000) == 0xFF, "CPU cannot read VRAM while pixels are drawn");

    for (int i = 80; i < 252; ++i) {
        game.ppu.tick();
    }

    check(bus.ppuMode == 0, "PPU reaches HBlank");
    check(game.ppu.pixels[0] == 0x88B068, "Tile bits and palette produce a pixel");

    for (int i = 252; i < 456 * 144; ++i) {
        game.ppu.tick();
    }

    check(game.ppu.frames == 1 && bus.io[0x44] == 144, "PPU reaches VBlank");
    check(bus.io[0x0F] & 1, "VBlank requests interrupt");

    // Tile 1 has one sprite pixel in its top-left corner, using color 2.
    bus.write(0xFF40, 0);
    bus.write(0x8011, 0x80);
    bus.write(0xFE00, 16);
    bus.write(0xFE01, 8);
    bus.write(0xFE02, 1);
    bus.write(0xFF48, 0xE4);
    bus.write(0xFF40, 0x93);
    game.ppu.reset();

    for (int i = 0; i < 252; ++i) {
        game.ppu.tick();
    }

    check(game.ppu.pixels[0] == 0x346856, "8x8 sprite draws over the background");
    check(game.ppu.pixels[7] == 0x88B068, "Transparent sprite pixels reveal the background");

    bus.write(0xFF40, 0);
    bus.write(0xFE03, 0x20); // Flip the sprite horizontally.
    bus.write(0xFF40, 0x93);
    game.ppu.reset();

    for (int i = 0; i < 252; ++i) {
        game.ppu.tick();
    }

    check(game.ppu.pixels[7] == 0x346856, "Horizontal flip moves the pixel to the right");

    bus.write(0xFF40, 0);
    bus.write(0xFE03, 0x40); // Flip vertically: row 0 moves to row 7.
    bus.write(0xFF40, 0x93);
    game.ppu.reset();

    for (int i = 0; i < 7 * 456 + 252; ++i) {
        game.ppu.tick();
    }

    check(game.ppu.pixels[0] == 0x88B068, "Vertical flip clears the old sprite pixel");
    check(game.ppu.pixels[7 * 160] == 0x346856, "Vertical flip works with an 8-pixel height");
}

void expectRejected(const std::vector<uint8_t>& bytes) {
    gb::Cartridge cartridge;
    bool rejected = false;

    try {
        cartridge.loadBytes(bytes);
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    check(rejected, "Unsupported or malformed ROM must be rejected");
}

void testCartridgeLimits() {
    expectRejected({});

    std::vector<uint8_t> bytes(32768, 0);
    bytes[0x147] = 0x01; // MBC1 is outside this project's scope.
    expectRejected(bytes);

    bytes[0x147] = 0;
    bytes[0x143] = 0xC0;
    expectRejected(bytes);

    bytes[0x143] = 0;
    bytes[0x148] = 1;
    expectRejected(bytes);

    bytes[0x148] = 0;
    bytes[0x149] = 2;
    expectRejected(bytes);
}

} // namespace

int main() {
    try {
        testFetchAndMemory();
        testArithmetic();
        testBitInstructions();
        testJumpsAndStack();
        testInterrupts();
        testButtonsAndTimer();
        testDMA();
        testPPU();
        testCartridgeLimits();

        std::cout << "PASS: all nine core test groups\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
