# 04 — Decisiones de Diseño

> Nota de numeración: los IDs de esta sección son los asignados durante la
> sesión de rediseño documentada en `checkpoint.md`. El proyecto ya tenía un
> historial de decisiones previo en `00-project-decisions-and-procedure.md`
> (DEC-H-001, DEC-H-002, DEC-H-003, DEC-F-003, DEC-R-002 entre otros). Al
> consolidar este archivo, revisa esos IDs y renumera si hay colisión — aquí
> se mantuvieron los mismos IDs cuando la decisión es la MISMA (solo
> reformulada en formato de alternativas), y se usan IDs nuevos para
> decisiones que no existían antes.

---

### DEC-H-001 — Método de control de los LEDs de estado

**Alternativas consideradas:**
- GPIOs directos, uno por LED.
- Registro de desplazamiento (SN74HC595N) controlado por SPI.

**Decisión:** Registro de desplazamiento único, 8 salidas (Qa-Qh).

**Justificación:** un registro de desplazamiento reduce de 8 a 3 los pines
del microcontrolador dedicados a LEDs, dejando más GPIOs libres para el resto
de periféricos (teclado, botón, keep-alive). El costo es la dependencia de un
componente adicional y de un driver SPI propio en vez de simples
`gpio_pin_set_dt()`.

**Consecuencias:** requirió escribir `drivers/shift_register.c` desde cero
(no existía ningún driver de este tipo en el proyecto). El refresco de todos
los LEDs pasa a ser atómico (una trama de 8 bits + pulso de LATCH), lo cual
además evita el parpadeo intermedio que tendría actualizar 8 GPIOs uno por
uno.

---

### DEC-H-002 — Esquema de representación visual (qué significa cada LED)

**Alternativas consideradas:**
- Un LED por variable de estado, sin reutilización (necesitaría más de 8 LEDs
  para cubrir todo lo que el sistema necesita comunicar).
- 8 LEDs con significados fijos, algunos con múltiples patrones de parpadeo
  para comunicar más de un estado por LED físico.

**Decisión:** la segunda opción — 8 LEDs de significado fijo, con hasta 3
velocidades de parpadeo distintas en el LED de mayor carga semántica (Qd:
200ms=Alarma Permanente, 500ms=Shutdown).

**Justificación:** con solo 8 salidas disponibles y más de 8 estados
relevantes que comunicar (heartbeat, salud de 2 subsistemas periféricos,
2 causas de apagado, 5 niveles térmicos, 2 causas de CRÍTICO), reutilizar un
mismo LED con distintos patrones de parpadeo era la única forma de cubrir
todo sin agregar hardware.

**Consecuencias:** el LED Amarillo (Qf, nivel MEDIO) quedó sin un patrón de
parpadeo propio para distinguir "actualmente en MEDIO" de "ya superado" —
limitación aceptada explícitamente por falta de LEDs disponibles, no es un
descuido.

---

### DEC-H-003 — Ecuación de caracterización del NTC

**Alternativas consideradas:**
- Ecuación de Steinhart-Hart (3 constantes, mayor precisión en todo el rango).
- Ecuación Beta (1 constante, más simple, suficiente en un rango acotado).

**Decisión:** Ecuación Beta (B=3470K).

**Justificación:** el rango de operación esperado del sistema (temperatura
ambiente a moderadamente caliente) está dentro de la zona donde la ecuación
Beta tiene error aceptable; la precisión adicional de Steinhart-Hart no se
traduce en un beneficio observable para este proyecto, y su implementación
es más compleja de validar sin instrumentación de laboratorio.

---

### DEC-H-004 — Nomenclatura del bus SPI del registro de desplazamiento

**Alternativas consideradas:**
- Usar los pines PA13/PA14/PA15 (documentados originalmente como "SPI_2").
- Usar los pines PA5/PA6/PA7 (físicamente SPI1 en el STM32L476RG).

**Decisión:** PA5 (SCK), PA6 (LATCH), PA7 (MOSI) — periférico SPI1 real.

**Justificación:** PA13/PA14 son SWDIO/SWCLK; usarlos hubiera arriesgado la
capacidad de reprogramar la placa por USB. Además, esos pines no
corresponden a SPI2 en este microcontrolador (SPI2 real usa puerto B/C), así
que la asignación original tenía dos problemas independientes, no uno.

**Consecuencias:** el overlay habilita `&spi1`, no `&spi2`, aunque el
proyecto se siga refiriendo coloquialmente a este bus como "SPI_2" en
conversación — vale la pena unificar la nomenclatura en algún momento para
evitar confusión futura.

---

### DEC-F-001 — Ubicación de la clasificación de umbrales térmicos

**Alternativas consideradas:**
- `temperature_manager.c` (como plantea el diseño original).
- `cooling_manager.c` (donde ya vivía la implementación previa a esta sesión).

**Decisión:** se mantuvo en `cooling_manager.c`.

**Justificación:** esa lógica ya estaba implementada y probada ahí antes de
esta sesión de rediseño; moverla a `temperature_manager.c` no aportaba
ningún beneficio funcional inmediato y sí arriesgaba romper algo que ya
funcionaba, en un contexto de tiempo acotado.

**Consecuencias:** desviación consciente respecto a la arquitectura descrita
en `02-firmware-architecture.md` en versiones previas del proyecto. Queda
documentado aquí para que no se lea como un descuido si alguien compara
contra el diseño original.

---

### DEC-F-002 — Implementación de histéresis para 4 umbrales

**Alternativas consideradas:**
- Un bloque `if/else` independiente por umbral (4 bloques casi idénticos).
- Un evaluador genérico: tabla ordenada de `{valor, estado}` recorrida una
  sola vez.

**Decisión:** evaluador genérico (`classify_with_hysteresis()`).

**Justificación:** evita duplicar la misma lógica de histéresis 4 veces;
agregar un 5to umbral en el futuro (si hiciera falta) sería una línea en la
tabla, no un bloque de código nuevo.

---

### DEC-F-003 — Criterio de envío de telemetría al ESP32

**Alternativas consideradas:**
- Enviar solo por temporizador fijo (ej. cada 10s).
- Enviar solo cuando la temperatura cambia más de cierto delta.
- Combinar ambos: lo que ocurra primero.

**Decisión:** combinación — cada 2000ms **o** si la temperatura cambió
≥0.5°C desde el último envío, lo que ocurra primero.

**Justificación:** el temporizador solo garantiza que el ESP32 nunca se
quede sin datos por mucho tiempo aunque la temperatura esté estable; el
delta garantiza reactividad ante cambios rápidos sin esperar el ciclo
completo del temporizador.

---

### DEC-F-004 — Entrada a CRÍTICO por sobretemperatura y revocación del keep-alive

**Alternativas consideradas:**
- Entrar a CRÍTICO únicamente tras sostener HIGH por un tiempo (sin umbral
  de temperatura propio para CRÍTICO).
- Entrar a CRÍTICO instantáneamente al cruzar un 4to umbral de temperatura
  (`threshold_critical`), con un temporizador aparte que actúa **dentro** de
  CRÍTICO para revocar el keep-alive si no se resuelve a tiempo.

**Decisión:** la segunda opción.

**Justificación:** un umbral de temperatura explícito para CRÍTICO es más
fácil de razonar y de mostrar en la interfaz de usuario que un tiempo
acumulado en HIGH; el temporizador de 20s dentro de CRÍTICO resuelve además
un punto que había quedado pendiente de definir desde el diseño original
(¿debe cortarse el keep-alive ante sobretemperatura sostenida?).

**Consecuencias:** esta es la decisión de todo el rediseño con mayor
probabilidad de necesitar ajuste — se basó en una instrucción verbal
ambigua del equipo del proyecto ("el temporizador es solo cuando está en
critical"), interpretada de la forma que mejor encajaba con el punto
pendiente del diseño original. Revisar en la próxima sesión de trabajo.

---

### DEC-P-001 — Persistencia de configuración y contadores (NVS)

**Alternativas consideradas:**
- Implementar NVS real para persistir umbrales, contador de arranques y
  contadores de error entre reinicios.
- No implementarla, dejar estos valores solo en RAM (se reinician en cada
  boot).

**Decisión:** no implementarla por ahora.

**Justificación:** el costo de implementarla correctamente (manejo de flash,
particiones, posible desgaste) no se justificaba dentro del tiempo
disponible del proyecto, y ninguna funcionalidad crítica depende de que
estos valores sobrevivan un reinicio.

**Consecuencias / mejora futura:** si se retoma, los campos a persistir son:
`threshold_low/medium/high/critical` (`ConfigState`), `system_boot_count`,
`error_count[4]` y `ntc_consecutive_failures` (`TelemetryState`). `prj.conf`
ya documenta en un comentario qué `CONFIG_*` reactivar (`CONFIG_FLASH`,
`CONFIG_FLASH_MAP`, `CONFIG_NVS`, `CONFIG_SETTINGS`).

---

### DEC-P-002 — Bus UART hacia el ESP32

**Alternativas consideradas:**
- USART1 (PA9/PA10) — el que usaba el código antes de esta sesión.
- USART3 (PC10/PC11).

**Decisión:** USART3.

**Justificación:** liberar PA9/PA10 era necesario para poder usarlos como
filas/columnas del teclado matricial sin necesitar más pines de los
disponibles en la placa.

---

### DEC-E-001 — Arquitectura de comunicación con el ESP32 (hilo único vs. dos hilos)

**Alternativas consideradas:**
- Un hilo dedicado a transmisión y otro dedicado a recepción, sincronizados.
- Un único hilo que hace TX y RX de forma no bloqueante en el mismo bucle.

**Decisión:** un único hilo.

**Justificación:** las tasas de datos de esta aplicación (como mucho un
paquete cada 200ms) no justifican la complejidad adicional de sincronizar
dos hilos accediendo al mismo periférico UART; `uart_poll_in()` no bloquea
si no hay datos, así que un solo bucle puede atender ambas direcciones sin
perder reactividad perceptible.

---

## Mejoras futuras (pendientes, no implementadas en esta sesión)

- **Watchdog** (`CONFIG_WATCHDOG=y` está habilitado pero nada lo alimenta ni
  lo configura — ver `02-firmware-architecture.md` Sección 7).
- **Escalada automática de `Failsafe` (NTC) a Alarma Permanente**: hoy el
  sistema reintenta la inicialización del ADC indefinidamente sin nunca
  declarar Alarma Permanente por sí solo; falta definir el criterio exacto
  (¿cuántos ciclos de `Failsafe` sostenido? ¿cuánto tiempo?) — ver
  `03-state-machines.md` Sección 6.
- **`power_status_manager.c`**: la pulsación corta hoy alterna directamente
  `SystemState.system_enabled`, cuando debería alternar el modo de la
  interfaz de usuario (`hmi_mode`, aún sin implementar) — `system_enabled`
  no debería cambiar por una pulsación de botón según el diseño original.
  Tampoco existe la "zona muerta" de 1-3s entre pulsación corta y larga.
- **NVS** — ver DEC-P-001.
- **Persistencia/gestión de `error_count[4]` por tipo específico de falla**
  más allá de simplemente incrementar contadores — no hay lógica que
  reaccione a un contador alto salvo el caso ya cubierto de NTC.
- **Firmware del ESP32** — no existe todavía, ver `firmware/esp32/README.md`.
