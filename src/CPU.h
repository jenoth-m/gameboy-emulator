#pragma once

#include "Bus.h"

namespace gb {

class CPU {
public:
    explicit CPU(Bus& memory) : bus(memory) {
        reset();
    }

    void reset();
    int step(); // Execute one instruction, or handle an interrupt; return ticks.

    uint8_t A = 0;
    uint8_t F = 0;
    uint8_t B = 0;
    uint8_t C = 0;
    uint8_t D = 0;
    uint8_t E = 0;
    uint8_t H = 0;
    uint8_t L = 0;

    uint16_t PC = 0; // Address of the next instruction byte.
    uint16_t SP = 0; // Address of the top of the stack.

    bool ime = false; // Whether the CPU may enter an interrupt handler.
    bool halted = false;
    bool stopped = false;

private:
    Bus& bus;
    int eiDelay = 0; // EI waits until the following instruction has finished.

    // Bits in F: zero, subtraction, half-carry, carry.
    static constexpr uint8_t Z = 0x80;
    static constexpr uint8_t N = 0x40;
    static constexpr uint8_t HF = 0x20;
    static constexpr uint8_t CF = 0x10;

    uint8_t fetch();
    uint16_t fetch16();

    // One argument reads a register; two arguments write it.
    uint8_t reg(int index) const;
    void reg(int index, uint8_t value);
    uint16_t pair(int index) const;
    void pair(int index, uint16_t value);
    uint16_t stackPair(int index) const;
    void stackPair(int index, uint16_t value);

    bool condition(int index) const;
    void push(uint16_t value);
    uint16_t pop();

    void alu(int operation, uint8_t value);
    void decimalAdjust();
    int addSignedSP(bool intoHL);

    int execute(uint8_t opcode);
    int executeCB(uint8_t opcode);
};

} // namespace gb
