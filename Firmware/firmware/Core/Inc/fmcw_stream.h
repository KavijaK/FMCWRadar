#ifndef FMCW_STREAM_H
#define FMCW_STREAM_H

#include "stm32f4xx_hal.h"

#define FMCW_STREAM_MAGIC              0x52444152UL
#define FMCW_STREAM_HEADER_BYTES       64U
#define FMCW_STREAM_ADC_BITS           12U
#define FMCW_STREAM_SAMPLE_RATE_HZ     10000000UL
#define FMCW_STREAM_SAMPLES_PER_SLOPE  10000UL
#define FMCW_STREAM_WORDS_PER_SLOPE    5000UL
#define FMCW_STREAM_PAYLOAD_BYTES      (FMCW_STREAM_WORDS_PER_SLOPE * 4U)
#define FMCW_STREAM_FRAME_BYTES        20480U
#define FMCW_STREAM_PADDING_BYTES      (FMCW_STREAM_FRAME_BYTES - \
                                        FMCW_STREAM_HEADER_BYTES - \
                                        FMCW_STREAM_PAYLOAD_BYTES)

void FMCWStream_Init(void);
uint8_t FMCWStream_Start(void);
void FMCWStream_Task(void);
uint8_t FMCWStream_DMA_IRQHandler(void);
void FMCWStream_MuxoutIRQHandler(void);

uint8_t FMCWStream_USBNextBuffer(const uint8_t **data, uint32_t *length);
void FMCWStream_USBTxDone(const uint8_t *data, uint32_t length);

#endif /* FMCW_STREAM_H */
