FROM python:3.12-slim
WORKDIR /app
RUN pip install fastapi uvicorn requests
COPY app.py /app/app.py
EXPOSE 9005
ENV PORT=9005
CMD ["uvicorn","app:app","--host","0.0.0.0","--port","9005"]
