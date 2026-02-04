#!/usr/bin/env python3
"""
Scrape the Bambu US filament storefront for code/name/color/material/variantId/imageUrl.
Outputs:
- data/store_index.json (array of objects)
- data/store_index.csv (Code,Name,Color,Material,VariantId,ImageUrl)

Usage:
    python scripts/scrape_store.py
    STORE_BASE=https://us.store.bambulab.com python scripts/scrape_store.py
"""
import csv
import json
import os
import re
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional
from urllib.parse import urlparse, urlunparse

import requests
from bs4 import BeautifulSoup

ROOT = Path(__file__).resolve().parents[1]
OUT_JSON = ROOT / "data" / "store_index.json"
OUT_CSV = ROOT / "data" / "store_index.csv"
OUT_TSV = ROOT / "data" / "store_index.tsv"
ARDUINO_SNIPPETS = [
    ROOT / "arduino" / "RFID_Bambu_lab_reader" / "generated" / "materials_snippet.h",
    ROOT / "arduino" / "RFID_Bambu_lab_reader_OLED" / "generated" / "materials_snippet.h",
]
SECRETS_ENV = ROOT / "scripts" / "secret.env"


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


load_local_env(SECRETS_ENV)
BASE_STORE = os.environ.get("STORE_BASE", "https://us.store.bambulab.com")
COLLECTION_PATH = "/collections/bambu-lab-3d-printer-filament"
PUSH_URL = os.environ.get("WEB_APP_URL")


def normalize_product_url(url: Optional[str]) -> str:
    """Ensure productUrl uses BASE_STORE host; handle relative paths gracefully."""
    if not url:
        return ""
    base = urlparse(BASE_STORE)
    parsed = urlparse(url)
    if not parsed.netloc:
        # Relative or path-only
        return f"{BASE_STORE.rstrip('/')}/{url.lstrip('/')}"
    return urlunparse((base.scheme or parsed.scheme or "https", base.netloc, parsed.path, parsed.params, parsed.query, parsed.fragment))


@dataclass
class Product:
    name: str
    slug: str
    color_list: List[dict]
    media_files: List[str]
    product_url: str


@dataclass
class ColorOption:
    color: str
    code: str
    index: int


def fetch(url: str, retries: int = 3, backoff: float = 1.5) -> str:
    """HTTP GET with basic backoff; retries on 429/5xx to soften rate limits."""
    for attempt in range(retries):
        resp = requests.get(url, timeout=30, headers={"User-Agent": "Mozilla/5.0"})
        if resp.status_code in (429, 500, 502, 503, 504):
            if attempt + 1 == retries:
                resp.raise_for_status()
            sleep_time = backoff ** attempt
            time.sleep(sleep_time)
            continue
        resp.raise_for_status()
        return resp.text
    resp.raise_for_status()
    return resp.text


def parse_product_list(html: str) -> List[Product]:
    idx = html.find("productList")
    if idx == -1:
        raise RuntimeError("productList not found in collection page")
    start = html.find("[", idx)
    level = 0
    in_str = False
    esc = False
    end = None
    for pos, ch in enumerate(html[start:], start):
        if esc:
            esc = False
            continue
        if ch == "\\":
            esc = True
            continue
        if ch == '"':
            in_str = not in_str
            continue
        if in_str:
            continue
        if ch == "[":
            level += 1
        elif ch == "]":
            level -= 1
            if level == 0:
                end = pos
                break
    if end is None:
        raise RuntimeError("could not bracket-match productList array")
    arr = html[start : end + 1]
    data = json.loads(bytes(arr, "utf-8").decode("unicode_escape"))
    products: List[Product] = []
    for item in data:
        slug = item.get("seoCode", "")
        products.append(
            Product(
                name=item.get("name", ""),
                slug=slug,
                color_list=sorted(item.get("colorList", []), key=lambda c: c.get("colorPosition", 0)),
                media_files=item.get("mediaFiles", []) or [],
                product_url=f"{BASE_STORE}/products/{slug}" if slug else "",
            )
        )
    return products


def parse_colors_from_page(html: str) -> List[ColorOption]:
    soup = BeautifulSoup(html, "html.parser")
    opts: List[ColorOption] = []
    idx = 0
    for li in soup.find_all("li"):
        val = li.get("value")
        if not val:
            continue
        m = re.match(r"^(.*) \((\d{5})\)$", val.strip())
        if not m:
            continue
        opts.append(ColorOption(color=m.group(1).strip(), code=m.group(2), index=idx))
        idx += 1
    return opts


def guess_material(name: str, slug: str) -> str:
    target = slug.lower() or name.lower()
    if "pla" in target:
        return "PLA"
    if "pet-cf" in target or "petcf" in target:
        return "PET-CF"
    if "petg" in target:
        return "PETG"
    if "paht" in target:
        return "PAHT"
    if "abs" in target:
        return "ABS"
    if "asa" in target:
        return "ASA"
    if "tpu" in target:
        return "TPU"
    if "pc" in target:
        return "PC"
    return name.split(" ")[0] if name else ""


def build_records(products: Iterable[Product]) -> List[dict]:
    records: List[dict] = []
    for product in products:
        if not product.slug:
            continue
        url = normalize_product_url(product.product_url) or f"{BASE_STORE}/products/{product.slug}"
        try:
            page_html = fetch(url)
            time.sleep(0.25)
        except Exception as exc:  # noqa: BLE001
            print(f"WARN: failed to fetch product page {url}: {exc}", file=sys.stderr)
            continue
        options = parse_colors_from_page(page_html)
        if not options:
            print(f"WARN: no color options found in {url}", file=sys.stderr)
            continue
        # Align options with colorList order by index.
        color_entries = product.color_list
        if len(color_entries) != len(options):
            print(
                f"WARN: color count mismatch for {product.name} ({len(options)} options vs {len(color_entries)} feed)",
                file=sys.stderr,
            )
        # Pair by position; fallback to product-level media if missing.
        for pos, opt in enumerate(options):
            color_data = color_entries[pos] if pos < len(color_entries) else {}
            media_files = color_data.get("mediaFiles") or product.media_files
            image_url = media_files[0] if media_files else None
            variant_id = color_data.get("propertyValueId")
            normalized_base = normalize_product_url(product.product_url)
            # Shopify-style variant selection uses the `variant` query param; `id` can be ignored by the store.
            variant_url = f"{normalized_base}?variant={variant_id}" if normalized_base and variant_id else normalized_base
            records.append(
                {
                    "code": opt.code,
                    "name": product.name,
                    "color": opt.color,
                    "material": guess_material(product.name, product.slug),
                    "variantId": variant_id,
                    "imageUrl": image_url,
                    "productUrl": normalize_product_url(variant_url),
                }
            )
    return records


def write_json(records: List[dict]) -> None:
    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    with OUT_JSON.open("w", encoding="utf-8") as fh:
        json.dump(records, fh, ensure_ascii=False, indent=2)


def write_csv(records: List[dict]) -> None:
    """Write CSV with proper CSV escaping; four data columns only."""
    OUT_CSV.parent.mkdir(parents=True, exist_ok=True)
    headers = ["Code", "Name", "Color", "ImageUrl"]
    with OUT_CSV.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.writer(fh, delimiter=",", quotechar="\"", quoting=csv.QUOTE_MINIMAL, lineterminator="\n")
        writer.writerow(headers)
        for rec in records:
            image_url = rec.get("imageUrl") or ""
            product_url = rec.get("productUrl") or ""
            code_val = rec.get("code") or ""
            code_cell = f'=HYPERLINK("{product_url}";"{code_val}")' if product_url else code_val
            writer.writerow([code_cell, rec.get("name") or "", rec.get("color") or "", image_url])


def write_tsv(records: List[dict]) -> None:
    """Write TSV with minimal quoting; four data columns only."""
    OUT_TSV.parent.mkdir(parents=True, exist_ok=True)
    headers = ["Code", "Name", "Color", "ImageUrl"]
    with OUT_TSV.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.writer(fh, delimiter="\t", quotechar="\"", quoting=csv.QUOTE_MINIMAL, lineterminator="\n")
        writer.writerow(headers)
        for rec in records:
            image_url = rec.get("imageUrl") or ""
            product_url = rec.get("productUrl") or ""
            code_val = rec.get("code") or ""
            code_cell = f'=HYPERLINK("{product_url}";"{code_val}")' if product_url else code_val
            writer.writerow([code_cell, rec.get("name") or "", rec.get("color") or "", image_url])


def write_arduino_snippet(records: List[dict]) -> None:
    """Emit generated/materials_snippet.h for both Arduino sketches from scraped data."""

    def esc(val: Optional[str]) -> str:
        if not val:
            return ""
        # Keep ASCII only to avoid surprises in the sketch sources.
        return (
            str(val)
            .encode("ascii", "ignore")
            .decode("ascii")
            .replace("\\", "\\\\")
            .replace("\"", "\\\"")
        )

    lines = [
        "// Generated by scripts/scrape_store.py (store scrape).",
        "// materialId not scraped; left blank. variantId comes from store feed when present.",
    ]
    # Sort deterministically by code then color for readable diffs.
    for rec in sorted(records, key=lambda r: (r.get("code") or "", r.get("color") or "")):
        code = esc(rec.get("code"))
        name = esc(rec.get("name"))
        color = esc(rec.get("color"))
        variant = esc(rec.get("variantId"))
        line = f'    {"{\"\", \"{variant}\", \"{code}\", \"{name}\", \"{color}\"}"},'
        lines.append(line)

    for path in ARDUINO_SNIPPETS:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"Wrote Arduino snippet: {path.relative_to(ROOT)}")


def push_store_index(records: List[dict]) -> None:
    """Send the scraped records directly to the Apps Script webhook to populate Store Index."""
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
    try:
        resp = requests.post(PUSH_URL, json=payload, timeout=30)
        resp.raise_for_status()
        print(f"Pushed {len(records)} records to Store Index via webhook")
    except Exception as exc:  # noqa: BLE001
        print(f"WARN: failed to push Store Index to webhook: {exc}", file=sys.stderr)


def main() -> int:
    collection_url = f"{BASE_STORE}{COLLECTION_PATH}"
    html = fetch(collection_url)
    products = parse_product_list(html)
    records = build_records(products)
    write_json(records)
    write_csv(records)
    write_tsv(records)
    write_arduino_snippet(records)
    if PUSH_URL:
        push_store_index(records)
    print(f"Wrote {len(records)} records to {OUT_JSON}, {OUT_CSV}, and {OUT_TSV}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
