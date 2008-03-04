#!/usr/bin/python

import os, signal, select

class Server:
    def __init__(self, pid, rd, wr):
        self.pid = pid
        self._rd = rd
        self._wr = wr

    def kill(self):
        os.kill(self.pid, signal.SIGTERM)

    def write(self, data):
        while len(data) > 0:
            nbytes = os.write(self._wr, data)
            data = data[nbytes:]

    def read(self):
        ready_to_read, ready_to_write, in_error = select.select((self._rd,), (), (), 1.0)
        if len(ready_to_read) == 0: return None
        return os.read(self._rd, 4096)

def start_server(path):
    to_server = os.pipe()
    from_server = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.dup2(to_server[0], 0)
        os.dup2(from_server[1], 1)
        for fd in to_server + from_server:
            os.close(fd)
        os.execv(path, [os.path.basename(path)])
    os.close(to_server[0])
    os.close(from_server[1])
    return pid, from_server[0], to_server[1]

def start_mirror():
    return start_server('./test/t-http-server-mirror')

server = Server(*start_mirror())

server.write("GET / HTTP/1.1\r\nconnection: keep-alive\r\n\r\n")
data = server.read()
assert data
assert '204 No Content' in data
assert 'keep-alive' in data
assert data.endswith('\r\n\r\n')

server.write("POST / HTTP/1.1\r\nconnection: keep-alive\r\ncontent-length: 3\r\n\r\nXYZ")
data = server.read()
assert data
assert '200 OK' in data
assert 'keep-alive' in data
assert data.endswith('\r\n\r\nXYZ')


server.kill()
