# CPP‑Typed‑Scanner

High‑performance, typed scanning utilities and micro‑benchmarks for common data ingestion primitives (numeric parsing, date parsing, arena allocation, tokenization). The repository ships with a Dockerized environment that:

* Builds the scanner and test binaries
* Runs a small validation suite on start
* Optionally starts a local server that continuously scans an `incoming/` folder and writes HTML reports to `artifacts/`

This README covers setup, running the server, running the micro‑benches, and comparing results against standard library baselines.

---

## Table of contents

- [CPP‑Typed‑Scanner](#cpptypedscanner)
  - [Table of contents](#table-of-contents)
  - [Prerequisites](#prerequisites)
  - [Quick start](#quick-start)
  - [What’s inside the container](#whats-inside-the-container)
  - [Running the server](#running-the-server)
  - [Running micro‑benchmarks](#running-microbenchmarks)
    - [Policy bench (numbers \& dates)](#policy-bench-numbers--dates)
    - [Arena allocator bench](#arena-allocator-bench)
    - [Tokenizer bench](#tokenizer-bench)
  - [Interpreting results](#interpreting-results)
  - [Reproducible benchmarking tips](#reproducible-benchmarking-tips)
  - [Troubleshooting](#troubleshooting)
  - [Project layout (host vs container)](#project-layout-host-vs-container)

---

## Prerequisites

* **Docker Desktop** (or Docker Engine) with **Docker Compose v2**.
* **Windows (PowerShell)**, **macOS**, or **Linux** shell. Examples include both PowerShell and POSIX‑shell friendly forms.

> **Note (Windows):** Use PowerShell with **double quotes outside** and **single quotes inside** for the `-lc '…'` bits in commands below.

---

## Quick start

Clone and `cd` into the repo, then:

```powershell
# from the repo root
cd .\docker
# Build and bring up the service (first run will build the image)
docker compose up --build
```

On startup the container:

1. Prints discovered test binaries and runs a small sanity test suite.
2. Starts the server (`typed-scanner`) on port **8080**.
3. Mirrors data from a MinIO bucket (or local mirror) into `/work/incoming` and continually scans files it finds there, writing HTML reports into `/artifacts/typed-scanner`.

If your `docker-compose.yml` maps `8080:8080`, you can open the UI at: `http://localhost:8080`.

> **Why do I see repeated "\[mirror] syncing" logs?** The entrypoint runs a **long‑lived** mirror+scan loop by design. If you only want to run a one‑off benchmark binary, skip the server — see the next section.

---

## What’s inside the container

The image includes these compiled binaries under `/opt/typed-scanner/bin`:

* `typed-scanner` – long‑running server
* `ts_bench_policy` – numeric & date parsing
* `ts_bench_arena_alloc` – arena allocator throughput
* `ts_bench_tokenizer` – tokenizer throughput
* `ts_test_*` – unit/validation tests executed at startup

There’s also a working directory at `/work` with:

* `/work/configs/config.toml` – server config
* `/work/incoming` – files to scan (CSV/JSONL samples included)
* `/artifacts/typed-scanner` – HTML reports output

On the host, these are typically bind‑mounted from the `docker/` subfolder of this repo (see the compose file).

---

## Running the server

To run the server and watch it scan `incoming/`:

```powershell
cd .\docker
# standard long‑running mode
docker compose up
```

Stop with `Ctrl+C`. Artifacts are written under the bind‑mounted `artifacts/` directory and indexed in `/artifacts/typed-scanner/index.html`.

---

## Running micro‑benchmarks

When you want to run a **single binary** instead of the long‑running server, override the entrypoint and call the binary explicitly. Use `--no-deps` to avoid starting side services and `--rm` for a clean ephemeral container.

> **PowerShell:**

```powershell
# Numbers & dates
docker compose run --rm --no-deps --entrypoint /bin/bash scanner -lc "/opt/typed-scanner/bin/ts_bench_policy --n=1000000 --iters=100"

# Arena allocator
docker compose run --rm --no-deps --entrypoint /bin/bash scanner -lc "/opt/typed-scanner/bin/ts_bench_arena_alloc --n=500000 --iters=100 --arena=8388608"

# Tokenizer (example)
docker compose run --rm --no-deps --entrypoint /bin/bash scanner -lc "/opt/typed-scanner/bin/ts_bench_tokenizer --iters=50"
```

> **bash/zsh:**

```bash
# Numbers & dates
docker compose run --rm --no-deps --entrypoint /bin/bash scanner -lc '/opt/typed-scanner/bin/ts_bench_policy --n=1000000 --iters=100'
# Arena allocator
docker compose run --rm --no-deps --entrypoint /bin/bash scanner -lc '/opt/typed-scanner/bin/ts_bench_arena_alloc --n=500000 --iters=100 --arena=8388608'
# Tokenizer
docker compose run --rm --no-deps --entrypoint /bin/bash scanner -lc '/opt/typed-scanner/bin/ts_bench_tokenizer --iters=50'
```

### Policy bench (numbers & dates)

Parses random decimal numbers and ISO‑8601 dates to measure per‑item throughput.

Key flags:

* `--n` – samples per iteration (default 1,000,000)
* `--iters` – iteration count (print each iteration’s rate)

### Arena allocator bench

Allocates a fixed item shape from a monotonic arena to measure allocator throughput.

Key flags:

* `--n` – items per iteration
* `--iters` – iteration count
* `--arena` – arena size (bytes), e.g. `8388608` (8 MiB)

### Tokenizer bench

Exercises the tokenizer state machine on bundled samples.

Key flags:

* `--iters` – iteration count

---

## Interpreting results

The benches print **throughput** in *million items per second*. To normalize across CPUs, convert to **cycles per item**:

```
cycles_per_item ≈ (CPU_Hz / items_per_second)
```

Example at 4.0 GHz:

* 58 M/s → \~69 cycles/item (numeric policy)
* 300 M/s → \~13 cycles/item (date policy)
* 50 M items/s → \~80 cycles/item (arena alloc)

Track **median** and **p95** over many iterations for a stable picture.

---

## Reproducible benchmarking tips

* **Pin cores:**

  ```bash
  docker compose run --rm --no-deps --cpus=1 --cpuset-cpus=0 --entrypoint /bin/bash scanner -lc '…'
  ```
* **Warm up:** Ignore the first \~5 iterations; report median of the rest.
* **Quiet system:** Close browsers/IDEs; disable background indexing.
* **Same data distribution:** Use fixed seeds where possible.
* **Same toolchain:** Compare inside the same container.

---

## Troubleshooting

**I get `no such service: /opt/typed-scanner/bin/…`**

> Compose thought the *command* was the service name. Ensure the order is:
> `docker compose run [OPTIONS] --entrypoint /bin/bash scanner -lc '…'`
> The word `scanner` must be the **service name**, followed by `-lc 'command'`.

**It just keeps printing mirror/scan logs**

> That’s the server mode. For one‑off benches, add `--no-deps --entrypoint /bin/bash … -lc '…'` and call the bench binary directly as shown above.

**Port 8080 is taken**

> Edit the port mapping in `docker-compose.yml` or stop the conflicting process, then re‑run.

**Windows quoting woes**

> Prefer the PowerShell examples. If using CMD, escape quotes accordingly or switch to PowerShell.

---

## Project layout (host vs container)

**Host (repo)**

```
CPP-Typed-Scanner/
├─ docker/
│  ├─ docker-compose.yml
│  ├─ configs/
│  ├─ incoming/        # place CSV/JSONL here to be scanned
│  └─ artifacts/       # HTML reports written here by the container
└─ … (source / scripts)
```

**Container (runtime)**

```
/opt/typed-scanner/bin/      # typed-scanner, ts_bench_* and ts_test_* binaries
/work/configs/config.toml
/work/incoming               # mirrored input data (bind-mounted)
/artifacts/typed-scanner     # HTML reports (bind-mounted)
```
