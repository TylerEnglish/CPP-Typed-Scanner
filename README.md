# CPP-Typed-Scanner

High-performance, typed scanning utilities and micro-benchmarks for common data-ingestion primitives (numeric parsing, date parsing, arena allocation, tokenization). The repo ships with a Dockerized environment and a one-shot `bench.sh` helper that:

* Builds the images you need (runtime + a “builder” image that has `g++`)
* Runs validation tests at container start
* Optionally runs a server that watches an `incoming/` folder and renders HTML reports into `artifacts/`
* Runs built-in benches (`ts_bench_*`) **and** baseline micro-benches (stdlib `from_chars`, `strptime`, `malloc/free`)
* Prints a clean summary (p50 / p90 / best) and drops raw logs in `bench_out/`

---

## Table of contents

- [CPP-Typed-Scanner](#cpp-typed-scanner)
  - [Table of contents](#table-of-contents)
  - [Prerequisites](#prerequisites)
  - [Quick start](#quick-start)
  - [What’s in the image](#whats-in-the-image)
  - [Run the server](#run-the-server)
  - [Benchmarking (easy mode)](#benchmarking-easy-mode)
  - [Benchmarking (manual mode)](#benchmarking-manual-mode)
  - [Reading the numbers](#reading-the-numbers)
  - [MinIO mirroring \& artifacts](#minio-mirroring--artifacts)
  - [Entrypoint knobs (env vars)](#entrypoint-knobs-env-vars)
  - [Troubleshooting](#troubleshooting)
  - [Project layout](#project-layout)
    - [Handy snippets](#handy-snippets)

---

## Prerequisites

* **Docker Desktop** (or Docker Engine) with **Compose v2**
* A shell: **PowerShell**, **bash**, or **zsh**

> Windows tip: prefer **PowerShell** examples. When using `-lc '…'`, keep outer **double quotes** and inner **single quotes**.

---

## Quick start

From the repo root:

```powershell
# Build and start the stack (scanner + minio + helpers)
cd .\docker
docker compose up --build
```

On startup the scanner container:

1. Lists discovered test binaries and runs a small test suite
2. Starts `typed-scanner` on **port 8080**
3. If MinIO is configured, mirrors a bucket into `/work/incoming` and scans any CSV/JSONL/NDJSON it finds, writing reports under `/artifacts/typed-scanner`

Open: [http://localhost:8080](http://localhost:8080) (if `8080:8080` is mapped in compose)

> If you only want *benchmarks*, you don’t need the long-running server. See **Benchmarking (easy mode)** below.

---

## What’s in the image

Installed under `/opt/typed-scanner/bin`:

* `typed-scanner` — HTTP server
* `ts_bench_policy` — numeric & date parsing
* `ts_bench_arena_alloc` — arena allocation throughput
* `ts_bench_tokenizer` — tokenizer throughput
* `ts_test_*` — unit tests executed by the entrypoint

Runtime working dir `/work` contains:

* `/work/configs/config.toml` — server config
* `/work/incoming` — drop files here to be scanned
* `/work/templates` & `/work/web` — report UX assets
* `/artifacts/typed-scanner` — rendered HTML reports

---

## Run the server

```powershell
cd .\docker
docker compose up
```

Stop with `Ctrl+C`. Reports land in `docker/artifacts/typed-scanner` on the host (bind-mounted).

---

## Benchmarking (easy mode)

Use the helper script — it finds your compose file, builds what’s needed, runs the benches, and prints a summary.

```bash
# From anywhere inside the repo
bash docker/bench.sh          # full run
bash docker/bench.sh --quick  # fewer iters (fast smoke)
```

**What it runs**

* Built-in: `ts_bench_policy`, `ts_bench_arena_alloc`, `ts_bench_tokenizer`
* Baselines (compiled in a separate “builder” image that has `g++`):

  * `from_chars` for decimal ints
  * `strptime` for `%Y-%m-%d`
  * `malloc/free` micro-alloc loop

**Options**

```
--quick               # ITERS=10 for faster smoke runs
--no-tokenizer        # skip tokenizer bench
--no-policy           # skip policy bench
--no-arena            # skip arena bench
--no-baselines        # skip stdlib/malloc baselines
--n <N>               # samples per iter for policy/tokenizer (default 1e6)
--iters <I>           # iterations for policy/tokenizer (default 50)
--malloc-n <N>        # items per iter for malloc baseline (default 5e5)
--malloc-iters <I>    # iterations for malloc baseline (default 50)
--malloc-sz <bytes>   # allocation size per item (default 64)
```

**Outputs**

* Human summary to stdout (p50 / p90 / best)
* Raw logs in `bench_out/` (one file per bench)
* Full build/bench transcript in `bench_out/_bench.log`

---

## Benchmarking (manual mode)

If you prefer to run the bench binaries yourself:

> **PowerShell**

```powershell
# Numbers & dates
docker compose run --rm --no-deps --entrypoint /bin/bash scanner -lc "/opt/typed-scanner/bin/ts_bench_policy --n=1000000 --iters=100"

# Arena allocator
docker compose run --rm --no-deps --entrypoint /bin/bash scanner -lc "/opt/typed-scanner/bin/ts_bench_arena_alloc --n=500000 --iters=100 --arena=8388608"

# Tokenizer
docker compose run --rm --no-deps --entrypoint /bin/bash scanner -lc "/opt/typed-scanner/bin/ts_bench_tokenizer --iters=50"
```

> **bash/zsh**

```bash
docker compose run --rm --no-deps --entrypoint /bin/bash scanner -lc '/opt/typed-scanner/bin/ts_bench_policy --n=1000000 --iters=100'
docker compose run --rm --no-deps --entrypoint /bin/bash scanner -lc '/opt/typed-scanner/bin/ts_bench_arena_alloc --n=500000 --iters=100 --arena=8388608'
docker compose run --rm --no-deps --entrypoint /bin/bash scanner -lc '/opt/typed-scanner/bin/ts_bench_tokenizer --iters=50'
```

*Why not `g++` inside the runtime container?* The runtime image is slim. If you need `g++` for local experiments, use the **builder** stage (or just run `bash docker/bench.sh` and let it handle that).

---

## Reading the numbers

All benches print **throughput**. Units:

* Policy/Tokenizer: **million items / second (M/s)**
* Arena/Malloc baseline: **million items / second (M items/s)**

If you want cycles per item (rough CPU-agnostic view):

```
cycles_per_item ≈ (CPU_GHz * 1e9) / (items_per_second)
```

Examples at 4.0 GHz:

* 58 M/s  → \~69 cycles/item (numeric policy)
* 300 M/s → \~13 cycles/item (date policy)
* 50 M items/s → \~80 cycles/item (arena alloc)

**Repro tips**

* Pin a CPU: `--cpus=1 --cpuset-cpus=0`
* Warm-up: ignore first \~5 iterations; report median of the rest
* Quiet box: close heavy apps/background indexing
* Same distro/toolchain: compare inside the **same container**
* Same data distribution: the benches already use fixed RNG seeds

---

## MinIO mirroring & artifacts

If you set these env vars, the scanner container mirrors a bucket into `/work/incoming`, scans new files, and writes fresh reports under `/artifacts/typed-scanner`:

* `MINIO_ENDPOINT` — e.g. `http://minio:9000`
* `MINIO_BUCKET` — e.g. `incoming`
* `MINIO_ROOT_USER` / `MINIO_ROOT_PASSWORD`

Optional: also push finished artifacts to a bucket:

* `PUSH_TO_MINIO=true`
* `ARTIFACTS_BUCKET=artifacts-typed-scanners` (default)

The entrypoint uses the `mc` (MinIO client) CLI preinstalled in the image.

---

## Entrypoint knobs (env vars)

Most folks don’t need these; they’re here if you want to customize behavior:

| Var                | Default                                | Meaning                                                                                                            |
| ------------------ | -------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| `SCANNER_BIN`      | `/opt/typed-scanner/bin/typed-scanner` | Path to server binary                                                                                              |
| `PORT`             | `8080`                                 | HTTP port for the server                                                                                           |
| `SCAN_INTERVAL`    | `15`                                   | Seconds between mirror/scan passes                                                                                 |
| `ART_ROOT`         | see below                              | Artifact root. Defaults to `/artifacts/typed-scanner` unless inferred from `SCANNER_ARGS`                          |
| `SCANNER_ARGS`     | *empty*                                | Extra flags passed to `typed-scanner`. If you don’t set `--artifact-root` or `--port`, the entrypoint injects them |
| `SLUG_MODE`        | `basename`                             | Report slugging mode                                                                                               |
| `SLUG_LEN`         | `64`                                   | Max slug length                                                                                                    |
| `RUN_TESTS`        | `1`                                    | Set to `0` to skip the startup test suite                                                                          |
| `MINIO_*`          | —                                      | See [MinIO mirroring & artifacts](#minio-mirroring--artifacts)                                                     |
| `PUSH_TO_MINIO`    | `false`                                | Mirror reports back to MinIO                                                                                       |
| `ARTIFACTS_BUCKET` | `artifacts-typed-scanners`             | Bucket for pushed artifacts                                                                                        |

The entrypoint tries to ensure `ART_ROOT` is writable; it will chmod `0777` or fall back to `/work/artifacts-fallback` if needed.

---

## Troubleshooting

**`no such service: /opt/typed-scanner/bin/...`**

> The service name goes **before** the command. Use
> `docker compose run --rm --no-deps --entrypoint /bin/bash scanner -lc '…'`
> (`scanner` is the service; the command is after `-lc`.)

**`failed to read dockerfile: open Dockerfile: no such file or directory` (from `bench.sh`)**

> Keep the repo layout intact (`docker/` folder next to the root). The script auto-detects compose in repo root or `./docker` and builds with **repo root** as context so `COPY . /src` works.

**`g++: command not found` inside the runtime container**

> Expected — the runtime is slim. Use the “builder” image (or just run `bash docker/bench.sh` to compile baselines there).

**Windows CR/LF line endings**

> If you edit scripts in Windows, strip CRLFs once inside WSL/bash:
> `sed -i 's/\r$//' docker/bench.sh docker/scripts/entrypoint.sh`

**Port 8080 already in use**

> Change the host mapping in `docker/docker-compose.yml` or stop the conflicting service.

---

## Project layout

**Host (repo)**

```
CPP-Typed-Scanner/
├─ docker/
│  ├─ docker-compose.yml
│  ├─ bench.sh                 # one-shot benchmark driver
│  ├─ configs/
│  ├─ artifacts/               # HTML reports (bind-mounted)
│  ├─ incoming/                # files to scan (bind-mounted)
│  └─ scripts/
│     └─ entrypoint.sh
├─ include/typed_scanner/…
├─ src/…                       # core library + server
├─ bench/…                     # ts_bench_* sources
└─ tests/…                     # unit tests + sample data
```

**Container (runtime)**

```
/opt/typed-scanner/bin/        # typed-scanner, ts_bench_*, ts_test_*
/work/configs/config.toml
/work/incoming
/artifacts/typed-scanner       # report roots
```

---

### Handy snippets

Pin 1 CPU while benchmarking:

```bash
docker compose run --rm --no-deps --cpus=1 --cpuset-cpus=0 \
  --entrypoint /bin/bash scanner -lc '/opt/typed-scanner/bin/ts_bench_policy --n=1000000 --iters=100'
```

Run benches fast:

```bash
bash docker/bench.sh --quick
```

Skip stdlib baselines (only built-ins):

```bash
bash docker/bench.sh --no-baselines
```
