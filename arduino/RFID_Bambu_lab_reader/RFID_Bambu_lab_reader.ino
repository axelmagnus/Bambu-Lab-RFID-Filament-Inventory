// Minimal RC522 + ESP8266 (Feather HUZZAH) reader for Bambu Lab filament tags (serial-only)
// Uses hardware SPI and reads MIFARE Classic data when provided per-sector keys
// derived with https://github.com/Bambu-Research-Group/RFID-Tag-Guide (deriveKeys.py).

#include <SPI.h>
#include <MFRC522.h>
#include <bearssl/bearssl_hmac.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <string.h>
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#include "material_lookup.h"

// Pin mapping (ESP8266 Arduino numbers, not Dx labels):
// SS/SDA  -> 16 (GPIO16, usually labeled D0) // user-chosen; OK as chip-select
// RST     -> 2  (GPIO2,  usually labeled D4) // must stay HIGH at boot; fine for RC522 reset input
// SCK     -> 14 (GPIO14, D5)
// MOSI    -> 13 (GPIO13, D7)
// MISO    -> 12 (GPIO12, D6)
// 3.3V and GND only; do NOT feed the RC522 with 5V.

static constexpr uint8_t SS_PIN = 16;     // SDA/SS moved to GPIO16 (D0)
static constexpr uint8_t RST_PIN = 2;     // RC522 RST on GPIO2 (D4)
static constexpr uint8_t BUZZER_PIN = 15; // GPIO15 (D8) passive piezo

// Wi-Fi + webhook (fill in for your environment; keep secrets out of source control if possible)
#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS "YOUR_PASS"
#endif
#ifndef WEB_APP_URL
#define WEB_APP_URL "https://script.google.com/macros/s/WEB_APP_ID/exec"
#endif

MFRC522 rfid(SS_PIN, RST_PIN);

// SECTOR_KEY_A will be auto-filled from the UID using the same HKDF as deriveKeys.py
// (salted with the known master and context "RFID-A\0"). Initial zeros are placeholders.
static byte SECTOR_KEY_A[16][6] = {
    {0, 0, 0, 0, 0, 0}, // sector  0
    {0, 0, 0, 0, 0, 0}, // sector  1
    {0, 0, 0, 0, 0, 0}, // sector  2
    {0, 0, 0, 0, 0, 0}, // sector  3
    {0, 0, 0, 0, 0, 0}, // sector  4
    {0, 0, 0, 0, 0, 0}, // sector  5
    {0, 0, 0, 0, 0, 0}, // sector  6
    {0, 0, 0, 0, 0, 0}, // sector  7
    {0, 0, 0, 0, 0, 0}, // sector  8
    {0, 0, 0, 0, 0, 0}, // sector  9
    {0, 0, 0, 0, 0, 0}, // sector 10
    {0, 0, 0, 0, 0, 0}, // sector 11
    {0, 0, 0, 0, 0, 0}, // sector 12
    {0, 0, 0, 0, 0, 0}, // sector 13
    {0, 0, 0, 0, 0, 0}, // sector 14
    {0, 0, 0, 0, 0, 0}  // sector 15
};

static const uint8_t HKDF_SALT[16] = {0x9a, 0x75, 0x9c, 0xf2, 0xc4, 0xf7, 0xca, 0xff, 0x22, 0x2c, 0xb9, 0x76, 0x9b, 0x41, 0xbc, 0x96};
static const uint8_t HKDF_INFO[7] = {'R', 'F', 'I', 'D', '-', 'A', 0x00};

static char lastCode[8] = "";
static char lastName[16] = "";
static char lastColor[16] = "";
static char lastUid[33] = "";     // up to 16 bytes UID -> 32 hex chars + NUL
static char lastTrayUid[33] = ""; // Tray UID from block 9

static unsigned long ledOffAt = 0;
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 10000;
static const char *TRAY_MISSING = "Tray UID missing";

static void hkdfFromUid(const uint8_t *uid, size_t uidLen, uint8_t *out, size_t outLen)
{
    uint8_t prk[32];

    br_hmac_key_context kc_salt;
    br_hmac_context ctx;
    br_hmac_key_init(&kc_salt, &br_sha256_vtable, HKDF_SALT, sizeof(HKDF_SALT));
    br_hmac_init(&ctx, &kc_salt, 0);
    br_hmac_update(&ctx, uid, uidLen);
    br_hmac_out(&ctx, prk);

    br_hmac_key_context kc_prk;
    br_hmac_context ctx_prk;
    br_hmac_key_init(&kc_prk, &br_sha256_vtable, prk, sizeof(prk));

    uint8_t t[32];
    size_t pos = 0;
    uint8_t counter = 1;
    size_t infoLen = sizeof(HKDF_INFO);

    while (pos < outLen)
    {
        br_hmac_init(&ctx_prk, &kc_prk, 0);
        if (counter > 1)
        {
            br_hmac_update(&ctx_prk, t, sizeof(t));
        }
        br_hmac_update(&ctx_prk, HKDF_INFO, infoLen);
        br_hmac_update(&ctx_prk, &counter, 1);
        br_hmac_out(&ctx_prk, t);

        size_t take = (outLen - pos < sizeof(t)) ? (outLen - pos) : sizeof(t);
        memcpy(out + pos, t, take);
        pos += take;
        counter++;
    }
}

static void deriveKeysFromUid(const byte *uid, byte uidLen)
{
    uint8_t derived[16 * 6];
    hkdfFromUid(uid, uidLen, derived, sizeof(derived));
    for (uint8_t s = 0; s < 16; s++)
    {
        memcpy(SECTOR_KEY_A[s], derived + s * 6, 6);
    }
}

static void printHex(byte *buffer, byte bufferSize)
{
    for (byte i = 0; i < bufferSize; i++)
    {
        if (buffer[i] < 0x10)
            Serial.print('0');
        Serial.print(buffer[i], HEX);
        if (i + 1 < bufferSize)
            Serial.print(' ');
    }
}

static void playScanTone(bool trayMissing)
{
    if (trayMissing)
    {
        // Invert tone order to flag missing tray UID (high then low).
        tone(BUZZER_PIN, 1200, 120);
        delay(150);
        tone(BUZZER_PIN, 900, 120);
    }
    else
    {
        tone(BUZZER_PIN, 900, 120);
        delay(150);
        tone(BUZZER_PIN, 1200, 120);
    }
    delay(150);
    noTone(BUZZER_PIN);
}

static uint16_t le16(const byte *p)
{
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static float leFloat(const byte *p)
{
    float f = 0.0f;
    memcpy(&f, p, sizeof(float));
    return f;
}

static void copyTrim(char *dst, size_t dstSize, const byte *src, size_t srcLen)
{
    size_t n = srcLen < (dstSize - 1) ? srcLen : (dstSize - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
    for (int i = static_cast<int>(n) - 1; i >= 0; --i)
    {
        if (dst[i] == ' ' || dst[i] == '\0')
            dst[i] = '\0';
        else
            break;
    }
}

static const MaterialInfo *lookupMaterial(const char *materialId, const char *variantId)
{
    // First, try exact variant match
    for (size_t i = 0; i < MATERIAL_COUNT; i++)
    {
        const MaterialInfo &m = MATERIALS[i];
        if (strlen(m.variantId) && strcmp(variantId, m.variantId) == 0)
        {
            return &m;
        }
    }
    // Fallback to exact material ID match
    for (size_t i = 0; i < MATERIAL_COUNT; i++)
    {
        const MaterialInfo &m = MATERIALS[i];
        if (strlen(m.materialId) && strcmp(materialId, m.materialId) == 0)
        {
            return &m;
        }
    }
    return nullptr;
}

static void uidToHexString(const byte *uid, byte uidLen, char *out, size_t outSize)
{
    size_t pos = 0;
    for (byte i = 0; i < uidLen && (pos + 2) < outSize; i++)
    {
        int written = snprintf(out + pos, outSize - pos, "%02X", uid[i]);
        if (written <= 0)
            break;
        pos += static_cast<size_t>(written);
    }
    out[outSize - 1] = '\0';
}

static bool ensureWifi()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        return true;
    }
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS)
    {
        delay(200);
    }
    return WiFi.status() == WL_CONNECTED;
}

static void connectWifiAtStartup()
{
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS)
    {
        delay(200);
    }
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.print("Connected to ");
        Serial.println(WIFI_SSID);
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    }
    else
    {
        Serial.println("WiFi connect failed (will retry on send)");
    }
}

static void sendScanToWebhook(const char *code, const char *trayUid, const char *chipUid)
{
    if (!code || !trayUid || !strlen(code) || !strlen(trayUid))
    {
        return;
    }
    if (!ensureWifi())
    {
        Serial.println("WiFi not connected; skipping webhook");
        return;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, WEB_APP_URL))
    {
        Serial.println("HTTP begin failed");
        return;
    }
    http.addHeader("Content-Type", "application/json");
    String payload = String("{\"code\":\"") + code + "\",\"trayUid\":\"" + trayUid + "\"";
    if (chipUid && strlen(chipUid))
    {
        payload += ",\"chipUid\":\"";
        payload += chipUid;
        payload += "\"";
    }
    payload += "}";
    int httpCode = http.POST(payload);
    if (httpCode > 0)
    {
        String resp = http.getString();
        Serial.print("Webhook POST ");
        Serial.print(httpCode);
        Serial.print(" resp: ");
        Serial.println(resp);
    }
    else
    {
        Serial.print("Webhook POST failed: ");
        Serial.println(http.errorToString(httpCode));
    }
    http.end();
}

static void decodeKnownBlock(uint8_t block, const byte *data)
{
    switch (block)
    {
    case 1: // Material IDs
    {
        char variant[9];
        char material[9];
        copyTrim(variant, sizeof(variant), data, 8);
        copyTrim(material, sizeof(material), data + 8, 8);

        const MaterialInfo *info = lookupMaterial(material, variant);
        if (info)
        {
            Serial.print("Filament code: ");
            Serial.print(info->filamentCode);
            Serial.print("  Name: ");
            Serial.print(info->name);
            if (strlen(info->color))
            {
                Serial.print("  Color: ");
                Serial.print(info->color);
            }
            Serial.print("  Variant: ");
            Serial.print(variant);
            Serial.print("  Material: ");
            Serial.print(material);
            strncpy(lastCode, info->filamentCode, sizeof(lastCode) - 1);
            lastCode[sizeof(lastCode) - 1] = '\0';
            strncpy(lastName, info->name, sizeof(lastName) - 1);
            lastName[sizeof(lastName) - 1] = '\0';
            strncpy(lastColor, info->color, sizeof(lastColor) - 1);
            lastColor[sizeof(lastColor) - 1] = '\0';
        }
        else
        {
            Serial.print("Variant: ");
            Serial.print(variant);
            Serial.print("  Material: ");
            Serial.print(material);
            Serial.print("  (no lookup; extend material_lookup.h)");
            strncpy(lastCode, "?", sizeof(lastCode) - 1);
            strncpy(lastName, material, sizeof(lastName) - 1);
            strncpy(lastColor, variant, sizeof(lastColor) - 1);
        }
        Serial.println();
        break;
    }
    case 2: // Filament type
        Serial.print("Filament type: ");
        for (int i = 0; i < 16; i++)
            Serial.write(data[i]);
        Serial.println();
        break;
    case 5: // Color/weight/diameter
        Serial.print("Color RGBA: 0x");
        for (int i = 3; i >= 0; i--)
        {
            if (data[i] < 0x10)
                Serial.print('0');
            Serial.print(data[i], HEX);
        }
        Serial.print("  Weight(g): ");
        Serial.print(le16(&data[4]));
        Serial.print("  Diameter(mm): ");
        Serial.println(leFloat(&data[8]), 3);
        break;
    case 6: // Temps
        Serial.print("DryTemp: ");
        Serial.print(le16(&data[0]));
        Serial.print("C  DryTime(h): ");
        Serial.print(le16(&data[2]));
        Serial.print("  BedTemp: ");
        Serial.print(le16(&data[6]));
        Serial.print("C  HotendMax: ");
        Serial.print(le16(&data[8]));
        Serial.print("C  HotendMin: ");
        Serial.println(le16(&data[10]));
        break;
    case 8: // Nozzle diameter
        Serial.print("Nozzle(mm): ");
        Serial.println(leFloat(&data[12]), 3);
        break;
    case 9: // Tray UID
        Serial.print("Tray UID: ");
        printHex((byte *)data, 16);
        Serial.println();
        uidToHexString(data, 16, lastTrayUid, sizeof(lastTrayUid));
        break;
    case 10: // Spool width *100
        Serial.print("Spool width(mm): ");
        Serial.println(le16(&data[4]) / 100.0f, 2);
        break;
    case 12: // Production date/time string
        Serial.print("Prod date: ");
        for (int i = 0; i < 16; i++)
            Serial.write(data[i]);
        Serial.println();
        break;
    case 14: // Filament length meters at offset 4
        Serial.print("Length(m): ");
        Serial.println(le16(&data[4]));
        break;
    case 16: // Extra color info
        Serial.print("FormatId: ");
        Serial.print(le16(&data[0]));
        Serial.print("  ColorCount: ");
        Serial.print(le16(&data[2]));
        Serial.print("  SecondColor ABGR: 0x");
        for (int i = 7; i >= 4; i--)
        {
            if (data[i] < 0x10)
                Serial.print('0');
            Serial.print(data[i], HEX);
        }
        Serial.println();
        break;
    default:
        break;
    }
}

static void readClassic()
{
    // Only read the blocks we need (reduces scan time).
    static const uint8_t TARGET_BLOCKS[] = {1, 2, 5, 6, 8, 9, 10, 12, 14, 16};
    MFRC522::MIFARE_Key key;
    byte buffer[18];
    byte size = sizeof(buffer);

    int8_t authedSector = -1;
    bool authOk = false;

    for (size_t i = 0; i < sizeof(TARGET_BLOCKS); i++)
    {
        uint8_t block = TARGET_BLOCKS[i];
        uint8_t sector = block / 4;

        if (sector != authedSector)
        {
            memcpy(key.keyByte, SECTOR_KEY_A[sector], 6);
            MFRC522::StatusCode auth = rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, block, &key, &(rfid.uid));
            authOk = (auth == MFRC522::STATUS_OK);
            authedSector = sector;
            if (!authOk)
            {
                Serial.print("Auth fail sector ");
                Serial.print(sector);
                Serial.print(": ");
                Serial.println(rfid.GetStatusCodeName(auth));
                continue;
            }
        }

        size = sizeof(buffer);
        MFRC522::StatusCode status = rfid.MIFARE_Read(block, buffer, &size);
        if (status != MFRC522::STATUS_OK)
        {
            Serial.print("Read fail block ");
            Serial.print(block);
            Serial.print(": ");
            Serial.println(rfid.GetStatusCodeName(status));
            continue;
        }

        decodeKnownBlock(block, buffer);
    }
}

void setup()
{
    Serial.begin(115200);
    while (!Serial)
    {
    }

    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH); // active LOW on ESP8266

    SPI.begin();
    rfid.PCD_Init();

    connectWifiAtStartup();

    Serial.println("RC522 ready. Present a Bambu Lab spool/tag...");
    rfid.PCD_DumpVersionToSerial();
}

void loop()
{
    unsigned long now = millis();
    if (ledOffAt && now >= ledOffAt)
    {
        digitalWrite(LED_BUILTIN, HIGH);
        ledOffAt = 0;
    }
    if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial())
    {
        delay(50);
        return;
    }

    Serial.println();
    Serial.print("Tag UID: ");
    printHex(rfid.uid.uidByte, rfid.uid.size);
    Serial.println();

    MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
    // Serial.print("Type: ");
    // Serial.println(rfid.PICC_GetTypeName(piccType));

    // reset last decoded data for this scan
    lastCode[0] = '\0';
    lastName[0] = '\0';
    lastColor[0] = '\0';
    strncpy(lastTrayUid, TRAY_MISSING, sizeof(lastTrayUid) - 1);
    lastTrayUid[sizeof(lastTrayUid) - 1] = '\0';
    uidToHexString(rfid.uid.uidByte, rfid.uid.size, lastUid, sizeof(lastUid));

    deriveKeysFromUid(rfid.uid.uidByte, rfid.uid.size);
    readClassic();

    if (strlen(lastCode) && lastCode[0] != '?' && strlen(lastTrayUid))
    {
        const bool trayMissing = (strcmp(lastTrayUid, TRAY_MISSING) == 0);
        const char *trayToSend = trayMissing ? TRAY_MISSING : lastTrayUid;
        const char *chipToSend = strlen(lastUid) ? lastUid : nullptr;
        sendScanToWebhook(lastCode, trayToSend, chipToSend);
        playScanTone(trayMissing);
    }

    digitalWrite(LED_BUILTIN, LOW);
    ledOffAt = millis() + 150;

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();

    Serial.println("-----");
    delay(200);
}
