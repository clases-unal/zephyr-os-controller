# Timers y PWM en STM32 con Zephyr OS

**Aplica a:** STM32L476RG · Zephyr OS · PlatformIO  
**Periférico documentado:** TIM3, canal 1, salida por PA6 (AF2)  
**Contexto del proyecto:** Control del ventilador en el sistema de gestión térmica

---

## 1. Por qué existe el Timer como periférico independiente

El propósito fundamental de un Timer en un microcontrolador es **contar pulsos de reloj de forma autónoma**, sin consumir ciclos de CPU. El contador corre en hardware y el firmware solo lee o configura sus registros; el CPU puede estar ejecutando otro código o durmiendo mientras tanto.

Esta autonomía es lo que permite generar una señal PWM perfectamente periódica y sin jitter causado por interrupciones o cambios de contexto entre hilos. Si se generara PWM por software (toggling manual de un GPIO en un loop), cada interrupción del sistema introduciría variación en el periodo. El Timer elimina ese problema completamente.

---

## 2. Anatomía del periférico Timer

Un Timer de propósito general en el STM32 (TIM2, TIM3, TIM4, TIM5 en el L476) tiene los siguientes bloques internos. Entender cada uno es prerequisito para configurarlo correctamente.

### 2.1 La señal de reloj de entrada (CK_INT)

El Timer recibe su reloj desde el bus APB al que está conectado. En el STM32L476:

- **TIM2, TIM3, TIM4, TIM5** están en **APB1**. Con la configuración de reloj por defecto de Zephyr para este board, APB1 corre a **80 MHz**.
- **TIM1, TIM8** están en APB2, también a 80 MHz en esta configuración.

Este valor es el punto de partida de todos los cálculos que siguen. Si se cambia la configuración del reloj del sistema, todos los valores derivados cambian también.

### 2.2 El Prescaler (PSC)

El prescaler es un divisor de frecuencia ubicado entre la señal de reloj de entrada y el contador. Su registro se llama `TIMx_PSC`.

**Comportamiento:** el prescaler cuenta internamente de 0 hasta el valor configurado en PSC, y cada vez que llega al final emite **un** pulso al contador. La frecuencia que ve el contador es:

```
f_CK_CNT = f_CK_INT / (PSC + 1)
```

El `+ 1` es fundamental: PSC = 0 no significa "sin divisor" en el sentido de que queda deshabilitado — significa división entre 1, es decir, el contador ve la frecuencia completa del reloj de entrada. PSC = 1 divide entre 2. PSC = 9999 divide entre 10000.

**Unidades:** el prescaler opera en el dominio de la frecuencia del contador. Su valor no tiene unidad — es simplemente el número de ciclos de reloj de entrada que deben pasar para que el contador avance una cuenta.

**Por qué existe:** permite adaptar la base de tiempo del contador a rangos útiles para la aplicación. Un prescaler alto produce una base de tiempo lenta (útil para medir eventos de baja frecuencia o generar delays largos). Un prescaler bajo o cero produce una base de tiempo rápida (útil para PWM de alta resolución o medición de pulsos rápidos).

### 2.3 El Contador (CNT)

El contador es un registro de 16 o 32 bits (TIM2 y TIM5 tienen 32 bits; TIM3 y TIM4 tienen 16 bits) que se incrementa con cada pulso que sale del prescaler. En modo de conteo hacia arriba (el modo por defecto y el que usa Zephyr para PWM), cuenta de 0 hasta el valor configurado en ARR, luego vuelve a 0 y repite.

El registro del contador es `TIMx_CNT`. Se puede leer en cualquier momento para saber dónde está el contador en su ciclo actual.

### 2.4 El Registro de Recarga Automática (ARR)

`TIMx_ARR` define el **tope** del contador — el valor máximo que alcanza antes de reiniciarse a 0. Cada vez que el contador llega a ARR y vuelve a 0 se genera un evento de **actualización** (Update Event, UEV), que entre otras cosas puede disparar una interrupción o un DMA.

**ARR define el periodo del Timer:**

```
T_timer = (ARR + 1) / f_CK_CNT
f_PWM   = f_CK_CNT / (ARR + 1)
```

El `+ 1` aparece porque el contador cuenta los valores 0, 1, 2, ..., ARR — eso son ARR + 1 estados, no ARR.

**ARR define también la resolución:** cuantos valores distintos puede tomar el contador en un ciclo completo. Con ARR = 99 hay 100 valores posibles (0..99), lo que permite representar duties en pasos de 1%. Con ARR = 999 hay 1000 valores, pasos de 0.1%. Con ARR = 0 solo hay un valor posible (0), y el PWM no puede variar — este fue exactamente el problema que ocurrió con PSC = 10000 en este proyecto.

### 2.5 Los Registros de Captura/Comparación (CCRx)

Cada canal del Timer tiene su propio registro `TIMx_CCRx`. En modo PWM, este registro define el **umbral de comparación**: el valor del contador al que cambia el estado de la salida.

En modo PWM1 (el estándar):
- Mientras `CNT < CCR`: la salida está en **alto** (activa).
- Mientras `CNT >= CCR`: la salida está en **bajo**.

Esto produce:

```
duty_cycle = CCR / (ARR + 1)
```

Si CCR = 0: la salida nunca está en alto → 0% de duty.  
Si CCR = ARR + 1 (o mayor): la salida siempre está en alto → 100%.  
Si CCR = (ARR + 1) / 2: la salida está en alto la mitad del tiempo → 50%.

La API de Zephyr (`pwm_set_pulse_dt`) recibe el tiempo en **nanosegundos** que debe durar el pulso en alto, y el driver convierte ese valor a counts de CCR usando la frecuencia real del contador. El firmware no escribe CCR directamente — eso lo hace el driver.

### 2.6 El Pin de Salida y la Función Alternativa

El Timer genera la señal PWM internamente, pero para que salga al mundo exterior necesita estar conectado a un pin físico del MCU. Los pines del STM32 son multiplexados: cada pin puede funcionar como GPIO genérico, como entrada analógica, o como una de hasta 16 funciones alternativas (AF0..AF15).

La función alternativa que conecta TIM3 canal 1 con PA6 es **AF2**. Este mapeo está definido en el datasheet del STM32L476 (tabla de funciones alternativas de pines) y es fijo en silicio — no se puede cambiar por software.

`pinctrl` en Zephyr es el subsistema que configura estos registros en el arranque. Cuando el overlay declara:

```dts
fan_pwm_pins: fan_pwm_pins {
    pinmux = <STM32_PINMUX('A', 6, AF2)>;
};
```

y ese grupo se referencia desde el nodo del timer con `pinctrl-0 = <&fan_pwm_pins>` y `pinctrl-names = "default"`, Zephyr escribe en los registros `GPIOA->MODER` (modo alterno) y `GPIOA->AFR` (función AF2) durante el boot, antes de que arranque ningún hilo. Sin `pinctrl-names = "default"`, pinctrl no aplica la configuración aunque el phandle esté declarado.

---

## 3. El ciclo completo: de reloj a señal en el pin

Con todos los bloques entendidos, el flujo completo es:

```
f_APB1 (80 MHz)
    │
    ▼
[PSC + 1]  ──── divide ────►  f_CK_CNT = 80 MHz / (PSC + 1)
    │
    ▼
[Contador CNT]  ──── cuenta 0..ARR ────►  f_PWM = f_CK_CNT / (ARR + 1)
    │
    ├── CNT < CCR  →  salida ALTA
    └── CNT ≥ CCR  →  salida BAJA
    │
    ▼
[PA6 en AF2]  ──── señal física al ventilador/LED
```

---

## 4. Las fórmulas de configuración

### 4.1 Frecuencia del contador

```
f_CK_CNT = f_APB1 / (PSC + 1)
```

### 4.2 Frecuencia del PWM

```
f_PWM = f_CK_CNT / (ARR + 1)
      = f_APB1 / ((PSC + 1) × (ARR + 1))
```

### 4.3 ARR a partir de la frecuencia deseada (con PSC conocido)

```
ARR = (f_APB1 / ((PSC + 1) × f_PWM)) - 1
```

### 4.4 Duty cycle

```
duty = CCR / (ARR + 1)          [0.0 .. 1.0]
CCR  = duty × (ARR + 1)
```

### 4.5 Resolución en bits

La resolución indica cuántos niveles distintos de duty cycle son representables:

```
niveles    = ARR + 1
resolución = log2(ARR + 1)  [bits]
```

---

## 5. El trade-off entre frecuencia y resolución

Frecuencia y resolución son inversamente proporcionales cuando el prescaler está fijo. Para una frecuencia de APB1 dada:

```
(ARR + 1) = f_APB1 / ((PSC + 1) × f_PWM)
```

Si `f_PWM` sube, `ARR + 1` baja, y con él la resolución. Si `f_PWM` baja, `ARR + 1` sube y la resolución mejora.

### Ejemplo numérico para este proyecto (PSC = 0, f_APB1 = 80 MHz)

| f_PWM | ARR | Niveles | Resolución |
|---|---|---|---|
| 1 kHz | 79 999 | 80 000 | ~16.3 bits |
| 4 kHz | 19 999 | 20 000 | ~14.3 bits |
| 25 kHz | 3 199 | 3 200 | ~11.6 bits |
| 100 kHz | 799 | 800 | ~9.6 bits |
| 1 MHz | 79 | 80 | ~6.3 bits |

A 25 kHz con PSC = 0 se dispone de 3200 niveles — suficiente para representar 0%, 30%, 60% y 100% con precisión de 0.03 puntos porcentuales. El límite práctico para PWM de motores suele ser ARR > 100 (resolución mínima de ~7 bits).

---

## 6. El efecto del prescaler en la resolución: el problema de este proyecto

El board Nucleo-L476RG configura TIM3 con `st,prescaler = <10000>` en su DTS base, pensado para uso como contador de bajo consumo. Con ese valor:

```
f_CK_CNT = 80 MHz / (10000 + 1) ≈ 7999 Hz
```

Para PWM de 4 kHz:

```
ARR = (7999 / 4000) - 1 ≈ 0.999 → ARR = 0
```

ARR = 0 significa que el contador solo tiene un estado (0), nunca cuenta, y la salida nunca puede tener un duty intermedio. El driver de Zephyr no reporta error porque el cálculo es aritméticamente válido — simplemente la resolución es de 1 bit (solo 0% o 100%). En la práctica, para este proyecto incluso esos dos extremos fallaban porque ARR = 0 impide que el timer genere la señal de actualización correctamente.

La solución fue sobreescribir el prescaler en el overlay:

```dts
&timers3 {
    status = "okay";
    st,prescaler = <0>;   /* reloj completo al contador */
    pwm3: pwm {
        status = "okay";
        pinctrl-0     = <&fan_pwm_pins>;
        pinctrl-names = "default";
    };
};
```

Con PSC = 0, ARR queda en 3199 para 25 kHz, dando 3200 niveles de resolución.

---

## 7. Configuración en Zephyr: el Device Tree end-to-end

La cadena completa de configuración en Zephyr para este proyecto es la siguiente. Cada línea en el overlay tiene un propósito específico.

### 7.1 Declaración del grupo de pines (pinctrl)

```dts
&pinctrl {
    fan_pwm_pins: fan_pwm_pins {
        pinmux = <STM32_PINMUX('A', 6, AF2)>;
    };
};
```

`STM32_PINMUX('A', 6, AF2)` codifica tres cosas en un solo valor de 32 bits: puerto A, pin 6, función alternativa 2 (TIM3_CH1). Este grupo se aplicará al registro `GPIOA->AFR` en el boot.

### 7.2 Habilitación del timer y su canal PWM

```dts
&timers3 {
    status = "okay";
    st,prescaler = <0>;
    pwm3: pwm {
        status = "okay";
        pinctrl-0     = <&fan_pwm_pins>;
        pinctrl-names = "default";
    };
};
```

- `status = "okay"` en `timers3`: habilita el driver del timer. Sin esto, el periférico no se instancia aunque el nodo exista.
- `st,prescaler = <0>`: sobreescribe el prescaler heredado del DTS base.
- `status = "okay"` en `pwm3`: habilita el sub-driver PWM dentro del timer.
- `pinctrl-0` + `pinctrl-names = "default"`: ambas líneas son necesarias juntas. `pinctrl-0` apunta al grupo de pines; `pinctrl-names = "default"` le indica al subsistema pinctrl que aplique ese grupo como configuración activa al inicializar el dispositivo. Sin `pinctrl-names`, la referencia en `pinctrl-0` es ignorada.

### 7.3 Referencia al canal desde el nodo de usuario

```dts
/ {
    zephyr,user {
        pwms = <&pwm3 1 PWM_KHZ(25) PWM_POLARITY_NORMAL>;
    };
};
```

Esta celda de tres valores codifica: driver `pwm3`, canal `1`, periodo en nanosegundos (calculado por `PWM_KHZ(25)` = 40 000 ns), polaridad normal. El canal 1 de TIM3 corresponde a PA6 según el datasheet del STM32L476.

### 7.4 Lectura en el firmware con PWM_DT_SPEC_GET

```c
static const struct pwm_dt_spec fan_pwm =
    PWM_DT_SPEC_GET(DT_PATH(zephyr_user));
```

Esta macro se expande en **tiempo de compilación** a una estructura con el puntero al driver de `pwm3`, el canal, y el periodo en nanosegundos. No hay búsqueda en tiempo de ejecución — si el nodo no existe o está disabled, el build falla aquí con un error de compilación.

### 7.5 Aplicación del duty cycle

```c
uint32_t pulse_ns = (fan_pwm.period * duty_percent) / 100;
pwm_set_pulse_dt(&fan_pwm, pulse_ns);
```

`fan_pwm.period` es el periodo declarado en el DTS (40 000 ns para 25 kHz). `pulse_ns` es el tiempo en alto deseado. El driver convierte este valor a counts de CCR usando la frecuencia real del contador y escribe `TIM3->CCR1`.

---

## 8. Evaluación y verificación del comportamiento

### 8.1 Verificación del DTS compilado

Antes de flashear, el DTS final compilado está en:

```
.pio/build/nucleo_l476rg/zephyr/zephyr.dts
```

Para verificar que la configuración del timer quedó correcta:

```bash
grep -A20 "timers@40000400" .pio/build/nucleo_l476rg/zephyr/zephyr.dts
```

Debe mostrar `status = "okay"` en timers3 y pwm3, `pinctrl-names = "default"`, y el prescaler correcto.

Para detectar conflictos de pin (otro periférico usando PA6):

```bash
grep -B2 -A8 "spi@40013000" .pio/build/nucleo_l476rg/zephyr/zephyr.dts | grep status
```

Si SPI1 tiene `status = "okay"` y está configurado con PA6 como MISO, ganará la configuración de pinctrl porque el DTS base lo declara con ese pin. La solución es deshabilitarlo en el overlay: `&spi1 { status = "disabled"; };`.

### 8.2 Verificación del prescaler y la resolución

Con los valores del DTS compilado, verificar que ARR sea razonable:

```
ARR = f_APB1 / ((PSC + 1) × f_PWM) - 1
```

Si ARR resulta menor que ~50, la resolución es demasiado baja para modular el duty con precisión y se debe reducir el prescaler o la frecuencia del PWM.

### 8.3 Verificación en tiempo de ejecución (log)

Instrumentar `cooling_manager_init()`:

```c
int rc = pwm_set_pulse_dt(&fan_pwm, fan_pwm.period / 2);
LOG_INF("PWM: device=%s ready=%d rc=%d period=%u ns",
        fan_pwm.dev ? fan_pwm.dev->name : "NULL",
        pwm_is_ready_dt(&fan_pwm), rc, fan_pwm.period);
```

Interpretación:
- `ready=0`: el driver no se inicializó — revisar `status = "okay"` en el DTS.
- `rc != 0`: el driver rechazó el comando — el periodo o el pulse están fuera de rango para la configuración actual de PSC/ARR.
- `period` en ns inusualmente pequeño: el prescaler produce una f_CK_CNT demasiado baja y el driver no puede representar el periodo pedido con ARR de 16 bits.

### 8.4 Verificación con osciloscopio

Medir en PA6 (D12 del Arduino header en el Nucleo) con:

- **Modo**: señal en tiempo, no RMS ni AC.
- **Trigger**: flanco de subida, nivel ~1.6 V (mitad de los 3.3 V de la lógica STM32).
- **Timebase**: para 25 kHz, el periodo es 40 µs — usar 10-20 µs/div para ver 2-4 ciclos.

Valores esperados para cada duty:

| duty_percent | Pulso alto | Pulso bajo | Verificar |
|---|---|---|---|
| 0% | 0 µs | 40 µs | Línea en bajo continuo |
| 30% | 12 µs | 28 µs | Pulso estrecho |
| 60% | 24 µs | 16 µs | Pulso ancho |
| 100% | 40 µs | 0 µs | Línea en alto continuo |

Si la frecuencia medida no coincide con la esperada, calcular la frecuencia real y usarla para despejar el ARR real que está usando el hardware — eso revela el PSC y la f_CK_CNT reales.

---

## 9. Consideraciones para la implementación real

Las siguientes consideraciones no son necesarias para la demostración del proyecto pero son relevantes para una implementación industrial.

### 9.1 Frecuencia del PWM y tipo de carga

- **Motores DC con escobillas y ventiladores:** la frecuencia óptima suele ser entre 10 kHz y 30 kHz para minimizar el ruido audible (por encima de ~20 kHz, fuera del rango auditivo humano) sin sacrificar demasiada resolución.
- **25 kHz** (valor configurado en este proyecto) es una elección estándar en la industria para ventiladores PWM — los ventiladores de PC de 4 pines, por ejemplo, usan exactamente 25 kHz según la especificación Intel.
- Frecuencias muy bajas (< 1 kHz) pueden hacer que el motor vibre audiblemente. Frecuencias muy altas (> 100 kHz) aumentan las pérdidas en el driver de potencia.

### 9.2 Dead time para drivers de potencia con puente H

Si en lugar de un driver de ventilador simple se usa un puente H (dos transistores por fase), se necesita un tiempo muerto entre la desactivación de un transistor y la activación del complementario para evitar cortocircuito. Los timers avanzados del STM32 (TIM1, TIM8) tienen soporte de dead time en hardware. TIM3 no tiene esta función — si se necesita puente H, usar TIM1.

### 9.3 Retroalimentación de velocidad (tacómetro)

El diseño actual no tiene retroalimentación de velocidad real del ventilador. En producción, los ventiladores de 4 pines incluyen una señal de tacómetro (2 pulsos por revolución) que permite medir la velocidad real y detectar si el ventilador está bloqueado. Esta señal puede medirse con el modo de captura de entrada (Input Capture) de un timer, que es el modo complementario al PWM en el mismo periférico.

### 9.4 El registro Shadow (preload)

Por defecto en Zephyr, los registros ARR y CCR tienen activada la función de **preload**: los nuevos valores escritos no se aplican inmediatamente sino en el próximo Update Event (cuando el contador llega a ARR y reinicia). Esto garantiza que el cambio de duty ocurra en un límite de periodo, evitando pulsos con duración anómala durante la transición. Es el comportamiento correcto para PWM y no requiere ninguna acción adicional del firmware.

---

## 10. Resumen de checklist de configuración

Al configurar un nuevo canal PWM en este proyecto (o en cualquier proyecto Zephyr + STM32):

- [ ] Verificar en el datasheet que el pin elegido tiene la función TIMx_CHy como función alternativa AFn.
- [ ] Declarar el grupo pinctrl con `STM32_PINMUX(puerto, pin, AFn)`.
- [ ] Habilitar el nodo del timer con `status = "okay"` y `st,prescaler` adecuado.
- [ ] Habilitar el sub-nodo pwm con `status = "okay"`, `pinctrl-0`, y `pinctrl-names = "default"`.
- [ ] Verificar que ningún otro periférico en `status = "okay"` use el mismo pin — deshabilitar el conflicto si existe.
- [ ] Calcular ARR esperado y confirmar que es mayor que ~100 para resolución aceptable.
- [ ] Limpiar el build completo (`rm -rf .pio/build/`) después de cambios en el overlay.
- [ ] Verificar el DTS compilado con `grep` antes de asumir que el cambio fue aplicado.
- [ ] Instrumentar `pwm_is_ready_dt` y el `rc` de `pwm_set_pulse_dt` en el init para detectar fallos silenciosos.
