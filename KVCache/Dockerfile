FROM ubuntu:24.04 AS builder
RUN apt-get update && apt-get install -y --no-install-recommends cmake g++ make protobuf-compiler libprotobuf-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /source
COPY CMakeLists.txt ./
COPY cmake/ cmake/
COPY proto/ proto/
COPY include/ include/
COPY src/ src/
COPY examples/ examples/
COPY benchmarks/ benchmarks/
COPY KVCache/include/ KVCache/include/
COPY KVCache/src/ KVCache/src/
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && cmake --build build --target kv_server --parallel 4

FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends libstdc++6 libprotobuf32t64 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --create-home kvrpc \
    && mkdir /data && chown kvrpc:kvrpc /data
COPY --from=builder /source/build/kv_server /usr/local/bin/kv_server
USER 10001
WORKDIR /data
EXPOSE 8080
STOPSIGNAL SIGTERM
ENTRYPOINT ["/usr/local/bin/kv_server"]
CMD ["8080", "/data/appendonly.aof", "0.0.0.0"]
