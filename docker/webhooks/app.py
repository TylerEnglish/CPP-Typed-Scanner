from fastapi import FastAPI, Request, Header, HTTPException
import os, requests

AUTH_TOKEN = os.getenv("AUTH_TOKEN", "secret123")

# Prefect
PREFECT_API_URL = os.getenv("PREFECT_API_URL", "http://prefect-server:4200/api")
FLOW_NAME = os.getenv("PREFECT_FLOW_NAME", "scan_file")
DEPLOYMENT_NAME = os.getenv("PREFECT_DEPLOYMENT_NAME", "typed-scanner")

# Airflow
AIRFLOW_API_URL = os.getenv("AIRFLOW_API_URL", "http://airflow-webserver:8080/api/v1")
AIRFLOW_USER = os.getenv("AIRFLOW_USER", "airflow")
AIRFLOW_PASSWORD = os.getenv("AIRFLOW_PASSWORD", "airflow")

app = FastAPI()

@app.get("/")
def root():
    return {"service": "typed-scanner-webhook", "endpoints": ["/health", "/ingest"]}

@app.get("/health")
def health():
    return {"ok": True}

def trigger_prefect(bucket, key, event, etag=None):
    r = requests.post(
        f"{PREFECT_API_URL}/deployments/filter",
        json={"deployments": {"name": {"any_": [DEPLOYMENT_NAME]}}},
        timeout=10,
    )
    r.raise_for_status()
    data = r.json()
    items = data.get("data", []) if isinstance(data, dict) else data
    if not items:
        print("Prefect deployment not found")
        return
    deployment_id = items[0]["id"]
    payload = {
        "parameters": {"bucket": bucket, "key": key, "event": event, "etag": etag},
        "name": f"minio-{key}",
    }
    cr = requests.post(
        f"{PREFECT_API_URL}/deployments/{deployment_id}/create_flow_run",
        json=payload,
        timeout=10,
    )
    cr.raise_for_status()
    print("Triggered Prefect run:", cr.json())

def trigger_airflow(bucket, key, event, etag=None):
    data = {"conf": {"bucket": bucket, "key": key, "event": event, "etag": etag}}
    ar = requests.post(
        f"{AIRFLOW_API_URL}/dags/typed_scanner/dagRuns",
        auth=(AIRFLOW_USER, AIRFLOW_PASSWORD),
        json=data,
        timeout=10,
    )
    if ar.status_code >= 400:
        print("Airflow trigger failed", ar.text)
    else:
        print("Triggered Airflow DAG:", ar.json())

@app.post("/ingest")
async def ingest(request: Request, authorization: str | None = Header(default=None)):
    if AUTH_TOKEN and (authorization or "").replace("Bearer ", "") != AUTH_TOKEN:
        raise HTTPException(status_code=401, detail="bad token")
    body = await request.json()
    records = body.get("Records") or []
    for rec in records:
        s3 = rec.get("s3", {})
        bucket = (s3.get("bucket") or {}).get("name")
        obj = s3.get("object") or {}
        key = obj.get("key")
        etag = obj.get("eTag")
        event = rec.get("eventName")
        if bucket and key:
            trigger_prefect(bucket, key, event, etag)
            trigger_airflow(bucket, key, event, etag)
    return {"ok": True, "count": len(records)}
