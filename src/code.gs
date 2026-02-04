const DEFAULT_SHEET_NAME = 'Inventory';
const IMAGES_SHEET_NAME = 'Store Index';
const TRAY_UID_COLUMN_INDEX = 6; // Column F: Tray UID for roll (also holds chip UID when tray missing)

/**
 * Webhook entry: accepts JSON body with RFID scan metadata and appends to a sheet.
 */
function doPost(e) {
  try {
    if (!e || !e.postData || !e.postData.contents) {
      return jsonResponse(400, { error: 'No body' });
    }
    const payload = JSON.parse(e.postData.contents || '{}');
    console.log('doPost payload', payload.code || '', JSON.stringify(payload));

    // Accept full Store Index uploads from the scraper (bypasses CSV imports).
    if (payload && payload.action === 'uploadStoreIndex') {
      return handleStoreIndexUpload(payload);
    }

    // Lightweight status probe to debug sheet connectivity without writing data.
    if (payload && payload.action === 'status') {
      const sheetId = getSheetId();
      if (!sheetId) {
        return jsonResponse(500, { error: 'SHEET_ID not configured (set in Script Properties)' });
      }
      return jsonResponse(200, getSheetStatus(sheetId));
    }

    if (!payload.code) {
      return jsonResponse(400, { error: 'code is required' });
    }

    const sheetId = getSheetId();
    if (!sheetId) {
      return jsonResponse(500, { error: 'SHEET_ID not configured (set in Script Properties)' });
    }

    const imageRecord = getImageRecord(sheetId, payload.code);
    console.log('lookup imageRecord', payload.code || '', imageRecord ? imageRecord.imageUrl : '');

    const result = appendRow(sheetId, payload, imageRecord);
    return jsonResponse(200, { ok: true, duplicate: result.duplicate, row: result.row });
  } catch (err) {
    console.error(err);
    return jsonResponse(500, { error: String(err) });
  }
}

/**
 * Append a row to the sheet with the supplied payload.
 */
function appendRow(sheetId, data, imageRecord) {
  const ss = SpreadsheetApp.openById(sheetId);
  const sheet = ss.getSheetByName(DEFAULT_SHEET_NAME);
  if (!sheet) {
    throw new Error(`Sheet not found: ${DEFAULT_SHEET_NAME}`);
  }
  const sep = getArgSeparator(ss);
  const ts = new Date();
  const trayUid = data.trayUid || '';
  const chipUid = data.chipUid || data.uid || data.tagUid || '';

  const imageUrl = imageRecord && imageRecord.imageUrl ? imageRecord.imageUrl : (data.imageUrl || '');
  const imageCell = imageUrl ? `=IMAGE("${imageUrl}")` : '';

  const productUrl = (imageRecord && imageRecord.productUrl) || data.productUrl || '';
  const codeCell = productUrl ? `=HYPERLINK("${productUrl}"${sep}"${data.code || ''}")` : (data.code || '');

  const name = data.name || (imageRecord && imageRecord.name) || '';
  const color = data.color || (imageRecord && imageRecord.color) || '';
  const material = data.material || (imageRecord && imageRecord.material) || '';
  const variantId = data.variantId || (imageRecord && imageRecord.variantId) || '';

  const trayCellValue = trayUid || chipUid || 'Tray ID missing';

  const row = [
    ts,                  // A: Time scanned
    codeCell,            // B: Filament Code (hyperlinked when productUrl known)
    name || material,    // C: Type (prefer name/display; fallback to material)
    color,               // D: Name (color / human label)
    imageCell,           // E: Image
    trayCellValue        // F: Tray UID for roll (or chip UID if tray missing)
  ];

  const cleanTrayUid = trayUid && trayUid !== 'Tray ID missing' ? trayUid : '';
  const dedupeKey = cleanTrayUid || chipUid;
  if (dedupeKey) {
    const existingRow = findRowByColumn(sheet, TRAY_UID_COLUMN_INDEX, dedupeKey);
    if (existingRow) {
      console.log('duplicate tray uid, update existing row', dedupeKey, 'row', existingRow);
      sheet.getRange(existingRow, 1, 1, row.length).setValues([row]);
      return { duplicate: true, row: existingRow, updated: true };
    }
  }
  const targetRow = findFirstEmptyRow(sheet); // first empty row, filling gaps if any
  console.log('appendRow -> sheet', sheet.getName(), 'writingRow', targetRow);
  sheet.getRange(targetRow, 1, 1, row.length).setValues([row]);
  return { duplicate: false, row: targetRow };
}

function findFirstEmptyRow(sheet) {
  const lastRow = sheet.getLastRow();
  if (lastRow === 0) {
    return 1;
  }
  const colA = sheet.getRange(1, 1, lastRow, 1).getValues();
  for (let i = 0; i < colA.length; i++) {
    const cell = String(colA[i][0] || '').trim();
    if (!cell) {
      return i + 1;
    }
  }
  return lastRow + 1;
}

function findRowByColumn(sheet, columnIndex, value) {
  const lastRow = sheet.getLastRow();
  if (!lastRow) return null;
  const colValues = sheet.getRange(1, columnIndex, lastRow, 1).getValues();
  for (let i = 0; i < colValues.length; i++) {
    const cell = String(colValues[i][0] || '').trim();
    if (cell && cell === String(value).trim()) {
      return i + 1; // 1-based row
    }
  }
  return null;
}

function getSheetStatus(sheetId) {
  const ss = SpreadsheetApp.openById(sheetId);
  const sheet = ss.getSheetByName(DEFAULT_SHEET_NAME);
  if (!sheet) {
    return { sheetFound: false, sheetName: DEFAULT_SHEET_NAME };
  }
  const lastRow = sheet.getLastRow();
  const lastCol = sheet.getLastColumn();
  let sample = [];
  if (lastRow > 0 && lastCol > 0) {
    const rowsToFetch = Math.min(3, lastRow);
    sample = sheet.getRange(1, 1, rowsToFetch, Math.min(5, lastCol)).getValues();
  }
  return {
    sheetFound: true,
    sheetName: sheet.getName(),
    lastRow,
    lastCol,
    sample
  };
}

/**
 * Lookup image metadata in the Images sheet; optionally refresh from STORE_INDEX_URL when missing.
 */
function getImageRecord(sheetId, code) {
  if (!code) return null;
  const ss = SpreadsheetApp.openById(sheetId);
  const sheet = ss.getSheetByName(IMAGES_SHEET_NAME);
  let record = sheet ? findImageRow(sheet, code) : null;
  return record;
}

function findImageRow(sheet, code) {
  const range = sheet.getDataRange();
  const values = range.getValues();
  const displays = range.getDisplayValues();
  if (!values || values.length < 2) return null;
  const headers = values[0].map(h => String(h || '').trim().toLowerCase());
  const idx = {
    code: headers.indexOf('code'),
    name: headers.indexOf('name'),
    color: headers.indexOf('color'),
    material: headers.indexOf('material'),
    variantId: headers.indexOf('variantid'),
    imageUrl: headers.indexOf('imageurl'),
    productUrl: headers.indexOf('producturl')
  };
  for (let i = 1; i < values.length; i++) {
    const row = values[i];
    const rowDisp = displays[i];
    const rowCode = idx.code >= 0 ? row[idx.code] : row[0];
    const rowCodeDisp = idx.code >= 0 ? rowDisp[idx.code] : rowDisp[0];
    const codeCandidate = String(rowCodeDisp || rowCode || '').trim();
    if (codeCandidate === String(code).trim()) {
      return {
        code: codeCandidate,
        name: idx.name >= 0 ? row[idx.name] : '',
        color: idx.color >= 0 ? row[idx.color] : '',
        material: idx.material >= 0 ? row[idx.material] : '',
        variantId: idx.variantId >= 0 ? row[idx.variantId] : '',
        imageUrl: idx.imageUrl >= 0 ? row[idx.imageUrl] : '',
        productUrl: idx.productUrl >= 0 ? row[idx.productUrl] : ''
      };
    }
  }
  return null;
}

/**
 * Populate the Store Index sheet from a JSON string (array of objects with Code/Name/Color/ImageUrl).
 * Run this manually after generating data/store_index.json; paste its contents into jsonText.
 */
function importStoreIndexFromJson(jsonText) {
  if (!jsonText) {
    throw new Error('jsonText is required');
  }
  const data = JSON.parse(jsonText);
  if (!Array.isArray(data)) {
    throw new Error('jsonText must be a JSON array');
  }
  const ss = SpreadsheetApp.getActive();
  const sep = getArgSeparator(ss);
  const rows = data.map(item => {
    const imageUrl = item.imageUrl || '';
    const productUrl = item.productUrl || '';
    const code = item.code || '';
    const codeCell = productUrl ? `=HYPERLINK("${productUrl}"${sep}"${code}")` : code;
    const imageCell = imageUrl ? `=IMAGE("${imageUrl}")` : '';
    return [
      codeCell,
      item.name || '',
      item.color || '',
      imageCell,
      productUrl,
      imageUrl
    ];
  });
  const headers = ['Code', 'Name', 'Color', 'Image', 'ProductUrl', 'ImageUrl'];
  const sheet = ss.getSheetByName(IMAGES_SHEET_NAME) || ss.insertSheet(IMAGES_SHEET_NAME);
  sheet.clearContents();
  sheet.getRange(1, 1, 1, headers.length).setValues([headers]);
  if (rows.length) {
    sheet.getRange(2, 1, rows.length, headers.length).setValues(rows);
  }
}

/**
 * Prompt-driven import: asks for either the raw JSON contents or a Drive file URL/ID for store_index.json.
 * Allows non-technical users to populate Store Index without handling formulas manually.
 */
function importStoreIndexInteractive() {
  const ui = SpreadsheetApp.getUi();
  const res = ui.prompt(
    'Import store_index.json',
    'Paste the JSON contents from data/store_index.json, or provide a Drive file URL/ID where you uploaded store_index.json.',
    ui.ButtonSet.OK_CANCEL
  );
  if (res.getSelectedButton() !== ui.Button.OK) {
    return;
  }
  const input = res.getResponseText();
  if (!input) {
    ui.alert('No input provided');
    return;
  }
  const jsonText = resolveJsonInput(input);
  importStoreIndexFromJson(jsonText);
  ui.alert('Store Index imported');
}

/**
 * Import from a Drive file ID (bypasses UI prompt).
 * Usage: importStoreIndexFromDrive('your-file-id');
 */
function importStoreIndexFromDrive(fileId) {
  if (!fileId) {
    throw new Error('fileId is required');
  }
  const file = DriveApp.getFileById(fileId);
  const jsonText = file.getBlob().getDataAsString();
  importStoreIndexFromJson(jsonText);
}

/**
 * Handle direct Store Index uploads from the scraper via POST.
 * Expects: { action: 'uploadStoreIndex', token: '<shared token>', records: [ { code, name, color, imageUrl, productUrl } ] }
 */
function handleStoreIndexUpload(payload) {
  const records = Array.isArray(payload.records) ? payload.records : [];
  if (!records.length) {
    return jsonResponse(400, { error: 'no records' });
  }
  const sheetId = getSheetId();
  if (!sheetId) {
    return jsonResponse(500, { error: 'SHEET_ID not configured (set in Script Properties)' });
  }
  const ss = SpreadsheetApp.openById(sheetId);
  const sheet = ss.getSheetByName(IMAGES_SHEET_NAME) || ss.insertSheet(IMAGES_SHEET_NAME);
  const sep = getArgSeparator(ss);
  const headers = ['Code', 'Name', 'Color', 'Image', 'ProductUrl', 'ImageUrl'];
  const rows = records.map(r => {
    const imageUrl = r.imageUrl || '';
    const productUrl = r.productUrl || '';
    const code = r.code || '';
    const codeCell = productUrl ? `=HYPERLINK("${productUrl}"${sep}"${code}")` : code;
    const imageCell = imageUrl ? `=IMAGE("${imageUrl}")` : '';
    return [
      codeCell,
      r.name || '',
      r.color || '',
      imageCell,
      productUrl,
      imageUrl
    ];
  });
  sheet.clearContents();
  sheet.getRange(1, 1, 1, headers.length).setValues([headers]);
  sheet.getRange(2, 1, rows.length, headers.length).setValues(rows);
  return jsonResponse(200, { ok: true, rows: rows.length });
}

function resolveJsonInput(input) {
  const trimmed = input.trim();
  // If it looks like JSON array/object, return as-is.
  if ((trimmed.startsWith('[') && trimmed.endsWith(']')) || (trimmed.startsWith('{') && trimmed.endsWith('}'))) {
    return trimmed;
  }
  // Try to extract Drive file ID from URL or plain ID.
  const idMatch = trimmed.match(/[-\w]{25,}/);
  if (!idMatch) {
    throw new Error('Input is neither JSON nor a recognizable Drive file ID/URL');
  }
  const fileId = idMatch[0];
  const file = DriveApp.getFileById(fileId);
  return file.getBlob().getDataAsString();
}

/**
 * Import when JSON is pasted into a sheet cell (e.g., tab "Store Index Source", cell A1).
 * This avoids UI prompts and Drive file IDs.
 */
function importStoreIndexFromSheetCell(sourceSheetName = 'Store Index Source', cellA1 = 'A1') {
  const ss = SpreadsheetApp.getActive();
  const sheet = ss.getSheetByName(sourceSheetName);
  if (!sheet) {
    throw new Error(`Source sheet not found: ${sourceSheetName}`);
  }
  const jsonText = String(sheet.getRange(cellA1).getValue() || '').trim();
  if (!jsonText) {
    throw new Error(`No JSON found in ${sourceSheetName}!${cellA1}`);
  }
  importStoreIndexFromJson(jsonText);
}

function getSheetId() {
  return PropertiesService.getScriptProperties().getProperty('SHEET_ID');
}

function getArgSeparator(spreadsheet) {
  try {
    const locale = (spreadsheet && spreadsheet.getSpreadsheetLocale && spreadsheet.getSpreadsheetLocale()) || '';
    if (locale && locale.match(/^(cs|da|de|es|fi|fr|it|nl|no|pl|pt|ru|sv|tr|hu|ro|sk|sl|hr|sr|bg|uk|et|lv|lt|is|el|he)/i)) {
      return ';';
    }
  } catch (err) {
    console.warn('arg-separator fallback to comma', err);
  }
  return ',';
}

function jsonResponse(_status, body) {
  return ContentService.createTextOutput(JSON.stringify(body))
    .setMimeType(ContentService.MimeType.JSON);
}
