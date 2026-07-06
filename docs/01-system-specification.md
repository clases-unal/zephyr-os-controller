# 01-SYSTEM-SPECIFICATION
## Sistema de Control Térmico de Alta Disponibilidad

Este documento detalla las especificaciones técnicas del sistema, las características eléctricas e internas de los componentes de hardware seleccionados y la parametrización de los periféricos del microcontrolador STM32L476RG bajo el sistema operativo de tiempo real Zephyr OS.

---

## 1. Introducción y Alcance del Sistema

El proyecto consiste en un **Sistema de Control Térmico de Alta Disponibilidad** diseñado para monitorear variables de temperatura ambiente o de maquinaria mediante una sonda NTC, gestionar dinámicamente un actuador de ventilación forzada en función de umbrales térmicos configurables, y proveer una interfaz de usuario local (pantalla OLED y teclado matricial) junto con telemetría visual (barra de LEDs mediante un registro de desplazamiento).

### Entorno de Desarrollo y Despliegue
* **Microcontrolador Objetivo:** STMicroelectronics STM32L476RG (Arquitectura ARM Cortex-M4 con FPU, ejecutándose hasta a 80 MHz).
* **Placa de Desarrollo:** NUCLEO-L476RG.
* **Sistema Operativo:** Zephyr OS RTOS, seleccionado por su robustez, abstracción de hardware mediante *Devicetree* (vía archivos `.overlay`), y un planificador nativo preentivo altamente eficiente para sistemas embebidos críticos.
* **Ecosistema de Compilación:** PlatformIO integrado sobre Visual Studio Code (VSCode).

---

## 2. Especificación del Hardware y Componentes Externos

El diseño de hardware externo está estructurado para aislar la etapa lógica de control de la etapa de potencia industrial, garantizando la inmunidad al ruido y la robustez del sistema ante fallos eléctricos.

### 2.1. Sensor de Temperatura (Sonda NTC)
El sistema implementa una sonda con termistor NTC encapsulada en acero inoxidable con un cable blindado de 60 cm. Su caracterización técnica es la siguiente:
* **Rango de Operación:** -30 °C a +120 °C.
* **Resistencia Nominal:** 10 kΩ a una temperatura de referencia de 25 °C (T₀ = 298.15 K).
* **Constante Beta (β₂₅/₅₀):** 3470 K. 

#### Circuito de Acondicionamiento (Divisor de Tensión)
Para traducir los cambios de resistencia del termistor en una señal de tensión legible por el microcontrolador, se implementa un divisor de voltaje conectado a la línea de alimentación analógica estabilizada de 3.3 V (V_REF).
* **Resistencia de Pull-Up (R_pull):** Resistencia fija de 10 kΩ con una tolerancia estricta del 1% y un bajo coeficiente de temperatura. Esta precisión es fundamental para mitigar desviaciones térmicas y errores de ganancia en la lectura analógica.
* **Ecuación de Transferencia:** La tensión medida en el nodo central (conectado al pin PA0 del ADC) se define matemáticamente como:
  
    V_out = V_REF * [ R_NTC / (R_pull + R_NTC) ]

* **Algoritmo de Conversión Interna (Steinhart-Hart Simplificado):** Una vez digitalizada la tensión por el ADC, el firmware recupera el valor de la resistencia actual del termistor (R_NTC) y aplica el modelo Beta para linealizar la respuesta y obtener la temperatura absoluta en Kelvin (T):
  
    1 / T = (1 / T₀) + (1 / β) * ln(R_NTC / R₀)
    
    Posteriormente, se realiza la conversión a grados Celsius: T_C = T - 273.15.

### 2.2. Etapa de Potencia y Control del Ventilador
La planta externa cuenta con un actuador mecánico de refrigeración por aire (ventilador) cuyas especificaciones eléctricas nominales son:
* **Tensión de Alimentación:** 12 Vcc (Voltaje real de la fuente externa dedicada).
* **Consumo de Corriente:** 0.1 A en régimen de funcionamiento continuo a máxima velocidad.

Para conmutar la carga de corriente continua mediante la señal PWM del microcontrolador sin introducir ruidos de conmutación inductiva o corrientes de retorno al dominio analógico/digital del STM32, se ha diseñado una etapa tripartita de aislamiento, pre-conducción y potencia:

`[STM32 - PB0] --> [Optoacoplador 6N137] --> [Driver TC4429CPA] --> [MOSFET IRFZ44ZPbF] --> [Ventilador 12V]`

1.  **Aislamiento Galvánico (Optoacoplador 6N137 - Lite-On):** Se utiliza un optoacoplador de alta velocidad basado en un LED emisor infrarrojo y un fotodiodo integrado con un amplificador estroboscópico de salida lógica tipo Open-Collector. Soporta tasas de conmutación de hasta 10 MBd, lo que garantiza una distorsión nula del ciclo de trabajo a la frecuencia de PWM de trabajo (25 kHz). Su aislamiento protege por completo los pines lógicos de la placa Nucleo contra picos de tensión transitorios del motor.
2.  **Driver de Compuerta (TC4429CPA - Microchip):** Dado que la salida del optoacoplador posee limitaciones de corriente y un comportamiento Open-Collector, se integra el TC4429CPA, un driver de MOSFET de alta velocidad e inversión lógica capaz de suministrar picos de corriente de hasta 6 A para cargar y descargar rápidamente la capacitancia de entrada de la compuerta (*Gate*) del MOSFET. Opera en el dominio de alimentación intermedio de 5 Vcc.
3.  **Transistor de Potencia (MOSFET IRFZ44ZPbF):** Un transistor de efecto de campo de canal N clasificado para soportar hasta 49 A y 55 V. 

#### Justificación del Sobredimensionamiento
Aunque el ventilador consume apenas 0.1 A (una fracción diminuta de la capacidad del transistor), la selección del IRFZ44ZPbF se fundamenta en principios de fiabilidad:
* **Resistencia en Conducción Mínima (R_DS(on)):** Posee una resistencia interna típica de apenas 13.9 mΩ cuando está totalmente saturado. La disipación de potencia estática en el transistor se calcula mediante la ley de Joule:
  
    P_disipada = I² * R_DS(on) = (0.1 A)² * 0.0139 Ω = 0.000139 W (139 µW)
    
    Esta disipación térmica es virtualmente nula, eliminando la necesidad de disipadores físicos de calor y garantizando que el componente opere a temperatura ambiente constante, extendiendo drásticamente el Tiempo Medio Entre Fallos (MTBF) de la placa.
* **Inmunidad a Transitorios:** Los motores de ventilación forzada generan fuerzas contraelectromotrices considerables durante el frenado o conmutación PWM rápida. Un MOSFET clasificado para 49 A absorbe estos transitorios sin aproximarse a sus límites de ruptura por avalancha (*Avalanche Breakdown*).
* **Regulador de Voltaje de Soporte Lógico (L78S05CV):** Provee una línea regulada y estable de 5 Vcc con una capacidad de hasta 2 A a partir de la fuente principal de 12 Vcc. Esta línea alimenta exclusivamente la electrónica interna de control del driver de compuerta y el registro de desplazamiento, aislando los picos de conmutación de la fuente de alimentación del microcontrolador.

### 2.3. Interfaz de Usuario Visual (OLED SSD1306)
* **Tecnología del Módulo:** Pantalla OLED monocromática de 128x64 píxeles.
* **Controlador Embebido:** SSD1306.
* **Protocolo de Comunicación:** Bus serial I2C (mapeado físicamente en el periférico I2C3 del microcontrolador).
* **Consideración de Diseño de Niveles Lógicos:** El módulo se conecta directamente a la línea de alimentación de 3.3 V del STM32. La comunicación se realiza **sin resistencias de pull-up externas** en el circuito impreso de soporte. El diseño aprovecha de forma explícita las resistencias internas de pull-up programables de los pines del periférico I2C3 en el STM32L476RG. Esto simplifica el ruteado de la placa y garantiza la compatibilidad total de niveles lógicos a 3.3 V, operando de forma segura dentro del margen de ruido electromagnético tolerado por el controlador SSD1306.

### 2.4. Barra de Representación Lógica (Registro de Desplazamiento SN74HC595N)
Para gestionar la telemetría visual local sin agotar los pines de Entrada/Salida de Propósito General (GPIO) del microcontrolador, se reincorpora al diseño un único circuito integrado **SN74HC595N** de 8 bits con registros de almacenamiento de salida tipo *Latch* de tres estados.
* **Alimentación Eléctrica:** Conectado a la línea estable de 5 Vcc provista por el regulador L78S05CV. Esto garantiza una óptima luminosidad en los 8 LEDs de telemetría (`Qa` a `Qh`) acoplados a sus salidas.
* **Interfaz de Control:** Se conecta al periférico SPI1 del microcontrolador a través de tres líneas de señal: Reloj serie (SCK), Entrada de datos serie (MOSI) y una línea dedicada de GPIO gestionada manualmente por software para actuar como el reloj de almacenamiento (*Latch* / RCLK).
* **Análisis del Umbral Lógico (Voltajes de Entrada):** Al alimentar el SN74HC595N a 5 Vcc, la tensión mínima requerida en sus entradas lógicas para ser interpretada de forma segura como un "Alto" lógico (V_IH) se sitúa típicamente en 3.5 V (0.7 * V_CC). Dado que las salidas del STM32 operan a un máximo de 3.3 V, el sistema trabaja en un margen de acoplamiento estrecho. En pruebas experimentales estáticas dentro de un entorno controlado en protoboard, las transiciones lógicas se validan de forma consistente debido a los márgenes térmicos favorables del silicio del SN74HC595N; no obstante, el firmware asegura la máxima estabilidad configurando las salidas del SPI1 del microcontrolador en modo de alta velocidad (*High-Speed Drive Mode*) para optimizar los tiempos de subida de los flancos.

### 2.5. Teclado Matricial 4x4
Una matriz física de 16 pulsadores organizados en 4 filas y 4 columnas. Las filas están asignadas a pines configurados como entradas con resistencias de pull-up activadas internamente por el STM32, mientras que las columnas operan como salidas lógicas que el firmware conmuta de forma secuencial (barrido activo en bajo) para detectar de forma inequívoca la pulsación de cualquier tecla sin rebotes perjudiciales.

---

## 3. Especificación y Configuración de Periféricos del Microcontrolador (STM32L476RG)

La abstracción de hardware implementada mediante los nodos de configuración del subsistema Zephyr OS garantiza un acceso seguro a registros mediante controladores validados por la comunidad. A continuación, se detalla formalmente el uso y la configuración exacta de cada periférico:

### 3.1. Convertidor Analógico-Digital (ADC1 - Pin PA0)
* **Mapeo físico:** Pin `PA0` (Canal 5 de entrada del ADC1).
* **Resolución Configurada:** 12 bits de resolución de conversión. Esto implica que el rango analógico de tensión de referencia (0.0 V a 3.3 V) se cuantifica discretamente en un rango numérico entero que va de `0` a `4095`.
* **Justificación de Conveniencia Técnica:** La utilización de una resolución de 12 bits proporciona una precisión analógica de:
  
    ΔV = 3.3 V / 4096 = 0.805 mV por paso discreto del ADC.
    
    Al combinarse con la curva exponencial de la sonda NTC, esta configuración otorga una sensibilidad térmica inferior a 0.1 °C por paso en la vecindad de los umbrales nominales de operación (entre 20 °C y 60 °C). Esta resolución es fundamental para evitar oscilaciones espurias o lecturas inestables que pudiesen gatillar conmutaciones erróneas en las tareas de control térmico.
* **Tiempo de Muestreo (Sampling Time):** Configurado a través de los registros internos para asegurar un tiempo de adquisición de carga adecuado en el capacitor interno de muestreo y retención (*Sample and Hold*), mitigando los efectos de la impedancia de salida del divisor de tensión resistivo de 10 kΩ.

### 3.2. Modulación por Ancho de Pulsos (PWM - Pin PB0 / TIM3_CH3)
* **Mapeo físico:** Pin `PB0` mapeado internamente mediante la función alternativa de hardware `AF2` al Temporizador General de 16 bits `TIM3`, operando de manera nativa en el Canal 3 (`TIM3_CH3`).
* **Frecuencia del PWM de Trabajo:** **25 kHz**.
* **Justificación de Conveniencia Técnica:** La frecuencia de 25 kHz ha sido seleccionada de forma deliberada por cumplir rigurosamente con dos estándares fundamentales en la ingeniería de control de climatización:
    1.  **Inaudibilidad Acústica:** El espectro de audición humana comprende típicamente frecuencias entre 20 Hz y 20 kHz. Si el ventilador fuese conmutado a frecuencias inferiores (por ejemplo, 1 kHz o 5 kHz), los bobinados internos del motor actuarían como transductores mecánicos induciendo un zumbido o pitido acústico constante de alta molestia operacional. Con 25 kHz, la conmutación se eleva por completo fuera del espectro audible humano.
    2.  **Estándar de Ventiladores Brushless de CC:** Sigue las directrices estándar de la industria electrónica para el control por velocidad de motores de CC sin escobillas, reduciendo drásticamente las pérdidas térmicas por conmutación en el núcleo del estator.
* **Resolución del Ciclo de Trabajo (Duty Cycle) y Perfiles Operacionales:** El firmware gestiona los perfiles de enfriamiento mediante pasos discretos definidos explícitamente en función de la clasificación del nivel térmico:
    * **Nivel COLD:** Duty cycle del **40%**. Provee una ventilación mínima de renovación de aire sin comprometer la eficiencia energética.
    * **Nivel LOW:** Duty cycle del **60%**. Flujo moderado para control térmico pasivo inicial.
    * **Nivel MEDIUM:** Duty cycle del **80%**. Incremento activo del flujo de aire ante cargas de calor evidentes.
    * **Nivel HIGH:** Duty cycle del **100%**. Capacidad máxima de disipación mecánica.
    * **Nivel CRITICAL:** Duty cycle del **100%**. Forzado eléctrico continuo a velocidad máxima.

### 3.3. Interfaz Periférica Serial (SPI1 - Pines PA5, PA6, PA7)
* **Mapeo físico:**
    * `PA5` -> `SPI1_SCK` (Línea de Reloj Serie).
    * `PA7` -> `SPI1_MOSI` (Línea de Salida de Datos del Maestro).
    * `PA6` -> Configurado como salida GPIO de propósito general, utilizada manualmente para controlar la línea de almacenamiento **RCLK / LATCH** del 74HC595N.
* **Configuración del Protocolo:** Opera en Modo SPI 0 (Polaridad de Reloj CPOL = 0, Fase de Reloj CPHA = 0).
* **Justificación de Conveniencia Técnica:** El uso de un bloque SPI por hardware dedicado elimina la sobrecarga de tiempo de ejecución de CPU que implicaría realizar una transmisión por manipulación directa de software (*Bit-Banging*). Además, optimiza masivamente la densidad de conexiones de la placa de desarrollo: controlar 8 canales independientes de LEDs requeriría de 8 pines GPIO individuales; con esta arquitectura de registro de desplazamiento SPI, se controla el mismo volumen de señales visuales empleando estrictamente **solo 3 pines lógicos**.

### 3.4. Interfaz Inter-Integrated Circuit (I2C3 - Pines PC0, PC1)
* **Mapeo físico:** `PC0` (SCL) y `PC1` (SDA) mediante función alternativa `AF4`.
* **Frecuencia del Bus:** Configurado en Modo Estándar (*Standard Mode*) a **100 kHz**.
* **Justificación de Conveniencia Técnica:** El uso del periférico físico I2C3 integrado maneja por hardware la generación de condiciones de inicio, detención, arbitraje y bits de reconocimiento. La frecuencia de 100 kHz es óptima para mantener una excelente integridad de señal en buses locales alambrados en protoboard, mitigando los acoplamientos capacitivos parásitos entre las líneas sin comprometer la tasa de refresco visual requerida por el framebuffer (CFB) de Zephyr OS.

---

## 4. Mapa de Distribución de Pines (Pinout Consolidado)

Para asegurar la coherencia absoluta entre el cableado físico y las asignaciones lógicas del firmware, se consolida la siguiente matriz de interconexión basada en las declaraciones explícitas del archivo Devicetree:

| Pin STM32L476RG | Función del Periférico | Nodo Zephyr | Componente Destino Hardware Externo               |
| :-------------- | :--------------------- | :---------- | :------------------------------------------------ |
| **PA0**         | ADC1_IN5 (Analógico)   | `&adc1`     | Nodo central Divisor de Tensión (Sonda NTC 10 kΩ) |
| **PB0**         | TIM3_CH3 (PWM, AF2)    | `&pwm3`     | Entrada lógica del aislamiento optoacoplado       |
| **PA5**         | SPI1_SCK (Clock)       | `&spi1`     | Pin 11 (SRCLK / Clock In) del Registro SN74HC595N |
| **PA7**         | SPI1_MOSI (Master Out) | `&spi1`     | Pin 14 (SER / Data In) del Registro SN74HC595N    |
| **PA6**         | GPIO_OUTPUT (Limpio)   | `GPIO`      | Pin 12 (RCLK / Latch de Salida) del Registro      |
| **PC0**         | I2C3_SCL (Clock, AF4)  | `&i2c3`     | Pin SCL de Pantalla OLED SSD1306                  |
| **PC1**         | I2C3_SDA (Datos, AF4)  | `&i2c3`     | Pin SDA de Pantalla OLED SSD1306                  |
| **PA4**         | GPIO_OUTPUT            | `GPIO`      | Keep-Alive de Planta de Calor Externa             |
| **PC13**        | GPIO_INPUT (EXTI)      | `ISR`       | Botón de Usuario (B1 de placa Nucleo)             |
| **PC7**         | GPIO_INPUT (Pull-Up)   | `Driver`    | Fila 0 (R0) del Teclado Matricial                 |
| **PA9**         | GPIO_INPUT (Pull-Up)   | `Driver`    | Fila 1 (R1) del Teclado Matricial                 |
| **PA8**         | GPIO_INPUT (Pull-Up)   | `Driver`    | Fila 2 (R2) del Teclado Matricial                 |
| **PB10**        | GPIO_INPUT (Pull-Up)   | `Driver`    | Fila 3 (R3) del Teclado Matricial                 |
| **PB4**         | GPIO_OUTPUT            | `Driver`    | Columna 0 (C0) del Teclado Matricial              |
| **PB5**         | GPIO_OUTPUT            | `Driver`    | Columna 1 (C1) del Teclado Matricial              |
| **PB3**         | GPIO_OUTPUT            | `Driver`    | Columna 2 (C2) del Teclado Matricial              |
| **PA10**        | GPIO_OUTPUT            | `Driver`    | Columna 3 (C3) del Teclado Matricial              |
| **PC10**        | USART3_TX (UART)       | `&usart3`   | RX bloque de telemetría ESP32                     |
| **PC11**        | USART3_RX (UART)       | `&usart3`   | TX bloque de telemetría ESP32                     |
