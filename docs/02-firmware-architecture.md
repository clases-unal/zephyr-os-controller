# 02-FIRMWARE-ARCHITECTURE
## Arquitectura de Software y Multihilo (Zephyr RTOS)

Este documento describe la arquitectura interna del firmware, el modelo de concurrencia basado en el sistema operativo en tiempo real (RTOS) Zephyr, la asignación de prioridades y el manejo seguro de la memoria compartida entre los distintos hilos de ejecución.

---

## 1. Paradigma del Sistema Operativo

El sistema está construido sobre **Zephyr OS**, utilizando su planificador de tareas preventivo (*preemptive scheduler*). Esto garantiza que las tareas de misión crítica (como la evaluación térmica y el control del hardware) siempre tengan preferencia de ejecución por encima de las tareas secundarias (como la actualización de la pantalla o la telemetría visual), asegurando tiempos de respuesta deterministas.

### Características implementadas del kernel:
* **Multihilo Preventivo:** Las interrupciones y los hilos de mayor prioridad pueden suspender instantáneamente a los hilos de menor prioridad.
* **Protección de Memoria Compartida:** Uso estricto de Mutexes (`k_mutex`) para evitar condiciones de carrera (*race conditions*) al acceder a variables globales.
* **Manejo de Interrupciones (ISR):** Delegación del procesamiento pesado desde las rutinas de interrupción hacia los hilos, manteniendo las ISR lo más cortas posible.

---

## 2. Mapa de Hilos de Ejecución (Threads)

El sistema se divide en módulos funcionales independientes que se ejecutan como hilos concurrentes. A continuación, se detalla la configuración del planificador para cada uno.

*(Nota: En Zephyr, los números de prioridad menores indican mayor urgencia lógica en la ejecución).*

| Hilo / Módulo       | Archivo Fuente                 | Prioridad          | Tamaño de Pila (Stack) | Responsabilidad Principal                                    |
| :------------------ | :----------------------------- | :----------------- | :--------------------- | :----------------------------------------------------------- |
| **Temp Manager** | `temperature_manager.c` | **Alta (2)** | 1024 Bytes | Muestreo periódico del canal ADC1 (PA0) para leer la sonda NTC. Aplica la ecuación de conversión a grados Celsius y actualiza continuamente el valor en tiempo real dentro del `ControlState`. |
| **Cooling Manager** | `cooling_manager.c`            | **Alta (2)**       | 1024 Bytes             | Lectura de temperatura, evaluación de umbrales con histéresis, ajuste del ciclo de trabajo del PWM del ventilador y gestión del temporizador de 20 segundos para escalada a OVERTMP. |
| **Power Status**    | `power_status_manager.c`       | **Alta (2)**       | 1024 Bytes             | Gestión del botón de usuario (PC13) vía ISR, implementación de *debounce* por software, y orquestación del apagado seguro (*Shutdown*) o transiciones al modo de configuración. |
| **UI & Keypad**     | `ui_keypad_task.c`             | **Media (4)**      | 2048 Bytes             | Barrido del teclado matricial 4x4 cada 20 ms. Renderizado del Framebuffer (CFB) en la pantalla OLED I2C. Requiere más pila debido a las operaciones del búfer de video. |
| **LED Manager**     | `led_representation_manager.c` | **Media (4)** | 1024 Bytes             | Transmisión por SPI hacia el registro de desplazamiento 74HC595N para actualizar la barra térmica visual y los LEDs de diagnóstico de hardware sin bloquear la CPU. |
| **Heater Sim**      | `heater_simulation_task.c`     | **Baja (6)**       | 1024 Bytes             | Simulación de la planta de calor (control del pin PA4 / *Keep-Alive*). Cede su ejecución frente a cualquier otro proceso del sistema. |

---

## 3. Gestión de Estados Compartidos (Arquitectura de Datos)

Para evitar un acoplamiento fuerte y código espagueti, los hilos no se comunican directamente ni modifican las variables internas de otros módulos de forma descontrolada. El sistema utiliza un patrón de **Estructuras de Estado Globales Protegidas**. 

Cada estructura cuenta con sus propios métodos de acceso (`getters` y `setters`) que encapsulan bloqueos mutuos (`k_mutex_lock` / `k_mutex_unlock`) para garantizar la integridad de los datos.

### 3.1. `SystemState`
* **Contenido:** Estado general del equipo (`system_enabled`), banderas de modo seguro o apagado inminente.
* **Productores:** `power_status_manager` (al detectar pulsaciones del botón).
* **Consumidores:** Todos los hilos (para saber si deben ejecutar su lógica o mantenerse en reposo).

### 3.2. `ControlState`
* **Contenido:** Datos dinámicos en tiempo real (Temperatura actual, código del umbral activo `current_threshold_code`, causa del estado crítico `critical_cause`, y bandera de revocación `keep_alive_revoked`).
* **Productores:** `cooling_manager` (escribe la evaluación térmica).
* **Consumidores:** `ui_keypad_task` (para mostrar los datos en pantalla) y `led_representation_manager` (para actualizar telemetría visual).

### 3.3. `ConfigState`
* **Contenido:** Parámetros configurables en RAM de los 4 umbrales de temperatura (`threshold_low`, `threshold_medium`, `threshold_high`, `threshold_critical`).
* **Productores:** `ui_keypad_task` (durante el modo edición por teclado).
* **Consumidores:** `cooling_manager` (para comparar la temperatura actual contra los límites definidos).

### 3.4. `TelemetryState`
* **Contenido:** Contadores estadísticos y banderas de diagnóstico de hardware (ej. falla I2C, falla de sonda NTC).
* **Productores:** Rutinas de inicialización de los drivers.
* **Consumidores:** Telemetría serial (USART3) y `led_representation_manager`.

---

## 4. Lógica de Control Temporal y Excepciones

### 4.1. Máquina de Estados Térmica (Histéresis)
Para evitar que el hardware oscile destructivamente cuando la temperatura fluctúa marginalmente en el borde de un umbral, el `cooling_manager` implementa una histéresis asimétrica:
* **Escalada (Subida):** Es inmediata. Si la temperatura cruza un umbral superior, el sistema transita al instante para garantizar la disipación.
* **Desescalada (Bajada):** Requiere un margen de seguridad predefinido (ej. 2.0 °C). El sistema no bajará el ciclo de trabajo de la ventilación hasta que la temperatura descienda consistentemente por debajo del margen.

### 4.2. Temporizador de Tolerancia Critica (Keep-Alive Revocation)
El sistema integra una medida de alta fiabilidad (*failsafe*) para el hardware acoplado:
1. Al entrar en nivel `CRITICAL` por sobretemperatura, se activa un conteo interno.
2. Si el conteo supera los **20,000 ms (20 segundos)** sin que la temperatura baje del umbral crítico (verificado por la histéresis), el sistema deduce que la ventilación forzada es insuficiente.
3. Se invoca una revocación lógica, obligando al módulo `heater_simulation_task` a desactivar inmediatamente la línea *Keep-Alive* (Pin PA4) que alimenta la planta externa.
4. El estado escala visualmente en la OLED (`CRITIC` a `OVERTMP`) y activa un patrón de sirena en los indicadores LED.

### 4.3. Rutina de Antirrebote (Debounce) Híbrida
* **Botón de Usuario (PC13):** Utiliza interrupciones por flanco (EXTI) para respuesta inmediata, pero delega el cálculo del tiempo de retención (corta, media, larga) a un bucle temporal dentro del hilo `power_status_manager`, liberando el hardware de interrupciones.
* **Teclado Matricial (Matriz 4x4):** Operado completamente por barrido de software (*polling*) dentro de `ui_keypad_task`. Se escanean las columnas (activas en bajo) y se leen las filas protegidas por resistencias *Pull-Up*, aplicando memoria de estado para ignorar oscilaciones mecánicas del contacto.
