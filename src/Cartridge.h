#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gb {

// The ROM holds the game's instructions and graphics, not the emulator's code.
class Cartridge {
public:
    void load(const std::filesystem::path& path);
    void loadBytes(std::vector<uint8_t> bytes);

    uint8_t read(uint16_t address) const;

    std::string title;
    std::vector<uint8_t> rom;
};

} // namespace gb
