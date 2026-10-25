# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Attribute register 13h shifts VGA text left: in the 400-line modes 8 is
no shift and 0 to 7 shift by 1 to 8 (IBM Video Subsystem 1992, Horizontal
PEL Panning Register, figures 2-87 and 2-88). ada-5bth."""

import io
import time

import pytest
from PIL import Image

ROW = 10
SCAN = 4
CELL_HEIGHT = 16
TEXT_BASE = 0xB8000
FULL_BLOCK = 0xDB
GREY_ON_BLACK = 0x07


def set_pel_panning(client, value: int) -> None:
    # Reading 3DAh resets the flip-flop to the index; 20h keeps the
    # palette address source set, so the display stays on.
    assert client.port_read(0x3DA).status_code == 200
    assert client.port_write(0x3C0, 0x20 | 0x13).status_code == 200
    assert client.port_write(0x3C0, value).status_code == 200


def first_lit_x(client) -> int:
    r = client.frame(fmt="png", mode="raw")
    assert r.status_code == 200, r.text
    image = Image.open(io.BytesIO(r.content)).convert("RGB")
    assert image.size == (720, 400), image.size
    y = ROW * CELL_HEIGHT + SCAN
    for x in range(image.width):
        if image.getpixel((x, y)) != (0, 0, 0):
            return x
    return -1


def settled_first_lit_x(client, timeout_s: float = 3.0) -> int:
    # The panning value is taken at vertical retrace; two equal readings
    # mean the frame after the write has been drawn.
    deadline = time.monotonic() + timeout_s
    previous = None
    while time.monotonic() < deadline:
        current = first_lit_x(client)
        if current == previous:
            return current
        previous = current
        time.sleep(0.1)
    pytest.fail(f"block edge did not settle, last reading {previous}")


@pytest.fixture(scope="module")
def block_row(dosbox):
    cells = bytearray()
    for column in range(80):
        cells += bytes([FULL_BLOCK if column == 1 else 0x20, GREY_ON_BLACK])
    r = dosbox.memory_write(TEXT_BASE + ROW * 160, bytes(cells))
    assert r.status_code == 200, r.text
    yield dosbox
    set_pel_panning(dosbox, 8)


@pytest.mark.parametrize(
    ("register", "expected_x"),
    [(8, 9), (0, 8), (2, 6), (7, 1)],
)
def test_panning_moves_text_left(block_row, register, expected_x):
    set_pel_panning(block_row, register)
    assert settled_first_lit_x(block_row) == expected_x
