#include "stm32f4xx_it.h"

#include "fmcw_board_config.h"
#include "fmcw_capture.h"
#include "stm32f4xx_hal.h"
#include "usb_stream.h"

extern PCD_HandleTypeDef hpcd_USB_OTG_HS;

/* Keeps HAL_Delay() and the HAL timebase running. */
void SysTick_Handler(void)
{
  HAL_IncTick();
}

/* Handles DCMI DMA completion/error interrupts. A transfer complete event means
 * one full 1 ms slope payload is ready for a USB frame header.
 */
void DMA2_Stream1_IRQHandler(void)
{
  uint32_t lisr = DMA2->LISR;
  uint32_t clear = 0u;

  if ((lisr & DMA_LISR_TCIF1) != 0u) {
    clear |= DMA_LIFCR_CTCIF1;
    DMA2->LIFCR = clear;
    fmcw_capture_dma_tc_isr();
    return;
  }

  if ((lisr & (DMA_LISR_TEIF1 | DMA_LISR_DMEIF1 | DMA_LISR_FEIF1)) != 0u) {
    clear |= DMA_LIFCR_CTEIF1 | DMA_LIFCR_CDMEIF1 | DMA_LIFCR_CFEIF1;
    DMA2->LIFCR = clear;
    fmcw_capture_dma_error_isr(lisr);
  }
}

/* Converts DCMI overrun/sync faults into frame flags for host-side filtering. */
void DCMI_IRQHandler(void)
{
  uint32_t mis = DCMI->MISR;
  uint32_t clear = 0u;

  if ((mis & DCMI_MIS_OVR_MIS) != 0u) {
    clear |= DCMI_ICR_OVR_ISC;
    fmcw_capture_note_dcmi_error(FMCW_FLAG_DCMI_OVR);
  }
  if ((mis & DCMI_MIS_ERR_MIS) != 0u) {
    clear |= DCMI_ICR_ERR_ISC;
    fmcw_capture_note_dcmi_error(FMCW_FLAG_ALIGN_LOST);
  }
  if (clear != 0u) {
    DCMI->ICR = clear;
  }
}

/* TIM9 has two jobs: CC2 performs the one-shot aligned DCMI start, and CC1
 * records later MUXOUT slope edges as a heartbeat.
 */
void TIM1_BRK_TIM9_IRQHandler(void)
{
  uint32_t sr = TIM9->SR;

  if ((sr & TIM_SR_CC2IF) != 0u) {
    TIM9->SR = (uint16_t)~TIM_SR_CC2IF;
    if ((DCMI->CR & DCMI_CR_CAPTURE) == 0u) {
      FMCW_DCMI_HSYNC_GPIO_PORT->BSRR = FMCW_DCMI_HSYNC_PIN;
      /* PA7 is the board-level feedback copy of PA4 HSYNC. If it did not go
       * high, still start capture but mark the first frame as alignment-suspect.
       */
      if ((FMCW_DCMI_HSYNC_FEEDBACK_GPIO_PORT->IDR &
           FMCW_DCMI_HSYNC_FEEDBACK_PIN) == 0u) {
        fmcw_capture_note_dcmi_error(FMCW_FLAG_ALIGN_LOST);
      }
      DCMI->CR |= DCMI_CR_CAPTURE;
      TIM9->DIER &= ~TIM_DIER_CC2IE;
      fmcw_capture_start_aligned();
    }
  }

  if ((sr & TIM_SR_CC1IF) != 0u) {
    TIM9->SR = (uint16_t)~TIM_SR_CC1IF;
    fmcw_capture_note_muxout();
  }
}

/* Lets the HAL USB device stack retire endpoint transfers and control traffic. */
void OTG_HS_IRQHandler(void)
{
  HAL_PCD_IRQHandler(&hpcd_USB_OTG_HS);
}
