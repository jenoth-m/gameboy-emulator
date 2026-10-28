#include "GameBoy.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

void check(bool passed, const char* message) {
    if (!passed) {
        throw std::runtime_error(message);
    }
}

void runFrames(gb::GameBoy& game, int count) {
    for (int i = 0; i < count; ++i) {
        game.runFrame();
    }
}

void pressStart(gb::GameBoy& game) {
    // Hold Start briefly, then release it so the next press is a new event.
    game.bus.setButtons(0x80);
    runFrames(game, 2);
    game.bus.setButtons(0);
    runFrames(game, 1);
}

void testPaddle(const std::filesystem::path& folder) {
    gb::GameBoy game;
    game.load(folder / "pocket_paddle.gb");
    runFrames(game, 120);

    // These addresses belong to this game, not to the emulator hardware.
    // RAM[0] = game state, RAM[1] = player X. See docs/GAMES.md.
    auto& ram = game.bus.wram;
    check(ram[0] == 0, "Paddle waits at its title screen");

    pressStart(game);
    check(ram[0] == 1, "Paddle starts");

    const uint8_t oldX = ram[1];
    game.bus.setButtons(2);
    runFrames(game, 4);
    game.bus.setButtons(0);
    check(ram[1] < oldX, "Left input moves the paddle");

    // Arrange a bounce, then let the ROM execute its own collision code.
    ram[1] = 68;
    ram[2] = 70;
    ram[3] = 113;
    ram[4] = 1;
    ram[5] = 1;
    runFrames(game, 1);
    check(ram[6] == 1 && ram[5] == 255, "Paddle bounce scores and reverses Y");

    // Arrange a missed ball with one life left.
    ram[8] = 1;
    ram[2] = 140;
    ram[3] = 127;
    ram[5] = 1;
    runFrames(game, 2);
    check(ram[0] == 2, "Paddle reaches game over");

    pressStart(game);
    check(ram[0] == 1 && ram[8] == 3 && ram[6] == 0, "Paddle restarts cleanly");
}

void testMeteor(const std::filesystem::path& folder) {
    gb::GameBoy game;
    game.load(folder / "meteor_dodge.gb");
    runFrames(game, 120);

    auto& ram = game.bus.wram;
    check(ram[0] == 0, "Meteor waits at its title screen");
    pressStart(game);
    check(ram[0] == 1, "Meteor starts");

    const uint8_t oldX = ram[1];
    game.bus.setButtons(1);
    runFrames(game, 4);
    game.bus.setButtons(0);
    check(ram[1] > oldX, "Right input moves the ship");

    // Arrange a meteor safely passing the player.
    ram[1] = 140;
    ram[0x20] = 20;
    ram[0x21] = 127;
    ram[9] = 1;
    runFrames(game, 1);
    check(ram[6] == 1 && ram[0x21] == 40, "Avoided meteor scores and respawns");

    ram[1] = 76;
    ram[0x20] = 76;
    ram[0x21] = 112;
    ram[9] = 1;
    runFrames(game, 2);
    check(ram[0] == 2, "Meteor collision ends the game");

    pressStart(game);
    check(ram[0] == 1 && ram[6] == 0, "Meteor restarts cleanly");
}

void testDemo(const std::filesystem::path& folder) {
    gb::GameBoy game;
    game.load(folder / "demo.gb");
    runFrames(game, 120);

    const uint8_t oldX = game.bus.oam[1];
    game.bus.setButtons(1);
    runFrames(game, 4);
    game.bus.setButtons(0);

    check(game.ppu.frames > 100, "Demo produces frames");
    check(game.bus.oam[1] > oldX, "Demo's sprite responds to Right");
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path folder = (argc > 1) ? argv[1] : "roms";

    try {
        testPaddle(folder);
        std::cout << "PASS: Pocket Paddle\n";

        testMeteor(folder);
        std::cout << "PASS: Meteor Dodge\n";

        testDemo(folder);
        std::cout << "PASS: sprite demo\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
