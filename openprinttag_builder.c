#include "openprinttag_i.h"
#include "cbor_encoder.h"
#include "material_types.h"
#include "openprinttag_fields.h"

#include <string.h>

// Layout of a new tag, modelled on tags made by the vendor:
//
//   capability container (4 bytes)   E1 40 <size/8> 01
//   NDEF TLV                         03 <length>
//     one NDEF media record          application/vnd.openprinttag
//       meta section                 {2: aux region offset, 3: aux region size}
//       main section                 the material data
//       spare bytes                  room for the main section to grow
//       auxiliary region             {} now, holds the consumed weight once it is updated
//   terminator TLV                   FE

#define CC_SIZE             (4U)
#define CC_MAGIC            (0xE1U)
#define CC_VERSION          (0x40U) // Version 1.0, read and write access
#define CC_FEATURES         (0x01U)
#define TLV_NDEF            (0x03U)
#define TLV_END             (0xFEU)
#define NDEF_MB_ME_SR_MEDIA (0xD2U) // Message begin + end, short record, media type
#define NDEF_MB_ME_MEDIA    (0xC2U) // Same with a 4 byte payload length

#define META_SIZE        (8U) // Fixed: A2 02 19 <hi> <lo> 03 18 <aux size>
#define AUX_REGION_SIZE  (32U)
#define MAIN_SPARE_BYTES (16U)
#define MAIN_BUFFER_SIZE (256U)

static bool encode_text_field(CborEncoder* encoder, uint32_t key, const char* text) {
    return cbor_encode_uint(encoder, key) && cbor_encode_text(encoder, text, strlen(text));
}

static bool encode_uint_field(CborEncoder* encoder, uint32_t key, uint32_t value) {
    return cbor_encode_uint(encoder, key) && cbor_encode_uint(encoder, value);
}

// Encodes the main section. Returns its size, 0 on failure. Keys are written in ascending order.
static size_t build_main_section(const OpenPrintTagCreateData* data, uint8_t* out, size_t size) {
    if(data->type_index >= MATERIAL_TYPES_COUNT) return 0;

    const bool has_material = data->material[0] != '\0';
    const bool has_brand = data->brand[0] != '\0';

    size_t count = 2; // Material class and type are always stored
    count += has_material;
    count += has_brand;
    if(data->weight > 0) count += 2; // Nominal and actual weight
    count += data->empty_weight > 0;
    count += data->nozzle_min > 0;
    count += data->nozzle_max > 0;
    count += data->bed_min > 0;
    count += data->bed_max > 0;

    CborEncoder encoder;
    cbor_encoder_init(&encoder, out, size);

    bool ok = cbor_encode_map(&encoder, count);
    ok = ok && encode_uint_field(&encoder, MAIN_MATERIAL_CLASS, 0); // 0 = FFF filament
    ok = ok &&
         encode_uint_field(&encoder, MAIN_MATERIAL_TYPE, material_types[data->type_index].key);
    if(has_material) ok = ok && encode_text_field(&encoder, MAIN_MATERIAL_NAME, data->material);
    if(has_brand) ok = ok && encode_text_field(&encoder, MAIN_BRAND_NAME, data->brand);
    if(data->weight > 0) {
        ok = ok && encode_uint_field(&encoder, MAIN_NOMINAL_NETTO_FULL_WEIGHT, data->weight);
        ok = ok && encode_uint_field(&encoder, MAIN_ACTUAL_NETTO_FULL_WEIGHT, data->weight);
    }
    if(data->empty_weight > 0) {
        ok = ok && encode_uint_field(&encoder, MAIN_EMPTY_CONTAINER_WEIGHT, data->empty_weight);
    }
    if(data->nozzle_min > 0) {
        ok = ok && encode_uint_field(&encoder, MAIN_MIN_PRINT_TEMPERATURE, data->nozzle_min);
    }
    if(data->nozzle_max > 0) {
        ok = ok && encode_uint_field(&encoder, MAIN_MAX_PRINT_TEMPERATURE, data->nozzle_max);
    }
    if(data->bed_min > 0) {
        ok = ok && encode_uint_field(&encoder, MAIN_MIN_BED_TEMPERATURE, data->bed_min);
    }
    if(data->bed_max > 0) {
        ok = ok && encode_uint_field(&encoder, MAIN_MAX_BED_TEMPERATURE, data->bed_max);
    }

    return ok ? cbor_encoder_get_size(&encoder) : 0;
}

size_t openprinttag_build_tag_image(
    const OpenPrintTagCreateData* data,
    size_t capacity,
    uint8_t* out) {
    furi_check(data);
    furi_check(out);

    uint8_t main_section[MAIN_BUFFER_SIZE];
    const size_t main_size = build_main_section(data, main_section, sizeof(main_section));
    if(main_size == 0) return 0;

    // Payload: meta, main section, spare bytes, auxiliary region
    const size_t aux_offset = META_SIZE + main_size + MAIN_SPARE_BYTES;
    const size_t payload_size = aux_offset + AUX_REGION_SIZE;
    const size_t type_size = strlen(OPENPRINTTAG_MIME_TYPE);

    // NDEF record header: a short record carries a 1 byte payload length, otherwise 4 bytes
    const bool short_record = payload_size <= 0xFF;
    const size_t record_header_size = short_record ? 3 : 6;
    const size_t message_size = record_header_size + type_size + payload_size;

    // TLV header: a 1 byte length, or 0xFF followed by 2 bytes
    const size_t tlv_header_size = message_size < 0xFF ? 2 : 4;

    const size_t total_size = CC_SIZE + tlv_header_size + message_size + 1;
    if(total_size > capacity) return 0;
    if(aux_offset > 0xFFFF || message_size > 0xFFFF) return 0;

    // The capability container size counts 8 byte units after the container itself
    const size_t area_units = (capacity - CC_SIZE) / 8;
    if(area_units == 0) return 0;

    memset(out, 0, capacity);
    size_t pos = 0;

    out[pos++] = CC_MAGIC;
    out[pos++] = CC_VERSION;
    out[pos++] = area_units > 0xFF ? 0xFF : (uint8_t)area_units;
    out[pos++] = CC_FEATURES;

    out[pos++] = TLV_NDEF;
    if(message_size < 0xFF) {
        out[pos++] = (uint8_t)message_size;
    } else {
        out[pos++] = 0xFF;
        out[pos++] = (uint8_t)(message_size >> 8);
        out[pos++] = (uint8_t)message_size;
    }

    if(short_record) {
        out[pos++] = NDEF_MB_ME_SR_MEDIA;
        out[pos++] = (uint8_t)type_size;
        out[pos++] = (uint8_t)payload_size;
    } else {
        out[pos++] = NDEF_MB_ME_MEDIA;
        out[pos++] = (uint8_t)type_size;
        out[pos++] = (uint8_t)(payload_size >> 24);
        out[pos++] = (uint8_t)(payload_size >> 16);
        out[pos++] = (uint8_t)(payload_size >> 8);
        out[pos++] = (uint8_t)payload_size;
    }
    memcpy(&out[pos], OPENPRINTTAG_MIME_TYPE, type_size);
    pos += type_size;

    // Meta section: {2: aux offset (16 bit), 3: aux size}
    out[pos++] = 0xA2;
    out[pos++] = META_AUX_REGION_OFFSET;
    out[pos++] = 0x19;
    out[pos++] = (uint8_t)(aux_offset >> 8);
    out[pos++] = (uint8_t)aux_offset;
    out[pos++] = META_AUX_REGION_SIZE;
    out[pos++] = 0x18;
    out[pos++] = AUX_REGION_SIZE;

    memcpy(&out[pos], main_section, main_size);
    pos += main_size;

    pos += MAIN_SPARE_BYTES; // Left zero

    out[pos] = 0xA0; // Empty auxiliary map, the rest of the region stays zero
    pos += AUX_REGION_SIZE;

    out[pos++] = TLV_END;

    furi_check(pos == total_size);
    return total_size;
}
