"""Exercise the built server and C++ client, including restart and malformed peers."""
import concurrent.futures
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
import time

SERVER, CLIENT = sys.argv[1:3]
MODE = sys.argv[3] if len(sys.argv) > 3 else "group"
HEADER = struct.Struct("!HBBII")


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def frame(command, key=b"", value=b""):
    return HEADER.pack(0xCAFE, 1, command, len(key), len(value)) + key + value


def read_exact(sock, size):
    data = bytearray()
    while len(data) < size:
        part = sock.recv(size - len(data))
        if not part:
            raise AssertionError("Truncated server response")
        data.extend(part)
    return bytes(data)


def response(sock, command, key):
    magic, version, actual, key_size, value_size = HEADER.unpack(read_exact(sock, 12))
    check((magic, version, actual) == (0xCAFE, 1, command), "Invalid response header")
    check(key_size <= 65536 and value_size <= 1048576, "Response too large")
    check(read_exact(sock, key_size) == key, "Incorrect response key")
    return read_exact(sock, value_size)


def call(sock, command, key=b"", value=b""):
    sock.sendall(frame(command, key, value))
    return response(sock, command, key)


with tempfile.TemporaryDirectory(prefix="kvrpc-integration-") as directory:
    path = Path(directory)
    with socket.socket() as reserve:
        reserve.bind(("127.0.0.1", 0))
        port = reserve.getsockname()[1]
    processes = []
    log = open(path / "server.log", "w+")

    def connect():
        return socket.create_connection(("127.0.0.1", port), timeout=3)

    def start():
        process = subprocess.Popen([SERVER, str(port), str(path / "data.aof"), "127.0.0.1", MODE], stdout=log, stderr=log)
        processes.append(process)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if process.poll() is not None:
                log.flush()
                raise AssertionError((path / "server.log").read_text())
            try:
                with connect():
                    return process
            except OSError:
                time.sleep(0.02)
        raise AssertionError("Server readiness timed out")

    try:
        process = start()
        subprocess.run([CLIENT, str(port)], check=True, timeout=10)
        if len(sys.argv) > 5:
            subprocess.run([sys.argv[4], str(port), sys.argv[5], "test-v1"], check=True, timeout=10)
        with connect() as sock:
            data = b"v\x00" + b"x" * 1048574
            # Exercise fragmented input, maximum values, and partial output handling.
            request = frame(1, b"large", data)
            for offset in range(0, len(request), 997):
                sock.sendall(request[offset:offset + 997])
            check(response(sock, 1, b"large") == b"", "SET acknowledgement has a payload")
            check(call(sock, 2, b"large") == data, "Large binary value corrupted")
            sock.sendall(frame(1, b"pipe", b"one") + frame(2, b"pipe") + frame(3, b"pipe") + frame(2, b"pipe"))
            check(response(sock, 1, b"pipe") == b"", "Pipelined SET failed")
            check(response(sock, 2, b"pipe") == b"one", "Pipelined GET failed")
            response(sock, 3, b"pipe")
            check(response(sock, 2, b"pipe") == b"", "DEL was not applied")
            call(sock, 1, b"durable", b"acknowledged")
            check(b"Hits:" in call(sock, 4), "STATS failed")

        def worker(index):
            with connect() as sock:
                for j in range(8):
                    key = f"worker:{index}:{j}".encode()
                    call(sock, 1, key, key)
                    check(call(sock, 2, key) == key, "Concurrent request mismatch")

        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
            list(executor.map(worker, range(8)))
        for header in (HEADER.pack(0, 1, 2, 0, 0), HEADER.pack(0xCAFE, 2, 2, 0, 0),
                       HEADER.pack(0xCAFE, 1, 2, 0xFFFFFFFF, 0), HEADER.pack(0xCAFE, 1, 9, 0, 0)):
            with connect() as sock:
                sock.sendall(header)
                try:
                    check(sock.recv(1) == b"", "Malformed frame was accepted")
                except ConnectionResetError:
                    pass
        # An acknowledged write survives an abrupt process death.
        process.kill()
        process.wait(timeout=5)
        process = start()
        with connect() as sock:
            check(call(sock, 2, b"durable") == b"acknowledged", "Acknowledged write lost on restart")
            check(call(sock, 2, b"pipe") == b"", "Deleted key resurrected")
            for index in range(8):
                for j in range(8):
                    key = f"worker:{index}:{j}".encode()
                    check(call(sock, 2, key) == key, "Acknowledged concurrent write lost on restart")
        process.terminate()
        check(process.wait(timeout=5) == 0, "SIGTERM did not shut down cleanly")
        with open(path / "data.aof", "ab") as aof:
            aof.write(b"broken")
        failed = subprocess.run([SERVER, str(port), str(path / "data.aof"), "127.0.0.1", MODE], stdout=log, stderr=log, timeout=5)
        check(failed.returncode != 0, "Corrupt AOF was silently accepted")
        print("Server integration passed: protocol, concurrency, restart, and shutdown")
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)
        log.close()
