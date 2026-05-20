cmake_minimum_required(VERSION 3.22)

project(fmcw_radar_f429 C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT STM32_CUBE_F4_PATH)
  message(FATAL_ERROR "Set -DSTM32_CUBE_F4_PATH to your STM32CubeF4 root")
endif()

set(CUBE "${STM32_CUBE_F4_PATH}")
set(HAL "${CUBE}/Drivers/STM32F4xx_HAL_Driver")
set(CMSIS "${CUBE}/Drivers/CMSIS")
set(USB_DEVICE "${CUBE}/Middlewares/ST/STM32_USB_Device_Library")

if(NOT EXISTS "${HAL}/Inc/stm32f4xx_hal.h")
  message(FATAL_ERROR "STM32_CUBE_F4_PATH does not look like STM32CubeF4: ${STM32_CUBE_F4_PATH}")
endif()

add_executable(fmcw_radar_f429.elf
  startup/startup_stm32f429xx.s
  Core/Src/main.c
  Core/Src/fmcw_adf4158.c
  Core/Src/fmcw_capture.c
  Core/Src/fmcw_frame.c
  Core/Src/fmcw_hardware.c
  Core/Src/fmcw_queue.c
  Core/Src/fmcw_time.c
  Core/Src/stm32f4xx_it.c
  Core/Src/syscalls.c
  Core/Src/usbd_conf.c
  Core/Src/usbd_desc.c
  Core/Src/usbd_vendor_stream.c
  Core/Src/usb_stream.c
  "${HAL}/Src/stm32f4xx_hal.c"
  "${HAL}/Src/stm32f4xx_hal_cortex.c"
  "${HAL}/Src/stm32f4xx_hal_dma.c"
  "${HAL}/Src/stm32f4xx_hal_flash.c"
  "${HAL}/Src/stm32f4xx_hal_flash_ex.c"
  "${HAL}/Src/stm32f4xx_hal_gpio.c"
  "${HAL}/Src/stm32f4xx_hal_pcd.c"
  "${HAL}/Src/stm32f4xx_hal_pcd_ex.c"
  "${HAL}/Src/stm32f4xx_hal_pwr.c"
  "${HAL}/Src/stm32f4xx_hal_pwr_ex.c"
  "${HAL}/Src/stm32f4xx_hal_rcc.c"
  "${HAL}/Src/stm32f4xx_hal_rcc_ex.c"
  "${HAL}/Src/stm32f4xx_hal_spi.c"
  "${HAL}/Src/stm32f4xx_ll_usb.c"
  "${CMSIS}/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.c"
  "${USB_DEVICE}/Core/Src/usbd_core.c"
  "${USB_DEVICE}/Core/Src/usbd_ctlreq.c"
  "${USB_DEVICE}/Core/Src/usbd_ioreq.c"
)

target_include_directories(fmcw_radar_f429.elf PRIVATE
  Core/Inc
  "${HAL}/Inc"
  "${HAL}/Inc/Legacy"
  "${CMSIS}/Include"
  "${CMSIS}/Device/ST/STM32F4xx/Include"
  "${USB_DEVICE}/Core/Inc"
)

target_compile_definitions(fmcw_radar_f429.elf PRIVATE
  STM32F429xx
  USE_HAL_DRIVER
)

target_compile_options(fmcw_radar_f429.elf PRIVATE
  -Wall -Wextra -Werror
  -ffunction-sections -fdata-sections
  $<$<COMPILE_LANGUAGE:C>:-fno-common>
)

target_link_options(fmcw_radar_f429.elf PRIVATE
  -T${CMAKE_CURRENT_LIST_DIR}/linker/STM32F429VETx_FLASH.ld
  -Wl,--gc-sections
  -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/fmcw_radar_f429.map
  --specs=nano.specs
  --specs=nosys.specs
)

add_custom_command(TARGET fmcw_radar_f429.elf POST_BUILD
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:fmcw_radar_f429.elf>
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:fmcw_radar_f429.elf> ${CMAKE_CURRENT_BINARY_DIR}/fmcw_radar_f429.bin
  COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:fmcw_radar_f429.elf> ${CMAKE_CURRENT_BINARY_DIR}/fmcw_radar_f429.hex
)
