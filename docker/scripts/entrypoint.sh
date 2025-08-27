set -euo pipefail

SCANNER_BIN="${SCANNER_BIN:-/opt/typed-scanner/bin/typed-scanner}"
PORT="${PORT:-8080}"
SCAN_INTERVAL="${SCAN_INTERVAL:-15}"

# One artifact root for both the server and CLI scans
if [[ -n "${ART_ROOT:-}" ]]; then
  :
elif [[ "${SCANNER_ARGS:-}" =~ --artifact-root=([^[:space:]]+) ]]; then
  ART_ROOT="${BASH_REMATCH[1]}"
else
  ART_ROOT="/artifacts/typed-scanner"
fi

# Force basename slugs and make them long enough to keep the extension (no truncation artifacts)
SLUG_MODE="${SLUG_MODE:-basename}"
SLUG_LEN="${SLUG_LEN:-64}"

echo "[entrypoint] SCANNER_BIN=${SCANNER_BIN}"
echo "[entrypoint] ART_ROOT=${ART_ROOT}"
echo "[entrypoint] PORT=${PORT}"
echo "[entrypoint] SLUG_MODE=${SLUG_MODE} SLUG_LEN=${SLUG_LEN}"

if [[ ! -x "${SCANNER_BIN}" ]]; then
  echo "[entrypoint] ERROR: scanner binary not found: ${SCANNER_BIN}" >&2
  exit 127
fi

copy_assets() {
  local dst="$1"
  mkdir -p "$dst"
  for a in /work/web/css/report.css \
           /work/web/js/vega.min.js \
           /work/web/js/vega-lite.min.js \
           /work/web/js/vega-embed.min.js; do
    if [[ -s "$a" ]]; then
      # overwrite if the copy on disk was the previous zero-length version
      cp -f "$a" "$dst"/
    else
      echo "[assets] WARNING: missing or empty asset: $a"
    fi
  done
}

slug_for_file() {
  # This mirrors basename+truncate behavior used by the scanner when SLUG_MODE=basename.
  local f="$1"
  local base
  base="$(basename "$f")"
  if [[ "$SLUG_LEN" -gt 0 ]]; then
    echo "${base:0:$SLUG_LEN}"
  else
    echo "$base"
  fi
}

write_root_redirect_to() {
  local dst="$1"
  local rel="${dst#$ART_ROOT}"
  [[ -z "$rel" ]] && rel="/"
  printf '<!doctype html><meta http-equiv="refresh" content="0; url=%s/report.html">' "$rel" > "${ART_ROOT}/index.html"
  echo "[post] wrote ${ART_ROOT}/index.html -> ${rel}/report.html"
}

# --- 1) Start server (serve ART_ROOT on :PORT) ---
read -r -a ARGS <<< "${SCANNER_ARGS:-}"
# Ensure args include our ART_ROOT and PORT
has_root=false; for a in "${ARGS[@]:-}"; do [[ "$a" == --artifact-root=* ]] && has_root=true; done
has_port=false; for a in "${ARGS[@]:-}"; do [[ "$a" == --port=* ]] && has_port=true; done
$has_root || ARGS+=(--artifact-root="${ART_ROOT}")
$has_port || ARGS+=(--port="${PORT}")

echo "[entrypoint] starting server: ${SCANNER_BIN} ${ARGS[*]}"
"${SCANNER_BIN}" "${ARGS[@]}" &
SERVER_PID=$!

# --- 2) Mirror MinIO and scan all new files ---
if [[ -n "${MINIO_ENDPOINT:-}" && -n "${MINIO_BUCKET:-}" && -n "${MINIO_ROOT_USER:-}" && -n "${MINIO_ROOT_PASSWORD:-}" ]]; then
  echo "[entrypoint] MinIO mirror+scan -> ${MINIO_ENDPOINT}/${MINIO_BUCKET}"
  INCOMING_DIR="/work/incoming"
  mkdir -p "${INCOMING_DIR}"

  # Configure mc alias (retry while MinIO warms up)
  for i in {1..20}; do
    if mc alias set local "${MINIO_ENDPOINT}" "${MINIO_ROOT_USER}" "${MINIO_ROOT_PASSWORD}" >/dev/null 2>&1; then
      echo "[mc] alias 'local' configured"
      break
    fi
    echo "[mc] alias set failed, retrying ($i/20) ..."
    sleep 1
  done

  (
    while true; do
      echo "[mirror] syncing bucket -> ${INCOMING_DIR} ..."
      mc mirror --overwrite --remove "local/${MINIO_BUCKET}" "${INCOMING_DIR}" || true

      # Read candidates into a Bash array (NUL-safe)
      files=()
      while IFS= read -r -d '' f; do files+=("$f"); done < <(
        find "${INCOMING_DIR}" -type f \
          \( -iname '*.csv' -o -iname '*.jsonl' -o -iname '*.ndjson' \) -print0
      )

      echo "[scan] found ${#files[@]} candidate file(s)"
      idx=0
      last_dir=""
      for f in "${files[@]}"; do
        idx=$((idx+1))
        slug="$(slug_for_file "$f")"
        outdir="${ART_ROOT}/${slug}"
        echo "[scan] (${idx}/${#files[@]}) $f -> slug=${slug}"

        if ! "${SCANNER_BIN}" \
              --config=/work/configs/config.toml \
              --artifact-root="${ART_ROOT}" \
              --slug-mode="${SLUG_MODE}" \
              --slug-len="${SLUG_LEN}" \
              --scan "$f"; then
          echo "[scan] ERROR scanning $f" >&2
          continue
        fi

        # Copy assets beside the generated report
        copy_assets "${outdir}"
        last_dir="${outdir}"
      done

      # Redirect index to the most recently processed artifact this round
      if [[ -n "${last_dir}" ]]; then
        write_root_redirect_to "${last_dir}"
      fi

      sleep "${SCAN_INTERVAL}"
    done
  ) &
else
  echo "[entrypoint] MinIO variables not set; skipping mirror+scan loop."
fi

wait "${SERVER_PID}"
