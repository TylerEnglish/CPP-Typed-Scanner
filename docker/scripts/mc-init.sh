set -eu

echo "Waiting for MinIO to be ready..."
for i in $(seq 1 30); do
  if nc -z minio 9000 >/dev/null 2>&1; then
    break
  fi
  sleep 1
done

MC_ALIAS="local"
MC_HOST="http://minio:9000"

echo "Configuring mc alias..."
mc alias set "${MC_ALIAS}" "${MC_HOST}" "${MINIO_ROOT_USER}" "${MINIO_ROOT_PASSWORD}"

echo "Creating bucket '${MINIO_BUCKET}' (if not exists)..."
mc mb --ignore-existing "${MC_ALIAS}/${MINIO_BUCKET}"

echo "Attaching webhook notifications to bucket '${MINIO_BUCKET}'..."
mc event add "${MC_ALIAS}/${MINIO_BUCKET}" arn:minio:sqs::events:webhook --event put,delete

echo "Bucket + events ready."