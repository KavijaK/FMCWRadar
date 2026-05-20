#ifndef FMCW_PROTOCOL_H
#define FMCW_PROTOCOL_H

#include <stdint.h>

#define FMCW_MAGIC              0x52444152u
#define FMCW_PROTOCOL_VERSION   1u
#define FMCW_HEADER_BYTES       64u
#define FMCW_PAYLOAD_BYTES      20000u
#define FMCW_FRAME_BYTES        (FMCW_HEADER_BYTES + FMCW_PAYLOAD_BYTES)
#define FMCW_SAMPLE_RATE_HZ     10000000u
#define FMCW_SAMPLES_PER_SLOPE  10000u
#define FMCW_ADC_BITS           12u
#define FMCW_PIPELINE_OFFSET    3

#define FMCW_FLAG_DROPPED       (1u << 0)
#define FMCW_FLAG_DCMI_OVR      (1u << 1)
#define FMCW_FLAG_USB_LATE      (1u << 2)
#define FMCW_FLAG_MUX_MISSING   (1u << 3)
#define FMCW_FLAG_ALIGN_LOST    (1u << 4)

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint16_t version;
  uint16_t header_len;
  uint32_t frame_len;
  uint32_t slope_seq;
  uint32_t triangle_seq;
  uint16_t slope_id;
  uint16_t flags;
  uint32_t sample_rate_hz;
  uint32_t samples_per_slope;
  uint16_t adc_bits;
  int16_t pipeline_offset;
  uint64_t timestamp_us;
  uint32_t muxout_count;
  uint32_t reserved[4];
} fmcw_frame_header_t;

_Static_assert(sizeof(fmcw_frame_header_t) == FMCW_HEADER_BYTES,
               "FMCW USB frame header must stay 64 bytes");

typedef struct {
  uint32_t slope_seq;
  uint32_t muxout_count;
  uint16_t slope_id;
  uint16_t flags;
  uint64_t timestamp_us;
} fmcw_frame_meta_t;

void fmcw_frame_fill(uint8_t *frame_base, const fmcw_frame_meta_t *meta);

#endif

