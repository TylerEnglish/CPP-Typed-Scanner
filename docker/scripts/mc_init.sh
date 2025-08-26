set -eu

# Wait for MinIO API to be reachable with these creds
i=0
while [ $i -lt 30 ]; do
  if mc alias set local http://minio:9000 "$MINIO_ROOT_USER" "$MINIO_ROOT_PASSWORD" >/dev/null 2>&1 && \
     mc ls local >/dev/null 2>&1 ; then
    break
  fi
  i=$((i+1))
  sleep 2
done

# Create bucket
mc mb --ignore-existing "local/${BUCKET}"

# Wire bucket events to the webhook target named "primary"
# (minio itself is configured via MINIO_NOTIFY_WEBHOOK_* envs)
mc event add "local/${BUCKET}" arn:minio:sqs::primary:webhook --event put,delete || true
mc event list "local/${BUCKET}"
echo "MinIO bucket '${BUCKET}' created and events configured."
