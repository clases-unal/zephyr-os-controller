# Sistema de Control Térmico de Alta Disponibilidad

Este repositorio contiene el firmware y la documentación para un sistema de control térmico robusto, diseñado con una arquitectura dividida entre un controlador principal (STM32) y un módulo de telemetría (ESP32).

## Arquitectura General

El sistema se compone de dos partes principales:
* **Controlador Principal (STM32L476RG):** Basado en Zephyr RTOS. Se encarga de la lectura del sensor NTC, el control PID/Histeresis del ventilador, la gestión de umbrales críticos de temperatura, el control de la planta térmica (Keep-Alive) y la interfaz de usuario local (OLED + Teclado + LEDs).
* **Módulo de Telemetría (ESP32):** (En desarrollo) Recibe datos del STM32 vía UART y los transmite a un dashboard remoto, manejando la reconexión y almacenamiento en caso de pérdida de red.

## Documentación

La documentación detallada del diseño se encuentra en el directorio `/docs`:
* `01-system-specification.md`: Especificaciones y requisitos del sistema.
* `02-firmware-architecture.md`: Arquitectura de software y tareas de Zephyr.
* `03-state-machines.md`: Diagramas de las máquinas de estado del sistema.
* `04-design-decisions.md`: Registro de decisiones de diseño y hardware.
* `05-validation-plan.md`: Plan de pruebas funcionales de software.

## Estructura del Repositorio

* `/firmware/stm32/`: Código fuente del controlador principal (Zephyr OS).
* `/firmware/esp32/`: Código fuente del módulo de telemetría (Próximamente).
* `/docs/`: Documentación técnica y diagramas.
* `/hardware/`: Diagramas de cableado y lista de materiales (BOM).
