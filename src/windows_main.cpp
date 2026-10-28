#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commdlg.h>

#include "GameBoy.h"

#include <chrono>
#include <filesystem>
#include <string>

namespace {

gb::GameBoy game;
HWND windowHandle = nullptr;
bool gameLoaded = false;
bool paused = false;
uint8_t buttons = 0;

std::wstring widen(const std::string& text) {
    // ROM titles and our error messages use ASCII characters.
    return std::wstring(text.begin(), text.end());
}

void showError(const std::exception& error) {
    MessageBoxW(windowHandle, widen(error.what()).c_str(), L"Game Boy Emulator",
                MB_OK | MB_ICONERROR);
}

void updateTitle() {
    std::wstring title = L"Game Boy Emulator";

    if (gameLoaded) {
        title += L" - " + widen(game.cartridge.title);
    }

    if (paused) {
        title += L" [Paused]";
    }

    title += L" | 1: Paddle | 2: Meteor | 3: Demo | Ctrl+O: Open";
    SetWindowTextW(windowHandle, title.c_str());
}

std::filesystem::path bundledROM(const wchar_t* name) {
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);

    return std::filesystem::path(executable).parent_path() / L"roms" / name;
}

void loadROM(const std::filesystem::path& path) {
    game.load(path);
    gameLoaded = true;
    paused = false;
    buttons = 0;

    updateTitle();
    InvalidateRect(windowHandle, nullptr, FALSE);
}

void chooseROM() {
    wchar_t path[32768]{};
    const std::wstring folder = bundledROM(L"").wstring();

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = windowHandle;
    dialog.lpstrFilter = L"Game Boy ROM (*.gb)\0*.gb\0All files\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = 32768;
    dialog.lpstrInitialDir = folder.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&dialog)) {
        loadROM(path);
    }
}

int buttonIndex(WPARAM key) {
    switch (key) {
    case VK_RIGHT:
        return 0;
    case VK_LEFT:
        return 1;
    case VK_UP:
        return 2;
    case VK_DOWN:
        return 3;
    case 'Z':
        return 4;
    case 'X':
        return 5;
    case VK_BACK:
        return 6;
    case VK_RETURN:
        return 7;
    default:
        return -1;
    }
}

void paint() {
    PAINTSTRUCT paintInfo{};
    HDC device = BeginPaint(windowHandle, &paintInfo);

    RECT client{};
    GetClientRect(windowHandle, &client);
    FillRect(device, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    if (gameLoaded) {
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = 160;
        bitmap.bmiHeader.biHeight = -144; // Negative means rows go top to bottom.
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        bitmap.bmiHeader.biCompression = BI_RGB;

        // Windows displays the PPU's pixels at four times their original size.
        SetStretchBltMode(device, COLORONCOLOR);
        StretchDIBits(device, 0, 0, 640, 576, 0, 0, 160, 144, game.ppu.pixels.data(), &bitmap,
                      DIB_RGB_COLORS, SRCCOPY);
    } else {
        SetTextColor(device, RGB(224, 240, 192));
        SetBkMode(device, TRANSPARENT);
        DrawTextW(device, L"Press Ctrl+O to open a 32 KiB ROM-only game.", -1, &client,
                  DT_CENTER | DT_WORDBREAK);
    }

    EndPaint(windowHandle, &paintInfo);
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM key, LPARAM data) {
    try {
        switch (message) {
        case WM_PAINT:
            paint();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_KILLFOCUS:
            // Avoid a stuck button if the user switches to another program.
            buttons = 0;
            game.bus.setButtons(0);
            return 0;

        case WM_KEYDOWN:
        case WM_KEYUP: {
            const bool down = message == WM_KEYDOWN;
            const int bit = buttonIndex(key);

            if (bit >= 0) {
                if (down) {
                    buttons |= uint8_t(1 << bit);
                } else {
                    buttons &= uint8_t(~(1 << bit));
                }

                game.bus.setButtons(buttons);
                return 0;
            }

            // Holding a shortcut key should not keep reloading or resetting a game.
            const bool repeated = (data & (1LL << 30)) != 0;

            if (down && !repeated) {
                if (key == 'O' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                    chooseROM();
                } else if (key == '1') {
                    loadROM(bundledROM(L"pocket_paddle.gb"));
                } else if (key == '2') {
                    loadROM(bundledROM(L"meteor_dodge.gb"));
                } else if (key == '3') {
                    loadROM(bundledROM(L"demo.gb"));
                } else if (key == 'P' && gameLoaded) {
                    paused = !paused;
                    updateTitle();
                } else if (key == 'R' && gameLoaded) {
                    game.reset();
                    buttons = 0;
                    InvalidateRect(windowHandle, nullptr, FALSE);
                }
            }

            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
    } catch (const std::exception& error) {
        showError(error);
        return 0;
    }

    return DefWindowProcW(window, message, key, data);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int show) {
    SetProcessDPIAware();

    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"GameBoyEmulatorWindow";
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    if (!RegisterClassW(&windowClass)) {
        return 1;
    }

    // Fixed-size window keeps the display code small and easy to follow.
    const DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rectangle{0, 0, 640, 576};
    AdjustWindowRect(&rectangle, style, FALSE);

    windowHandle =
        CreateWindowExW(0, windowClass.lpszClassName, L"Game Boy Emulator", style, CW_USEDEFAULT,
                        CW_USEDEFAULT, rectangle.right - rectangle.left,
                        rectangle.bottom - rectangle.top, nullptr, nullptr, instance, nullptr);

    if (!windowHandle) {
        return 1;
    }

    ShowWindow(windowHandle, show);
    UpdateWindow(windowHandle);

    try {
        loadROM(bundledROM(L"pocket_paddle.gb"));
    } catch (const std::exception& error) {
        showError(error);
    }

    using Clock = std::chrono::steady_clock;
    const auto frameTime = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(70224.0 / 4194304.0));
    auto nextFrame = Clock::now();

    while (true) {
        // Let Windows deliver key presses, repaint requests and close events.
        MSG message{};

        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                return int(message.wParam);
            }

            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (!gameLoaded || paused || IsIconic(windowHandle)) {
            nextFrame = Clock::now();
            Sleep(10);
            continue;
        }

        if (Clock::now() < nextFrame) {
            Sleep(1);
            continue;
        }

        try {
            game.runFrame();
            InvalidateRect(windowHandle, nullptr, FALSE);
        } catch (const std::exception& error) {
            paused = true;
            updateTitle();
            showError(error);
        }

        nextFrame += frameTime;

        if (Clock::now() - nextFrame > frameTime * 4) {
            nextFrame = Clock::now();
        }
    }
}
