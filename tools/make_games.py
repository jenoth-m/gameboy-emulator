"""Assemble two original SM83 games. No external assembler/assets are required.

The output is real Game Boy ROM code, executed by the emulated CPU. Game logic
is not implemented in the Windows frontend. Python is only a build-time tool.
Run: python tools/make_games.py
"""

from pathlib import Path
import ast

ROOT = Path(__file__).resolve().parents[1]
# Share the demo's letter shapes without running the demo-building script.
source = ast.parse((ROOT / 'tools' / 'make_demo.py').read_text())
FONT = next(
    ast.literal_eval(n.value)
    for n in source.body
    if isinstance(n, ast.Assign)
    and any(isinstance(t, ast.Name) and t.id == 'FONT' for t in n.targets)
)
FONT.update(
    {
        '0': [14, 17, 19, 21, 25, 17, 14],
        '1': [4, 12, 4, 4, 4, 4, 14],
        '2': [14, 17, 1, 2, 4, 8, 31],
        '3': [30, 1, 1, 14, 1, 1, 30],
        '4': [2, 6, 10, 18, 31, 2, 2],
        '5': [31, 16, 16, 30, 1, 1, 30],
        '6': [14, 16, 16, 30, 17, 17, 14],
        '7': [31, 1, 2, 4, 8, 8, 8],
        '8': [14, 17, 17, 14, 17, 17, 14],
        '9': [14, 17, 17, 15, 1, 1, 14],
    }
)

# Shared WRAM layout. Tests use these addresses to arrange deterministic scenarios.
STATE = 0xC000
PLAYER = 0xC001
BALLX = 0xC002
BALLY = 0xC003
DX = 0xC004
DY = 0xC005
ONES = 0xC006
TENS = 0xC007
LIVES = 0xC008
FRAME = 0xC009
RNG = 0xC00A
DIR = 0xC010
PREV_START = 0xC011
METEORS = [(0xC020, 0xC021), (0xC022, 0xC023), (0xC024, 0xC025)]


# These helpers write CPU instructions into the ROM; they do not execute them.
class Assembler:
    def __init__(self):
        self.code = bytearray()
        self.labels = {}
        self.fixups = []
        self.label_count = 0

    def emit(self, *data):
        self.code.extend(data)

    def label(self, name):
        assert name not in self.labels, name
        self.labels[name] = 0x150 + len(self.code)

    def word(self, op, value):
        self.emit(op, value & 255, value >> 8)

    def jump(self, label, op=0xC3):
        # Leave room for the address. finish() fills it in once all labels exist.
        self.emit(op, 0, 0)
        self.fixups.append((len(self.code) - 2, label))

    def call(self, label):
        self.jump(label, 0xCD)

    def load(self, address):
        self.word(0xFA, address)

    def store(self, address):
        self.word(0xEA, address)

    def value(self, address, value):
        self.emit(0x3E, value)
        self.store(address)

    def unique(self, name):
        self.label_count += 1
        return f'{name}_{self.label_count}'

    def finish(self):
        for at, label in self.fixups:
            address = self.labels[label]
            self.code[at] = address & 255
            self.code[at + 1] = address >> 8
        assert len(self.code) + 0x150 < 0x2000
        return self.code


def assets():
    tiles = bytearray(16)  # blank tile 0
    # tile 1: beveled paddle segment; tile 2: ball; tile 3: player ship; tile 4: meteor.
    patterns = [
        [(0, 0), (0, 0), (0xFF, 0xFF), (0xFF, 0x81), (0xFF, 0), (0xFF, 0xFF), (0, 0), (0, 0)],
        [
            (0, 0),
            (0x3C, 0x3C),
            (0x7E, 0x42),
            (0x7E, 0x42),
            (0x7E, 0x42),
            (0x7E, 0x42),
            (0x3C, 0x3C),
            (0, 0),
        ],
        [
            (0x18, 0x18),
            (0x18, 0x18),
            (0x3C, 0x24),
            (0x3C, 0x24),
            (0x7E, 0x42),
            (0xFF, 0xA5),
            (0xFF, 0xFF),
            (0x24, 0x24),
        ],
        [
            (0x3C, 0x3C),
            (0x7E, 0x42),
            (0xFF, 0x99),
            (0xFF, 0x81),
            (0xFF, 0xA5),
            (0xFF, 0x81),
            (0x7E, 0x42),
            (0x3C, 0x3C),
        ],
    ]
    for pattern in patterns:
        for lo, hi in pattern:
            tiles.extend((lo, hi))
    # tile 5: frame border, color 1.
    for y in range(8):
        tiles.extend((0xFF if y in (0, 7) else 0x81, 0))
    ids = {' ': 0}
    for char, rows in FONT.items():
        ids[char] = len(tiles) // 16
        for row in rows + [0]:
            tiles.extend((row << 2, row << 2))
    return tiles, ids


class Game:
    def __init__(self, kind):
        self.kind = kind
        self.a = Assembler()
        self.tiles, self.ids = assets()
        self.map = bytearray(1024)
        for x in range(20):
            self.map[x] = self.map[17 * 32 + x] = 5
        for y in range(1, 17):
            self.map[y * 32] = self.map[y * 32 + 19] = 5
        self.text_map(1, 'POCKET PADDLE' if kind == 'paddle' else 'METEOR DODGE')
        self.text_map(3, 'SCORE', 2)
        self.text_map(3, '00', 8)
        if kind == 'paddle':
            self.text_map(3, 'LIFE', 11)
            self.text_map(3, '3', 16)
        self.text_map(7, 'ENTER TO START')
        self.text_map(9, 'KEEP BALL UP' if kind == 'paddle' else 'AVOID METEORS')
        self.text_map(16, 'LEFT / RIGHT')

    def text_map(self, row, text, col=None):
        if col is None:
            col = (20 - len(text)) // 2
        for i, ch in enumerate(text):
            self.map[row * 32 + col + i] = self.ids[ch]

    def text_code(self, row, text, col=None):
        if col is None:
            col = (20 - len(text)) // 2
        self.a.word(0x21, 0x9800 + row * 32 + col)
        for ch in text:
            self.a.emit(0x3E, self.ids[ch], 0x22)

    def clear_status(self):
        for row in [7, 8, 9, 10]:
            self.a.word(0x21, 0x9800 + row * 32 + 1)
            self.a.emit(0x06, 18, 0xAF)
            label = self.a.unique('clear')
            self.a.label(label)
            self.a.emit(0x22, 0x05)
            self.a.jump(label, 0xC2)

    def copy(self, source, dest, count):
        a = self.a
        a.word(0x11, source)
        a.word(0x21, dest)
        a.word(0x01, count)
        label = a.unique('copy')
        a.label(label)
        a.emit(0x1A, 0x22, 0x13, 0x0B, 0x78, 0xB1)
        a.jump(label, 0xC2)

    def init(self):
        # The ROM starts with the LCD off so it can upload tiles and sprite data.
        a = self.a
        a.emit(0xF3)
        a.word(0x31, 0xFFFE)
        a.emit(0xAF, 0xE0, 0x40)
        self.copy(0x2000, 0x8000, len(self.tiles))
        self.copy(0x2400, 0x9800, len(self.map))
        # Zero state and OAM.
        for address, count in [(0xC000, 64), (0xFE00, 160)]:
            a.word(0x21, address)
            a.emit(0x06, count, 0xAF)
            label = a.unique('zero')
            a.label(label)
            a.emit(0x22, 0x05)
            a.jump(label, 0xC2)
        a.value(PLAYER, 68 if self.kind == 'paddle' else 76)
        a.value(BALLX, 80)
        a.value(BALLY, 72)
        a.value(LIVES, 3)
        a.value(RNG, 93)
        for i, (x, y) in enumerate(METEORS):
            a.value(x, 32 + i * 40)
            a.value(y, 40 + i * 24)
        a.call('draw')
        for register, value in [(0x42, 0), (0x43, 0), (0x47, 0xE4), (0x48, 0xE4), (0x40, 0x93)]:
            a.emit(0x3E, value, 0xE0, register)
        a.label('frame')
        a.emit(0xF0, 0x44, 0xFE, 144)
        a.jump('frame', 0xDA)
        a.call('input')
        a.load(STATE)
        a.emit(0xFE, 1)
        a.jump('draw_frame', 0xC2)
        a.call('update')
        a.label('draw_frame')
        a.call('draw')
        a.label('wait_end')
        a.emit(0xF0, 0x44, 0xFE, 144)
        a.jump('wait_end', 0xD2)
        a.jump('frame')

    def common(self):
        # Both games share input, starting a round, scoring, and game-over text.
        a = self.a
        a.label('input')
        a.emit(0x3E, 0x20, 0xE0, 0, 0xF0, 0, 0x2F, 0xE6, 15)
        a.store(DIR)
        a.emit(0x3E, 0x10, 0xE0, 0, 0xF0, 0, 0x2F, 0xE6, 8, 0x47)
        a.load(PREV_START)
        a.emit(0x4F, 0x78)
        a.store(PREV_START)
        a.emit(0xB7, 0xC8, 0x79, 0xB7, 0xC0)
        a.call('start')
        a.emit(0xC9)
        a.label('start')
        a.value(STATE, 1)
        a.emit(0x3E, 0x93, 0xE0, 0x40)
        a.value(ONES, 0)
        a.value(TENS, 0)
        a.value(FRAME, 0)
        a.value(LIVES, 3)
        a.value(PLAYER, 68 if self.kind == 'paddle' else 76)
        if self.kind == 'paddle':
            a.call('reset_ball')
        else:
            for i, (x, y) in enumerate(METEORS):
                a.value(x, 32 + i * 40)
                a.value(y, 40 + i * 24)
        self.clear_status()
        a.emit(0xC9)
        a.label('move_player')
        maximum = 128 if self.kind == 'paddle' else 144
        for bit, right in [(0, True), (1, False)]:
            end = a.unique('move_end')
            a.load(DIR)
            a.emit(0xE6, 1 << bit)
            a.jump(end, 0xCA)
            a.load(PLAYER)
            a.emit(0xFE, maximum if right else 10)
            a.jump(end, 0xD2 if right else 0xDA)
            a.emit(0xC6 if right else 0xD6, 2)
            a.store(PLAYER)
            a.label(end)
        a.emit(0xC9)
        a.label('score')
        # Saturate at 99, rather than overflow the display.
        a.load(TENS)
        a.emit(0xFE, 9)
        a.jump('score_add', 0xC2)
        a.load(ONES)
        a.emit(0xFE, 9, 0xC8)
        a.label('score_add')
        a.word(0x21, ONES)
        a.emit(0x34, 0x7E, 0xFE, 10, 0xD8, 0x36, 0)
        a.word(0x21, TENS)
        a.emit(0x34, 0xC9)
        a.label('over')
        a.value(STATE, 2)
        a.emit(0x3E, 0x91, 0xE0, 0x40)
        self.text_code(8, 'GAME OVER')
        self.text_code(10, 'ENTER TO RETRY')
        a.emit(0xC9)

    def hud(self):
        # The score and lives are background tiles, not a separate window layer.
        a = self.a
        for addr, col in [(TENS, 8), (ONES, 9)] + ([(LIVES, 16)] if self.kind == 'paddle' else []):
            a.load(addr)
            a.emit(0xC6, self.ids['0'])
            a.store(0x9800 + 3 * 32 + col)

    def paddle(self):
        a = self.a
        a.label('reset_ball')
        a.value(BALLX, 80)
        a.value(BALLY, 72)
        a.value(DX, 1)
        a.value(DY, 255)
        a.emit(0xC9)
        a.label('update')
        a.call('move_player')
        # Move ball horizontally; reverse at the two walls.
        a.load(DX)
        a.emit(0x47)
        a.load(BALLX)
        a.emit(0x80)
        a.store(BALLX)
        a.emit(0xFE, 8)
        a.jump('bounce_x', 0xCA)
        a.emit(0xFE, 144)
        a.jump('no_bounce_x', 0xC2)
        a.label('bounce_x')
        a.load(DX)
        a.emit(0x2F, 0x3C)
        a.store(DX)
        a.label('no_bounce_x')
        a.load(DY)
        a.emit(0x47)
        a.load(BALLY)
        a.emit(0x80)
        a.store(BALLY)
        a.emit(0xFE, 40)
        a.jump('below_ceiling', 0xC2)
        a.value(DY, 1)
        a.label('below_ceiling')
        a.load(DY)
        a.emit(0xFE, 1, 0xC0)
        a.load(BALLY)
        a.emit(0xFE, 114)
        a.jump('paddle_hit_check', 0xCA)
        a.emit(0xFE, 128, 0xD8)  # Return unless ball has fallen below paddle.
        a.word(0x21, LIVES)
        a.emit(0x35)
        a.jump('over', 0xCA)
        a.jump('reset_ball')
        a.label('paddle_hit_check')
        # Overlap if ball right edge >= paddle left and ball left < paddle right.
        a.load(PLAYER)
        a.emit(0x47)
        a.load(BALLX)
        a.emit(0xC6, 6, 0xB8, 0xD8)
        a.load(PLAYER)
        a.emit(0xC6, 24, 0x47)
        a.load(BALLX)
        a.emit(0xB8, 0xD0)
        a.value(DY, 255)
        a.call('score')
        a.emit(0xC9)
        a.label('draw')
        # Three adjacent 8x8 sprites make the paddle; another sprite is the ball.
        for i in range(3):
            a.value(0xFE00 + i * 4, 136)
            a.load(PLAYER)
            a.emit(0xC6, 8 + i * 8)
            a.store(0xFE01 + i * 4)
            a.value(0xFE02 + i * 4, 1)
        a.load(BALLY)
        a.emit(0xC6, 16)
        a.store(0xFE0C)
        a.load(BALLX)
        a.emit(0xC6, 8)
        a.store(0xFE0D)
        a.value(0xFE0E, 2)
        self.hud()
        a.emit(0xC9)

    def meteor(self):
        a = self.a
        a.label('random_x')
        # 8-bit LCG; map its state into [8, 135]. It is deterministic, not cryptographic.
        a.load(RNG)
        a.emit(0x47, 0x87, 0x87, 0x80, 0xC6, 17)
        a.store(RNG)
        a.emit(0xE6, 127, 0xC6, 8, 0xC9)
        a.label('update')
        a.call('move_player')
        a.word(0x21, FRAME)
        a.emit(0x34)
        # Move every second frame initially; after 10 points move every frame.
        a.load(TENS)
        a.emit(0xB7)
        a.jump('move_meteors', 0xC2)
        a.load(FRAME)
        a.emit(0xE6, 1, 0xC0)
        a.label('move_meteors')
        for i, (x, y) in enumerate(METEORS):
            collision = a.unique('collision')
            end = a.unique('meteor_done')
            a.word(0x21, y)
            a.emit(0x34, 0x7E, 0xFE, 128)
            a.jump(collision, 0xDA)
            a.value(y, 40)
            a.call('random_x')
            a.store(x)
            a.call('score')
            a.jump(end)
            a.label(collision)
            a.emit(0xFE, 113)
            a.jump(end, 0xDA)
            # Player top=120, height=8. Collision interval uses 8x8 bounds.
            a.load(PLAYER)
            a.emit(0x47)
            a.load(x)
            a.emit(0xC6, 7, 0xB8)
            a.jump(end, 0xDA)
            a.load(PLAYER)
            a.emit(0xC6, 8, 0x47)
            a.load(x)
            a.emit(0xB8)
            a.jump(end, 0xD2)
            a.jump('over')
            a.label(end)
        a.emit(0xC9)
        a.label('draw')
        a.value(0xFE00, 136)
        a.load(PLAYER)
        a.emit(0xC6, 8)
        a.store(0xFE01)
        a.value(0xFE02, 3)
        for i, (x, y) in enumerate(METEORS, 1):
            a.load(y)
            a.emit(0xC6, 16)
            a.store(0xFE00 + i * 4)
            a.load(x)
            a.emit(0xC6, 8)
            a.store(0xFE01 + i * 4)
            a.value(0xFE02 + i * 4, 4)
        self.hud()
        a.emit(0xC9)

    def build(self):
        # Assemble the program, then put its header, instructions and graphics in place.
        self.init()
        self.common()
        if self.kind == 'paddle':
            self.paddle()
        else:
            self.meteor()
        code = self.a.finish()
        rom = bytearray(32768)
        rom[0x100:0x104] = bytes([0, 0xC3, 0x50, 1])
        title = b'POCKET PADDLE' if self.kind == 'paddle' else b'METEOR DODGE'
        rom[0x134 : 0x134 + len(title)] = title
        rom[0x14A] = 1
        rom[0x150 : 0x150 + len(code)] = code
        rom[0x2000 : 0x2000 + len(self.tiles)] = self.tiles
        rom[0x2400:0x2800] = self.map
        checksum = 0
        for byte in rom[0x134:0x14D]:
            checksum = (checksum - byte - 1) & 255
        rom[0x14D] = checksum
        checksum = sum(rom) & 65535
        rom[0x14E] = checksum >> 8
        rom[0x14F] = checksum & 255
        name = 'pocket_paddle.gb' if self.kind == 'paddle' else 'meteor_dodge.gb'
        (ROOT / 'roms' / name).write_bytes(rom)
        print(f'{name}: {len(code)} bytes of SM83 code, {len(self.tiles)//16} tiles')


if __name__ == '__main__':
    Game('paddle').build()
    Game('meteor').build()
