#
# Generic client for the beng-proxy remote control protocol.
#
# Author: Max Kellermann <max.kellermann@ionos.com>
#

import socket
import struct
from typing import Optional, Tuple, Union
from collections.abc import Mapping

from .protocol import *
from .serialize import make_packet

class Client:
    def __init__(self, host: str, port: int=5478, broadcast: bool=False, timeout: int=10):
        if host.startswith('/') or host.startswith('@'):
            s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            s.settimeout(timeout)

            s.connect(host)
        else:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            if broadcast:
                s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            s.connect((host, port))
            s.settimeout(timeout)

        self.__socket = s

    @staticmethod
    def __to_bytes(s: Union[str, bytes]) -> bytes:
        if not isinstance(s, bytes):
            s = s.encode('ascii')
        return s

    def send(self, command: int, payload: Optional[Union[str, bytes]]=None) -> None:
        if payload is None: payload = b''
        payload = self.__to_bytes(payload)

        self.__socket.send(make_packet(command, payload))

    def send_tcache_invalidate(self, vary: Mapping[int, Union[str, bytes]]) -> None:
        payload = b''
        for command, value in vary.items():
            assert isinstance(command, int)
            value = self.__to_bytes(value)

            length = len(value)
            assert length <= 0xffff
            payload += struct.pack('>HH', length, command)
            payload += value
            payload += b' ' * (3 - ((length - 1) & 0x3))

        self.send(CONTROL_TCACHE_INVALIDATE, payload)

    def send_enable_node(self, node: str, port: int) -> None:
        self.send(CONTROL_ENABLE_NODE, f'{node}:{port}')

    def send_fade_node(self, node: str, port: int) -> None:
        self.send(CONTROL_FADE_NODE, f'{node}:{port}')

    def send_node_status(self, node: str, port: int) -> None:
        self.send(CONTROL_NODE_STATUS, f'{node}:{port}')

    def send_verbose(self, verbose: int) -> None:
        assert verbose >= 0
        assert verbose <= 0xff

        self.send(CONTROL_VERBOSE, struct.pack('B', verbose))

    def send_fade_children(self, tag: Optional[str]=None) -> None:
        self.send(CONTROL_FADE_CHILDREN, tag)

    def send_terminate_children(self, tag: Optional[str]=None) -> None:
        self.send(CONTROL_TERMINATE_CHILDREN, tag)

    def send_enable_zeroconf(self) -> None:
        self.send(CONTROL_ENABLE_ZEROCONF)

    def send_disable_zeroconf(self) -> None:
        self.send(CONTROL_DISABLE_ZEROCONF)

    def send_flush_filter_cache(self) -> None:
        self.send(CONTROL_FLUSH_FILTER_CACHE)

    def send_enable_queue(self, name: Optional[str]=None) -> None:
        self.send(CONTROL_ENABLE_QUEUE, name)

    def send_disable_queue(self, name: Optional[str]=None) -> None:
        self.send(CONTROL_DISABLE_QUEUE, name)

    def send_disconnect_database(self, account: str) -> None:
        self.send(CONTROL_DISCONNECT_DATABASE, account)
