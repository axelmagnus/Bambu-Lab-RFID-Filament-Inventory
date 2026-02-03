# Bambu Lab RFID Filament Inventory

Apps Script Web App to log RFID filament scans into Google Sheets. The ESP8266/RC522 posts the filament code plus tray/chip UID; the script enriches from a `Store Index` tab (name/color/material/variant/imageUrl) and writes an `=IMAGE(url)` formula into the Image column. It uses only `SpreadsheetApp`—no external fetches. There is also a script to scrape the Bambu Store.

## Features
- Appends to the first empty row (fills gaps) on the `Inventory` tab.
- Writes timestamp, filament code, type (name/material), color, image, tray/chip UID.
- Status probe (`{"action":"status"}`) reports sheet connectivity and a sample of recent rows for quick debugging.
- Deduplicates by tray UID (or chip UID if tray missing). Matching rows are updated in place with fresh data and timestamp, returning `duplicate:true`.

## Setup (Apps Script)
1) Create a new Google Sheet with two tabs: rename the first tab to `Inventory` and add a second tab named `Store Index`.
2) Open the Apps Script editor: in the Sheet, click `Extensions → Apps Script`. In the left file tree, delete any starter files. Add a file named `code.gs` and paste [src/code.gs](src/code.gs). Click the gear icon (Project Settings) and toggle on “Show "appsscript.json" manifest file”; a file named `appsscript.json` will appear—open it and replace its contents with [appsscript.json](appsscript.json).
3) Set Script Properties (done inside the Apps Script editor): click the gear icon (Project Settings) → `Script properties` → `Add script property`. Add `SHEET_ID=<your sheet id>` (required, keep private) — find it in your sheet URL `https://docs.google.com/spreadsheets/d/<SHEET_ID>/edit`. If your `Inventory` tab is renamed, change `DEFAULT_SHEET_NAME` in code.gs.
4) Ensure the `Store Index` tab has headers `Code`, `Name`, `Color`, `ImageUrl`, `Image` in row 1. (Populating options are in “Populate Store Index” below.)
5) Deploy the web app: click `Deploy → New deployment` → `Select type: Web app`. Set `Execute as: Me` and `Who has access: Anyone`. Click Deploy, authorize when prompted, then copy the Web App URL ending in `/exec`; set this as `WEB_APP_URL` in your local `arduino/secrets.h`, where you also set the SSID and password.

## Testing
- Status check:
  - `curl -s -X POST "$WEB_APP_URL" -H "Content-Type: application/json" -d '{"action":"status"}'`
- Append a scan (uses first empty row, dedupe by uid):
  - `curl -s -X POST "$WEB_APP_URL" -H "Content-Type: application/json" -d '{"code":"10503","uid":"DEADBEEF"}'`
Response should be `{"ok":true,"duplicate":false}`; a repeat with the same uid returns `duplicate:true` and updates that row with a fresh timestamp/payload instead of adding a new row.

## Populate Store Index

- Quick, no-scrape option (recommended): use the bundled [data/store_index.json](data/store_index.json).
  - Fastest: run `python scripts/push_store_index.py` after setting `WEB_APP_URL` in `scripts/secret.env`; it uploads the bundled JSON (`data/store_index.json`) via `action:"uploadStoreIndex"`.

- Scrape-and-push option: set `WEB_APP_URL=<your Web App URL /exec>` (same URL used for scans) in `scripts/secret.env`, then run `python scripts/scrape_store.py`. It scrapes the store in current state, writes `data/store_index.{json,csv,tsv}`, and, if `WEB_APP_URL` is set, POSTs records to that same Web App using `action:"uploadStoreIndex"` (same endpoint, different action field). Run sparingly and respect store rate limits to avoid hammering the site. The bundled JSON already covers most filaments; if a new one appears, you can add it manually to the JSON/CSV/TSV and import without re-scraping.
- Manual POST example (same Web App URL):
    ```bash
    WEB_APP_URL="https://script.google.com/macros/s/<WEB_APP_ID>/exec"
    curl -X POST "$WEB_APP_URL" \
      -H "Content-Type: application/json" \
      -d '{
            "action": "uploadStoreIndex",
            "records": [
              { "code": "10503", "name": "PLA Basic", "color": "Bright Green", "imageUrl": "https://..." }
            ]
          }'
    ```

- Manual import: use the generated TSV/CSV if you prefer (File → Import into `Store Index`, in Google Sheets). The script still writes row-level IMAGE formulas when handling scans.

### Scraper environment variables
- `WEB_APP_URL`: your Apps Script Web App URL (`/exec`, same one the scanner uses). When set, the scraper posts scraped records directly to the `Store Index` tab using `action:"uploadStoreIndex"`.
- `STORE_BASE`: base store URL; defaults to `https://us.store.bambulab.com`. Set to `https://eu.store.bambulab.com` for EU.

Quick setup: edit `scripts/secret.env`, set `WEB_APP_URL=<your Web App URL /exec>`, and run the scraper. The script auto-loads `scripts/secret.env` if present. Shell alternative: `export WEB_APP_URL=...` then run the scraper. Set the same `WEB_APP_URL` plus Wi-Fi creds in `arduino/secrets.h`. Keep secret files untracked (gitignored).

## Payload shape
```json
{
  "code": "10503",      // required (filament code)
  "uid": "DEADBEEF",    // strongly recommended (chip UID); used for dedupe if trayUid missing
  "trayUid": "abcd1234" // optional; preferred dedupe key when present
}
```
Only `code` is required. The Arduino sketches already send `trayUid` (preferred) and `chipUid`/`uid` automatically; you just set Wi-Fi and `WEB_APP_URL` in secrets.h. Additional fields are ignored by the Web App.

## ESP8266 POST example
```cpp
const char* webhook = "https://script.google.com/macros/s/<WEB_APP_URL>/exec"; // your WEB_APP_URL from secrets.h

void postScan() {
    WiFiClientSecure client;
    client.setInsecure();
    if (!client.connect("script.google.com", 443)) return;
    String payload = R"PAY({"code":"10100","trayUid":"abcd1234"})PAY";
    String req;
    req += String("POST ") + web_app_url + " HTTP/1.1\r\n";
    req += "Host: script.google.com\r\n";
    req += "Content-Type: application/json\r\n";
    req += "Content-Length: " + String(payload.length()) + "\r\n\r\n";
    req += payload;
    client.print(req);
}
```

## Columns written (Inventory tab)
Timestamp | Code | Type (name/material) | Image (formula) | Tray/Chip UID

## Security and privacy
- Keep `SHEET_ID` and deployment URL out of source control.
- Web app should run as you; set access to “Anyone” only if you expect anonymous posts.
- Web App URL (the `/exec` URL) comes from the Apps Script deployment dialog; treat it as sensitive. Wi-Fi creds and the Web App URL for the Arduino sketches should live in a local, untracked header (e.g., `arduino/secrets.h`), not in commits.

## Arduino sketches
- `RFID_Bambu_lab_reader/` (serial + webhook POST)
- `RFID_Bambu_lab_reader_OLED/` (OLED + webhook POST)
- Configure Wi-Fi and Web App URL via a local header: copy `arduino/secrets.example.h` → `arduino/secrets.h` (gitignored) and set `WIFI_SSID`, `WIFI_PASS`, `WEB_APP_URL`. Each scan sends JSON `{ "code": "<filament code>", "uid": "<tag uid hex>" }` to the webhook; repeats with the same UID are ignored server-side.
- Build with `arduino-cli` (ESP8266 HUZZAH example):
  - `arduino-cli compile --fqbn esp8266:esp8266:huzzah arduino/RFID_Bambu_lab_reader/RFID_Bambu_lab_reader.ino`
  - `arduino-cli compile --fqbn esp8266:esp8266:huzzah arduino/RFID_Bambu_lab_reader_OLED/RFID_Bambu_lab_reader_OLED.ino`
- Upload example: `arduino-cli upload -p /dev/cu.usbserial-<port> --fqbn esp8266:esp8266:huzzah arduino/RFID_Bambu_lab_reader_OLED/RFID_Bambu_lab_reader_OLED.ino`
- Both sketches include `material_lookup.h` and generated `materials_snippet.h` with filament codes; extend if you add new materials. The snippets under `arduino/**/generated/` are now produced by this repo’s scraper (`python scripts/scrape_store.py`) so the Arduino lookup, Store Index, and Sheets data stay in sync. After running the scraper, it writes `data/store_index.{json,csv,tsv}` and regenerates both Arduino snippet headers automatically.
- Buzzer: passive piezo on GPIO15 (D8) to GND (add ~100–220 Ω series resistor if available). Each successful scan plays a two-tone chirp.
