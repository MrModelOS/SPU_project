#!/usr/bin/env python3
"""
sppu_client.py — Python client for SPPU Search HTTP API

Demonstrates integration with sppu_searchd (semantic search server).
Requires: pip install requests
"""

import json
import sys

try:
    import requests
except ImportError:
    print("Install requests: pip install requests")
    sys.exit(1)

BASE_URL = "http://localhost:8080"


def health():
    """Check server health."""
    r = requests.get(f"{BASE_URL}/health", timeout=5)
    return r.json()


def status():
    """Get database status."""
    r = requests.get(f"{BASE_URL}/status", timeout=5)
    return r.json()


def load_db(filepath):
    """Load a vector database file (.bin or .csv)."""
    r = requests.post(f"{BASE_URL}/load",
                      json={"file": filepath},
                      timeout=30)
    return r.json()


def search(vector, top_k=5):
    """Search for similar vectors."""
    r = requests.post(f"{BASE_URL}/search",
                      json={"vector": vector, "top_k": top_k},
                      timeout=10)
    return r.json()


def main():
    print(f"[*] Connecting to SPPU daemon at {BASE_URL}")

    # 1. Health check
    try:
        h = health()
        print(f"[+] Health: {h}")
    except requests.ConnectionError:
        print(f"[-] Cannot connect to {BASE_URL}")
        print("    Start daemon: ./examples/semantic_search/sppu_searchd --port 8080 --db vectors.bin")
        sys.exit(1)

    # 2. Load database
    db_path = sys.argv[1] if len(sys.argv) > 1 else "tools/test_vectors/small_db.csv"
    print(f"[*] Loading database: {db_path}")
    load_res = load_db(db_path)
    print(f"[+] Load: {load_res}")

    # 3. Search
    query = [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8]
    print(f"[*] Searching for vector: {query}")
    results = search(query, top_k=3)
    print(f"[+] Results:\n{json.dumps(results, indent=2)}")

    # 4. Status
    st = status()
    print(f"[+] Status: {st}")


if __name__ == "__main__":
    main()
