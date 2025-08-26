import hashlib, sys, re

def slugify(key: str, mode="hashprefix", length=8) -> str:
    if mode == "basename":
        base = key.rsplit("/", 1)[-1]
        return re.sub(r"[^a-zA-Z0-9_.-]+", "-", base)
    if mode == "keypath":
        return re.sub(r"[^a-zA-Z0-9_.-/]+", "-", key)
    # default: hashprefix(key)
    return hashlib.sha256(key.encode("utf-8")).hexdigest()[:length]

if __name__ == "__main__":
    print(slugify(sys.argv[1] if len(sys.argv) > 1 else "incoming/simple.csv"))
