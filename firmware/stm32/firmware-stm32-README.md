# firmware/stm32 — Nucleo-L476RG

Firmware principal del Sistema de Control Térmico de Alta Disponibilidad.
Framework: Zephyr OS, gestionado con PlatformIO.

## Build y Flasheo

```bash
pio run                 # Compilar
pio run -t upload       # Compilar y flashear por ST-LINK
pio device monitor      # Monitor serial (115200 8N1)

```

## Hardware conectado a esta placa (Pinout Final)

| Función | Pines | Periférico | Notas |
| --- | --- | --- | --- |
| Termistor NTC | PA0 | ADC1 canal 5 | Divisor con resistencia de 10k, Beta 3470K |
| PWM ventilador | PB0 | TIM3_CH3 | 25kHz base frequency |
| Registro de desplazamiento (74HC595) | PA5 (SCK), PA6 (LATCH), PA7 (MOSI) | SPI1 + GPIO | Controla barra LED y estados |
| OLED SSD1306 128×64 | PC0 (SCL), PC1 (SDA) | I2C3 (100kHz) | Sin pull-ups externos (usa los internos del STM32) |
| Enlace serie con ESP32 | PC10 (TX), PC11 (RX) | USART3 | Telemetría y Heartbeat |
| Teclado 4×4 — Filas | PC7, PA9, PA8, PB10 | GPIO (Input) | Configurado con Pull-Up interno |
| Teclado 4×4 — Columnas | PB4, PB5, PB3, PA10 | GPIO (Output) | Escaneo de matriz |
| Botón de usuario | PC13 | GPIO + EXTI | Físico de la Nucleo (B1) |
| Keep-alive (Planta) | PA4 | GPIO (Output) | Autorización de calentamiento |

**Nota:** Los pines PA13/PA14 (SWDIO/SWCLK) están reservados para programación ST-LINK y no deben ser reutilizados.

## Arquitectura de Tareas (Zephyr RTOS)

* **cooling_manager:** (Prioridad 2) Lee ADC, calcula umbrales, controla PWM y maneja la regla de seguridad de 20s para el estado CRITICAL.
* **power_status_manager:** (Prioridad 2) Maneja interrupciones del botón (debounce) y el estado global (ENABLED / DISABLED / SHUTDOWN).
* **temperature_manager:** (Prioridad 2) Muestrea el ADC periódicamente.
* **ui_keypad_task:** (Prioridad 4) Polling del teclado (20ms) y renderizado OLED (CFB).
* **led_representation_manager:** (Prioridad 4) Actualiza el 74HC595N vía SPI según el estado del sistema.
* **heater_simulation_task:** (Prioridad 6) Controla el Keep-Alive (PA4).
