# CubeIDE-friendly wrapper around the real CMake build.
#
# CubeIDE can open this firmware folder as an existing C/C++ project and run
# `make all`. This wrapper then configures and builds the CMake target that is
# actually maintained by the repository.

BUILD_DIR ?= ../build/firmware-cubeide
CUBE_F4 ?= C:/Users/karun/STM32Cube/Repository/STM32Cube_FW_F4_V1.28.3
CMAKE ?= cmake

ARM_GCC_BIN ?= C:/ST/STM32CubeIDE_2.0.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.13.3.rel1.win32_1.0.100.202509120712/tools/bin
NINJA_BIN ?= C:/ST/STM32CubeIDE_2.0.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.ninja.win32_1.1.0.202511131536/tools/bin
PROGRAMMER_CLI ?= C:/ST/STM32CubeIDE_2.0.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.300.202508131133/tools/bin/STM32_Programmer_CLI.exe

export PATH := $(ARM_GCC_BIN);$(NINJA_BIN);$(PATH)

.PHONY: all configure build clean flash

all: configure build

configure:
	$(CMAKE) -G Ninja -S . -B $(BUILD_DIR) "-DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/cmake/arm-none-eabi-gcc.cmake" "-DSTM32_CUBE_F4_PATH=$(CUBE_F4)"

build:
	$(CMAKE) --build $(BUILD_DIR)

clean:
	$(CMAKE) --build $(BUILD_DIR) --target clean

flash: all
	$(PROGRAMMER_CLI) -c port=SWD -w $(BUILD_DIR)/fmcw_radar_f429.elf -v -rst
