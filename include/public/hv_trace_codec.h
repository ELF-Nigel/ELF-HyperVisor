#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define HV_TRACE_ENCODING_RAW 0u
#define HV_TRACE_ENCODING_DELTA 1u
#define HV_TRACE_ENCODED_EVENT_MAX 25u

typedef struct hv_trace_stream_header_t {
    uint32_t version;
    uint32_t encoding;
    uint32_t count;
    uint32_t payload_size;
} hv_trace_stream_header_t;

static __inline size_t hv_trace_put_varint(uint8_t* out, uint64_t value) {
    size_t n = 0;
    do {
        uint8_t byte = (uint8_t)(value & 127);
        value >>= 7;
        out[n++] = (uint8_t)(byte | (value ? 128 : 0));
    } while (value);
    return n;
}

static __inline size_t hv_trace_get_varint(const uint8_t* in, size_t size, uint64_t* value) {
    uint64_t result = 0;
    size_t i;
    if (!in || !value) return 0;
    for (i = 0; i < size && i < 10; ++i) {
        uint8_t byte = in[i];
        if (i == 9 && byte > 1) return 0;
        result |= (uint64_t)(byte & 127) << (i * 7);
        if (!(byte & 128)) {
            *value = result;
            return i + 1;
        }
    }
    return 0;
}

static __inline size_t hv_trace_encode_event(uint8_t* out, size_t size,
    uint64_t* previous_tsc, uint64_t tsc, uint64_t reason, uint32_t cpu) {
    uint8_t encoded[HV_TRACE_ENCODED_EVENT_MAX];
    size_t n;
    if (!out || !previous_tsc) return 0;
    n = hv_trace_put_varint(encoded, tsc - *previous_tsc);
    n += hv_trace_put_varint(encoded + n, reason);
    n += hv_trace_put_varint(encoded + n, cpu);
    if (n > size) return 0;
    memcpy(out, encoded, n);
    *previous_tsc = tsc;
    return n;
}

static __inline size_t hv_trace_decode_event(const uint8_t* in, size_t size,
    uint64_t* previous_tsc, uint64_t* tsc, uint64_t* reason, uint32_t* cpu) {
    uint64_t fields[3];
    size_t offset = 0, i;
    if (!in || !previous_tsc || !tsc || !reason || !cpu) return 0;
    for (i = 0; i < 3; ++i) {
        size_t n = hv_trace_get_varint(in + offset, size - offset, &fields[i]);
        if (!n) return 0;
        offset += n;
    }
    if (fields[2] > UINT32_MAX) return 0;
    *tsc = *previous_tsc + fields[0];
    *reason = fields[1];
    *cpu = (uint32_t)fields[2];
    *previous_tsc = *tsc;
    return offset;
}
