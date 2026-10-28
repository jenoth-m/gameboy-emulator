#include "Cartridge.h"

#include <fstream>
#include <stdexcept>
#include <utility>

namespace gb {

void Cartridge::load(const std::filesystem::path& path) {
    // A plain cartridge holds exactly 32 KiB of program and graphics data.
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file) {
        throw std::runtime_error("Could not open the ROM file.");
    }

    if (file.tellg() != std::streampos(32768)) {
        throw std::runtime_error("This version only supports 32 KiB ROM files.");
    }

    std::vector<uint8_t> bytes(32768);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), 32768);

    if (!file) {
        throw std::runtime_error("Could not read the complete ROM file.");
    }

    loadBytes(std::move(bytes));
}

void Cartridge::loadBytes(std::vector<uint8_t> bytes) {
    // Header bytes describe the hardware that the game expects.
    // Check the size first so the header reads below are safe.
    if (bytes.size() != 32768) {
        throw std::runtime_error("This version only supports 32 KiB ROM files.");
    }

    if (bytes[0x143] == 0xC0) {
        throw std::runtime_error("Game Boy Color-only games are not supported.");
    }

    if (bytes[0x147] != 0x00) {
        throw std::runtime_error("Only ROM-only cartridges (type 0x00) are supported.");
    }

    if (bytes[0x148] != 0x00 || bytes[0x149] != 0x00) {
        throw std::runtime_error("Expected a 32 KiB ROM with no cartridge RAM.");
    }

    std::string newTitle;

    // Color-compatible headers use fewer bytes for the title.
    const size_t titleEnd = (bytes[0x143] == 0x80) ? 0x13F : 0x144;

    for (size_t i = 0x134; i < titleEnd && bytes[i] != 0; ++i) {
        if (bytes[i] >= 32 && bytes[i] <= 126) {
            newTitle += char(bytes[i]);
        } else {
            newTitle += '?';
        }
    }

    title = std::move(newTitle);
    rom = std::move(bytes);
}

uint8_t Cartridge::read(uint16_t address) const {
    // No banks: a Game Boy ROM address is also an index into this vector.
    if (address < rom.size()) {
        return rom[address];
    }

    return 0xFF;
}

} // namespace gb
