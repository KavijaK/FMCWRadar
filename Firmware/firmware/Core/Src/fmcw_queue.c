#include "fmcw_queue.h"

/* Empties a small ring queue used for free DMA slots and USB-ready slots. */
void fmcw_queue_reset(fmcw_queue_t *q)
{
  q->head = 0u;
  q->tail = 0u;
  q->count = 0u;
}

/* Adds one slot id at the tail; returns false if the queue is full. */
bool fmcw_queue_push(fmcw_queue_t *q, uint8_t value)
{
  if (q->count >= FMCW_QUEUE_CAPACITY) {
    return false;
  }

  q->item[q->tail] = value;
  q->tail = (uint8_t)((q->tail + 1u) % FMCW_QUEUE_CAPACITY);
  q->count++;
  return true;
}

/* Removes one slot id from the head; returns false if the queue is empty. */
bool fmcw_queue_pop(fmcw_queue_t *q, uint8_t *value)
{
  if (q->count == 0u) {
    return false;
  }

  *value = q->item[q->head];
  q->head = (uint8_t)((q->head + 1u) % FMCW_QUEUE_CAPACITY);
  q->count--;
  return true;
}

/* Reads the next slot id without removing it. */
bool fmcw_queue_peek(const fmcw_queue_t *q, uint8_t *value)
{
  if (q->count == 0u) {
    return false;
  }

  *value = q->item[q->head];
  return true;
}

/* Reports the number of queued slot ids. */
uint8_t fmcw_queue_count(const fmcw_queue_t *q)
{
  return q->count;
}
