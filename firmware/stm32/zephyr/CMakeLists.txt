# zephyr/CMakeLists.txt — Sistema de Control Térmico de Alta Disponibilidad (STM32)
# Layout específico de PlatformIO (no usa find_package(Zephyr) como west puro;
# PlatformIO inyecta su propio boilerplate.cmake). Ver firmware/stm32/README.md.
#
# Nota: las rutas en target_sources son RELATIVAS a esta carpeta (zephyr/), por eso
# todas empiezan con ../src/.

cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(thermal_control_system)

target_sources(app PRIVATE
    ../src/main.c

    # state/ — estructuras globales + mutex, sin lógica de negocio
    ../src/state/control_state.c
    ../src/state/transmission_state.c
    ../src/state/config_state.c
    ../src/state/telemetry_state.c
    ../src/state/system_state.c

    # tasks/ — los 7 hilos del sistema
    ../src/tasks/power_status_manager.c
    ../src/tasks/temperature_manager.c
    ../src/tasks/cooling_manager.c
    ../src/tasks/ui_keypad_task.c
    ../src/tasks/led_representation_manager.c
    ../src/tasks/heater_simulation_task.c
    ../src/tasks/esp32_comm_manager.c

    # drivers/ — código de bajo nivel reutilizable
    ../src/drivers/ntc_sensor.c
    ../src/drivers/matrix_keypad.c
    ../src/drivers/shift_register.c

    # protocol/ — construcción/parseo de paquetes UART
    ../src/protocol/uart_packet.c
)

# Permite hacer #include "state/control_state.h" desde cualquier archivo en src/
target_include_directories(app PRIVATE ../src)
