# 03-STATE-MACHINES
## Arquitectura de Control Lógico y Navegación

Este documento describe las Máquinas de Estado Finitas (FSM) que gobiernan el comportamiento del sistema. Se detallan las transiciones, condiciones de disparo y acciones asociadas para el control de energía, la lógica térmica y la interfaz de usuario.

---

## 1. Máquina de Estados del Sistema (System FSM)

Gestiona el ciclo de vida global de la aplicación. Es controlada por el hilo `power_status_manager.c` mediante el botón físico PC13.

| Estado              | Descripción                                                  | Disparador de Salida                                         |
| :------------------ | :----------------------------------------------------------- | :----------------------------------------------------------- |
| **SYSTEM_DISABLED** | El equipo está alimentado pero el control está inactivo. PWM al 0%, Keep-Alive cortado. LEDs en modo "Alarma Permanente" (Qd parpadeo 200ms). | Pulsación corta (<=500ms) -> ENABLED.                        |
| **SYSTEM_ENABLED**  | Operación normal. Todos los hilos ejecutan su lógica de control y monitoreo. | Pulsación corta -> DISABLED. Pulsación larga (>=5000ms) -> SHUTDOWN. |
| **SYSTEM_SHUTDOWN** | Estado transitorio de apagado. Limpia el bus I2C, muestra mensaje de despedida y detiene los hilos. Finaliza con `sys_poweroff()`. | N/A (Estado final).                                          |

---

## 2. Máquina de Estados Térmica (Cooling FSM)

Reside en `cooling_manager.c`. Clasifica la temperatura de la sonda NTC y ajusta la respuesta de los actuadores. Implementa **histéresis de 2.0°C** en la dirección de bajada.

### Niveles y Umbrales
1.  **COLD (Frío):** Estado por defecto. PWM 40%.
2.  **LOW (Bajo):** T > `threshold_low`. PWM 60%.
3.  **MEDIUM (Medio):** T > `threshold_medium`. PWM 80%. **Punto de restauración de Keep-Alive**.
4.  **HIGH (Alto):** T > `threshold_high`. PWM 100%.
5.  **CRITICAL (Crítico):** T > `threshold_critical`. PWM 100%.

### Escalada de Seguridad (Sub-estados de CRITICAL)
Cuando el sistema entra en nivel CRITICAL, la lógica evoluciona temporalmente:
*   **Estado CRITIC (Inicial):** Se muestra en OLED y LEDs durante los primeros 20 segundos. El ventilador opera al 100%. El Keep-Alive se mantiene activo esperando recuperación.
*   **Estado OVERTMP (Escalado):** Si tras 20s la temperatura no baja del umbral crítico (aplicando histéresis), el sistema escala:
    *   OLED muestra `Lvl: OVERTMP`.
    *   LEDs activan baliza alternada (Barra vs Qh).
    *   **Revocación de autorización:** Se corta el pin PA4 (Keep-Alive) para detener la planta de calor.

**Restauración:** El Keep-Alive solo se volverá a autorizar cuando la temperatura descienda hasta el umbral **MEDIUM** (incluyendo su margen de histéresis).

---

## 3. Máquina de Estados de la Interfaz (HMI FSM)

Gestionada en `ui_keypad_task.c`, permite la visualización y edición de parámetros mediante el teclado 4x4.

### Flujo de Navegación
*   **MONITOR:** Pantalla principal. Muestra temperatura, nivel térmico y duty cycle.
    *   Entrada: Al arrancar o por pulsación de '*' en edición.
*   **EDIT_LOW -> EDIT_MEDIUM -> EDIT_HIGH -> EDIT_CRITIC:** Menús cíclicos de configuración.
    *   Disparador: Pulsación de '#' para avanzar al siguiente umbral.
    *   Acción: Teclas 'A' (+5°C) / 'B' (-5°C) o ingreso numérico.
    *   Validación: Al presionar 'C' (Guardar), el sistema verifica que `L < M < H < C`. Si no se cumple, rechaza el cambio.

---

## 4. Lógica de Representación Visual (LEDs)

Mapeo de telemetría hacia el registro SN74HC595N (Salidas Qa-Qh):

| Perfil de Estado               | Comportamiento Visual                                        |
| :----------------------------- | :----------------------------------------------------------- |
| **Normal (COLD/LOW/MED/HIGH)** | Barra progresiva (Qe-Qg). El LED del nivel actual parpadea (500ms), los niveles inferiores quedan fijos. |
| **Alarma (CRITIC)**            | Barra (Qe-Qg) fija al máximo + Qh parpadeando (500ms).       |
| **Falla Grave (OVERTMP)**      | **Baliza Alternada:** Barra completa (Qe-Qg) ON / Qh OFF -> Barra OFF / Qh ON (Cada 500ms). |
| **Falla Sensor (S.FAULT)**     | Barra térmica OFF. LED Qh fijo (Alarma de hardware).         |

