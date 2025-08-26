# ---------- Builder ----------
FROM debian:bookworm-slim AS builder
ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake git ninja-build ca-certificates wget \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build --target typed-scanner -j

# ---------- Runtime ----------
FROM debian:bookworm-slim AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends \
      ca-certificates libstdc++6 curl \
    && rm -rf /var/lib/apt/lists/*
RUN useradd -r -u 10001 appuser
WORKDIR /srv

COPY --from=builder /app/build/typed-scanner /usr/local/bin/typed-scanner
COPY --from=builder /app/templates /srv/templates
COPY --from=builder /app/web       /srv/web
COPY --from=builder /app/configs   /srv/configs

ENV TS_CONFIG=/srv/configs/config.toml \
    TS_PIPELINE=/srv/configs/pipeline.toml \
    TS_ARTIFACT_ROOT=/artifacts/typed-scanner \
    TS_PORT=8080

EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=3s --retries=3 CMD curl -fsS http://127.0.0.1:8080/ || exit 1

USER appuser
ENTRYPOINT ["/usr/local/bin/typed-scanner"]
