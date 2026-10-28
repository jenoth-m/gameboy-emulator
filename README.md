# Game Boy Emulator

A C++17 Game Boy emulator with a Windows frontend built using the Win32 API. It supports 32 KiB ROM-only cartridges and renders a 160 × 144 display with backgrounds and 8 × 8 sprites.

The core handles CPU instructions, memory, interrupts, timers, and sprite-memory transfers.

## Preview

![Pocket Paddle and Meteor Dodge title screens](docs/games-preview.png)

Title screens rendered by the emulator from the included ROMs.

## Included games

| Game | Gameplay |
| --- | --- |
| Pocket Paddle | Return the ball with the paddle. Each return scores a point; missing costs a life. |
| Meteor Dodge | Avoid falling meteors. Each avoided meteor scores a point. |
| Sprite demo | Move a sprite and change its palette. |

Press **Enter** to start or restart either game. See [the game notes](docs/GAMES.md) for details.

## Build and run

### Windows

Install Visual Studio or Visual Studio Build Tools with the **Desktop development with C++** workload.

1. Open the project folder containing `Build.bat`.
2. Run `Build.bat` to compile the application and run the tests.
3. After it succeeds, run `Run.bat`.

The ROMs are included. Python is only needed to regenerate them using the scripts in `tools`.

### CMake

Requires CMake 3.20+ and a C++17 compiler.

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --config Release
ctest --test-dir build-cmake -C Release --output-on-failure
```

With the Visual Studio generator, the application is saved as `build-cmake/Release/GameBoyEmulator.exe`.

On Linux, the build includes the portable core, command-line runner, and tests. The graphical application requires Windows.

## Controls

| Key | Action |
| --- | --- |
| Arrow keys | Direction buttons |
| Z / X | A / B |
| Enter / Backspace | Start / Select |
| 1 / 2 / 3 | Pocket Paddle / Meteor Dodge / sprite demo |
| Ctrl+O | Open a supported ROM |
| P | Pause or resume |
| R | Reset |

## Tests

The tests cover CPU operations and flags, memory, interrupts, timers, DMA, and graphics. Game tests check input, scoring, collisions, and restarting, including scenarios arranged directly in game RAM.

The portable build and tests passed on Linux with GCC 13.3.0. The Windows build and interactive gameplay still need verification.

## Limitations

- Only 32 KiB ROM-only cartridges are supported. There is no bank switching, cartridge RAM, saved-game storage, audio, or Game Boy Color support.
- The window layer, 8 × 16 sprites, and serial transfers are not implemented.
- Timing uses instruction totals and fixed scanline phases. DMA pauses the CPU, STOP uses button-edge wake-up, and the HALT fetch bug is omitted.
- The boot ROM is skipped. Included ROMs omit Nintendo logo data and require that startup path.

Compatibility testing currently covers the included ROMs.

## References

- [Pan Docs](https://gbdev.io/pandocs/)
- [Game Boy opcode tables](https://gbdev.io/gb-opcodes/optables/)