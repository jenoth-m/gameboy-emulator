#include "CPU.h"

#include <sstream>
#include <stdexcept>

namespace gb {

// Starting state and instruction bytes.

void CPU::reset() {
    // A common original Game Boy state after boot. The boot ROM is skipped.
    A = 0x01;
    F = 0xB0;
    B = 0x00;
    C = 0x13;
    D = 0x00;
    E = 0xD8;
    H = 0x01;
    L = 0x4D;

    PC = 0x0100;
    SP = 0xFFFE;

    ime = false;
    halted = false;
    stopped = false;
    eiDelay = 0;
}

uint8_t CPU::fetch() {
    const uint8_t value = bus.read(PC);
    ++PC;
    return value;
}

uint16_t CPU::fetch16() {
    // Note: the low byte comes first, so 00 C0 becomes the address C000.
    const uint8_t low = fetch();
    const uint8_t high = fetch();

    return uint16_t(low | (high << 8));
}

// Register helpers.

uint16_t CPU::pair(int index) const {
    switch (index) {
    case 0:
        return uint16_t((B << 8) | C);
    case 1:
        return uint16_t((D << 8) | E);
    case 2:
        return uint16_t((H << 8) | L);
    default:
        return SP;
    }
}

void CPU::pair(int index, uint16_t value) {
    switch (index) {
    case 0:
        B = uint8_t(value >> 8);
        C = uint8_t(value);
        break;

    case 1:
        D = uint8_t(value >> 8);
        E = uint8_t(value);
        break;

    case 2:
        H = uint8_t(value >> 8);
        L = uint8_t(value);
        break;

    default:
        SP = value;
        break;
    }
}

uint16_t CPU::stackPair(int index) const {
    // PUSH/POP use AF where other pair instructions use SP.
    if (index == 3) {
        return uint16_t((A << 8) | F);
    }

    return pair(index);
}

void CPU::stackPair(int index, uint16_t value) {
    if (index == 3) {
        A = uint8_t(value >> 8);
        F = uint8_t(value & 0xF0); // The bottom four flag bits stay zero.
    } else {
        pair(index, value);
    }
}

uint8_t CPU::reg(int index) const {
    // The opcode encodes B, C, D, E, H, L, (HL), A as numbers 0 through 7.
    switch (index) {
    case 0:
        return B;
    case 1:
        return C;
    case 2:
        return D;
    case 3:
        return E;
    case 4:
        return H;
    case 5:
        return L;
    case 6:
        return bus.read(pair(2)); // (HL) means memory at the address in HL.
    default:
        return A;
    }
}

void CPU::reg(int index, uint8_t value) {
    switch (index) {
    case 0:
        B = value;
        break;
    case 1:
        C = value;
        break;
    case 2:
        D = value;
        break;
    case 3:
        E = value;
        break;
    case 4:
        H = value;
        break;
    case 5:
        L = value;
        break;
    case 6:
        bus.write(pair(2), value);
        break;
    default:
        A = value;
        break;
    }
}

bool CPU::condition(int index) const {
    switch (index) {
    case 0:
        return (F & Z) == 0; // NZ: result was not zero.
    case 1:
        return (F & Z) != 0; // Z: result was zero.
    case 2:
        return (F & CF) == 0; // NC: no carry.
    default:
        return (F & CF) != 0; // C: carry.
    }
}

void CPU::push(uint16_t value) {
    // The stack grows toward lower addresses.
    --SP;
    bus.write(SP, uint8_t(value >> 8));

    --SP;
    bus.write(SP, uint8_t(value));
}

uint16_t CPU::pop() {
    const uint8_t low = bus.read(SP);
    ++SP;

    const uint8_t high = bus.read(SP);
    ++SP;

    return uint16_t(low | (high << 8));
}

// Arithmetic and flags.

void CPU::alu(int operation, uint8_t value) {
    const int old = A;
    const int carry = (F & CF) ? 1 : 0;

    switch (operation) {
    case 0:   // ADD
    case 1: { // ADC: addition including the previous carry.
        const int extra = (operation == 1) ? carry : 0;
        const int result = old + value + extra;

        F = 0;

        if ((old & 0x0F) + (value & 0x0F) + extra > 0x0F) {
            F |= HF;
        }

        if (result > 0xFF) {
            F |= CF;
        }

        A = uint8_t(result);

        if (A == 0) {
            F |= Z;
        }

        break;
    }

    case 2:   // SUB
    case 3:   // SBC: subtraction including the previous carry/borrow.
    case 7: { // CP: calculate subtraction flags, but leave A unchanged.
        const int extra = (operation == 3) ? carry : 0;
        const int result = old - value - extra;

        F = N;

        if ((old & 0x0F) < (value & 0x0F) + extra) {
            F |= HF;
        }

        if (result < 0) {
            F |= CF;
        }

        if (uint8_t(result) == 0) {
            F |= Z;
        }

        if (operation != 7) {
            A = uint8_t(result);
        }

        break;
    }

    case 4: // AND
        A &= value;
        F = HF;

        if (A == 0) {
            F |= Z;
        }

        break;

    case 5: // XOR
        A ^= value;
        F = (A == 0) ? Z : 0;
        break;

    case 6: // OR
        A |= value;
        F = (A == 0) ? Z : 0;
        break;
    }
}

void CPU::decimalAdjust() {
    // DAA corrects A after arithmetic on packed decimal digits.
    // For example, packed decimal 42 is stored as 0x42, not decimal 42.
    int correction = 0;
    bool carry = (F & CF) != 0;

    if (!(F & N)) {
        if ((F & HF) || (A & 0x0F) > 9) {
            correction |= 0x06;
        }

        if (carry || A > 0x99) {
            correction |= 0x60;
            carry = true;
        }

        A = uint8_t(A + correction);
    } else {
        if (F & HF) {
            correction |= 0x06;
        }

        if (carry) {
            correction |= 0x60;
        }

        A = uint8_t(A - correction);
    }

    F &= N;

    if (carry) {
        F |= CF;
    }

    if (A == 0) {
        F |= Z;
    }
}

int CPU::addSignedSP(bool intoHL) {
    const uint8_t raw = fetch();
    const int offset = (raw < 128) ? int(raw) : int(raw) - 256;

    // These instructions calculate H/C from the low byte, even for negatives.
    F = 0;

    if ((SP & 0x0F) + (raw & 0x0F) > 0x0F) {
        F |= HF;
    }

    if ((SP & 0xFF) + raw > 0xFF) {
        F |= CF;
    }

    const uint16_t result = uint16_t(SP + offset);

    if (intoHL) {
        pair(2, result);
        return 12;
    }

    SP = result;
    return 16;
}

// CB-prefixed instructions: rotations, shifts and individual bits.

int CPU::executeCB(uint8_t opcode) {
    const int x = opcode >> 6;
    const int y = (opcode >> 3) & 7;
    const int z = opcode & 7;

    uint8_t value = reg(z);
    const bool oldCarry = (F & CF) != 0;

    if (x == 0) {
        bool carry = false;

        switch (y) {
        case 0: // RLC: rotate left, wrapping bit 7 to bit 0.
            carry = (value & 0x80) != 0;
            value = uint8_t((value << 1) | (value >> 7));
            break;

        case 1: // RRC: rotate right, wrapping bit 0 to bit 7.
            carry = (value & 1) != 0;
            value = uint8_t((value >> 1) | (value << 7));
            break;

        case 2: // RL: rotate left through the carry flag.
            carry = (value & 0x80) != 0;
            value = uint8_t((value << 1) | (oldCarry ? 1 : 0));
            break;

        case 3: // RR: rotate right through the carry flag.
            carry = (value & 1) != 0;
            value = uint8_t((value >> 1) | (oldCarry ? 0x80 : 0));
            break;

        case 4: // SLA: shift left, putting zero into bit 0.
            carry = (value & 0x80) != 0;
            value = uint8_t(value << 1);
            break;

        case 5: // SRA: shift right, preserving the sign bit.
            carry = (value & 1) != 0;
            value = uint8_t((value >> 1) | (value & 0x80));
            break;

        case 6: // SWAP: exchange the top and bottom four bits.
            value = uint8_t((value << 4) | (value >> 4));
            break;

        case 7: // SRL: shift right, putting zero into bit 7.
            carry = (value & 1) != 0;
            value = uint8_t(value >> 1);
            break;
        }

        reg(z, value);
        F = 0;

        if (value == 0) {
            F |= Z;
        }

        if (carry) {
            F |= CF;
        }
    } else if (x == 1) {
        // BIT changes flags, but does not change the tested value.
        F = uint8_t((F & CF) | HF);

        if (!(value & (1 << y))) {
            F |= Z;
        }
    } else if (x == 2) {
        reg(z, uint8_t(value & ~(1 << y))); // RES: clear bit y.
    } else {
        reg(z, uint8_t(value | (1 << y))); // SET: set bit y.
    }

    if (z == 6) {
        return (x == 1) ? 12 : 16;
    }

    return 8;
}

// Main instruction decoder.

int CPU::execute(uint8_t opcode) {
    // An opcode's bits encode a family and its operands.
    // x: top 2 bits; y: middle 3 bits; z: bottom 3 bits.
    // p/q split y further for instructions using register pairs.
    const int x = opcode >> 6;
    const int y = (opcode >> 3) & 7;
    const int z = opcode & 7;
    const int p = y >> 1;
    const int q = y & 1;

    // 00-3F: loads, increments, relative jumps and accumulator operations.
    if (x == 0) {
        switch (z) {
        case 0: {
            if (y == 0) { // NOP
                return 4;
            }

            if (y == 1) { // LD (a16), SP
                const uint16_t address = fetch16();
                bus.write(address, uint8_t(SP));
                bus.write(uint16_t(address + 1), uint8_t(SP >> 8));
                return 20;
            }

            if (y == 2) { // STOP
                fetch();  // Consume the following byte.
                stopped = true;
                bus.joyEdge = false;
                return 4;
            }

            // JR: the offset is relative to PC AFTER reading that offset.
            const uint8_t raw = fetch();
            const int offset = (raw < 128) ? int(raw) : int(raw) - 256;

            if (y == 3 || condition(y - 4)) {
                PC = uint16_t(PC + offset);
                return 12;
            }

            return 8;
        }

        case 1: {
            if (q == 0) { // LD register pair, d16
                pair(p, fetch16());
                return 12;
            }

            // ADD HL, register pair. The existing zero flag is preserved.
            const uint32_t left = pair(2);
            const uint32_t right = pair(p);
            const uint32_t result = left + right;
            F &= Z;

            if ((left & 0x0FFF) + (right & 0x0FFF) > 0x0FFF) {
                F |= HF;
            }

            if (result > 0xFFFF) {
                F |= CF;
            }

            pair(2, uint16_t(result));
            return 8;
        }

        case 2: {
            // Transfer A through BC, DE, HL+ or HL-.
            const uint16_t address = pair((p < 2) ? p : 2);

            if (q == 1) {
                A = bus.read(address);
            } else {
                bus.write(address, A);
            }

            if (p == 2) {
                pair(2, uint16_t(address + 1));
            } else if (p == 3) {
                pair(2, uint16_t(address - 1));
            }

            return 8;
        }

        case 3: // INC / DEC register pair. Flags do not change.
            if (q == 0) {
                pair(p, uint16_t(pair(p) + 1));
            } else {
                pair(p, uint16_t(pair(p) - 1));
            }

            return 8;

        case 4: { // INC register or (HL)
            const uint8_t old = reg(y);
            const uint8_t value = uint8_t(old + 1);
            reg(y, value);
            F &= CF;

            if (value == 0) {
                F |= Z;
            }

            if ((old & 0x0F) == 0x0F) {
                F |= HF;
            }

            return (y == 6) ? 12 : 4;
        }

        case 5: { // DEC register or (HL)
            const uint8_t old = reg(y);
            const uint8_t value = uint8_t(old - 1);
            reg(y, value);
            F = uint8_t((F & CF) | N);

            if (value == 0) {
                F |= Z;
            }

            if ((old & 0x0F) == 0) {
                F |= HF;
            }

            return (y == 6) ? 12 : 4;
        }

        case 6: // LD register or (HL), d8
            reg(y, fetch());
            return (y == 6) ? 12 : 8;

        case 7: {
            const bool carry = (F & CF) != 0;

            // These A-only rotations clear Z, unlike CB rotations.
            switch (y) {
            case 0: // RLCA
                F = (A & 0x80) ? CF : 0;
                A = uint8_t((A << 1) | (A >> 7));
                break;

            case 1: // RRCA
                F = (A & 1) ? CF : 0;
                A = uint8_t((A >> 1) | (A << 7));
                break;

            case 2: // RLA
                F = (A & 0x80) ? CF : 0;
                A = uint8_t((A << 1) | (carry ? 1 : 0));
                break;

            case 3: // RRA
                F = (A & 1) ? CF : 0;
                A = uint8_t((A >> 1) | (carry ? 0x80 : 0));
                break;

            case 4: // DAA
                decimalAdjust();
                break;

            case 5: // CPL: invert every bit in A.
                A = uint8_t(~A);
                F |= N | HF;
                break;

            case 6: // SCF: set carry.
                F = uint8_t((F & Z) | CF);
                break;

            case 7: // CCF: toggle carry.
                F = uint8_t((F & Z) | (carry ? 0 : CF));
                break;
            }

            return 4;
        }
        }
    }

    // 40-7F: move a value between registers or memory; 76 is HALT.
    if (x == 1) {
        if (y == 6 && z == 6) {
            // HALT waits for an enabled interrupt request. The fetch quirk is omitted.
            halted = true;
            return 4;
        }

        reg(y, reg(z));
        return (y == 6 || z == 6) ? 8 : 4;
    }

    // 80-BF: arithmetic or logic with A and the selected register/memory byte.
    if (x == 2) {
        alu(y, reg(z));
        return (z == 6) ? 8 : 4;
    }

    // C0-FF: control flow, stack operations, immediate arithmetic and I/O.
    switch (z) {
    case 0:
        if (y < 4) { // Conditional RET
            if (condition(y)) {
                PC = pop();
                return 20;
            }

            return 8;
        }

        if (y == 4) { // LDH (a8), A
            const uint16_t address = uint16_t(0xFF00 + fetch());
            bus.write(address, A);
            return 12;
        }

        if (y == 5) {
            return addSignedSP(false);
        }

        if (y == 6) { // LDH A, (a8)
            A = bus.read(uint16_t(0xFF00 + fetch()));
            return 12;
        }

        return addSignedSP(true);

    case 1:
        if (q == 0) { // POP
            stackPair(p, pop());
            return 12;
        }

        if (p == 0) { // RET
            PC = pop();
            return 16;
        }

        if (p == 1) { // RETI: return and enable interrupts.
            PC = pop();
            ime = true;
            eiDelay = 0;
            return 16;
        }

        if (p == 2) { // JP (HL)
            PC = pair(2);
            return 4;
        }

        SP = pair(2); // LD SP, HL
        return 8;

    case 2:
        if (y < 4) { // Conditional JP a16
            const uint16_t address = fetch16();

            if (condition(y)) {
                PC = address;
                return 16;
            }

            return 12;
        }

        if (y == 4) {
            bus.write(uint16_t(0xFF00 + C), A);
            return 8;
        }

        if (y == 5) {
            const uint16_t address = fetch16();
            bus.write(address, A);
            return 16;
        }

        if (y == 6) {
            A = bus.read(uint16_t(0xFF00 + C));
            return 8;
        }

        A = bus.read(fetch16());
        return 16;

    case 3:
        if (y == 0) { // JP a16
            PC = fetch16();
            return 16;
        }

        if (y == 1) { // CB prefix: decode the next byte in the other table.
            return executeCB(fetch());
        }

        if (y == 6) { // DI
            ime = false;
            eiDelay = 0;
            return 4;
        }

        if (y == 7) { // EI takes effect after the following instruction.
            if (eiDelay == 0) {
                eiDelay = 2;
            }

            return 4;
        }

        break;

    case 4:
        if (y < 4) { // Conditional CALL a16
            const uint16_t address = fetch16();

            if (condition(y)) {
                push(PC);
                PC = address;
                return 24;
            }

            return 12;
        }

        break;

    case 5:
        if (q == 0) { // PUSH
            push(stackPair(p));
            return 16;
        }

        if (p == 0) { // CALL a16
            const uint16_t address = fetch16();
            push(PC);
            PC = address;
            return 24;
        }

        break;

    case 6: // ADD/ADC/SUB/SBC/AND/XOR/OR/CP with the next byte.
        alu(y, fetch());
        return 8;

    case 7: // RST: call a fixed address, selected by y.
        push(PC);
        PC = uint16_t(y * 8);
        return 16;
    }

    // Invalid instructions are errors. Silently ignoring them hides bugs.
    std::ostringstream message;
    message << "Invalid opcode 0x" << std::hex << int(opcode) << " at 0x" << uint16_t(PC - 1);
    throw std::runtime_error(message.str());
}

// One CPU step.

int CPU::step() {
    if (stopped) {
        if (!bus.joyEdge) {
            return 4;
        }

        stopped = false;
        bus.joyEdge = false;
    }

    // IE says which interrupts are enabled; IF says which were requested.
    const uint8_t pending = bus.ie & bus.io[0x0F] & 0x1F;

    if (pending != 0) {
        halted = false;

        if (ime) {
            int bit = 0;

            while (!(pending & (1 << bit))) {
                ++bit;
            }

            ime = false;
            eiDelay = 0;

            bus.io[0x0F] &= uint8_t(~(1 << bit));
            push(PC);
            PC = uint16_t(0x40 + bit * 8);
            return 20;
        }
    }

    if (halted) {
        // The CPU waits, but GameBoy::step() still advances the timer and PPU.
        return 4;
    }

    const uint8_t opcode = fetch();
    const int cycles = execute(opcode);

    if (eiDelay > 0) {
        --eiDelay;

        if (eiDelay == 0) {
            ime = true;
        }
    }

    F &= 0xF0;
    return cycles;
}

} // namespace gb
