# firmware/esp32

**Estado: no implementado.** Esta carpeta existe para dejar reservado el
lugar donde irá el firmware del ESP32 cuando se desarrolle — por ahora
documenta el contrato de interfaz ya definido desde el lado del STM32, para
que quien lo implemente (quizás tú, en otra sesión) no tenga que adivinar
nada del protocolo.

## Qué debe hacer, mínimamente

1. Escuchar en el UART que conecta con el STM32 (mismo baudrate que
   `firmware/stm32/zephyr/prj.conf` — revisar `CONFIG_SERIAL`/`CONFIG_UART_*`
   ahí, o el valor por defecto de Zephyr si no se sobreescribió: 115200 8N1).
2. Parsear tramas con el formato definido en
   `firmware/stm32/src/protocol/uart_packet.h`:
   `[SOF 0xAA][TYPE 1B][SEQ 1B][LEN 1B][PAYLOAD nB][CRC16 2B]`
   (CRC16-CCITT, polinomio 0x1021, init 0xFFFF, calculado sobre TYPE+SEQ+LEN+PAYLOAD).
3. Responder a los `PACKET_TYPE_HEARTBEAT` (0x04, sin payload) del STM32 con
   su propio heartbeat cada ~5s — el STM32 declara el enlace caído si no
   recibe **ninguna** trama válida (de cualquier tipo) en 12s.
4. Interpretar `PACKET_TYPE_TELEMETRY` (0x01) para mostrar temperatura, duty
   del ventilador y umbral activo en el portal web / modo AP.
5. Interpretar `PACKET_TYPE_CONFIG` (0x02) para reflejar los umbrales
   configurados.
6. Interpretar `PACKET_TYPE_DIAGNOSTIC` (0x03, solo se envía una vez al boot
   del STM32) para mostrar contador de arranques y contadores de error.

## Qué NO está definido todavía (pendiente de diseño, ver DEC-E-001)

- Si el ESP32 necesita enviar algo MÁS que heartbeats hacia el STM32 (por
  ejemplo, comandos remotos desde el portal web) — el protocolo ya soporta
  agregar tipos de paquete nuevos sin romper lo existente, pero ningún tipo
  "STM32 <- comando" existe todavía.
- Diseño del portal web / modo AP en sí (discussion.md menciona la intención
  pero no el detalle de pantallas).
- Gestión de credenciales WiFi.

## Por qué esta carpeta no está vacía del todo

Aunque no hay código, tenerla creada desde ya dentro de `firmware/` (en vez
de agregarla después) deja clara la intención de que el proyecto es de dos
placas desde el día uno, y evita que alguien asuma por accidente que
`firmware/stm32/` es "todo el firmware".
