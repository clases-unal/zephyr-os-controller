# Project Checkpoint — High-Availability Thermal Control System
STM32L476RG (Nucleo) + Zephyr OS + ESP32. This file is the absolute source of truth for the project's current state, consolidating all decisions made during previous sessions regarding hardware, firmware architecture, and control logic.

## 1. Confirmed Hardware & Pinout
- **NTC Sensor:** PA0 (ADC1_IN5, 12-bit). 10k resistor, Beta 3470K.
- **Fan PWM:** PB0 (TIM3_CH3). 25kHz base frequency.
- **Shift Register (SN74HC595N):** PA5 (SCK), PA7 (MOSI) via SPI1. PA6 (LATCH) via GPIO. (Single register design restored, superseding DEC-H-001).
- **OLED (SSD1306 128x64):** PC0 (SCL), PC1 (SDA) via I2C3 (100kHz). No external pull-ups (using internal STM32 pull-ups). Framebuffer memory allocation fixed in `prj.conf`.
- **ESP32 UART Telemetry:** PC10 (TX), PC11 (RX) via USART3.
- **4x4 Keypad:** Rows (PC7, PA9, PA8, PB10) as inputs with Pull-Up. Columns (PB4, PB5, PB3, PA10) as outputs.
- **User Button:** PC13 (EXTI, Nucleo B1).
- **Keep-Alive (Plant Control):** PA4 (GPIO output).

## 2. Firmware Architecture (Zephyr RTOS)
- **cooling_manager (Prio 2, 1024B):** Reads ADC, calculates thresholds, controls PWM, manages hysteresis and the 20s critical timer.
- **power_status_manager (Prio 2, 1024B):** Button EXTI + software debounce. Manages SYSTEM_ENABLED / DISABLED / SHUTDOWN.
- **temperature_manager (Prio 2, 1024B):** Samples ADC periodically and applies the Beta equation.
- **ui_keypad_task (Prio 4, 2048B):** Polling 4x4 keypad every 20ms, rendering OLED CFB.
- **led_representation_manager (Prio 4, 1024B):** Updates the 74HC595N via SPI based on system state.
- **heater_simulation_task (Prio 6, 1024B):** Controls PA4 Keep-Alive. Cedes to others.

## 3. Core Logic & State Machines
### 3.1 Cooling & Hysteresis
- **States:** COLD (40%), LOW (60%), MEDIUM (80%), HIGH (100%), CRITICAL (100%).
- **Hysteresis:** 2.0°C. Escalation is immediate; de-escalation requires `T < threshold - 2.0`.
- **Keep-Alive Restore Point:** Once Keep-Alive is revoked, it ONLY restores when the system successfully cools down to the **MEDIUM** threshold.

### 3.2 Security Escalation (20s Rule)
- When entering CRITICAL due to overtemperature, a 20s timer starts.
- **< 20s (CRITIC):** OLED shows `Lvl: CRITIC`. LEDs show full bar (Qe, Qf, Qg solid) + Qh blinking (500ms).
- **> 20s (OVERTMP):** Keep-Alive (PA4) is REVOKED. OLED shows `Lvl: OVERTMP`. LEDs switch to Siren/Beacon mode (Bar ON/Qh OFF -> Bar OFF/Qh ON).
- **Sensor Fault (S.FAULT):** Immediate CRITICAL. OLED: `Lvl: S.FAULT`. LEDs: Bar OFF, Qh solid. Keep-alive revoked.

## 4. Pending Tasks for Next Session
1. Generate structural and state machine diagrams (Mermaid/Draw.io XML) with specific color coding.
2. Update `firmware-stm32-README.md` to reflect the final operational state.
3. Synchronize all `.c` file header comments to match this checkpoint's logic.
