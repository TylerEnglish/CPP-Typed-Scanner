from __future__ import annotations
from datetime import datetime, timedelta
import os, json
from airflow import DAG
from airflow.decorators import task
from airflow.providers.amazon.aws.hooks.s3 import S3Hook
import docker

BUCKET = os.environ.get("BUCKET","incoming")
PREFIXES = [""]  # adjust as needed
AWS_CONN_ID = "minio"  # provided via AIRFLOW_CONN_MINIO env
ARTIFACTS_VOL = "/var/lib/docker/volumes/typed-scanner_artifacts/_data"

default_args = {
    "owner": "airflow",
    "retries": 0,
}

with DAG(
    dag_id="typed_scanner",
    default_args=default_args,
    start_date=datetime(2025, 1, 1),
    schedule_interval="*/2 * * * *",
    catchup=False,
    tags=["typed-scanner"],
) as dag:

    @task
    def run_scanner_for_conf(**context):
        conf = context["dag_run"].conf or {}
        if not conf:
            return "no-conf"
        bucket, key, event = conf.get("bucket"), conf.get("key"), conf.get("event","manual")
        if not (bucket and key):
            return "missing-params"
        client = docker.from_env()
        env = {
            "SCAN_BUCKET": bucket,
            "SCAN_KEY": key,
            "SCAN_EVENT": event,
            "TS_CONFIG": "/srv/configs/config.toml",
            "TS_PIPELINE": "/srv/configs/pipeline.toml",
            "TS_PORT": "8080",
        }
        out = client.containers.run(
            image="typed-scanner:local",
            remove=True,
            environment=env,
            network="typed-scanner_typed_net",
            volumes={ARTIFACTS_VOL: {'bind': '/artifacts', 'mode': 'rw'}},
            detach=False,
        )
        return out.decode("utf-8") if isinstance(out, (bytes,bytearray)) else str(out)

    @task
    def poll_and_process():
        s3 = S3Hook(aws_conn_id=AWS_CONN_ID)
        client = docker.from_env()
        processed = 0
        for prefix in PREFIXES:
            keys = s3.list_keys(bucket_name=BUCKET, prefix=prefix) or []
            for key in keys:
                env = {
                    "SCAN_BUCKET": BUCKET,
                    "SCAN_KEY": key,
                    "SCAN_EVENT": "poll",
                    "TS_CONFIG": "/srv/configs/config.toml",
                    "TS_PIPELINE": "/srv/configs/pipeline.toml",
                    "TS_PORT": "8080",
                }
                client.containers.run(
                    image="typed-scanner:local",
                    remove=True,
                    environment=env,
                    network="typed-scanner_typed_net",
                    volumes={ARTIFACTS_VOL: {'bind': '/artifacts', 'mode': 'rw'}},
                    detach=False,
                )
                processed += 1
        return processed

    run_scanner_for_conf()
    poll_and_process()
