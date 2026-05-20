#ifndef FMCW_QUEUE_H
#define FMCW_QUEUE_H

#include <stdbool.h>
#include <stdint.h>

#define FMCW_QUEUE_CAPACITY 4u
#define FMCW_SLOT_INVALID   0xffu

typedef struct {
  uint8_t item[FMCW_QUEUE_CAPACITY];
  uint8_t head;
  uint8_t tail;
  uint8_t count;
} fmcw_queue_t;

void fmcw_queue_reset(fmcw_queue_t *q);
bool fmcw_queue_push(fmcw_queue_t *q, uint8_t value);
bool fmcw_queue_pop(fmcw_queue_t *q, uint8_t *value);
bool fmcw_queue_peek(const fmcw_queue_t *q, uint8_t *value);
uint8_t fmcw_queue_count(const fmcw_queue_t *q);

#endif

