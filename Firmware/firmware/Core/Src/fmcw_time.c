#include "fmcw_time.h"

#include "stm32f4xx.h"

static volatile uint32_t s_last_cycles;
static volatile uint64_t s_cycle_high;

/* Enables the Cortex-M DWT cycle counter for low-overhead frame timestamps. */
void fmcw_time_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0u;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  s_last_cycles = 0u;
  s_cycle_high = 0u;
}

/* Converts the extended cycle counter into microseconds, handling 32-bit wrap. */
uint64_t fmcw_time_us(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();

  uint32_t now = DWT->CYCCNT;
  if (now < s_last_cycles) {
    s_cycle_high += (1ull << 32);
  }
  s_last_cycles = now;
  uint64_t cycles = s_cycle_high | now;

  __set_PRIMASK(primask);

  return cycles / (SystemCoreClock / 1000000u);
}
