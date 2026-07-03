# 03 — Máquinas de Estado

Convención de color usada en todo este documento (para que sea fácil saltar
de un diagrama a otro sin perder el hilo visual): cada FSM tiene su propia
paleta, pero dentro de todas ellas el **verde** siempre significa "operación
normal/sana", el **amarillo/naranja** siempre significa "degradado pero
funcional", y el **rojo** siempre significa "falla / requiere atención". El
**morado** se reserva para estados transitorios (arrancando, resincronizando).

---

## 1. FSM de Arranque

```mermaid
stateDiagram-v2
    [*] --> PowerOn
    PowerOn --> InicializandoEstados: reset / power-on
    InicializandoEstados --> IncrementandoBootCount: 5 estados listos (control, config, system, telemetry, transmission)
    IncrementandoBootCount --> HilosActivos: telemetry_state_increment_boot_count()
    HilosActivos --> OperacionNormal: K_THREAD_DEFINE ya arrancó los 7 hilos de tasks/

    classDef transitorio fill:#9b7fd4,stroke:#5b3fa0,color:#fff
    classDef normal fill:#4caf50,stroke:#2e7031,color:#fff
    class PowerOn,InicializandoEstados,IncrementandoBootCount,HilosActivos transitorio
    class OperacionNormal normal
```

Nota de implementación: los 7 hilos de `tasks/` se registran de forma
estática con `K_THREAD_DEFINE()`, lo que significa que técnicamente pueden
empezar a ejecutarse antes de que `main()` termine de inicializar los 5
estados — `main.c` ya documenta esto como una condición de carrera teórica
pendiente de revisar (ver TODO en ese archivo).

---

## 2. FSM de Gestión Térmica

```mermaid
stateDiagram-v2
    [*] --> COLD
    COLD --> LOW: T >= threshold_low
    LOW --> COLD: T < threshold_low - 2°C
    LOW --> MEDIUM: T >= threshold_medium
    MEDIUM --> LOW: T < threshold_medium - 2°C
    MEDIUM --> HIGH: T >= threshold_high
    HIGH --> MEDIUM: T < threshold_high - 2°C
    HIGH --> CRITICO_SOBRETEMP: T >= threshold_critical
    CRITICO_SOBRETEMP --> HIGH: T < threshold_critical - 2°C

    COLD --> CRITICO_SENSOR: falla NTC (5 lecturas invalidas seguidas)
    LOW --> CRITICO_SENSOR: falla NTC
    MEDIUM --> CRITICO_SENSOR: falla NTC
    HIGH --> CRITICO_SENSOR: falla NTC
    CRITICO_SOBRETEMP --> CRITICO_SENSOR: falla NTC
    CRITICO_SENSOR --> COLD: sensor recupera lectura valida

    classDef normal fill:#4caf50,stroke:#2e7031,color:#fff
    classDef degradado fill:#ff9800,stroke:#b36a00,color:#fff
    classDef critico fill:#e53935,stroke:#a02622,color:#fff
    class COLD,LOW normal
    class MEDIUM,HIGH degradado
    class CRITICO_SOBRETEMP,CRITICO_SENSOR critico
```

La histéresis de 2°C se aplica por umbral, no solo entre estados adyacentes —
ver `cooling_manager.c`, función `classify_with_hysteresis()`, para la
implementación genérica que evalúa los 4 umbrales de una sola pasada.
`CRITICO_SENSOR` tiene prioridad absoluta: se puede entrar desde cualquier
estado y no depende en absoluto de la temperatura (que en ese momento no es
confiable).

---

## 3. FSM de Gestión de Alarmas

```mermaid
stateDiagram-v2
    [*] --> Normal
    Normal --> CriticoSobretempSostenido: entra en CRITICO_SOBRETEMP
    CriticoSobretempSostenido --> Normal: temperatura baja antes de 20s
    CriticoSobretempSostenido --> KeepAliveRevocado: 20s continuos en CRITICO_SOBRETEMP
    KeepAliveRevocado --> Normal: temperatura baja lo suficiente (se restaura autorizacion)

    Normal --> AlarmaPermanente: falla NTC sostenida sin recuperacion (ver FSM de Recuperacion de Errores)
    AlarmaPermanente --> [*]: SOLO via ciclo de energia fisico, no hay transicion de software

    Normal --> IniciandoShutdown: pulsacion larga del boton (>=3s)
    KeepAliveRevocado --> IniciandoShutdown: pulsacion larga del boton
    IniciandoShutdown --> Apagado: todos los hilos completan su detencion ordenada

    classDef normal fill:#4caf50,stroke:#2e7031,color:#fff
    classDef degradado fill:#ff9800,stroke:#b36a00,color:#fff
    classDef critico fill:#e53935,stroke:#a02622,color:#fff
    classDef transitorio fill:#9b7fd4,stroke:#5b3fa0,color:#fff
    class Normal normal
    class KeepAliveRevocado degradado
    class AlarmaPermanente critico
    class IniciandoShutdown,Apagado transitorio
```

`AlarmaPermanente` es deliberadamente un estado sin salida por software — ver
`01-system-specification.md` Sección 8 para la justificación. `Apagado` no es
un estado que el firmware pueda "salir" por sí mismo tampoco: la placa queda
con todos los hilos detenidos hasta que alguien corte/restaure la
alimentación físicamente.

---

## 4. FSM de Comunicación ESP32

```mermaid
stateDiagram-v2
    [*] --> Desconectado
    Desconectado --> Conectado: trama valida recibida (cualquier tipo)
    Conectado --> Conectado: heartbeat cada 5s (ambos sentidos) / telemetria cada 2s o por delta
    Conectado --> Desconectado: 12s sin recibir ninguna trama valida
    Desconectado --> Resincronizando: trama valida recibida tras estar desconectado
    Resincronizando --> Conectado: reenvio de diagnostico + configuracion completado

    classDef normal fill:#4caf50,stroke:#2e7031,color:#fff
    classDef degradado fill:#ff9800,stroke:#b36a00,color:#fff
    classDef transitorio fill:#9b7fd4,stroke:#5b3fa0,color:#fff
    class Conectado normal
    class Desconectado degradado
    class Resincronizando transitorio
```

El sistema arranca en `Desconectado` por diseño — antes de esta sesión de
trabajo, `esp32_connected` se fijaba en `true` incondicionalmente al boot,
lo cual no distinguía entre "conectado de verdad" y "nunca se verificó
nada" (ver `checkpoint.md` Sección 5 para el detalle de ese hallazgo).

---

## 5. FSM de Interfaz de Usuario (OLED + teclado)

```mermaid
stateDiagram-v2
    [*] --> Monitor
    Monitor --> EditandoBajo: tecla '1'
    Monitor --> EditandoMedio: tecla '2'
    Monitor --> EditandoAlto: tecla '3'
    Monitor --> EditandoCritico: tecla '4'

    EditandoBajo --> EditandoBajo: tecla 'A' (+1) / 'B' (-1)
    EditandoMedio --> EditandoMedio: tecla 'A' / 'B'
    EditandoAlto --> EditandoAlto: tecla 'A' / 'B'
    EditandoCritico --> EditandoCritico: tecla 'A' / 'B'

    EditandoBajo --> Monitor: 'D' con validacion OK, o '*' (cancelar)
    EditandoMedio --> Monitor: 'D' con validacion OK, o '*' (cancelar)
    EditandoAlto --> Monitor: 'D' con validacion OK, o '*' (cancelar)
    EditandoCritico --> Monitor: 'D' con validacion OK, o '*' (cancelar)

    EditandoBajo --> EditandoBajo: 'D' con validacion invalida (rechazado, se mantiene editando)
    EditandoMedio --> EditandoMedio: 'D' con validacion invalida
    EditandoAlto --> EditandoAlto: 'D' con validacion invalida
    EditandoCritico --> EditandoCritico: 'D' con validacion invalida

    EditandoBajo --> Monitor: timeout 30s sin actividad
    EditandoMedio --> Monitor: timeout 30s sin actividad
    EditandoAlto --> Monitor: timeout 30s sin actividad
    EditandoCritico --> Monitor: timeout 30s sin actividad

    classDef normal fill:#4caf50,stroke:#2e7031,color:#fff
    classDef transitorio fill:#9b7fd4,stroke:#5b3fa0,color:#fff
    class Monitor normal
    class EditandoBajo,EditandoMedio,EditandoAlto,EditandoCritico transitorio
```

Validación al confirmar (tecla 'D'): se exige `BAJO < MEDIO < ALTO < CRÍTICO`
antes de aceptar el cambio — si no se cumple, la edición se rechaza y el
usuario permanece en el mismo modo para corregir (ver
`ui_keypad_task.c`, `process_key_edit()`).

---

## 6. FSM de Recuperación ante Errores (patrón genérico + caso NTC)

Patrón que se repite para cada módulo periférico (OLED, teclado, ESP32) con
su propio bit en `ERROR_FLAG_*`:

```mermaid
stateDiagram-v2
    [*] --> OK
    OK --> Falla: fallo de inicializacion o de comunicacion detectado
    Falla --> OK: el modulo vuelve a responder correctamente

    classDef normal fill:#4caf50,stroke:#2e7031,color:#fff
    classDef critico fill:#e53935,stroke:#a02622,color:#fff
    class OK normal
    class Falla critico
```

Caso específico del NTC, que es el único con una vía hacia Alarma Permanente:

```mermaid
stateDiagram-v2
    [*] --> Sano
    Sano --> FallasAcumulando: 1 lectura fuera de rango fisico
    FallasAcumulando --> Sano: una lectura valida reinicia el contador
    FallasAcumulando --> Failsafe: 5 lecturas invalidas consecutivas
    Failsafe --> Sano: sensor recupera lectura valida (reintento automatico cada ciclo)
    Failsafe --> AlarmaPermanente: falla sostenida sin recuperacion

    classDef normal fill:#4caf50,stroke:#2e7031,color:#fff
    classDef degradado fill:#ff9800,stroke:#b36a00,color:#fff
    classDef critico fill:#e53935,stroke:#a02622,color:#fff
    class Sano normal
    class FallasAcumulando degradado
    class Failsafe,AlarmaPermanente critico
```

**Punto pendiente marcado explícitamente**: el umbral exacto de "sin
recuperación" que dispara `Failsafe -> AlarmaPermanente` (¿cuántos ciclos de
`Failsafe` sostenido? ¿cuánto tiempo?) no está implementado todavía en
`temperature_manager.c` — hoy el código reintenta la inicialización del ADC
indefinidamente sin nunca escalar a Alarma Permanente por sí solo. Ver
`04-design-decisions.md`, sección de mejoras futuras.
