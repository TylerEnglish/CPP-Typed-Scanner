from __future__ import annotations
import argparse, os
from prefect import flow, get_run_logger
import docker

@flow(name="scan_file", log_prints=True)
def scan_file(bucket: str, key: str, event: str = "ObjectCreated", etag: str | None = None):
    """
    Launches a one-off scanner container to process a single object.
    Expects the scanner image to be available locally as 'typed-scanner:local'.
    """
    log = get_run_logger()
    client = docker.from_env()
    env = {
        "SCAN_BUCKET": bucket,
        "SCAN_KEY": key,
        "SCAN_EVENT": event,
        "SCAN_ETAG": etag or "",
        "TS_CONFIG": "/srv/configs/config.toml",
        "TS_PIPELINE": "/srv/configs/pipeline.toml",
        "TS_PORT": "8080",
    }
    # Pass artifacts volume; join Compose network via "network_mode"
    # Container exits after generating artifacts.
    log.info(f"Launching scanner for s3://{bucket}/{key} ({event})")
    out = client.containers.run(
        image="typed-scanner:local",
        remove=True,
        environment=env,
        network=os.getenv("PREFECT_DOCKER_NETWORK","typed-scanner_typed_net"),
        volumes={"/var/lib/docker/volumes/typed-scanner_artifacts/_data": {'bind': '/artifacts', 'mode': 'rw'}},
        detach=False,
    )
    log.info("scanner output: %s", out.decode("utf-8") if isinstance(out, (bytes,bytearray)) else out)

def make_deployment():
    """
    Creates/updates a deployment named 'typed-scanner' on work pool 'ts-docker'.
    Uses the 'python:3.12-slim' job image and installs extra packages at runtime.
    """
    from prefect import deploy
    from prefect.docker import DockerImage

    deploy(
        scan_file.to_deployment(
            name="typed-scanner",
            work_pool_name="ts-docker",
            parameters={"bucket":"incoming","key":"sample.csv","event":"manual"},
            job_variables={
                # Ensure docker SDK is available in the job container and connect to the host Docker
                "image": "python:3.12-slim",
                "env": {"EXTRA_PIP_PACKAGES": "docker requests"},
                "volumes": ["/var/run/docker.sock:/var/run/docker.sock"],
                "network": "typed-scanner_typed_net",
            },
        ),
        push=False,
    )

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--deploy", action="store_true")
    args = ap.parse_args()
    if args.deploy:
        make_deployment()
    else:
        scan_file("incoming","simple.csv","manual")
