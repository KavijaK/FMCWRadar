#include "fmcw_protocol.h"

#include <string.h>

/* Writes the 64-byte protocol header immediately before the DMA payload. The
 * host uses this metadata to identify slope order, timing, and error flags.
 */
void fmcw_frame_fill(uint8_t *frame_base, const fmcw_frame_meta_t *meta)
{
  fmcw_frame_header_t *hdr = (fmcw_frame_header_t *)frame_base;

  memset(hdr, 0, sizeof(*hdr));
  hdr->magic = FMCW_MAGIC;
  hdr->version = FMCW_PROTOCOL_VERSION;
  hdr->header_len = FMCW_HEADER_BYTES;
  hdr->frame_len = FMCW_FRAME_BYTES;
  hdr->slope_seq = meta->slope_seq;
  hdr->triangle_seq = meta->slope_seq / 2u;
  hdr->slope_id = meta->slope_id;
  hdr->flags = meta->flags;
  hdr->sample_rate_hz = FMCW_SAMPLE_RATE_HZ;
  hdr->samples_per_slope = FMCW_SAMPLES_PER_SLOPE;
  hdr->adc_bits = FMCW_ADC_BITS;
  hdr->pipeline_offset = FMCW_PIPELINE_OFFSET;
  hdr->timestamp_us = meta->timestamp_us;
  hdr->muxout_count = meta->muxout_count;
}
