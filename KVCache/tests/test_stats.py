import socket
import struct

MAGIC = 0xCAFE
VERSION = 1
CMD_SET = 1
CMD_GET = 2
CMD_DEL = 3
CMD_STATS = 4


def encode_message(cmd, key, value=""):
    key_bytes = key.encode()
    value_bytes = value.encode()
    header = struct.pack("!HBBII", MAGIC, VERSION, cmd, len(key_bytes), len(value_bytes))
    return header + key_bytes + value_bytes


def decode_message(data):
    if len(data) < 10:
        return None, None, None
    magic, version, cmd, key_len, value_len = struct.unpack("!HBBII", data[:10])
    if magic != MAGIC:
        raise ValueError("Invalid Magic")
    key = data[10 : 10 + key_len].decode()
    value = data[10 + key_len : 10 + key_len + value_len].decode()
    return cmd, key, value


def test_stats():
    client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client.connect(("127.0.0.1", 8082))

    # 1. SET key1
    print("SET key1=value1")
    client.send(encode_message(CMD_SET, "key1", "value1"))
    client.recv(1024)

    # 2. GET key1 (Hit)
    print("GET key1")
    client.send(encode_message(CMD_GET, "key1"))
    client.recv(1024)

    # 3. GET key2 (Miss)
    print("GET key2")
    client.send(encode_message(CMD_GET, "key2"))
    client.recv(1024)

    # 4. STATS
    print("STATS")
    client.send(encode_message(CMD_STATS, ""))
    data = client.recv(1024)
    cmd, key, value = decode_message(data)
    print(f"Stats Response: {value}")

    client.close()


if __name__ == "__main__":
    test_stats()
