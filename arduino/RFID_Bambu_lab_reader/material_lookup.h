#pragma once

#include <stddef.h>

struct MaterialInfo
{
    const char *materialId;   // e.g., "GFA50"
    const char *variantId;    // e.g., "A00-K0"
    const char *filamentCode; // 5-digit code as string, e.g., "10100"
    const char *name;         // material name/category, e.g., "PLA Basic"
    const char *color;        // human color name
    const char *productUrl;   // product page URL (when known)
};

// Curated entries first, then auto-included generated list from Bambu-Lab-RFID-Library README (materialId may be blank). Extend if needed; sketch prints a fallback when missing.
static const MaterialInfo MATERIALS[] = {
    // PLA Basic
    {"GFA00", "A00-W1", "10100", "PLA Basic", "Jade White", ""},
    {"GFA00", "A00-P0", "10201", "PLA Basic", "Beige", ""},
    {"GFA00", "A00-D2", "10104", "PLA Basic", "Light Gray", ""},
    {"GFA00", "A00-Y0", "10400", "PLA Basic", "Yellow", ""},
    {"GFA00", "A00-Y2", "10402", "PLA Basic", "Sunflower Yellow", ""},
    {"GFA00", "A00-A1", "10301", "PLA Basic", "Pumpkin Orange", ""},
    {"GFA00", "A00-A0", "10300", "PLA Basic", "Orange", ""},
    {"GFA00", "A00-Y4", "10401", "PLA Basic", "Gold", ""},
    {"GFA00", "A00-G3", "10503", "PLA Basic", "Bright Green", ""},
    {"GFA00", "A00-G1", "10501", "PLA Basic", "Bambu Green", ""},
    {"GFA00", "A00-G2", "10502", "PLA Basic", "Mistletoe Green", ""},
    {"GFA00", "A00-P6", "10202", "PLA Basic", "Magenta", ""},
    {"GFA00", "A00-R0", "10200", "PLA Basic", "Red", ""},
    {"GFA00", "A00-R2", "10205", "PLA Basic", "Maroon Red", ""},
    {"GFA00", "A00-P5", "10700", "PLA Basic", "Purple", ""},
    {"GFA00", "A00-P2", "10701", "PLA Basic", "Indigo Purple", ""},
    {"GFA00", "A00-B8", "10603", "PLA Basic", "Cyan", ""},
    {"GFA00", "A00-B3", "10604", "PLA Basic", "Cobalt Blue", ""},
    {"GFA09", "A09-B4", "10601", "PLA Basic", "Blue", ""},
    {"GFA00", "A00-N0", "10800", "PLA Basic", "Brown", ""},
    {"GFA00", "A00-N1", "10802", "PLA Basic", "Cocoa Brown", ""},
    {"GFA00", "A00-Y3", "10801", "PLA Basic", "Bronze", ""},
    {"GFA00", "A00-D0", "10103", "PLA Basic", "Gray", ""},
    {"GFA00", "A00-D1", "10102", "PLA Basic", "Silver", ""},
    {"GFA00", "A00-B1", "10602", "PLA Basic", "Blue Grey", ""},
    {"GFA00", "A00-D3", "10105", "PLA Basic", "Dark Gray", ""},
    {"GFA00", "A00-K0", "10101", "PLA Basic", "Black", ""},
    // PLA Lite
    {"GFA18", "A18-K0", "16100", "PLA Lite", "Black", ""},
    {"GFA18", "A18-D0", "16101", "PLA Lite", "Gray", ""},
    {"GFA18", "A18-W0", "16103", "PLA Lite", "White", ""},
    {"GFA18", "A18-R0", "16200", "PLA Lite", "Red", ""},
    {"GFA18", "A18-Y0", "16400", "PLA Lite", "Yellow", ""},
    {"GFA18", "A18-B0", "16600", "PLA Lite", "Cyan", ""},
    {"GFA18", "A18-B1", "16601", "PLA Lite", "Blue", ""},
    {"GFA18", "A18-P0", "16602", "PLA Lite", "Matte Beige", ""},
    // PLA Matte (subset)
    {"GFA01", "A01-W2", "11100", "PLA Matte", "Ivory White", ""},
    {"GFA01", "A01-W3", "11103", "PLA Matte", "Bone White", ""},
    {"GFA01", "A01-Y2", "11400", "PLA Matte", "Lemon Yellow", ""},
    {"GFA01", "A01-A2", "11300", "PLA Matte", "Mandarin Orange", ""},
    {"GFA01", "A01-P3", "11201", "PLA Matte", "Sakura Pink", ""},
    {"GFA01", "A01-P4", "11700", "PLA Matte", "Lilac Purple", ""},
    {"GFA01", "A01-R1", "11200", "PLA Matte", "Scarlet Red", ""},
    {"GFA01", "A01-G7", "11501", "PLA Matte", "Dark Green", ""},
    {"GFA01", "A01-G1", "11500", "PLA Matte", "Grass Green", ""},
    {"GFA01", "A01-B4", "11601", "PLA Matte", "Ice Blue", ""},
    {"GFA01", "A01-B3", "11600", "PLA Matte", "Marine Blue", ""},
    {"GFA01", "A01-D3", "11102", "PLA Matte", "Ash Gray", ""},
    {"GFA01", "A01-D0", "11104", "PLA Matte", "Nardo Gray", ""},
    {"GFA01", "A01-K1", "11101", "PLA Matte", "Charcoal", ""},
    // PLA Marble (subset)
    {"", "A07-D4", "13103", "PLA Marble", "White Marble", ""},
    // PLA Metal (subset)
    {"", "A02-Y1", "13400", "PLA Metal", "Iridium Gold Metallic", ""},
    // PETG HF (subset)
    {"GFH02", "G02-K0", "33102", "PETG HF", "Black", ""},
    {"GFH02", "G02-W0", "33100", "PETG HF", "White", ""},
    {"GFH02", "G02-R0", "33200", "PETG HF", "Red", ""},
    {"GFH02", "G02-D0", "33101", "PETG HF", "Gray", ""},
    {"GFH02", "G02-D1", "33103", "PETG HF", "Dark Gray", ""},
    {"GFH02", "G02-Y0", "33400", "PETG HF", "Yellow", ""},
    {"GFH02", "G02-A0", "33300", "PETG HF", "Orange", ""},
    {"GFH02", "G02-G1", "33501", "PETG HF", "Lime Green", ""},
    {"GFH02", "G02-G0", "33500", "PETG HF", "Green", ""},
    {"GFH02", "G02-B1", "33601", "PETG HF", "Lake Blue", ""},
    {"GFH02", "G02-B0", "33600", "PETG HF", "Blue", ""},
    // ABS (subset)
    {"GFB00", "B00-D1", "40102", "ABS", "Silver", ""},
    {"GFB00", "B00-K0", "40101", "ABS", "Black", ""},
    {"GFB00", "B00-W0", "40100", "ABS", "White", ""},
    {"GFB00", "B00-G6", "40500", "ABS", "Bambu Green", ""},
    {"GFB00", "B00-Y1", "40402", "ABS", "Tangerine Yellow", ""},
    {"GFB00", "B00-A0", "40300", "ABS", "Orange", ""},
    {"GFB00", "B00-R0", "40200", "ABS", "Red", ""},
    {"GFB00", "B00-B4", "40601", "ABS", "Azure", ""},
// Auto-generated entries from Bambu-Lab-RFID-Library (materialId blank when unknown)
#include "generated/materials_snippet.h"
};

static const size_t MATERIAL_COUNT = sizeof(MATERIALS) / sizeof(MATERIALS[0]);
