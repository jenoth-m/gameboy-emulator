"""Build the 32 KiB sprite demo ROM. Python is not needed to run the emulator.
No commercial game data or Nintendo boot ROM/logo is included.
"""

from pathlib import Path

# Each number describes one row of a small letter shape.
FONT = {
    'A': [14, 17, 17, 31, 17, 17, 17],
    'B': [30, 17, 17, 30, 17, 17, 30],
    'C': [14, 17, 16, 16, 16, 17, 14],
    'D': [30, 17, 17, 17, 17, 17, 30],
    'E': [31, 16, 16, 30, 16, 16, 31],
    'F': [31, 16, 16, 30, 16, 16, 16],
    'G': [14, 17, 16, 23, 17, 17, 15],
    'H': [17, 17, 17, 31, 17, 17, 17],
    'I': [14, 4, 4, 4, 4, 4, 14],
    'J': [7, 2, 2, 2, 18, 18, 12],
    'K': [17, 18, 20, 24, 20, 18, 17],
    'L': [16, 16, 16, 16, 16, 16, 31],
    'M': [17, 27, 21, 21, 17, 17, 17],
    'N': [17, 25, 21, 19, 17, 17, 17],
    'O': [14, 17, 17, 17, 17, 17, 14],
    'P': [30, 17, 17, 30, 16, 16, 16],
    'Q': [14, 17, 17, 17, 21, 18, 13],
    'R': [30, 17, 17, 30, 20, 18, 17],
    'S': [15, 16, 16, 14, 1, 1, 30],
    'T': [31, 4, 4, 4, 4, 4, 4],
    'U': [17, 17, 17, 17, 17, 17, 14],
    'V': [17, 17, 17, 17, 17, 10, 4],
    'W': [17, 17, 17, 21, 21, 21, 10],
    'X': [17, 17, 10, 4, 10, 17, 17],
    'Y': [17, 17, 10, 4, 4, 4, 4],
    'Z': [31, 1, 2, 4, 8, 16, 31],
    '+': [0, 4, 4, 31, 4, 4, 0],
    '/': [1, 2, 2, 4, 8, 8, 16],
}
tiles = bytearray(16)  # Tile 0 is blank.
# Tile 1 is the diamond sprite. Its corners are transparent.
for low, high in [
    (0x18, 0x18),
    (0x3C, 0x24),
    (0x7E, 0x42),
    (0xFF, 0xA5),
    (0xFF, 0x81),
    (0x7E, 0x42),
    (0x3C, 0x24),
    (0x18, 0x18),
]:
    tiles.extend((low, high))
# Tile 2: border.
for y in range(8):
    mask = 0xFF if y in (0, 7) else 0x81
    tiles.extend((mask, 0))
ids = {' ': 0}
for char, rows in FONT.items():
    ids[char] = len(tiles) // 16
    for row in rows + [0]:
        tiles.extend((row << 2, row << 2))
map_data = bytearray(1024)
for x in range(20):
    map_data[x] = map_data[17 * 32 + x] = 2
for y in range(1, 17):
    map_data[y * 32] = map_data[y * 32 + 19] = 2
for y, text in [
    (2, 'JENOTH'),
    (4, 'GAME BOY'),
    (6, 'C++ EMULATOR'),
    (12, 'ARROWS MOVE'),
    (14, 'Z / X COLORS'),
    (16, 'ENTER CENTERS'),
]:
    start = (20 - len(text)) // 2
    for i, ch in enumerate(text):
        map_data[y * 32 + start + i] = ids[ch]

# Build the instruction bytes, keeping labels for jumps.
code = bytearray()
labels = {}
fixups = []


def emit(*values):
    code.extend(values)


def label(name):
    labels[name] = 0x150 + len(code)


def jr(op, target):
    emit(op, 0)
    fixups.append((len(code) - 1, target))


def ld16(op, value):
    emit(op, value & 255, value >> 8)


def copy_block(source, destination, count, name):
    # Emit a Game Boy loop that copies a block of bytes through DE and HL.
    ld16(0x11, source)
    ld16(0x21, destination)
    ld16(0x01, count)
    label(name)
    emit(0x1A, 0x22, 0x13, 0x0B, 0x78, 0xB1)
    jr(0x20, name)


emit(0xF3)  # DI
ld16(0x31, 0xFFFE)
emit(0xAF, 0xE0, 0x40)  # LCD off
copy_block(0x1000, 0x8000, len(tiles), 'tiles')
copy_block(0x1400, 0x9800, len(map_data), 'map')
ld16(0x21, 0xFE00)
emit(0x06, 160, 0xAF)
label('clear_oam')
emit(0x22, 0x05)
jr(0x20, 'clear_oam')
# One sprite: Y, X, tile, attributes.
ld16(0x21, 0xFE00)
for value in [88, 84, 1, 0]:
    emit(0x3E, value, 0x22)
for register, value in [(0x42, 0), (0x43, 0), (0x47, 0xE4), (0x48, 0xE4), (0x40, 0x93)]:
    emit(0x3E, value, 0xE0, register)
label('frame')
emit(0xF0, 0x44, 0xFE, 144)
jr(0x38, 'frame')  # Wait for VBlank.
emit(0x3E, 0x20, 0xE0, 0, 0xF0, 0, 0x2F, 0xE6, 15, 0x47)  # B = directions.
for bit, address, increment, limit in [
    (0, 0xFE01, True, 160),
    (1, 0xFE01, False, 9),
    (2, 0xFE00, False, 17),
    (3, 0xFE00, True, 152),
]:
    end = 'direction' + str(bit)
    emit(0xCB, 0x40 + bit * 8)
    jr(0x28, end)  # BIT bit,B / JR Z.
    ld16(0x21, address)
    emit(0x7E, 0xFE, limit)
    jr(0x30 if increment else 0x38, end)
    emit(0x34 if increment else 0x35)
    label(end)
emit(0x3E, 0x10, 0xE0, 0, 0xF0, 0, 0x2F, 0xE6, 15, 0x47)
for bit, palette in [(0, 0xE4), (1, 0x1B)]:
    emit(0xCB, 0x40 + bit * 8)
    jr(0x28, 'color' + str(bit))
    emit(0x3E, palette, 0xE0, 0x48)
    label('color' + str(bit))
emit(0xCB, 0x58)
jr(0x28, 'wait_end')  # Start centers the character.
ld16(0x21, 0xFE00)
emit(0x3E, 88, 0x22, 0x3E, 84, 0x77)
label('wait_end')
emit(0xF0, 0x44, 0xFE, 144)
jr(0x30, 'wait_end')
ld16(0xC3, labels['frame'])
# Now the labels have addresses, so the relative jumps can be filled in.
for pos, target in fixups:
    delta = labels[target] - (0x150 + pos + 1)
    assert -128 <= delta <= 127, (target, delta)
    code[pos] = delta & 255
assert 0x150 + len(code) < 0x1000
# Package the instruction bytes and graphics into a complete ROM file.
rom = bytearray(32768)
rom[0x100:0x104] = bytes([0, 0xC3, 0x50, 1])
rom[0x134:0x13F] = b'JENOTH DEMO'
rom[0x147] = rom[0x148] = rom[0x149] = 0
rom[0x14A] = 1
rom[0x150 : 0x150 + len(code)] = code
rom[0x1000 : 0x1000 + len(tiles)] = tiles
rom[0x1400:0x1800] = map_data
checksum = 0
for value in rom[0x134:0x14D]:
    checksum = (checksum - value - 1) & 255
rom[0x14D] = checksum
checksum = sum(rom) & 65535
rom[0x14E] = checksum >> 8
rom[0x14F] = checksum & 255
path = Path(__file__).resolve().parents[1] / 'roms' / 'demo.gb'
path.write_bytes(rom)
print(f'Wrote {path.name}: {len(rom)} bytes, {len(code)} bytes of code, {len(tiles)//16} tiles')
