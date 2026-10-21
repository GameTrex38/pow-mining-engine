# Multi-stage build for ultra-lightweight C++20 container
FROM gcc:13-bookworm AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy source code and build files
COPY CMakeLists.txt ./
COPY include/ ./include/
COPY src/ ./src/
COPY tests/ ./tests/
COPY web/ ./web/

# Configure and compile with full Release optimizations
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --config Release -j2 && \
    ./build/test_pow

# Minimal Debian runtime image
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy binaries and web dashboard assets
COPY --from=builder /app/build/pow_engine /app/pow_engine
COPY --from=builder /app/build/test_pow /app/test_pow
COPY --from=builder /app/web /app/web

# Support dynamic port binding across Render, Railway, Fly.io, and Cloud Run
ENV PORT=8080
EXPOSE 8080

# Launch PoW engine listening on $PORT with auto-detected CPU cores
CMD ["sh", "-c", "./pow_engine --port ${PORT:-8080}"]
