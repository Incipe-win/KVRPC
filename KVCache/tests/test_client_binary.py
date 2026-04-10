import socket
import struct
import time

MAGIC = 0xCAFE
VERSION = 1
CMD_SET = 1
CMD_GET = 2
CMD_DEL = 3


def encode_msg(cmd, key, value=""):
    key_bytes = key.encode()
    value_bytes = value.encode()
    header = struct.pack("!HBBII", MAGIC, VERSION, cmd, len(key_bytes), len(value_bytes))
    return header + key_bytes + value_bytes


def decode_msg(data):
    if len(data) < 12:
        return None, None, None
    magic, version, cmd, key_len, val_len = struct.unpack("!HBBII", data[:12])
    if magic != MAGIC:
        raise ValueError("Invalid Magic")

    key = data[12 : 12 + key_len].decode()
    value = data[12 + key_len : 12 + key_len + val_len].decode()
    return cmd, key, value


def send_cmd(port, cmd, key, value=""):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("localhost", port))
        msg = encode_msg(cmd, key, value)
        s.sendall(msg)

        # Read header first
        header_data = s.recv(12)
        if len(header_data) < 12:
            return "Error: Incomplete Header"

        magic, version, resp_cmd, key_len, val_len = struct.unpack("!HBBII", header_data)

        # Read body
        body_len = key_len + val_len
        body_data = b""
        while len(body_data) < body_len:
            chunk = s.recv(body_len - len(body_data))
            if not chunk:
                break
            body_data += chunk

        s.close()

        full_data = header_data + body_data
        _, _, resp_val = decode_msg(full_data)
        return resp_val
    except Exception as e:
        return str(e)


import sys

PORT = 8085
if len(sys.argv) > 1:
    PORT = int(sys.argv[1])


print("Sending SET...")
print(send_cmd(PORT, CMD_SET, "name", "copilot"))
time.sleep(0.1)

print("Sending GET...")
print(send_cmd(PORT, CMD_GET, "name"))
time.sleep(0.1)

print("Sending GET unknown...")
print(send_cmd(PORT, CMD_GET, "unknown"))
