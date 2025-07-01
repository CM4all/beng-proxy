# SPDX-License-Identifier: BSD-2-Clause
# Copyright CM4all GmbH
# author: Max Kellermann <max.kellermann@ionos.com>

import struct

from .protocol import control_magic

def make_header(command: int, length: int) -> bytes:
    assert length <= 0xffff

    return struct.pack('>IHH', control_magic, length, command)

def make_packet(command: int, payload: bytes|None) -> bytes:
    if payload is None:
        return make_header(command, 0)

    length = len(payload)
    header = make_header(command, length)
    padding = b' ' * (3 - ((length - 1) & 0x3))
    return header + payload + padding
