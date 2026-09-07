# Wire protocols

## RPC version 2

Each message has a 16-byte header:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 4 | ASCII `KVR2` |
| 4 | 4 | Unsigned Protobuf payload length, network byte order |
| 8 | 8 | Nonzero request ID, network byte order |
| 16 | length | `kvrpc.wire.Envelope` |

See [the schema](../proto/rpc.proto). REQUEST carries method and typed arguments; RESPONSE carries result or status/error. Status 0 means success, 1 unknown method, 2 invalid arguments, 3 callback failure. Callback exception details are not exposed remotely. Unknown Protobuf fields support compatible schema extension; field numbers must not be reused. The outer length is bounded before body buffering.

Values distinguish signed integer, unsigned integer, double, bool, binary string, and serialized Protobuf message. Integer conversions check the destination range. The application registers the expected argument/result types. Embedded Protobuf message types are defined by the method contract, not negotiated dynamically.

Responses may arrive out of order. The ID is connection-scoped correlation, not an idempotency token or a retry guarantee. Generic version-1 framing is intentionally unsupported in version 2.

## KV compatibility protocol

The existing 12-byte network-order header remains: magic `0xCAFE` (2), version `1` (1), command (1), key length (4), value length (4), followed by key and value bytes.

| Command | Value in request | Value in response |
| --- | --- | --- |
| SET = 1 | Raw value | Empty acknowledgement |
| GET = 2 | Empty | Raw value; missing and empty remain ambiguous |
| DEL = 3 | Empty | Empty acknowledgement |
| STATS = 4 | Empty; key must be empty | Text counters |
| LOOKUP = 5 | Empty | Protobuf `CacheValue` with found/value |
| SET_TTL = 6 | Protobuf `CacheValue` with value/ttl_ms | Empty acknowledgement |

The key limit is 64 KiB and application value limit is 1 MiB. Wire value allowance includes 64 bytes for the TTL/lookup envelope. TTL must be positive and no greater than ten 365-day years. Clients use `Lookup` for an optional result. Requests on one KV connection execute sequentially; use separate connections for concurrency.

Malformed requests, storage errors, and overload close the relevant connection. A disconnected write may have committed; never infer failure-to-apply from loss of its acknowledgement.

## AOF version 2

Every new record starts with `AOF2` (4), payload length (4, network order), and IEEE CRC32 (4, network order), followed by an encoded KV mutation. SET_TTL records carry value/absolute expires_unix_ms, not relative ttl_ms. Only SET, SET_TTL, and DEL are valid log operations.

Replay supports legacy bare KV records and new checksummed records, allowing append migration. Rewriting produces only version-2 records. Old binaries cannot read upgraded files. A truncated record or CRC mismatch rejects startup rather than silently discarding history.
