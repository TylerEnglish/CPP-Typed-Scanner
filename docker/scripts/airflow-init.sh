set -euo pipefail

pip install --user -r /requirements.txt
airflow db migrate
airflow users create \
  --role Admin \
  --username "${AIRFLOW_UI_USER}" \
  --password "${AIRFLOW_UI_PASSWORD}" \
  --firstname A --lastname F --email a@b.c || true
echo "Airflow DB ready; admin user ensured."