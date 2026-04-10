# Build stage
FROM ubuntu:22.04 AS builder

# Avoid interactive prompts
ENV DEBIAN_FRONTEND=noninteractive
ENV XMAKE_ROOT=y

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    curl \
    git \
    unzip \
    zip \
    && rm -rf /var/lib/apt/lists/*

# Install xmake via PPA
RUN apt-get update && apt-get install -y software-properties-common \
    && add-apt-repository -y ppa:xmake-io/xmake \
    && apt-get update \
    && apt-get install -y xmake \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy xmake config
COPY xmake.lua .

# Copy source code
COPY include/ include/
COPY src/ src/
COPY tests/ tests/

# Build the project
RUN xmake f -m release -y
RUN xmake -y
RUN xmake install -o /app/install kv_server

# Runtime stage
FROM ubuntu:22.04

# Create a non-root user
RUN useradd -m appuser

WORKDIR /app

# Copy the binary
COPY --from=builder /app/install/bin/kv_server /app/kv_server

# Create directory for AOF file and set permissions
RUN touch appendonly.aof && chown appuser:appuser appendonly.aof

# Switch to non-root user
USER appuser

# Expose the port
EXPOSE 8080

# Run the server
CMD ["./kv_server", "8080"]
