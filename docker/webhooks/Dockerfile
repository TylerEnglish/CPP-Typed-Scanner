FROM python:3.11-slim

WORKDIR /app
ENV PYTHONDONTWRITEBYTECODE=1
ENV PYTHONUNBUFFERED=1

RUN pip install --no-cache-dir flask==3.0.3 requests==2.32.3 gunicorn==22.0.0

COPY docker/webhooks/app.py /app/app.py

EXPOSE 5000
HEALTHCHECK --interval=10s --timeout=3s --start-period=5s --retries=6 \
  CMD curl -fsS http://127.0.0.1:5000/health || exit 1

CMD ["gunicorn", "-w", "2", "-b", "0.0.0.0:5000", "app:app"]
