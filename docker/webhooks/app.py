from __future__ import annotations

import json
import os
from typing import Any, Dict

import requests
from flask import Flask, request, jsonify

app = Flask(__name__)

WEBHOOK_AUTH_TOKEN = os.getenv("WEBHOOK_AUTH_TOKEN", "")
PREFECT_API = os.getenv("PREFECT_API", "")
PREFECT_DEPLOYMENT_ID = os.getenv("PREFECT_DEPLOYMENT_ID", "")
AIRFLOW_BASE_URL = os.getenv("AIRFLOW_BASE_URL", "")
AIRFLOW_DAG_ID = os.getenv("AIRFLOW_DAG_ID", "typed_scanner")

def _auth_ok(req) -> bool:
    """Optional shared-secret check from MinIO -> this service."""
    if not WEBHOOK_AUTH_TOKEN:
        return True
    return req.headers.get("Authorization") == f"Bearer {WEBHOOK_AUTH_TOKEN}"

@app.get("/health")
def health():
    return jsonify({"ok": True})

@app.post("/minio")
def minio_events():
    if not _auth_ok(request):
        return jsonify({"ok": False, "error": "unauthorized"}), 401

    try:
        payload: Dict[str, Any] = request.get_json(force=True, silent=False)
    except Exception:
        return jsonify({"ok": False, "error": "invalid json"}), 400

    # MinIO sends S3-compatible records. We log & optionally forward.
    records = payload.get("Records", [])
    app.logger.info("Received %d event(s) from MinIO", len(records))
    app.logger.debug(json.dumps(payload, indent=2))

    # Pull out minimal info (bucket, key, event)
    extracted = []
    for r in records:
        s3 = r.get("s3", {})
        bucket = s3.get("bucket", {}).get("name")
        # key may be URL-encoded per S3 semantics; leave as-is for now
        key = s3.get("object", {}).get("key")
        event = r.get("eventName")
        if bucket and key:
            extracted.append({"bucket": bucket, "key": key, "event": event})

    # Optional: forward to Prefect Deployment trigger (if configured)
    if PREFECT_API and PREFECT_DEPLOYMENT_ID and extracted:
        try:
            # Prefect 3 "create flow run" endpoint (deployment based)
            # (Keep this as a stub; adjust path/token as needed in your env)
            for item in extracted:
                body = {"parameters": item}
                requests.post(
                    f"{PREFECT_API}/deployments/{PREFECT_DEPLOYMENT_ID}/create_flow_run",
                    json=body, timeout=5
                )
        except Exception as e:
            app.logger.warning("Prefect forward failed: %s", e)

    # Optional: forward to Airflow DAG run API (if configured)
    if AIRFLOW_BASE_URL and AIRFLOW_DAG_ID and extracted:
        try:
            for item in extracted:
                body = {"conf": item}
                # Airflow Stable REST: POST /api/v1/dags/{dag_id}/dagRuns
                requests.post(
                    f"{AIRFLOW_BASE_URL}/api/v1/dags/{AIRFLOW_DAG_ID}/dagRuns",
                    json=body, timeout=5
                )
        except Exception as e:
            app.logger.warning("Airflow forward failed: %s", e)

    return jsonify({"ok": True, "events": extracted})
