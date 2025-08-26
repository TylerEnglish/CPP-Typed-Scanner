import sys, json, random, time
n = int(sys.argv[1]) if len(sys.argv) > 1 else 100_000
for i in range(1, n+1):
    rec = {
        "id": i,
        "name": f"user_{i}",
        "val": round(random.random()*1000, 3),
        "active": i % 2 == 0,
        "ts": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(1_700_000_000 + i))
    }
    sys.stdout.write(json.dumps(rec, ensure_ascii=False) + "\n")
