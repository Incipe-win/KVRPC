# KVCache

The Linux cache application bundled with KVRPC 2. It uses the shared epoll ET transport, asynchronous clients, a dedicated group-commit storage worker, sharded LRU with byte budgets and TTL, and CRC-protected AOF records with atomic rewrite.

Build from the repository root using CMake and Protobuf. See the [root README](../README.md), [operations](../docs/OPERATIONS.md), [protocol](../docs/PROTOCOL.md), and [architecture](../docs/ARCHITECTURE.md).

SET, GET, DEL, and STATS retain their original wire format. LOOKUP distinguishes absent keys from empty values. SET_TTL persists an absolute expiration deadline. This remains a single-node cache backed by an authoritative data source; replay does not preserve unlogged read-driven LRU changes.

Historical cache microbenchmarks under tests/ are retained as examples; the root CTest suite is the verification path.
