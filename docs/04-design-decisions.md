# 04-DESIGN-DECISIONS
## Registro de Decisiones de Arquitectura (ADR)

Este documento recopila las decisiones críticas de diseño de hardware y software adoptadas durante el desarrollo del sistema de control térmico. Cada entrada justifica técnicamente la selección de componentes, topologías y estrategias de mitigación frente a las alternativas consideradas.

---

## 1. DEC-H-001: Arquitectura de Indicadores Visuales (GPIO Directo vs. Registro de Desplazamiento)

* **Estatus:** Aprobado.
* **Contexto:** El sistema requiere controlar un conjunto de 8 indicadores LED independientes destinados a la barra métrica de temperatura (5 niveles), latido de corazón del kernel (*heartbeat*), diagnóstico de salud de periféricos (I2C/Teclado) y estado de alarma por fallo de enlace o hardware.
* **Alternativas Consideradas:**
    * **Alternativa A (Control por GPIO Directo):** Conectar cada LED a un pin independiente del microcontrolador STM32L476RG.
    * **Alternativa B (Expansor de Pines vía Registro de Desplazamiento síncrono):** Utilizar un circuito integrado **SN74HC595N** conectado a un periférico SPI físico de la CPU.
* **Evaluación Técnica:** La *Alternativa A* consume 8 pines de propósito general de manera exclusiva. Dado el tamaño del teclado matricial (4x4 = 8 pines) y los buses de comunicación (I2C + UART), esta opción agotaba las líneas de E/S disponibles en zonas de periféricos nativos o interfería con las líneas de depuración JTAG/SWD (PA13/PA14). La *Alternativa B* requiere únicamente 3 líneas: dos compartidas por el bus SPI1 (SCK, MOSI) y una línea de control exclusiva para el cerrojo de salida (LATCH/RCLK) manejada por software.
* **Decisión Justificada:** Se seleccionó la **Alternativa B (Registro de Desplazamiento Único)**. Su implementación permite un ahorro neto de 5 pines críticos del microcontrolador, garantizando la viabilidad física del teclado extendido y la pantalla OLED en el encapsulado LQFP64, manteniendo una frecuencia de refresco visual despreciable en consumo de CPU gracias al uso del periférico SPI por hardware.

---

## 2. DEC-H-002: Reasignación y Consolidación del Mapa de Pines (Hardware Pinout)

* **Estatus:** Aprobado.
* **Contexto:** La introducción del teclado matricial 4x4 y el retorno del registro de desplazamiento generaron conflictos de multiplexación y solapamiento de funciones sobre la placa de desarrollo Nucleo-L476RG.
* **Criterio de Diseño:**
    1.  Aislar por completo los pines PA13 (SWDIO) y PA14 (SWCLK) para impedir que el barrido del teclado corrompa la comunicación con el depurador ST-LINK durante pruebas en tiempo real.
    2.  Liberar canales USART y buses I2C redundantes, concentrando la telemetría externa en un único canal UART robusto hacia el módulo ESP32.
* **Decisión Justificada:** Se estructuró un mapa de pines consolidado y no conflictivo reflejado de forma estricta en el archivo `nucleo_l476rg.overlay`:

| Periférico / Línea        | Pin STM32                   | Modo / Configuración      | Justificación de Ingeniería                                  |
| :------------------------ | :-------------------------- | :------------------------ | :----------------------------------------------------------- |
| **Sonda NTC (Sensor)**    | `PA0`                       | Analógico (ADC1_IN5)      | Canal de muestreo dedicado de baja impedancia.               |
| **Ventilador (Actuador)** | `PB0`                       | Alterno (TIM3_CH3)        | Generación nativa de PWM por hardware a 25 kHz.              |
| **Registro 74HC595N**     | `PA5` / `PA7`               | Alterno (SPI1_SCK / MOSI) | Transmisión síncrona de alta velocidad hacia los LEDs.       |
| **LATCH Registro**        | `PA6`                       | Salida Digital (GPIO)     | Control manual del flanco de disparo para el cerrojo.        |
| **Pantalla OLED**         | `PC0` / `PC1`               | Alterno (I2C3_SCL / SDA)  | Aislamiento del bus I2C común para evitar colisiones.        |
| **Enlace ESP32**          | `PC10` / `PC11`             | Alterno (USART3_TX / RX)  | Comunicación serial asíncrona dedicada a telemetría.         |
| **Línea Keep-Alive**      | `PA4`                       | Salida Digital (GPIO)     | Control directo y aislado del contactor de potencia.         |
| **Botón de Usuario**      | `PC13`                      | Entrada Digital (EXTI)    | Pin físico B1 de la placa; gestiona el encendido/apagado.    |
| **Filas Teclado 4x4**     | `PC7`, `PA9`, `PA8`, `PB10` | Entrada con Pull-Up       | Captura segura del flanco de bajada durante el barrido.      |
| **Columnas Teclado**      | `PB4`, `PB5`, `PB3`, `PA10` | Salida Digital            | Excitación secuencial de las líneas de control de la matriz. |

---

## 3. DEC-S-001: Ubicación del Algoritmo de Clasificación Térmica

* **Estatus:** Aprobado.
* **Contexto:** Definición del módulo de software encargado de procesar la temperatura en grados Celsius, contrastarla contra los 4 umbrales configurables en la RAM y aplicar la lógica de histéresis asimétrica.
* **Alternativas Consideradas:**
    * **Alternativa A (Temperature Manager):** Realizar la clasificación dentro del hilo de adquisición del ADC, entregando un código de estado ya procesado al resto del firmware.
    * **Alternativa B (Cooling Manager):** Mantener al administrador de temperatura limitado estrictamente a tareas de conversión física (ADC -> Celsius) y transferir la evaluación completa de umbrales e histéresis al hilo de control de refrigeración.
* **Evaluación Técnica:** La *Alternativa A* acopla el subsistema de lectura de sensores con las reglas de negocio de los actuadores. Si los umbrales cambian dinámicamente desde la interfaz, el hilo del ADC debe gestionar mutaciones de configuración. La *Alternativa B* respeta el principio de responsabilidad única: el hilo térmico solo provee datos limpios y el `cooling_manager` centraliza toda la lógica reactiva (control del ventilador, conteo del temporizador de protección de 20s y revocación del *Keep-Alive*).
* **Decisión Justificada:** Se implementó la **Alternativa B**. Centralizar la toma de decisiones lógicas en el `cooling_manager` reduce la complejidad del intercambio de datos, protege las variables globales mediante exclusión mutua coordinada y facilita la implementación de la rutina de *failsafe* en caso de desconexión del sensor NTC.

---

## 4. DEC-S-002: Gestión de Memoria Dinámica para el Framebuffer Gráfico (OLED Fix)

* **Estatus:** Aprobado.
* **Contexto:** El subsistema de visualización OLED basado en el driver SSD1306 y la API Character Framebuffer (CFB) de Zephyr experimentaba fallas críticas de inicialización (pantalla en negro), debido a la asignación nula de memoria dinámicamente gestionada por el microcontrolador para el búfer gráfico.
* **Decisión Justificada:** Se rechazó la inhabilitación del hardware y se intervino la configuración base del sistema operativo. Se incrementó de manera explícita el parámetro `CONFIG_HEAP_MEM_POOL_SIZE` en el archivo `prj.conf` a un tamaño mínimo seguro que permita alojar las estructuras del mapa de bits de 128x64 píxeles. Asimismo, se encapsuló la inicialización del display dentro de un esquema de verificación preventiva; si el periférico no responde en el bus I2C3, el hilo de la interfaz continúa su ejecución en modo degradado (evitando el pánico del kernel) y reporta la falla de forma persistente en la telemetría visual mediante el parpadeo del LED de diagnóstico `Qb`.

