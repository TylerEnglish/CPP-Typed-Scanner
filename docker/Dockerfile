# --- Builder stage ------------------------------------------------------------
FROM debian:bookworm AS builder

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    ca-certificates \
    curl \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

RUN test -f /src/include/typed_scanner/http_server.hpp || (echo "Missing headers in build context!" && ls -la /src && ls -la /src/include || true && false)

# Configure + build + install to /opt/typed-scanner
RUN cmake -S . -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=OFF \
      -DCMAKE_INSTALL_PREFIX=/opt/typed-scanner \
 && cmake --build build -j \
 && cmake --install build

# --- Runtime stage ------------------------------------------------------------
FROM debian:bookworm-slim AS runtime
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates curl bash \
   && rm -rf /var/lib/apt/lists/*

# Install mc
RUN set -eux; \
    arch="$(dpkg --print-architecture)"; \
    case "$arch" in \
      amd64) url="https://dl.min.io/client/mc/release/linux-amd64/mc" ;; \
      arm64) url="https://dl.min.io/client/mc/release/linux-arm64/mc" ;; \
      *) echo "Unsupported arch: $arch" >&2; exit 1 ;; \
    esac; \
    curl -fsSL "$url" -o /usr/local/bin/mc; \
    chmod +x /usr/local/bin/mc; \
    /usr/local/bin/mc --version >/dev/null

RUN useradd -m -u 10001 -s /bin/bash scanner
COPY --from=builder /opt/typed-scanner /opt/typed-scanner
WORKDIR /work
COPY --from=builder /src/configs   /work/configs
COPY --from=builder /src/templates /work/templates
COPY --from=builder /src/web       /work/web
RUN mkdir -p /artifacts && ln -s /artifacts /work/artifacts
COPY docker/scripts/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN set -eux; f=/usr/local/bin/entrypoint.sh; sed -i 's/\r$//' "$f"; \
    case "$(head -c3 "$f" | od -An -t x1)" in " ef bb bf") \
      tail -c +4 "$f" > "$f.nobom" && mv "$f.nobom" "$f";; esac; \
    chmod 0755 "$f"; head -n1 "$f"
HEALTHCHECK --interval=10s --timeout=3s --start-period=20s --retries=6 \
  CMD curl -fsS http://127.0.0.1:8080/ || exit 1
EXPOSE 8080
USER scanner
ENTRYPOINT ["bash", "/usr/local/bin/entrypoint.sh"]
