from datetime import datetime, timedelta
from airflow import DAG
from airflow.providers.amazon.aws.sensors.s3 import S3KeySensor
from airflow.operators.empty import EmptyOperator

BUCKET = "incoming"  # must match .env/config.toml

with DAG(
    dag_id="typed_scanner",
    start_date=datetime(2024, 1, 1),
    schedule=None,
    catchup=False,
    default_args={"retries": 0, "owner": "airflow"},
    dagrun_timeout=timedelta(minutes=30),
    tags=["typed-scanner"],
) as dag:
    wait_for_object = S3KeySensor(
        task_id="wait_for_object",
        bucket_key="*",
        bucket_name=BUCKET,
        wildcard_match=True,
        aws_conn_id="aws_default",  # provided via env in compose
        poke_interval=10,
        timeout=60*60,
        soft_fail=False,
    )

    # replace with a KubernetesPodOperator, DockerOperator, or HTTP trigger to your scanner if you want
    done = EmptyOperator(task_id="done")

    wait_for_object >> done
