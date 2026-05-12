################################################################################
# MRS Version: 2.4.0
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../User/HX711.c \
../User/L298N.c \
../User/TMC2209.c \
../User/ch32_app.c \
../User/ch32v20x_it.c \
../User/esp32_com.c \
../User/inner_door.c \
../User/main.c \
../User/system_ch32v20x.c 

C_DEPS += \
./User/HX711.d \
./User/L298N.d \
./User/TMC2209.d \
./User/ch32_app.d \
./User/ch32v20x_it.d \
./User/esp32_com.d \
./User/inner_door.d \
./User/main.d \
./User/system_ch32v20x.d 

OBJS += \
./User/HX711.o \
./User/L298N.o \
./User/TMC2209.o \
./User/ch32_app.o \
./User/ch32v20x_it.o \
./User/esp32_com.o \
./User/inner_door.o \
./User/main.o \
./User/system_ch32v20x.o 

DIR_OBJS += \
./User/*.o \

DIR_DEPS += \
./User/*.d \

DIR_EXPANDS += \
./User/*.234r.expand \


# Each subdirectory must supply rules for building sources it contributes
User/%.o: ../User/%.c
	@	riscv-none-embed-gcc -march=rv32imacxw -mabi=ilp32 -msmall-data-limit=8 -msave-restore -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -fno-common -Wunused -Wuninitialized -g -I"c:/beifen/ESP32_P4_EV/Embedded_competition/Embedded_competition_CH32/Debug" -I"c:/beifen/ESP32_P4_EV/Embedded_competition/Embedded_competition_CH32/Core" -I"c:/beifen/ESP32_P4_EV/Embedded_competition/Embedded_competition_CH32/User" -I"c:/beifen/ESP32_P4_EV/Embedded_competition/Embedded_competition_CH32/Peripheral/inc" -std=gnu99 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@)" -c -o "$@" "$<"

