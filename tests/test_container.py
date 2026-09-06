"""Verify non-root container operation, volume persistence, and clean shutdown."""
import json
import socket
import struct
import subprocess
import sys
import time

image = sys.argv[1] if len(sys.argv) > 1 else "kvrpc-server:local"


def docker(*args):
    return subprocess.check_output(["docker", *args], text=True).strip()


def check(condition, message):
    if not condition:
        raise AssertionError(message)


container = docker("run", "--detach", "--publish", "127.0.0.1::8080",
                   "--mount", "type=volume,destination=/data", image)
try:
    def endpoint():
        state = json.loads(docker("inspect", container))[0]
        check(state["Config"]["User"] == "10001", "Container must run as UID 10001")
        return int(state["NetworkSettings"]["Ports"]["8080/tcp"][0]["HostPort"])

    def exact(sock, size):
        data = b""
        while len(data) < size:
            chunk = sock.recv(size - len(data))
            if not chunk:
                raise OSError("Truncated response")
            data += chunk
        return data

    def call(command, key=b"", value=b""):
        with socket.create_connection(("127.0.0.1", endpoint()), timeout=2) as sock:
            sock.sendall(struct.pack("!HBBII", 0xCAFE, 1, command, len(key), len(value)) + key + value)
            magic, version, cmd, keys, values = struct.unpack("!HBBII", exact(sock, 12))
            check((magic, version, cmd, keys) == (0xCAFE, 1, command, len(key)), "Invalid response header")
            check(values <= 1048576, "Oversized response")
            check(exact(sock, keys) == key, "Wrong response key")
            return exact(sock, values)

    def ready():
        deadline = time.monotonic() + 10
        while True:
            try:
                check(b"Hits:" in call(4), "Invalid STATS response")
                return
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.1)

    ready()
    check(call(1, b"container:key", b"persisted") == b"", "SET failed")
    docker("stop", "--time", "10", container)
    state = json.loads(docker("inspect", container))[0]["State"]
    check(state["ExitCode"] == 0, "Unclean container shutdown")
    docker("start", container)
    ready()
    check(call(2, b"container:key") == b"persisted", "Volume recovery failed")
    docker("stop", "--time", "10", container)
    check(json.loads(docker("inspect", container))[0]["State"]["ExitCode"] == 0, "Unclean final shutdown")
    print("Container test passed: non-root execution, TCP, volume recovery, SIGTERM")
finally:
    # Remove only the container and anonymous volume created by this test.
    subprocess.run(["docker", "rm", "--force", "--volumes", container], check=True, stdout=subprocess.DEVNULL)
