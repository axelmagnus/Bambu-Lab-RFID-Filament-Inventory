#!/usr/bin/env python3
"""
Push the existing data/store_index.json to the Apps Script Web App.
Usage:
    python scripts/push_store_index.py

Relies on WEB_APP_URL in scripts/secret.env (same as scrape_store.py).
"""
import json
import os
import sys
from pathlib import Path
from typing import Any, Dict, List

import requests

ROOT = Path(__file__).resolve().parents[1]
SECRETS_ENV = ROOT / "scripts" / "secret.env"
STORE_INDEX_JSON = ROOT / "data" / "store_index.json"


def load_local_env(env_path: Path) -> None:
    """Load simple KEY=VALUE lines into os.environ if not already set."""
    if not env_path.exists():
        return
    for line in env_path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" not in line:
            continue
        key, val = line.split("=", 1)
        key = key.strip()
        val = val.strip().strip('"').strip("'")
        if key and key not in os.environ:
            os.environ[key] = val


def build_payload(records: List[Dict[str, Any]]) -> Dict[str, Any]:
    payload = {"action": "uploadStoreIndex", "records": []}
    for rec in records:
        payload["records"].append(
            {
                "code": rec.get("code") or "",
                "name": rec.get("name") or "",
                "color": rec.get("color") or "",
                "imageUrl": rec.get("imageUrl") or "",
                "productUrl": rec.get("productUrl") or "",
            }
        )
    return payload


def main() -> int:
    load_local_env(SECRETS_ENV)
    push_url = os.environ.get("WEB_APP_URL")
    if not push_url:
        print("ERROR: WEB_APP_URL is not set. Populate scripts/secret.env.", file=sys.stderr)
        return 1
    if not STORE_INDEX_JSON.exists():
        print(f"ERROR: {STORE_INDEX_JSON} not found. Run scrape_store.py first.", file=sys.stderr)
        return 1

    try:
        records = json.loads(STORE_INDEX_JSON.read_text(encoding="utf-8"))
    except Exception as exc:  # noqa: BLE001
        print(f"ERROR: failed to read {STORE_INDEX_JSON}: {exc}", file=sys.stderr)
        return 1

    if not isinstance(records, list):
        print(f"ERROR: {STORE_INDEX_JSON} is not a JSON array", file=sys.stderr)
        return 1

    payload = build_payload(records)
    try:
        resp = requests.post(push_url, json=payload, timeout=30)
        resp.raise_for_status()
        print(f"Pushed {len(records)} records to Store Index via {push_url}")
        return 0
    except Exception as exc:  # noqa: BLE001
        status = getattr(resp, "status_code", "?") if 'resp' in locals() else "?"
        text = getattr(resp, "text", "") if 'resp' in locals() else ""
        print(f"ERROR: push failed (status {status}): {exc}\n{text}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
