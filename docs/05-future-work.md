# 05-FUTURE-WORK
## Hoja de Ruta y Mejoras Futuras del Sistema

Este documento establece la hoja de ruta (*roadmap*) para la evolución del sistema. Si bien el prototipo actual cumple con los requisitos críticos de control térmico de alta disponibilidad, se han identificado áreas de mejora para escalar el proyecto desde un prototipo en placa de pruebas (*protoboard*) hacia una solución de grado industrial.

---

## 1. Topología Física Actual (Diagrama de Bloques)

*Nota: La representación gráfica de las conexiones actuales del hardware, incluyendo el aislamiento optoacoplado de la etapa de potencia, la interfaz de usuario y la telemetría visual, se encuentra detallada en el diagrama de bloques adjunto en formato `.svg`.*

El esquema actual demuestra la viabilidad de la arquitectura dividida en tres capas (Aquisición analógica, Control lógico y Potencia aislada), pero está sujeto a las limitaciones físicas intrínsecas de las conexiones no soldadas.

---

## 2. Fase II: Integración IoT y Telemetría Remota (ESP32)

Actualmente, el firmware del STM32L476RG recopila estadísticas vitales (ciclos de arranque, tiempo en estado crítico, causas de falla térmica y de hardware) en la estructura protegida `TelemetryState`. 

### Estado Actual
* El hardware STM32 tiene configurado y habilitado el periférico **USART3** en los pines `PC10` (TX) y `PC11` (RX).
* El hilo `esp32_comm_manager` (actualmente en fase de prototipado en el código) está diseñado para empaquetar estos datos.

### Mejora Propuesta
Desarrollar e integrar el firmware definitivo para el coprocesador Wi-Fi/Bluetooth **ESP32**. El objetivo es que el ESP32 actúe como un puente (*Gateway*) que reciba las tramas seriales desde el STM32 y las publique en un servidor MQTT o un Dashboard (ej. Grafana). Esto permitirá:
* Monitoreo remoto en tiempo real sin depender de la pantalla OLED local.
* Registro histórico (*datalogging*) de las curvas de temperatura para análisis de tendencias y mantenimiento predictivo.

---

## 3. Migración a Circuito Impreso (Custom PCB) y Compatibilidad Electromagnética (EMC)

El uso de *protoboards* para este prototipo fue ideal para la validación lógica, pero presenta vulnerabilidades frente a entornos industriales reales.

### Mejoras a implementar en el diseño del PCB:
1. **Integridad de Señal:** Las señales de alta velocidad, como el bus SPI a los registros de desplazamiento y el PWM a 25 kHz del ventilador, pueden sufrir acoplamiento parásito (diafonía) en cables sueltos. Un PCB de 2 o 4 capas con un plano de tierra (GND plane) sólido eliminará estas interferencias.
2. **Aislamiento de Zonas:** El diseño del circuito impreso permitirá separar físicamente la "zona ruidosa" de potencia (Mosfet, Regulador de 12V a 5V) de la "zona analógica sensible" (Divisor de tensión del sensor NTC).
3. **Resistencias de Soporte Físicas:** Incorporar resistencias de *Pull-Up* de 4.7 kΩ físicamente en el bus I2C de la pantalla OLED, reemplazando la dependencia exclusiva de las resistencias internas del STM32, lo que otorgará mayor robustez frente a desconexiones en caliente (*hot-plugging*).

---

## 4. Redundancia de Hardware y Tolerancia a Fallos

El sistema ya cuenta con medidas de *failsafe* robustas por software (como la revocación del *Keep-Alive* a los 20 segundos y la detección de cables rotos en la NTC). Sin embargo, se proponen las siguientes mejoras de hardware:

* **Watchdog Externo:** Implementar un circuito *Watchdog Timer (WDT)* por hardware. Aunque Zephyr OS es extremadamente estable, un WDT físico forzaría el corte de la planta térmica si el microcontrolador llegase a congelarse por una descarga electrostática severa (ESD).
* **Sonda Térmica Redundante:** Añadir un segundo canal analógico con otro termistor NTC. El algoritmo de `cooling_manager` pasaría a promediar las lecturas o descartar un sensor si las mediciones entre ambos difieren en más de 5°C, garantizando que un fallo en un solo sensor nunca detenga injustificadamente el proceso de control.
