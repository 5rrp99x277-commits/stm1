cmake_minimum_required(VERSION 3.22)
project(stm32h7_oled_scope C ASM)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)
set(CMAKE_OBJCOPY arm-none-eabi-objcopy)
set(CMAKE_SIZE arm-none-eabi-size)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# MCU флаги
set(CPU_FLAGS "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")

set(CMAKE_C_FLAGS   "${CPU_FLAGS} -Wall -fdata-sections -ffunction-sections")
set(CMAKE_ASM_FLAGS "${CPU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS
    "${CPU_FLAGS} -T${CMAKE_SOURCE_DIR}/STM32H743VITX_FLASH.ld \
     -Wl,--gc-sections -Wl,-Map=${PROJECT_NAME}.map \
     --specs=nano.specs -lnosys -lm")

# Defines
add_definitions(
    -DSTM32H743xx
    -DUSE_HAL_DRIVER
    -DARM_MATH_CM7
)

# Включаемые пути
include_directories(
    Core/Inc
    Drivers/SSD1306
    Drivers/STM32H7xx_HAL_Driver/Inc
    Drivers/CMSIS/Device/ST/STM32H7xx/Include
    Drivers/CMSIS/Include
    Drivers/CMSIS/DSP/Include
)

# Исходники
file(GLOB_RECURSE SOURCES
    Core/Src/*.c
    Drivers/SSD1306/*.c
    Drivers/STM32H7xx_HAL_Driver/Src/*.c
    startup_stm32h743xx.s
)

# CMSIS-DSP (статическая библиотека из CubeMX или отдельно)
# arm_cortexM7lfdp_math.lib / libarm_cortexM7lfdp_math.a
find_library(CMSIS_DSP_LIB
    NAMES arm_cortexM7lfdp_math
    PATHS Drivers/CMSIS/DSP/Lib/GCC
)

add_executable(${PROJECT_NAME}.elf ${SOURCES})
target_link_libraries(${PROJECT_NAME}.elf ${CMSIS_DSP_LIB})

# Генерация .bin и .hex
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}.elf>
            ${PROJECT_NAME}.bin
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${PROJECT_NAME}.elf>
            ${PROJECT_NAME}.hex
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}.elf>
)
