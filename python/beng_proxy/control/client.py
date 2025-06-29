#
# Generic client for the beng-proxy remote control protocol.
#
# Author: Max Kellermann <max.kellermann@ionos.com>
#

import socket
import struct
from typing import Optional, Tuple, Union
from collections.abc import Mapping

from beng_proxy.control.protocol import *

class Client:
    def __init__(self, host: str, port: int=5478, broadcast: bool=False, timeout: int=10):
        if host:
            try:
                # connect to a specific process by its PID
                pid = int(host)
                host = '@beng_control:pid=%d' % pid
            except ValueError:
                pass

        if host and host[0] in '/@':
            self._socket = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
            self._socket.settimeout(timeout)

            # bind to a unique address, so the server has something it
            # can send a reply to
            import os
            self._socket.bind('\0beng-proxy-client-' + str(os.getpid()))

            if host[0] == '@':
                # abstract socket
                host = '\0' + host[1:]

            self._socket.connect(host)
        else:
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            if broadcast:
                self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            self._socket.connect((host, port))
            self._socket.settimeout(timeout)

    def receive(self) -> list[Tuple[int, bytes]]:
        """Receive a datagram from the server.  Returns a list of
        (command, payload) tuples."""
        packets = []
        data = self._socket.recv(8192)
        while len(data) > 4:
            header, data = data[:4], data[4:]
            length, command = struct.unpack('>HH', header)
            if length > len(data):
                break
            payload, data = data[:length], data[length:]
            packets.append((command, payload))
        return packets

    @staticmethod
    def __to_bytes(s: Union[str, bytes]) -> bytes:
        if not isinstance(s, bytes):
            s = s.encode('ascii')
        return s

    def send(self, command: int, payload: Optional[Union[str, bytes]]=None) -> None:
        if payload is None: payload = b''
        payload = self.__to_bytes(payload)

        length = len(payload)
        assert length <= 0xffff
        header = struct.pack('>IHH', control_magic, length, command)
        padding = b' ' * (3 - ((length - 1) & 0x3))
        self._socket.send(header + payload + padding)

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
        self.send(CONTROL_ENABLE_NODE, '%s:%i' % (node, port))

    def send_fade_node(self, node: str, port: int) -> None:
        self.send(CONTROL_FADE_NODE, '%s:%i' % (node, port))

    def send_node_status(self, node: str, port: int) -> None:
        self.send(CONTROL_NODE_STATUS, '%s:%i' % (node, port))

    def send_dump_pools(self) -> None:
        self.send(CONTROL_DUMP_POOLS)

    def send_stats(self) -> None:
        self.send(CONTROL_STATS)

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

    def send_flush_nfs_cache(self) -> None:
        self.send(CONTROL_FLUSH_NFS_CACHE)

    def send_flush_filter_cache(self) -> None:
        self.send(CONTROL_FLUSH_FILTER_CACHE)

    def send_enable_queue(self, name: Optional[str]=None) -> None:
        self.send(CONTROL_ENABLE_QUEUE, name)

    def send_disable_queue(self, name: Optional[str]=None) -> None:
        self.send(CONTROL_DISABLE_QUEUE, name)

    def send_disconnect_database(self, account: str) -> None:
        self.send(CONTROL_DISCONNECT_DATABASE, account)
