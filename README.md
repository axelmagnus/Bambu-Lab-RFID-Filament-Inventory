# Bambu Lab RFID Filament Inventory

Apps Script webhook to log RFID filament scans into Google Sheets. The ESP8266/RC522 posts the 5-digit filament code; the script enriches from a `Store Index` tab (name/color/material/variant/imageUrl) and writes an `=IMAGE(url)` formula into column E. It uses only `SpreadsheetApp`—no external fetches.

## Features
- Appends to the first empty row (fills gaps) on the `Inventory` tab.
- Writes the image formula in column E alongside timestamp, code, name, color, material, variantId, materialId, trayUid, nozzle, width, productionDate, and length.
- Status probe (`{"action":"status"}`) reports sheet connectivity and a sample of recent rows for quick debugging.
- Optional Store Index uploader token to gate scraper pushes.

## Setup (Apps Script)
1) Create a new Apps Script project (or open `Extensions → Apps Script` from your target Sheet). Replace contents with [src/code.gs](src/code.gs) and [appsscript.json](appsscript.json).
2) Set Script Properties: `SHEET_ID=<your sheet id>` (required, keep private). Optionally `INDEX_TOKEN=<shared secret>` if you allow Store Index uploads. If your inventory tab is renamed, change `DEFAULT_SHEET_NAME` in code.
3) Create a tab named `Store Index` with columns `Code`, `Name`, `Color`, `ImageUrl`, `Image` (header row). Populate it (see “Populate Store Index” below). The webhook only reads this tab.
4) Deploy: `Deploy → New deployment → Web app`, Execute as `Me`, Who has access `Anyone`. Copy the `/exec` URL; this is your webhook.

## Testing
- Status check:
  - `curl -s -X POST "$WEBHOOK" -H "Content-Type: application/json" -d '{"action":"status"}'`
- Append a scan (uses first empty row):
  - `curl -s -X POST "$WEBHOOK" -H "Content-Type: application/json" -d '{"code":"10503"}'`
Response should be `{"ok":true}`; verify the row shows image in column E.

## Populate Store Index
- Preferred: scraper pushes directly. Set `PUSH_STORE_INDEX_URL=<your /exec URL>` and `PUSH_STORE_INDEX_TOKEN=<shared token>` (optional) before running `python scripts/scrape_store.py`. The scraper will POST records into the `Store Index` tab via the webhook. You still add the Image arrayformula once in the sheet.
- The scraper also writes `data/store_index.json`, `data/store_index.csv`, and `data/store_index.tsv` for reference or manual import.
- Manual import fallback: use the TSV (File → Import → Upload → Separator: Tab) into `Store Index`, then add one arrayformula in the Image column header row (example in E1 or E2 depending on your sheet):
  - Semicolon locale: `=ARRAYFORMULA(IF(LEN(D2:D); IMAGE(D2:D); ""))`
  - Comma locale: `=ARRAYFORMULA(IF(LEN(D2:D), IMAGE(D2:D), ""))`
- Other import helpers remain available if you need them without pasting the JSON into a sheet cell: `importStoreIndexFromDrive("<file-id>")`, `importStoreIndexFromJson(<jsonText>)`, or `importStoreIndexInteractive()`.

## Payload shape
```json
{
  "code": "10100",              
  "materialId": "10100",
  "trayUid": "abcd1234",
  "nozzle": 0.4,
  "width": 1.75,
  "productionDate": "2024-10-31",
  "length": 1000
}
```
Only `code` is required; other fields are appended if present.

## ESP8266 POST example
```cpp
const char* webhook = "https://script.google.com/macros/s/WEB_APP_ID/exec";

void postScan() {
  WiFiClientSecure client;
  client.setInsecure();
  if (!client.connect("script.google.com", 443)) return;
  String payload = R"PAY({"code":"10100"})PAY";
  String req;
  req += String("POST ") + webhook + " HTTP/1.1\r\n";
  req += "Host: script.google.com\r\n";
  req += "Content-Type: application/json\r\n";
  req += "Content-Length: " + String(payload.length()) + "\r\n\r\n";
  req += payload;
  client.print(req);
}
```

## Columns written (Inventory tab)
Timestamp | Code | Name | Color | Image (formula) | Material | VariantId | MaterialId | TrayUid | Nozzle | Width | ProductionDate | Length

## Security and privacy
- Keep `SHEET_ID`, deployment URL, and optional `INDEX_TOKEN` out of source control.
- Web app should run as you; set access to “Anyone” only if you expect anonymous posts.

## Publish to GitHub (manual)
1) Create the repo on GitHub named `Bambu-Lab-RFID-Filament-Inventory` (public).
2) In this project root:
   - `git init`
   - `git remote add origin git@github.com:<you>/Bambu-Lab-RFID-Filament-Inventory.git`
   - `git add .`
   - `git commit -m "Initial public release"`
   - `git push -u origin main`
3) Add the Apps Script files (src/code.gs, appsscript.json) as-is; do not commit private IDs or tokens.
