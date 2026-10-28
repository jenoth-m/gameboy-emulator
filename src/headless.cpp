#include "GameBoy.h"

#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: gb_headless game.gb [frames=120]\n";
        return 1;
    }

    try {
        int frames = 120;

        if (argc >= 3) {
            const std::string argument = argv[2];
            size_t used = 0;
            frames = std::stoi(argument, &used);

            if (used != argument.size()) {
                throw std::runtime_error("Frame count must be a whole number.");
            }
        }

        if (frames < 1 || frames > 100000) {
            throw std::runtime_error("Frame count must be from 1 to 100000.");
        }

        gb::GameBoy game;
        game.load(argv[1]);

        // Useful for checking that a ROM runs without opening a Windows window.
        for (int i = 0; i < frames; ++i) {
            game.runFrame();
        }

        std::cout << "Title: " << game.cartridge.title << '\n';
        std::cout << "Ticks: " << game.ticks << '\n';
        std::cout << "PPU frames: " << game.ppu.frames << '\n';
        std::cout << "PC: " << std::hex << game.cpu.PC << std::dec << '\n';

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
