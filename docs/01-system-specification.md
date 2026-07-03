# 01 — Especificación del Sistema

## 1. Objetivo

Diseñar e implementar un sistema embebido de control térmico de alta
disponibilidad sobre una placa STM32L476RG (Nucleo) corriendo Zephyr OS, capaz
de:

- Medir temperatura de forma continua y confiable.
- Reaccionar a esa temperatura controlando un ventilador por PWM.
- Comunicar su estado a un módulo ESP32 externo.
- Ofrecer una interfaz local de monitoreo y configuración (pantalla OLED +
  teclado matricial).
- Representar su estado operativo mediante una barra de LEDs, de forma que un
  observador pueda saber qué está pasando sin necesidad de leer ni la OLED ni
  el monitor serial.
- Degradarse de forma segura y predecible ante fallas de sus propios
  subsistemas (sensor, interfaz local, enlace con el ESP32), en vez de fallar
  de forma silenciosa o indefinida.

"Alta disponibilidad" en este proyecto no significa redundancia de hardware
(no hay sensores ni controladores duplicados) — significa que el sistema
**nunca deja de tener una respuesta definida** ante cualquier combinación de
fallas de sus módulos periféricos, incluso si esa respuesta es "detener todo
de forma segura y avisarlo con claridad".

## 2. Alcance

Incluye: firmware del STM32 (control térmico, HMI local, protocolo hacia
ESP32, representación visual de estado). Incluye el contrato de interfaz
hacia el ESP32 (formato de paquetes), pero **no** incluye el firmware del
ESP32 en sí — ver `firmware/esp32/README.md` para el estado de eso.

No incluye: diseño de PCB, caracterización eléctrica instrumentada del
circuito (sin multímetro/osciloscopio/analizador lógico disponibles durante
el desarrollo), ni un portal web o dashboard remoto (responsabilidad del
ESP32, fuera de este documento).

## 3. Requisitos funcionales

| ID | Requisito |
|---|---|
| RF-01 | El sistema debe medir la temperatura mediante un termistor NTC y filtrarla para reducir ruido de lectura. |
| RF-02 | El sistema debe clasificar la temperatura filtrada en uno de 5 niveles (FRÍO, BAJO, MEDIO, ALTO, CRÍTICO) contra 4 umbrales configurables, con histéresis para evitar oscilación cerca de los límites. |
| RF-03 | El sistema debe controlar la velocidad de un ventilador por PWM en función del nivel térmico activo. |
| RF-04 | El sistema debe permitir editar los 4 umbrales de temperatura desde un teclado matricial local, validando que se mantenga el orden BAJO < MEDIO < ALTO < CRÍTICO antes de aceptar el cambio. |
| RF-05 | El sistema debe mostrar en una pantalla OLED local: temperatura actual, nivel térmico (o causa específica si es CRÍTICO), duty cycle del ventilador, y estado global del sistema. |
| RF-06 | El sistema debe representar su estado operativo completo mediante 8 LEDs controlados por un registro de desplazamiento, de forma que sea diagnosticable sin pantalla ni monitor serial. |
| RF-07 | El sistema debe enviar telemetría periódica al ESP32 por UART, con un protocolo de tramas verificado por CRC16. |
| RF-08 | El sistema debe detectar la pérdida del enlace con el ESP32 mediante heartbeats bidireccionales, y reflejarlo visualmente. |
| RF-09 | El sistema debe detectar la falla del sensor NTC (lectura fuera de rango físico válido) y entrar en modo failsafe (ventilador a máxima velocidad, LED de causa específica). |
| RF-10 | El sistema debe permitir apagar el sistema de forma ordenada mediante un botón físico dedicado, deteniendo limpiamente todos los subsistemas activos. |
| RF-11 | El sistema debe simular una fuente de calor externa (para fines de demostración/pruebas) mediante un GPIO de "keep-alive" pulsado periódicamente, y debe poder revocar esa autorización de forma autónoma si la temperatura permanece en CRÍTICO por sobretemperatura más allá de un tiempo de tolerancia. |
| RF-12 | Ante una falla sostenida e irrecuperable del sensor NTC, el sistema debe entrar en Alarma Permanente: deshabilitarse completamente y señalizarlo de forma indefinida hasta que una persona intervenga físicamente. |

## 4. Requisitos no funcionales

| ID | Requisito |
|---|---|
| RNF-01 | Toda estructura de estado compartida entre hilos debe estar protegida por su propio mutex — ningún hilo accede a datos de otro directamente. |
| RNF-02 | El sistema debe operar con lógica de 3.3V en todos los periféricos (sin conversores de nivel disponibles). |
| RNF-03 | El sistema debe seguir funcionando en modo degradado (sin OLED, sin teclado, sin enlace ESP32) mientras el lazo de control térmico en sí siga operativo — la pérdida de un módulo periférico no debe detener el control térmico. |
| RNF-04 | El código debe estar organizado por responsabilidad única: drivers/ (hardware puro), state/ (datos compartidos), protocol/ (formato de comunicación), tasks/ (lógica de negocio por hilo) — ver `02-firmware-architecture.md`. |
| RNF-05 | Todos los parámetros de tiempo (períodos de hilos, timeouts, temporizadores de histéresis) deben ser lo suficientemente cortos para ser observables en una demostración en vivo de duración acotada. |

## 5. Restricciones

- Sin instrumentación de laboratorio disponible (multímetro, osciloscopio,
  generador de señal, analizador lógico) — toda validación es funcional, no
  eléctrica (ver `05-validation-plan.md`).
- Tiempo de desarrollo acotado (proyecto académico de pregrado).
- Alimentación y lógica exclusivamente a 3.3V.
- Placa fija: Nucleo-L476RG, sin posibilidad de rediseño de PCB.

## 6. Hardware empleado

Ver `firmware/stm32/README.md` para la tabla completa de pines. Componentes
principales: STM32L476RG (Nucleo), termistor NTC 10kΩ B25/50=3470K, ventilador
DC controlado por PWM, registro de desplazamiento SN74HC595N + 8 LEDs, OLED
SSD1306 128×64 I2C, teclado matricial 4×4, módulo ESP32 (enlace UART), botón
físico dedicado para apagado.

## 7. Modos de operación

| Modo | Descripción | Cómo se entra | Cómo se sale |
|---|---|---|---|
| Normal | Control térmico activo, HMI disponible, enlace ESP32 intentando conectar/conectado | Estado por defecto tras el arranque | — |
| Degradado (parcial) | Uno o más módulos periféricos (OLED, teclado, ESP32) no disponibles, pero el lazo de control térmico sigue activo | Falla de inicialización o timeout de heartbeat de un módulo periférico | El módulo vuelve a responder |
| Failsafe térmico | Sensor NTC en falla; ventilador forzado a 100%, CRÍTICO por causa SENSOR_FAULT | 5 lecturas ADC consecutivas fuera de rango físico | Sensor vuelve a leer dentro de rango |
| Alarma Permanente | Sistema deshabilitado por completo, requiere intervención humana | Falla de NTC sostenida más allá del umbral de recuperación (ver `03-state-machines.md`) | Ciclo de energía físico tras corregir la causa — **no hay salida por software** |
| Shutdown | Apagado ordenado en curso | Pulsación larga (≥3s) del botón físico dedicado | Automático al terminar la secuencia de detención de todos los hilos |

## 8. Alarmas y seguridad

- **CRÍTICO por sobretemperatura**: temperatura ≥ `threshold_critical`. Si se
  sostiene más de 20s sin recuperarse, se revoca automáticamente la
  autorización de la planta térmica externa (GPIO keep-alive) como medida de
  seguridad adicional, independientemente de cualquier acción del usuario.
- **CRÍTICO por falla de sensor**: lectura ADC fuera de rango físico válido
  durante 5 ciclos consecutivos. Failsafe inmediato (ventilador a 100%). Si
  la falla persiste sin recuperación, escala a Alarma Permanente.
- **Alarma Permanente**: es intencionalmente irreversible por software — la
  filosofía de diseño es que una falla de sensor sostenida es una condición
  que requiere revisión física del cableado/hardware, no algo que el
  firmware deba "reintentar para siempre" sin que un humano se entere.

## 9. Casos de uso

1. **Operación normal**: el sistema mide temperatura, ajusta el ventilador,
   muestra el estado en OLED y LEDs, reporta telemetría al ESP32.
2. **Ajuste de umbrales**: el usuario navega el teclado, entra a modo edición
   de un umbral, lo modifica, confirma — el sistema valida el orden y aplica
   el cambio, notificando también al ESP32.
3. **Pérdida temporal del enlace ESP32**: el sistema detecta la ausencia de
   heartbeats, lo señaliza en el LED Qc y en la OLED, sigue controlando la
   temperatura con normalidad, y se resincroniza automáticamente (reenvía
   diagnóstico + configuración) cuando el enlace vuelve.
4. **Sobretemperatura sostenida**: el sistema escala progresivamente
   (BAJO→MEDIO→ALTO→CRÍTICO), lo refleja en la barra de LEDs, y si el
   CRÍTICO no se resuelve en 20s, corta la autorización de la fuente de
   calor externa.
5. **Falla del sensor NTC**: el sistema entra en failsafe, lo señaliza de
   forma inconfundible en el LED Qh (fijo, distinto del parpadeo de
   sobretemperatura), y si no se recupera, escala a Alarma Permanente.
6. **Apagado deliberado**: el usuario mantiene presionado el botón físico
   ≥3s; el sistema detiene ordenadamente todos sus subsistemas y lo
   señaliza con el único LED que sigue activo durante ese proceso.

---

## Anexo — Analogías para conceptos técnicos

**Histéresis con margen de 2°C**: es lo mismo que el termostato de una casa —
no se apaga la calefacción exactamente a 20°C y se prende exactamente a
19.99°C, porque eso haría que el sistema estuviera prendiendo y apagando
constantemente por el ruido natural de la medición. Se le da un "colchón": si
ya estaba encendida, se apaga solo cuando baja claramente por debajo del
punto de referencia (no apenas lo toca).

**Alarma Permanente como "fusible que hay que cambiar a mano"**: un fusible
quemado no se repara solo con reiniciar el circuito — hay que abrir la caja y
revisarlo físicamente. La Alarma Permanente funciona igual a propósito: si el
sistema decidiera "reintentar solo" indefinidamente ante una falla de sensor,
alguien podría no enterarse nunca de que hay un cable suelto.

**Heartbeat como "¿sigues ahí?" periódico**: es análogo a un chat que muestra
"escribiendo..." o "en línea" — cada cierto tiempo cada lado le confirma al
otro que sigue activo, y si ese aviso deja de llegar por más tiempo del esperado,
se asume que la conexión se cayó, aunque nadie haya cerrado la sesión
explícitamente. "Sigue activo" es literalmente lo único que confirma un
heartbeat — no transporta ningún otro dato.
