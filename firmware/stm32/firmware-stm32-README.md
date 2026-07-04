# firmware/stm32 — Nucleo-L476RG

Firmware principal del Sistema de Control Térmico de Alta Disponibilidad.
Framework: Zephyr OS 4.4.0, gestionado con PlatformIO.

## Build

```bash
pio run                 # compilar
pio run -t upload       # compilar y flashear por ST-LINK
pio device monitor       # monitor serial (115200 8N1, ajustar si cambiaste CONFIG_UART)
```

## Hardware conectado a esta placa

| Función | Pines | Periférico |
|---|---|---|
| Termistor NTC (divisor resistivo) | PA0 | ADC1 canal 5 |
| PWM ventilador | PB0 | TIM3_CH3, 25kHz |
| Registro de desplazamiento SN74HC595N (8 LEDs) | PA5 (SCK), PA6 (LATCH), PA7 (MOSI) | SPI1 |
| OLED SSD1306 128×64 | PC0 (SCL), PC1 (SDA) | I2C3 |
| Enlace serie con ESP32 | PC10 (TX), PC11 (RX) | USART3 |
| Teclado matricial 4×4 — filas | PC7, PA9, PA8, PB10 | GPIO salida |
| Teclado matricial 4×4 — columnas | PB4, PB5, PB3, PA10 | GPIO entrada, pull-up |
| Botón de usuario (B1 físico de la Nucleo) | PC13 | GPIO + EXTI |
| Keep-alive hacia planta térmica externa (simulada) | PA4 | GPIO salida |

Ver `zephyr/boards/nucleo_l476rg.overlay` para la definición completa — ese
archivo es la fuente de verdad, esta tabla es solo un resumen para no tener
que abrirlo cada vez.

**Pines evitados a propósito, no los reutilices:** PA13/PA14 (SWDIO/SWCLK —
romperían la programación por USB).

## Estructura de `src/`

```
src/
├── main.c                     Arranque: inicializa los 5 estados compartidos
├── state/                     5 estructuras de estado, cada una con su mutex
│   ├── control_state.*        Lazo de control térmico (temperatura, duty, umbral)
│   ├── config_state.*         Umbrales editables por el usuario
│   ├── system_state.*         Banderas globales (habilitado, shutdown)
│   ├── telemetry_state.*      Diagnóstico y banderas de error
│   └── transmission_state.*   Estado del enlace con el ESP32
├── drivers/                   Acceso directo a hardware, sin lógica de negocio
│   ├── ntc_sensor.*           ADC + ecuación Beta -> temperatura
│   ├── matrix_keypad.*        Escaneo de teclado 4×4
│   └── shift_register.*       SN74HC595N genérico por SPI
├── protocol/
│   └── uart_packet.*          Construcción/parseo de tramas hacia el ESP32
└── tasks/                     Un hilo Zephyr (K_THREAD_DEFINE) por archivo
    ├── temperature_manager.c
    ├── cooling_manager.c
    ├── heater_simulation_task.c
    ├── led_representation_manager.c
    ├── power_status_manager.c
    ├── ui_keypad_task.c
    └── esp32_comm_manager.c
```

## Estado funcional (última actualización de esta sesión de trabajo)

| Módulo | Estado |
|---|---|
| ADC / NTC / umbrales con histéresis | Funcional |
| PWM ventilador | Funcional |
| LEDs (registro de desplazamiento único) | Implementado, pendiente de prueba en hardware real |
| OLED | Implementado (fix del framebuffer), pendiente de prueba en hardware real |
| Teclado matricial | Implementado desde antes, nunca probado en hardware real |
| Botón de apagado | Implementado, semántica de pulsación corta con un bug conocido (ver `docs/04-design-decisions.md`) |
| Comunicación ESP32 (heartbeat, detección de desconexión) | Implementado, pendiente de prueba con el ESP32 real |
| Persistencia (NVS) | Descartada deliberadamente — ver `docs/04-design-decisions.md` |

Para el detalle de qué se probó y cómo, ver `docs/05-validation-plan.md`.
