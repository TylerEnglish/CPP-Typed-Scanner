set -euo pipefail
IMAGE=${IMAGE:-typed-scanner:local}
docker build -f docker/Dockerfile -t "$IMAGE" .
docker run --rm -it \
  -p 8080:8080 \
  -e TS_CONFIG=/srv/configs/config.toml \
  -e TS_PIPELINE=/srv/configs/pipeline.toml \
  -v "$(pwd)/artifacts:/artifacts" \
  -v "$(pwd)/configs:/srv/configs:ro" \
  -v "$(pwd)/templates:/srv/templates:ro" \
  -v "$(pwd)/web:/srv/web:ro" \
  "$IMAGE"