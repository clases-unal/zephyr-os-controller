# 02 — Arquitectura del Firmware

## 1. Arquitectura general

El firmware sigue un modelo de **concurrencia por hilos con estado
compartido protegido por mutex**, nativo de Zephyr OS. No hay un
"orquestador" central: cada responsabilidad vive en su propio hilo,
registrado de forma estática con `K_THREAD_DEFINE()` directamente en su
archivo `.c` dentro de `src/tasks/`. `main.c` únicamente inicializa las
estructuras de estado compartido antes de que los hilos empiecen a
ejecutarse — ver `src/main.c` para el detalle, ya está bien documentado ahí.

```
src/
├── state/      Datos compartidos entre hilos, cada uno con su propio mutex.
├── drivers/    Acceso directo a hardware. Sin lógica de negocio, sin conocer
│               qué significa cada valor — solo "leer/escribir el pin/bus X".
├── protocol/   Formato de datos para comunicación (construcción/parseo de
│               tramas), independiente de qué hilo las use.
└── tasks/      Un hilo Zephyr por archivo. Aquí vive toda la lógica de
                negocio: "qué hacer" con los datos de state/ y drivers/.
```

Esta separación existe para que cada capa se pueda razonar (y probar) por
separado: un driver no sabe qué threshold_code significa, un estado no sabe
quién lo lee, y una tarea no sabe cómo funciona el bus SPI por debajo del
driver del registro de desplazamiento.

## 2. Hilos de Zephyr

| Hilo | Archivo | Prioridad | Responsabilidad |
|---|---|---|---|
| `temperature_manager` | `tasks/temperature_manager.c` | 2 (alta) | Lee el NTC, filtra, publica en `ControlState`, detecta falla de sensor |
| `cooling_manager` | `tasks/cooling_manager.c` | 2 (alta) | Clasifica el nivel térmico (con histéresis), controla el PWM, gestiona la entrada/salida de CRÍTICO |
| `power_status_manager` | `tasks/power_status_manager.c` | 2 (alta) | ISR + debounce del botón físico, detecta pulsación larga (shutdown) |
| `led_representation_manager` | `tasks/led_representation_manager.c` | 4 (media) | Traduce el estado del sistema a los 8 bits del registro de desplazamiento |
| `ui_keypad_task` | `tasks/ui_keypad_task.c` | 4 (media) | OLED + teclado matricial: monitoreo y edición de umbrales |
| `heater_simulation_task` | `tasks/heater_simulation_task.c` | 6 (baja) | Simula la fuente de calor externa vía el GPIO keep-alive |
| `esp32_comm_manager` | `tasks/esp32_comm_manager.c` | 6 (baja) | Telemetría, heartbeat, detección de desconexión con el ESP32 |

Prioridades más bajas = número más alto en Zephyr (`THREAD_PRIORITY`). Las
tareas de lazo de control (temperatura, ventilador, botón de apagado) tienen
prioridad alta porque su latencia afecta directamente la seguridad del
sistema; las de interfaz/comunicación tienen prioridad baja porque un
retraso de milisegundos ahí es imperceptible para una persona o para un
enlace serie de baja velocidad.

## 3. Timers y periodos

| Hilo | Periodo de ciclo | Notas |
|---|---|---|
| `temperature_manager` | 500ms | Filtro de promedio móvil sobre 5 muestras |
| `cooling_manager` | 1000ms | Incluye el conteo del temporizador de 20s en CRÍTICO-sobretemperatura |
| `led_representation_manager` | 50ms | Resolución suficiente para blinks de 200/500/1000ms |
| `ui_keypad_task` | 20ms | Escaneo de teclado a 50Hz; timeout de edición a 30s |
| `esp32_comm_manager` | 20ms (bucle TX/RX combinado) | Telemetría cada 2s o por salto de 0.5°C; heartbeat cada 5s; timeout de enlace 12s |
| `heater_simulation_task` | 500ms (periodo del pulso) | Pulso de 200ms ON / 300ms OFF cuando autorizado |

## 4. Mutexes y orden de adquisición

Cada una de las 5 estructuras de `state/` tiene su propio mutex privado
(`static struct k_mutex`, no expuesto fuera del archivo — ver cualquier
`*_state.c` para el patrón). Ningún hilo mantiene dos mutex tomados a la vez
en este diseño: todo acceso es "tomar mutex → copiar/escribir → soltar
mutex" en una sola función `_get()`/`_set_*()`, nunca se retiene un mutex
mientras se hace otra llamada de estado. Por eso no hay un orden de
adquisición complejo que documentar más allá de una regla: **si alguna vez
un hilo necesita tocar `SystemState` junto con otro estado en la misma
operación, `sys_mutex` se adquiere al final** (ya anotado en
`system_state.h`) — es la única estructura con esa restricción, porque es la
que más hilos consultan y la que menos cambia, así que minimizar el tiempo
que se mantiene tomada reduce el riesgo de contención.

## 5. Eventos (banderas de una sola escritura)

Además de los 5 estados con mutex, hay dos mecanismos de señalización más
livianos para comunicación entre hilos que no necesita un snapshot completo
de una estructura, solo un booleano:

- `heater_simulation_set_authorized(bool)` — `cooling_manager` revoca/restaura
  la autorización del keep-alive sin tocar el GPIO directamente.
- `esp32_comm_manager_notify_config_changed(void)` — `ui_keypad_task` avisa
  que hay una configuración nueva para reenviar de inmediato.

Ambos usan una variable `static volatile bool` de archivo, sin mutex propio,
documentado explícitamente en cada sitio como aceptable porque es una
escritura atómica de un solo booleano donde un ciclo de retraso en el peor
caso no tiene consecuencias.

## 6. Comunicación UART (hacia el ESP32)

Ver `protocol/uart_packet.h` para el formato de trama completo y
`tasks/esp32_comm_manager.c` para la lógica de heartbeat/detección de
desconexión — ambos archivos están comentados en detalle, este documento no
repite esa información. En resumen: un solo hilo hace TX y RX de forma no
bloqueante sobre USART3, sin necesidad de un segundo hilo dedicado a
recepción, porque las tasas de datos de esta aplicación (un paquete cada
200ms como mucho) no lo justifican.

## 7. Watchdog

**No implementado todavía.** `prj.conf` tiene `CONFIG_WATCHDOG=y` habilitado
pero ningún hilo lo alimenta (`wdt_feed()`) ni lo configura
(`wdt_install_timeout()`). Es un punto pendiente real, no solo un TODO
decorativo — sin esto, un hilo colgado (por ejemplo si el bus I2C se cuelga
esperando un ACK que nunca llega) no tiene ningún mecanismo de recuperación
automática a nivel de sistema. Ver `04-design-decisions.md` Sección de
mejoras futuras.

## 8. Organización modular — regla de dependencia

`drivers/` no incluye nada de `state/` ni de `tasks/`. `state/` no incluye
nada de `tasks/`. `tasks/` puede incluir de `state/`, `drivers/` y
`protocol/`. Esta dirección única de dependencias es lo que permite que, por
ejemplo, `shift_register.c` se pueda reutilizar sin arrastrar ningún
conocimiento sobre qué representa cada LED — esa lógica vive exclusivamente
en `led_representation_manager.c`.
