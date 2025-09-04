set -euo pipefail

SCANNER_BIN="${SCANNER_BIN:-/opt/typed-scanner/bin/typed-scanner}"
PORT="${PORT:-8080}"
SCAN_INTERVAL="${SCAN_INTERVAL:-15}"

# Resolve artifact root (one place of truth)
if [[ -n "${ART_ROOT:-}" ]]; then
  :
elif [[ "${SCANNER_ARGS:-}" =~ --artifact-root=([^[:space:]]+) ]]; then
  ART_ROOT="${BASH_REMATCH[1]}"
else
  ART_ROOT="/artifacts/typed-scanner"
fi

SLUG_MODE="${SLUG_MODE:-basename}"
SLUG_LEN="${SLUG_LEN:-64}"

# Optional: push finished artifacts to a MinIO bucket as well
PUSH_TO_MINIO="${PUSH_TO_MINIO:-false}"               # "true" to enable
ARTIFACTS_BUCKET="${ARTIFACTS_BUCKET:-artifacts-typed-scanners}"

echo "[entrypoint] SCANNER_BIN=${SCANNER_BIN}"
echo "[entrypoint] ART_ROOT=${ART_ROOT}"
echo "[entrypoint] PORT=${PORT}"
echo "[entrypoint] SLUG_MODE=${SLUG_MODE}  SLUG_LEN=${SLUG_LEN}"
echo "[entrypoint] PUSH_TO_MINIO=${PUSH_TO_MINIO}  ARTIFACTS_BUCKET=${ARTIFACTS_BUCKET}"

[[ -x "$SCANNER_BIN" ]] || { echo "[entrypoint] ERR: missing $SCANNER_BIN"; exit 127; }

ensure_writable_art_root() {
  mkdir -p "${ART_ROOT}" 2>/dev/null || true
  if ! touch "${ART_ROOT}/.w"; then
    echo "[entrypoint] WARN: ${ART_ROOT} not writable; chmod 777 attempt…"
    chmod -R 0777 "${ART_ROOT}" 2>/dev/null || true
  fi
  if ! touch "${ART_ROOT}/.w"; then
    echo "[entrypoint] ERR: ${ART_ROOT} still not writable; falling back to /work/artifacts-fallback"
    ART_ROOT="/work/artifacts-fallback"
    mkdir -p "${ART_ROOT}"
  fi
}
ensure_writable_art_root

copy_assets() {
  local dst="$1"
  [[ -d "$dst" ]] || { echo "[assets] skip (no dir): $dst"; return 0; }
  for a in /work/web/css/report.css \
           /work/web/js/vega.min.js \
           /work/web/js/vega-lite.min.js \
           /work/web/js/vega-embed.min.js; do
    if [[ -s "$a" ]]; then
      cp -f "$a" "$dst/" 2>/dev/null || echo "[assets] WARN cp failed: $a -> $dst"
    else
      echo "[assets] missing or empty: $a"
    fi
  done
}

write_root_redirect_to() {
  local dst="$1"
  local rel="${dst#$ART_ROOT}"; [[ -z "$rel" ]] && rel="/"
  printf '<!doctype html><meta http-equiv="refresh" content="0; url=/reports%s/report.html">' "$rel" \
    > "${ART_ROOT}/index.html" || true
  echo "[post] index -> ${rel}/report.html"
}

push_dir_to_minio() {
  [[ "$PUSH_TO_MINIO" == "true" ]] || return 0
  [[ -n "${MINIO_ENDPOINT:-}" && -n "${MINIO_ROOT_USER:-}" && -n "${MINIO_ROOT_PASSWORD:-}" ]] || return 0
  mc mirror --overwrite "${ART_ROOT}" "local/${ARTIFACTS_BUCKET}" || true
}

# ---- run tests once; refuse to start if they fail ---------------------------
TEST_BIN_DIR="/opt/typed-scanner/bin"
TEST_LOG="/work/tests.log"

# Unit tests we expect by name
RUN_THESE=(
  ts_test_chunk_reader
  ts_test_csv_fsm
  ts_test_jsonl_tokenizer
  ts_test_parse_policy
  ts_test_record_view
  ts_test_run_json
)

# Auto-discover any integration tests that were installed (ts_it_*)
shopt -s nullglob
IT_FOUND=("${TEST_BIN_DIR}"/ts_it_*)
shopt -u nullglob
for p in "${IT_FOUND[@]}"; do
  [[ -x "$p" ]] && RUN_THESE+=("$(basename "$p")")
done

if [[ "${RUN_TESTS:-1}" != "0" ]]; then
  echo "[entrypoint] running tests (logs also in ${TEST_LOG})…"
  : > "${TEST_LOG}"

  # Some tests may need to exec the app
  export TS_SCANNER_BIN="${SCANNER_BIN}"

  # Show what we actually have in the image
  echo "[tests] discovered test bins:" | tee -a "${TEST_LOG}"
  (ls -1 "${TEST_BIN_DIR}"/ts_* 2>/dev/null || true) | sed 's/^/[tests]   /' | tee -a "${TEST_LOG}"

  failures=0
  for t in "${RUN_THESE[@]}"; do
    if [[ -x "${TEST_BIN_DIR}/${t}" ]]; then
      echo "[tests] running: ${TEST_BIN_DIR}/${t}" | tee -a "${TEST_LOG}"
      if "${TEST_BIN_DIR}/${t}" 2>&1 | tee -a "${TEST_LOG}"; then
        echo "[tests] PASS: ${t}" | tee -a "${TEST_LOG}"
      else
        echo "[tests] FAILED: ${t} (see ${TEST_LOG})"
        failures=$((failures+1))
        # Fail fast (remove this break if you want to run all)
        echo "[entrypoint] tests FAILED — not starting server. Tail of ${TEST_LOG}:"
        tail -n 200 "${TEST_LOG}"
        exit 1
      fi
    else
      echo "[tests] SKIP: ${t} (missing: ${TEST_BIN_DIR}/${t})" | tee -a "${TEST_LOG}"
    fi
  done

  echo "[tests] summary: failures=${failures}" | tee -a "${TEST_LOG}"
  if (( failures > 0 )); then
    echo "[entrypoint] tests FAILED — not starting server."
    exit 1
  fi
else
  echo "[entrypoint] tests skipped (RUN_TESTS=0)"
fi

# -------- start HTTP server --------
read -r -a ARGS <<< "${SCANNER_ARGS:-}"
has_root=false; for a in "${ARGS[@]:-}"; do [[ "$a" == --artifact-root=* ]] && has_root=true; done
has_port=false; for a in "${ARGS[@]:-}"; do [[ "$a" == --port=* ]] && has_port=true; done
$has_root || ARGS+=(--artifact-root="${ART_ROOT}")
$has_port || ARGS+=(--port="${PORT}")

echo "[entrypoint] starting server: ${SCANNER_BIN} ${ARGS[*]}"
"${SCANNER_BIN}" "${ARGS[@]}" &
SERVER_PID=$!

# -------- MinIO mirror + scan loop --------
if [[ -n "${MINIO_ENDPOINT:-}" && -n "${MINIO_BUCKET:-}" && -n "${MINIO_ROOT_USER:-}" && -n "${MINIO_ROOT_PASSWORD:-}" ]]; then
  echo "[entrypoint] MinIO mirror+scan -> ${MINIO_ENDPOINT}/${MINIO_BUCKET}"
  INCOMING_DIR="/work/incoming"
  mkdir -p "${INCOMING_DIR}"

  for i in {1..20}; do
    if mc alias set local "${MINIO_ENDPOINT}" "${MINIO_ROOT_USER}" "${MINIO_ROOT_PASSWORD}" >/dev/null 2>&1; then
      echo "[mc] alias 'local' configured"; break
    fi
    echo "[mc] alias set failed, retrying ($i/20)…"; sleep 1
  done

  if [[ "$PUSH_TO_MINIO" == "true" ]]; then
    mc mb --ignore-existing "local/${ARTIFACTS_BUCKET}" >/dev/null 2>&1 || true
  fi

  (
    while true; do
      echo "[mirror] syncing bucket -> ${INCOMING_DIR} ..."
      mc mirror --overwrite --remove "local/${MINIO_BUCKET}" "${INCOMING_DIR}" || true

      mapfile -d '' files < <(find "${INCOMING_DIR}" -type f \
        \( -iname '*.csv' -o -iname '*.jsonl' -o -iname '*.ndjson' \) -print0)
      echo "[scan] found ${#files[@]} candidate file(s)"

      for ((i=0; i<${#files[@]}; ++i)); do
        f="${files[$i]}"
        echo "[scan] ($((i+1))/${#files[@]}) $f"
        mark="${ART_ROOT}/.start.$(date +%s%N)"
        : > "$mark" || true

        if "${SCANNER_BIN}" \
              --config=/work/configs/config.toml \
              --artifact-root="${ART_ROOT}" \
              --slug-mode="${SLUG_MODE}" \
              --slug-len="${SLUG_LEN}" \
              --scan "$f"; then
          new_report="$(find "${ART_ROOT}" -type f -name 'report.html' -newer "$mark" -print | sort | tail -n1 || true)"
          rm -f "$mark" || true
          if [[ -n "$new_report" && -f "$new_report" ]]; then
            outdir="$(dirname "$new_report")"
            echo "[post] artifact: ${outdir}"
            copy_assets "${outdir}"
            write_root_redirect_to "${outdir}"
          else
            echo "[post] WARN: no new report found after scanning $f"
          fi
        else
          echo "[scan] ERROR scanning $f"
        fi
      done

      push_dir_to_minio
      sleep "${SCAN_INTERVAL}"
    done
  ) &
else
  echo "[entrypoint] MinIO variables not set; skipping mirror+scan loop."
fi

wait "${SERVER_PID}"
