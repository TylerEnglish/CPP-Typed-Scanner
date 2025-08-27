set -euo pipefail

for i in {1..30}; do
  if airflow version >/dev/null 2>&1; then
    break
  fi
  sleep 1
done

airflow db migrate

airflow users create \
  --role Admin \
  --username admin \
  --password admin \
  --firstname Dev \
  --lastname Admin \
  --email admin@example.com \
  >/dev/null 2>&1 || true

airflow connections delete minio_s3 >/dev/null 2>&1 || true
airflow connections add minio_s3 \
  --conn-type aws \
  --conn-login "${MINIO_ROOT_USER}" \
  --conn-password "${MINIO_ROOT_PASSWORD}" \
  --conn-extra '{"host":"http://minio:9000","region_name":"us-east-1"}'