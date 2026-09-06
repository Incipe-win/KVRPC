# Protocol Reference

KVRPC supports two independent TCP wire formats. TCP packet boundaries have no meaning in either format; receivers must handle fragmented headers and bodies as well as multiple frames in one read.

## Generic RPC

| Field | Size | Encoding |
| --- | --- | --- |
| Payload length | 4 bytes | Unsigned, little endian |
| Payload | Declared length | Serialized values |

A request payload begins with a serialized method-name string, followed by the method's arguments in declaration order. A response payload contains exactly one value of the requested return type. A `void` result requires an empty response payload. Extra bytes after a decoded result are a protocol error.

### Value encoding

| Type | Encoding |
| --- | --- |
| Fixed-width integer | Little-endian bytes; signed values use two's complement |
| `float`, `double` | IEEE 754 bits in little-endian order |
| `bool` | One byte: `0` or `1` |
| `std::string` | 4-byte little-endian byte length, then raw bytes |
| C string argument | Converted to an owned string before dispatch; null pointers are rejected |

The default payload limit is 2 MiB in each direction. The standalone serializer defaults to a 64 MiB serialization limit. Frame lengths are checked before response allocation. Embedded string lengths are checked against the remaining payload.

Use `int32_t`, `uint32_t`, `int64_t`, and other fixed-width types for portable interfaces. Platform-dependent types such as `long`, pointers, aggregates, and `long double` are not portable RPC signatures. The protocol has no method schema negotiation or structured remote-error envelope.

The little-endian encoding preserves the original prototype's wire bytes on ordinary little-endian Linux/macOS systems. Original native-endian peers on big-endian systems need migration.

## KVCache version 1

All multibyte header integers use network byte order, which is big endian. The header is always 12 bytes; its layout does not depend on C++ structure packing.

| Offset | Field | Size | Required value |
| --- | --- | --- | --- |
| 0 | Magic | 2 bytes | `0xCAFE` |
| 2 | Version | 1 byte | `1` |
| 3 | Command | 1 byte | `1`–`4` |
| 4 | Key length | 4 bytes | At most 65,536 bytes |
| 8 | Value length | 4 bytes | At most 1,048,576 bytes |
| 12 | Key | Key length | Raw bytes |
| 12 + key length | Value | Value length | Raw bytes |

The maximum complete frame size is 1,114,124 bytes. Keys and values may contain NUL bytes. Empty keys are accepted. Lengths count bytes, not characters.

### Commands and responses

| Command | Code | Request | Response value |
| --- | --- | --- | --- |
| `SET` | 1 | Key and value | Empty acknowledgement |
| `GET` | 2 | Key; empty value | Stored bytes, or empty on a miss |
| `DEL` | 3 | Key; empty value | Empty acknowledgement, including an absent key |
| `STATS` | 4 | Empty key and value | UTF-8 text: `Hits: N, Misses: N` |

Every response repeats the command and key of its request. The client reads the complete key and value, verifies the echoed key, and then resolves the future. A sent `SET` request alone is not success.

For example, `SET k v` is:

```text
CA FE 01 01 00 00 00 01 00 00 00 01 6B 76
```

The corresponding acknowledgement is:

```text
CA FE 01 01 00 00 00 01 00 00 00 00 6B
```

Version 1 has no explicit status code. A missing key and a stored empty value are indistinguishable through `GET`. `DEL` acknowledges completion rather than reporting whether a key existed. Invalid frames and storage failures cause connection closure. Clients treat them as failures and do not infer success from a successful send.

### Compatibility

Valid original KVCache version-1 frames remain compatible. The implementation now rejects invalid magic/version values, unknown commands, excessive lengths, illegal command bodies, mismatched response keys, and unexpected mutation payloads. The bundled server now implements deletion.

Generic RPC framing must never be sent to the KVCache port. The server does not implement generic `RpcClient::Call` dispatch.

## Failure reporting

`kvrpc::Error` derives from `std::runtime_error` and exposes `code()`:

| Code | Meaning |
| --- | --- |
| `invalid_argument` | Invalid configuration or request limits |
| `overloaded` | No space in the client waiting queue |
| `closed` | Client or connection pool no longer accepts work |
| `timeout` | Pool acquisition, connect, or request deadline expired |
| `connection` | TCP connection could not be established |
| `transport` | Established connection failed during a transfer |
| `protocol` | Peer response failed framing or payload validation |

Allocation failures and standalone serializer errors retain their standard C++ exception types. Validation before dispatch can throw synchronously; execution and overload errors are observed through `future::get()`.

A transport error after sending a mutation does not reveal whether the mutation committed. Reconcile state before retrying a non-idempotent application workflow. The library never retries requests automatically.
