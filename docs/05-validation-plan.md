# 05 — Plan de Validación

**Alcance:** pruebas funcionales por software y observación directa
únicamente. No hay multímetro, osciloscopio, generador de señal ni
analizador lógico disponibles — ninguna prueba de este documento depende de
instrumentación de laboratorio. Todo se verifica con: el monitor serial, la
propia pantalla OLED, la barra de LEDs, y manipulación física directa
(tocar el NTC, presionar el teclado/botón, desconectar el ESP32).

**Orden recomendado:** de abajo hacia arriba de la pila de dependencias —
primero lo que no depende de nada nuevo (ADC/PWM, que ya funcionaban),
después lo que sí cambió en esta sesión, para poder aislar rápido si algo
se rompió y de dónde vino. Sigue los bloques en el orden en que aparecen.

---

## Bloque 0 — Regresión de lo que ya funcionaba antes de esta sesión

Antes de probar nada nuevo, confirma que nada de lo ya validado se rompió
con el remapeo de pines.

| ID | Objetivo | Procedimiento | Resultado esperado |
|---|---|---|---|
| T0.1 | El ADC sigue leyendo el NTC en el pin nuevo (PA0 no cambió, pero confírmalo) | Monitor serial, buscar líneas `NTC: raw=... filtered=...` de `temperature_manager` | Valores estables, cambian al tocar/soplar el NTC |
| T0.2 | El PWM del ventilador sigue funcionando (PB0 no cambió) | Observar/escuchar el ventilador mientras subes la temperatura del NTC con la mano | El duty cycle sube en el monitor serial y el ventilador acelera perceptiblemente |

Si alguna de estas dos falla, **detente** — el problema es del remapeo de
pines o de la compilación, no de la lógica nueva, y hay que resolverlo antes
de seguir.

---

## Bloque 1 — Clasificación térmica con histéresis y CRÍTICO

| ID | Objetivo | Procedimiento | Resultado esperado |
|---|---|---|---|
| T1.1 | Transición COLD→LOW→MEDIUM→HIGH ocurre en los umbrales configurados | Calienta el NTC gradualmente (con los dedos o una fuente de calor controlada) mientras observas `cooling_manager` en el monitor serial (`T=... umbral=... causa=...`) | El campo `umbral` sube de 0 a 3 exactamente al cruzar cada `threshold_*` por defecto (30/45/60°C) |
| T1.2 | Histéresis de 2°C al bajar | Tras alcanzar LOW, deja enfriar el NTC lentamente | El umbral NO baja a COLD hasta que la temperatura cae por debajo de `threshold_low - 2°C`, no apenas toca `threshold_low` |
| T1.3 | Entrada a CRÍTICO por sobretemperatura | Calienta el NTC hasta superar 70°C (default de `threshold_critical`) | `umbral=4 causa=1` (OVERTEMP) en el log; en la OLED aparece "CRIT: SOBRETEMP"; en los LEDs, Qe/Qf/Qg quedan fijos y Qh parpadea a 500ms |
| T1.4 | Temporizador de 20s y revocación de keep-alive | Mantén el NTC por encima de `threshold_critical` sin interrupción durante más de 20s | A los 20s aparece en el log "CRITICAL por sobretemperatura sostenido... revocando keep-alive"; verificar con un multímetro NO es posible, pero el pulso en PA4 (visible indirectamente si el heater_simulation_task tiene algún LED de depuración, o agregando un LOG_INF temporal) debería dejar de producirse |
| T1.5 | Recuperación tras revocación | Deja enfriar el NTC por debajo de `threshold_critical - 2°C` | Log muestra "restaurando autorizacion de keep-alive"; el sistema vuelve a clasificar con normalidad |
| T1.6 | Validación de orden de umbrales al editar | Desde el teclado, intenta configurar `threshold_medium` por encima de `threshold_high` | El cambio se rechaza (log "Configuracion invalida rechazada"), el umbral vuelve a su valor previo, la UI permanece en modo edición |

---

## Bloque 2 — Falla de sensor NTC y Alarma Permanente

| ID | Objetivo | Procedimiento | Resultado esperado |
|---|---|---|---|
| T2.1 | Detección de falla de sensor | Desconecta físicamente el NTC (o el cable a PA0) | Tras 5 lecturas fallidas consecutivas (~2.5s a 500ms/ciclo), `ERROR_FLAG_NTC_SENSOR` se activa, `cooling_manager` reporta `umbral=4 causa=2` (SENSOR_FAULT), el ventilador va a 100% |
| T2.2 | Representación visual de falla de sensor vs. sobretemperatura | Con el NTC desconectado, observar los LEDs | Qe/Qf/Qg **apagados** (a diferencia de T1.3 donde quedan fijos), Qh **fijo** (no parpadea) — esta es la única forma de distinguir visualmente las dos causas de CRÍTICO |
| T2.3 | Recuperación tras reconexión | Reconecta el NTC | El sistema vuelve a clasificar con normalidad en el siguiente ciclo de lectura válida |
| T2.4 | Alarma Permanente (falla sostenida) | **Conocido pendiente**: hoy el sistema NO escala automáticamente a Alarma Permanente por falla sostenida del NTC — reintenta indefinidamente. Esta prueba queda documentada como bloqueada hasta implementar el criterio de escalada (ver `04-design-decisions.md`, mejoras futuras) | N/A por ahora |

---

## Bloque 3 — LEDs (registro de desplazamiento)

Antes de nada: confirma que el registro responde en absoluto.

| ID | Objetivo | Procedimiento | Resultado esperado |
|---|---|---|---|
| T3.0 | El registro de desplazamiento inicializa | Monitor serial al boot | "Registro de desplazamiento listo (SPI1 + LATCH en PA6)" — si en cambio aparece "SPI1 no listo" o "Pin de LATCH no listo", revisar cableado antes de seguir con el resto del bloque |
| T3.1 | Qa — heartbeat | Observar el LED blanco en operación normal | Parpadeo constante y visible a 1000ms (500ms encendido / 500ms apagado) |
| T3.2 | Qb — salud de teclado+OLED | Desconecta la OLED (o el teclado) físicamente | Qb pasa de fijo a parpadeo de 500ms |
| T3.3 | Qc — salud del ESP32 | Desconecta el ESP32 (o no lo conectes) | Qc pasa de fijo a parpadeo de 500ms tras ~12s (timeout de heartbeat) |
| T3.4 | Qd — Alarma Permanente | No ejecutable hasta resolver T2.4 — dejar pendiente | N/A por ahora |
| T3.5 | Qd — Shutdown | Mantén presionado el botón físico ≥3s | Todo se apaga excepto Qd, que parpadea a 500ms hasta que termina la secuencia |
| T3.6 | Barra térmica Qe-Qg | Repetir T1.1-T1.3 observando los LEDs en paralelo al log | Coincide exactamente con la tabla de `checkpoint.md` Sección 2 (COLD=Qe parpadea, LOW=Qe fijo, MEDIUM=+Qf fijo, HIGH=+Qg parpadea, CRITICO=+Qg fijo) |

---

## Bloque 4 — OLED

| ID | Objetivo | Procedimiento | Resultado esperado |
|---|---|---|---|
| T4.0 | La OLED inicializa | Monitor serial al boot | "Display OLED listo" — si aparece "SSD1306 no listo" o "cfb_framebuffer_init fallo", revisar `CONFIG_HEAP_MEM_POOL_SIZE` en `prj.conf` primero (causa más probable), cableado I2C3 después |
| T4.1 | Vista de monitoreo se actualiza | Observar la pantalla en operación normal | Temperatura, umbral (o causa de CRÍTICO), duty cycle y estado global se actualizan en tiempo real |
| T4.2 | Entrada a modo edición | Presiona '1', '2', '3' y '4' desde el monitor | Cada tecla entra al modo de edición correspondiente, con la etiqueta correcta en pantalla |
| T4.3 | Timeout de edición | Entra a un modo de edición y no toques nada por 30s | La pantalla vuelve sola al modo monitoreo |

---

## Bloque 5 — Teclado matricial

**Nunca probado en hardware real antes de esta sesión — tratar todo este
bloque como de mayor riesgo.**

| ID | Objetivo | Procedimiento | Resultado esperado |
|---|---|---|---|
| T5.0 | El teclado inicializa | Monitor serial al boot | Sin errores de `matrix_keypad_init` |
| T5.1 | Cada tecla se detecta correctamente | Presiona cada una de las 16 teclas una por una, observando el log o el efecto en pantalla | Cada tecla produce el carácter esperado según el mapa de `matrix_keypad.h` — si alguna tecla no responde o responde con el carácter de otra, es indicio de que filas/columnas están cruzadas: intercambia los grupos en el overlay (ver comentario "CÓMO INTERCAMBIAR FILAS POR COLUMNAS" en `nucleo_l476rg.overlay`) |
| T5.2 | Sin rebote (debounce) | Presiona una tecla una sola vez, de forma normal | Un único registro de pulsación, no varios seguidos |
| T5.3 | Sin fantasmas (ghosting) | Mientras editas un umbral, presiona dos teclas de filas/columnas distintas casi al mismo tiempo | Ninguna tercera tecla "fantasma" se registra (limitación conocida de teclados matriciales sin diodos — si ocurre, es un límite de hardware, no un bug de software) |

---

## Bloque 6 — Botón de apagado

| ID | Objetivo | Procedimiento | Resultado esperado |
|---|---|---|---|
| T6.1 | Pulsación corta | Presiona y suelta rápido | **Comportamiento conocido como incorrecto** (ver `04-design-decisions.md`): hoy alterna `system_enabled` directamente. Documentar el comportamiento observado, no se espera que coincida con el diseño ideal todavía |
| T6.2 | Pulsación larga (shutdown) | Mantén presionado ≥3s | `system_state_request_shutdown()` se dispara, ver Bloque 3/T3.5 para la verificación visual |

---

## Bloque 7 — Comunicación con el ESP32

Sin un ESP32 real con firmware propio (ver `firmware/esp32/README.md`),
estas pruebas requieren un sustituto: otra placa Nucleo, un adaptador
USB-UART, o un script en la PC que hable el protocolo de
`protocol/uart_packet.h` por un conversor serie-USB.

| ID | Objetivo | Procedimiento | Resultado esperado |
|---|---|---|---|
| T7.1 | Diagnóstico al boot | Capturar los primeros bytes que salen por USART3 tras el arranque | Una trama `PACKET_TYPE_DIAGNOSTIC` (0x03) válida (CRC correcto) |
| T7.2 | Heartbeat saliente | Capturar el tráfico de USART3 durante 15s sin enviar nada de vuelta | Una trama `PACKET_TYPE_HEARTBEAT` (0x04) cada ~5s |
| T7.3 | Detección de conexión | Enviar manualmente cualquier trama válida (con CRC correcto) hacia el STM32 | `transmission_state.esp32_connected` pasa a `true`, Qc dejar de parpadear, log "ESP32 reconectado" con reenvío de diagnóstico+configuración |
| T7.4 | Detección de desconexión | Dejar de enviar nada durante >12s tras haber estado conectado | `esp32_connected` pasa a `false`, Qc empieza a parpadear, `ERROR_FLAG_ESP32_LINK` activo |
| T7.5 | Envío de configuración al confirmar edición | Con el ESP32 "conectado" (T7.3), edita y confirma un umbral desde el teclado | Una trama `PACKET_TYPE_CONFIG` (0x02) sale inmediatamente, sin esperar al próximo ciclo periódico |
| T7.6 | CRC rechaza tramas corruptas | Enviar una trama con un byte del payload alterado (CRC ahora inválido) | El parser no la cuenta como válida (no cambia `last_rx_ms`, no se loguea como recibida) — verificar que NO aparece el log "Trama recibida del ESP32" para esa trama específica |

---

## Criterios de aceptación general

- Ningún bloque de este documento debe dejar el sistema en un estado sin
  salida aparente salvo los explícitamente diseñados así (Alarma Permanente,
  Shutdown) — cualquier otro "cuelgue" observado es un bug a reportar.
- El heartbeat (Qa) debe seguir parpadeando durante **todas** las pruebas de
  los Bloques 1, 2, 3, 4, 5 y 7 (nunca durante Shutdown, Bloque 6/T6.2) —
  es la señal de que el kernel y el hilo de LEDs siguen vivos incluso si algo
  más falló.
- Cualquier desconexión de un módulo periférico (OLED, teclado, ESP32) no
  debe impedir que el lazo de control térmico (Bloques 0 y 1) siga
  funcionando con normalidad — es el requisito RNF-03 de
  `01-system-specification.md`.
