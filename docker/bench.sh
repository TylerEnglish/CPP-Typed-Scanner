set -Eeuo pipefail

# ==============================================================================
# bench.sh — build images, run built-in benches (policy/tokenizer/arena),
#             compile + run baseline micro-benches (from_chars/strptime/malloc),
#             and print a clean summary (p50/p90/best).
# ==============================================================================

# ------------------------------------------------------------------------------
# Locate repo + compose file
# ------------------------------------------------------------------------------
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "${script_dir}/.." && pwd)"

find_compose() {
  local d f
  for d in "$project_root" "$project_root/docker"; do
    for f in docker-compose.yml docker-compose.yaml compose.yml compose.yaml; do
      if [[ -f "$d/$f" ]]; then
        COMPOSE_DIR="$d"
        COMPOSE_FILE="$d/$f"
        return 0
      fi
    done
  done
  return 1
}

have(){ command -v "$1" >/dev/null 2>&1; }

if have docker && docker compose version >/dev/null 2>&1; then
  COMPOSE_CMD=(docker compose)
elif have docker-compose; then
  COMPOSE_CMD=(docker-compose)
else
  echo "ERR: docker (with compose) is required" >&2; exit 1
fi

if ! find_compose; then
  echo "ERR: could not find a compose file in $project_root or $project_root/docker" >&2
  echo "     Expected one of: docker-compose.yml, docker-compose.yaml, compose.yml, compose.yaml" >&2
  exit 1
fi

SERVICE="${SERVICE:-scanner}"   # compose service name

# ------------------------------------------------------------------------------
# Defaults (override via env or flags)
# ------------------------------------------------------------------------------
N="${N:-1000000}"            # samples for policy/tokenizer
ITERS="${ITERS:-50}"         # iterations for policy/tokenizer
MALLOC_N="${MALLOC_N:-500000}"
MALLOC_ITERS="${MALLOC_ITERS:-50}"
MALLOC_SZ="${MALLOC_SZ:-64}"

DO_TOKENIZER="${DO_TOKENIZER:-1}"
DO_POLICY="${DO_POLICY:-1}"
DO_ARENA="${DO_ARENA:-1}"
DO_BASELINES="${DO_BASELINES:-1}"

usage(){
cat <<EOF
Usage: bash docker/bench.sh [options]

Options:
  --quick             Run fewer iters (ITERS=10, MALLOC_ITERS=10)
  --no-tokenizer      Skip ts_bench_tokenizer
  --no-policy         Skip ts_bench_policy
  --no-arena          Skip ts_bench_arena_alloc
  --no-baselines      Skip baseline C++ benches (use builder image)
  --n <N>             Sample count (policy/tokenizer), default $N
  --iters <I>         Iterations (policy/tokenizer), default $ITERS
  --malloc-n <N>      malloc/free items, default $MALLOC_N
  --malloc-iters <I>  malloc/free iters, default $MALLOC_ITERS
  --malloc-sz <B>     malloc size per item, default $MALLOC_SZ

Env:
  SERVICE (default: scanner)   Compose service name
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --quick) ITERS=10; MALLOC_ITERS=10; shift;;
    --no-tokenizer) DO_TOKENIZER=0; shift;;
    --no-policy) DO_POLICY=0; shift;;
    --no-arena) DO_ARENA=0; shift;;
    --no-baselines) DO_BASELINES=0; shift;;
    --n) N="$2"; shift 2;;
    --iters) ITERS="$2"; shift 2;;
    --malloc-n) MALLOC_N="$2"; shift 2;;
    --malloc-iters) MALLOC_ITERS="$2"; shift 2;;
    --malloc-sz) MALLOC_SZ="$2"; shift 2;;
    -h|--help) usage; exit 0;;
    *) echo "Unknown arg: $1"; usage; exit 1;;
  esac
done

OUT_DIR="$project_root/bench_out"
mkdir -p "$OUT_DIR"

log(){ printf "\n==== %s ====\n" "$*" | tee -a "$OUT_DIR/_bench.log"; }

# ------------------------------------------------------------------------------
# Helpers for summary
# ------------------------------------------------------------------------------
p50p90() {
  mapfile -t arr < <(cat | awk '{print $1}' | sed '/^$/d' | sort -n)
  local n=${#arr[@]}
  if (( n == 0 )); then echo "0 0"; return; fi
  local p50i=$(( (n+1)*50/100 - 1 )); (( p50i<0 )) && p50i=0; (( p50i>=n )) && p50i=$((n-1))
  local p90i=$(( (n+1)*90/100 - 1 )); (( p90i<0 )) && p90i=0; (( p90i>=n )) && p90i=$((n-1))
  printf "%.3f %.3f\n" "${arr[$p50i]}" "${arr[$p90i]}"
}

# Grab all numeric values that follow "rate=<num>"
rates_from(){
  grep -ho 'rate=[0-9.]\+ [A-Za-z/]\+' "$1" 2>/dev/null | awk '{print $1}' | sed 's/rate=//'
}

# Extract rates only for a named section like [numeric] or [dates]
# Robust across gawk/mawk/busybox: track current section with a simple state machine
rates_from_section() { # file, section_name
  local f="$1" want="$2"
  awk -v want="$want" '
    /^\[[^]]+\]/ {
      sect = $0
      sub(/].*$/, "", sect)  # trim from closing ] to EOL
      sub(/^\[/, "", sect)   # trim leading [
      next
    }
    /rate=/ && sect == want {
      if (match($0, /rate=([0-9.]+)/, m)) print m[1]
    }
  ' "$f"
}

summarize_stream(){
  local unit="$1" label="$2"
  mapfile -t vals
  if (( ${#vals[@]} == 0 )); then
    echo "  - $label: (no data)" | tee -a "$OUT_DIR/_bench.log"; return
  fi
  local best; best=$(printf "%s\n" "${vals[@]}" | sort -nr | head -n1)
  read -r p50 p90 < <(printf "%s\n" "${vals[@]}" | p50p90)
  echo "  - $label: p50=${p50} $unit, p90=${p90} $unit, best=${best} $unit" | tee -a "$OUT_DIR/_bench.log"
}

summarize_file(){
  local f="$1" unit="$2" label="$3"
  if [[ -s "$f" ]]; then
    summarize_stream "$unit" "$label" < <(rates_from "$f")
  else
    echo "  - $label: (no data)" | tee -a "$OUT_DIR/_bench.log"
  fi
}

# ------------------------------------------------------------------------------
# Build images
# ------------------------------------------------------------------------------
log "Building runtime image (compose service: $SERVICE)"
( cd "$COMPOSE_DIR" && "${COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" build "$SERVICE" ) | tee -a "$OUT_DIR/_bench.log"

if (( DO_BASELINES )); then
  log "Building builder image (Dockerfile --target builder)"
  DOCKERFILE_PATH="$COMPOSE_DIR/Dockerfile"
  if [[ ! -f "$DOCKERFILE_PATH" ]]; then
    DOCKERFILE_PATH="$project_root/Dockerfile"
  fi
  if [[ ! -f "$DOCKERFILE_PATH" ]]; then
    echo "ERR: Couldn’t find Dockerfile in $COMPOSE_DIR or $project_root" >&2
    exit 1
  fi
  # Keep context at repo root so "COPY . /src" sees entire tree
  docker build -f "$DOCKERFILE_PATH" --target builder -t typed-scanner:builder "$project_root" \
    | tee -a "$OUT_DIR/_bench.log"
fi

# ------------------------------------------------------------------------------
# Run built-in benches (runtime image)
# ------------------------------------------------------------------------------
if (( DO_POLICY )); then
  log "ts_bench_policy (N=$N, iters=$ITERS)"
  POL="$OUT_DIR/policy.txt"
  ( cd "$COMPOSE_DIR" && \
    "${COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" run --rm --no-deps --entrypoint /bin/bash "$SERVICE" -lc \
      "/opt/typed-scanner/bin/ts_bench_policy --n=$N --iters=$ITERS" \
  ) | tee "$POL"
fi

if (( DO_ARENA )); then
  log "ts_bench_arena_alloc (N=$MALLOC_N, iters=$MALLOC_ITERS, arena=$((8*1024*1024)))"
  ARENA="$OUT_DIR/arena.txt"
  ( cd "$COMPOSE_DIR" && \
    "${COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" run --rm --no-deps --entrypoint /bin/bash "$SERVICE" -lc \
      "/opt/typed-scanner/bin/ts_bench_arena_alloc --n=$MALLOC_N --iters=$MALLOC_ITERS --arena=$((8*1024*1024))" \
  ) | tee "$ARENA"
fi

if (( DO_TOKENIZER )); then
  log "ts_bench_tokenizer (iters=$ITERS)"
  TOK="$OUT_DIR/tokenizer.txt"
  ( cd "$COMPOSE_DIR" && \
    "${COMPOSE_CMD[@]}" -f "$COMPOSE_FILE" run --rm --no-deps --entrypoint /bin/bash "$SERVICE" -lc \
      "/opt/typed-scanner/bin/ts_bench_tokenizer --iters=$ITERS" \
  ) | tee "$TOK"
fi

# ------------------------------------------------------------------------------
# Baseline micro-benches (compiled in builder image)
# ------------------------------------------------------------------------------
if (( DO_BASELINES )); then
  log "Baseline: from_chars (N=$N, iters=$ITERS)"
  FCH="$OUT_DIR/from_chars.txt"
  docker run --rm -i typed-scanner:builder bash -lc '
cat > /tmp/bench_from_chars.cpp << "CPP"
#include <charconv>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <iostream>
int main(int argc, char** argv){
  size_t N = 1000000, iters = 50;
  if (argc > 1) N = std::stoull(argv[1]);
  if (argc > 2) iters = std::stoull(argv[2]);
  std::mt19937_64 rng(123);
  std::uniform_int_distribution<int> dist(0, 99999999);
  std::vector<std::string> strs; strs.reserve(N);
  for(size_t i=0;i<N;i++){ strs.push_back(std::to_string(dist(rng))); }
  std::vector<long long> out(N);
  for(size_t it=1; it<=iters; ++it){
    auto t0 = std::chrono::high_resolution_clock::now();
    size_t ok=0;
    for(size_t i=0;i<N;i++){
      long long v;
      auto p = std::from_chars(strs[i].data(), strs[i].data()+strs[i].size(), v, 10);
      ok += (p.ec == std::errc{}); out[i]=v;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double s = std::chrono::duration<double>(t1-t0).count();
    std::cout << "[from_chars] iter " << it << ": ok=" << ok
              << " time=" << s << "s  rate=" << (N/s)/1e6 << " M/s\n";
  }
}
CPP
g++ -O3 -march=native -std=c++20 /tmp/bench_from_chars.cpp -o /tmp/bench_from_chars
/tmp/bench_from_chars '"$N"' '"$ITERS"'
' | tee "$FCH"

  log "Baseline: strptime (N=$N, iters=$ITERS)"
  STP="$OUT_DIR/strptime.txt"
  docker run --rm -i typed-scanner:builder bash -lc '
cat > /tmp/bench_strptime.cpp << "CPP"
#define _XOPEN_SOURCE
#include <ctime>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <iostream>
int main(int argc, char** argv){
  size_t N = 1000000, iters = 50;
  if (argc > 1) N = std::stoull(argv[1]);
  if (argc > 2) iters = std::stoull(argv[2]);
  std::mt19937 rng(123);
  std::uniform_int_distribution<int> y(1970, 2099), m(1,12), d(1,28);
  std::vector<std::string> strs; strs.reserve(N);
  auto two = [](int x){ char b[3]; snprintf(b, sizeof b, "%02d", x); return std::string(b); };
  for(size_t i=0;i<N;i++){ int Y=y(rng), M=m(rng), D=d(rng);
    strs.push_back(std::to_string(Y)+"-"+two(M)+"-"+two(D)); }
  for(size_t it=1; it<=iters; ++it){
    auto t0 = std::chrono::high_resolution_clock::now();
    size_t ok=0; std::tm tm{};
    for(size_t i=0;i<N;i++){ tm = {};
      ok += (strptime(strs[i].c_str(), "%Y-%m-%d", &tm) != nullptr);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double s = std::chrono::duration<double>(t1-t0).count();
    std::cout << "[strptime] iter " << it << ": ok=" << ok
              << " time=" << s << "s  rate=" << (N/s)/1e6 << " M/s\n";
  }
}
CPP
g++ -O3 -march=native -std=gnu++20 /tmp/bench_strptime.cpp -o /tmp/bench_strptime
/tmp/bench_strptime '"$N"' '"$ITERS"'
' | tee "$STP"

  log "Baseline: malloc/free (N=$MALLOC_N, iters=$MALLOC_ITERS, sz=$MALLOC_SZ)"
  MLF="$OUT_DIR/malloc.txt"
  docker run --rm -i typed-scanner:builder bash -lc '
cat > /tmp/bench_malloc.cpp << "CPP"
#include <cstdlib>
#include <vector>
#include <chrono>
#include <iostream>
int main(int argc, char** argv){
  size_t N = 500000, iters = 50, sz = 64;
  if (argc>1) N=std::stoull(argv[1]);
  if (argc>2) iters=std::stoull(argv[2]);
  if (argc>3) sz=std::stoull(argv[3]);
  for(size_t it=1; it<=iters; ++it){
    auto t0 = std::chrono::high_resolution_clock::now();
    std::vector<void*> ptrs; ptrs.reserve(N);
    for(size_t i=0;i<N;i++){ ptrs.push_back(std::malloc(sz)); }
    for(void* p: ptrs) std::free(p);
    auto t1 = std::chrono::high_resolution_clock::now();
    double s = std::chrono::duration<double>(t1-t0).count();
    std::cout << "[malloc/free] iter " << it
              << ": items=" << N << " time=" << s
              << "s  rate=" << (N/s)/1e6 << " M items/s\n";
  }
}
CPP
g++ -O3 -march=native -std=c++20 /tmp/bench_malloc.cpp -o /tmp/bench_malloc
/tmp/bench_malloc '"$MALLOC_N"' '"$MALLOC_ITERS"' '"$MALLOC_SZ"'
' | tee "$MLF"
fi

# ------------------------------------------------------------------------------
# Summary
# ------------------------------------------------------------------------------
log "Summary (p50 / p90 / best)"

# Policy: split numeric vs dates cleanly
if [[ -n "${POL:-}" && -s "$POL" ]]; then
  summarize_stream "M/s" "ts_bench_policy (numeric)" < <(rates_from_section "$POL" numeric)
  summarize_stream "M/s" "ts_bench_policy (dates)"   < <(rates_from_section "$POL" dates)
else
  echo "  - ts_bench_policy (numeric): (no data)" | tee -a "$OUT_DIR/_bench.log"
  echo "  - ts_bench_policy (dates):   (no data)" | tee -a "$OUT_DIR/_bench.log"
fi

if [[ -n "${ARENA:-}" && -s "$ARENA" ]]; then
  summarize_file "$ARENA" "M items/s" "ts_bench_arena_alloc"
else
  echo "  - ts_bench_arena_alloc: (no data)" | tee -a "$OUT_DIR/_bench.log"
fi

if [[ -n "${TOK:-}" && -s "$TOK" ]]; then
  summarize_file "$TOK" "M/s" "ts_bench_tokenizer"
else
  echo "  - ts_bench_tokenizer: (no data)" | tee -a "$OUT_DIR/_bench.log"
fi

if (( DO_BASELINES )); then
  if [[ -n "${FCH:-}" && -s "$FCH" ]]; then
    summarize_file "$FCH" "M/s" "baseline: from_chars"
  else
    echo "  - baseline: from_chars: (no data)" | tee -a "$OUT_DIR/_bench.log"
  fi
  if [[ -n "${STP:-}" && -s "$STP" ]]; then
    summarize_file "$STP" "M/s" "baseline: strptime"
  else
    echo "  - baseline: strptime: (no data)" | tee -a "$OUT_DIR/_bench.log"
  fi
  if [[ -n "${MLF:-}" && -s "$MLF" ]]; then
    summarize_file "$MLF" "M items/s" "baseline: malloc/free"
  else
    echo "  - baseline: malloc/free: (no data)" | tee -a "$OUT_DIR/_bench.log"
  fi
fi

echo
echo "Raw outputs in: $OUT_DIR"
