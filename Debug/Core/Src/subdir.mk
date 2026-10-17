################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Core/Src/bl0906.c \
../Core/Src/crc.c \
../Core/Src/dhcp.c \
../Core/Src/gpio.c \
../Core/Src/html_flash.c \
../Core/Src/main.c \
../Core/Src/mqtt_client.c \
../Core/Src/mqtt_config.c \
../Core/Src/octospi_ll.c \
../Core/Src/ota.c \
../Core/Src/secure_packet.c \
../Core/Src/spi.c \
../Core/Src/stm32h5xx_hal_msp.c \
../Core/Src/stm32h5xx_it.c \
../Core/Src/syscalls.c \
../Core/Src/sysmem.c \
../Core/Src/system_stm32h5xx.c \
../Core/Src/usart.c \
../Core/Src/web_cmnd.c \
../Core/Src/web_config.c 

OBJS += \
./Core/Src/bl0906.o \
./Core/Src/crc.o \
./Core/Src/dhcp.o \
./Core/Src/gpio.o \
./Core/Src/html_flash.o \
./Core/Src/main.o \
./Core/Src/mqtt_client.o \
./Core/Src/mqtt_config.o \
./Core/Src/octospi_ll.o \
./Core/Src/ota.o \
./Core/Src/secure_packet.o \
./Core/Src/spi.o \
./Core/Src/stm32h5xx_hal_msp.o \
./Core/Src/stm32h5xx_it.o \
./Core/Src/syscalls.o \
./Core/Src/sysmem.o \
./Core/Src/system_stm32h5xx.o \
./Core/Src/usart.o \
./Core/Src/web_cmnd.o \
./Core/Src/web_config.o 

C_DEPS += \
./Core/Src/bl0906.d \
./Core/Src/crc.d \
./Core/Src/dhcp.d \
./Core/Src/gpio.d \
./Core/Src/html_flash.d \
./Core/Src/main.d \
./Core/Src/mqtt_client.d \
./Core/Src/mqtt_config.d \
./Core/Src/octospi_ll.d \
./Core/Src/ota.d \
./Core/Src/secure_packet.d \
./Core/Src/spi.d \
./Core/Src/stm32h5xx_hal_msp.d \
./Core/Src/stm32h5xx_it.d \
./Core/Src/syscalls.d \
./Core/Src/sysmem.d \
./Core/Src/system_stm32h5xx.d \
./Core/Src/usart.d \
./Core/Src/web_cmnd.d \
./Core/Src/web_config.d 


# Each subdirectory must supply rules for building sources it contributes
Core/Src/%.o Core/Src/%.su Core/Src/%.cyclo: ../Core/Src/%.c Core/Src/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m33 -std=gnu11 -g3 -DDEBUG -DUSE_FULL_LL_DRIVER -DUSE_HAL_DRIVER -DSTM32H523xx -c -I../Core/Inc -I../Drivers/STM32H5xx_HAL_Driver/Inc -I../Drivers/STM32H5xx_HAL_Driver/Inc/Legacy -I../Drivers/CMSIS/Device/ST/STM32H5xx/Include -I../Drivers/CMSIS/Include -Os -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfpu=fpv5-sp-d16 -mfloat-abi=hard -mthumb -o "$@"

clean: clean-Core-2f-Src

clean-Core-2f-Src:
	-$(RM) ./Core/Src/bl0906.cyclo ./Core/Src/bl0906.d ./Core/Src/bl0906.o ./Core/Src/bl0906.su ./Core/Src/crc.cyclo ./Core/Src/crc.d ./Core/Src/crc.o ./Core/Src/crc.su ./Core/Src/dhcp.cyclo ./Core/Src/dhcp.d ./Core/Src/dhcp.o ./Core/Src/dhcp.su ./Core/Src/gpio.cyclo ./Core/Src/gpio.d ./Core/Src/gpio.o ./Core/Src/gpio.su ./Core/Src/html_flash.cyclo ./Core/Src/html_flash.d ./Core/Src/html_flash.o ./Core/Src/html_flash.su ./Core/Src/main.cyclo ./Core/Src/main.d ./Core/Src/main.o ./Core/Src/main.su ./Core/Src/mqtt_client.cyclo ./Core/Src/mqtt_client.d ./Core/Src/mqtt_client.o ./Core/Src/mqtt_client.su ./Core/Src/mqtt_config.cyclo ./Core/Src/mqtt_config.d ./Core/Src/mqtt_config.o ./Core/Src/mqtt_config.su ./Core/Src/octospi_ll.cyclo ./Core/Src/octospi_ll.d ./Core/Src/octospi_ll.o ./Core/Src/octospi_ll.su ./Core/Src/ota.cyclo ./Core/Src/ota.d ./Core/Src/ota.o ./Core/Src/ota.su ./Core/Src/secure_packet.cyclo ./Core/Src/secure_packet.d ./Core/Src/secure_packet.o ./Core/Src/secure_packet.su ./Core/Src/spi.cyclo ./Core/Src/spi.d ./Core/Src/spi.o ./Core/Src/spi.su ./Core/Src/stm32h5xx_hal_msp.cyclo ./Core/Src/stm32h5xx_hal_msp.d ./Core/Src/stm32h5xx_hal_msp.o ./Core/Src/stm32h5xx_hal_msp.su ./Core/Src/stm32h5xx_it.cyclo ./Core/Src/stm32h5xx_it.d ./Core/Src/stm32h5xx_it.o ./Core/Src/stm32h5xx_it.su ./Core/Src/syscalls.cyclo ./Core/Src/syscalls.d ./Core/Src/syscalls.o ./Core/Src/syscalls.su ./Core/Src/sysmem.cyclo ./Core/Src/sysmem.d ./Core/Src/sysmem.o ./Core/Src/sysmem.su ./Core/Src/system_stm32h5xx.cyclo ./Core/Src/system_stm32h5xx.d ./Core/Src/system_stm32h5xx.o ./Core/Src/system_stm32h5xx.su ./Core/Src/usart.cyclo ./Core/Src/usart.d ./Core/Src/usart.o ./Core/Src/usart.su ./Core/Src/web_cmnd.cyclo ./Core/Src/web_cmnd.d ./Core/Src/web_cmnd.o ./Core/Src/web_cmnd.su ./Core/Src/web_config.cyclo ./Core/Src/web_config.d ./Core/Src/web_config.o ./Core/Src/web_config.su

.PHONY: clean-Core-2f-Src

