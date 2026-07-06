/*
 * led_representation_manager.c — Hilo visualizador del estado del sistema.
 *
 * Diseño vigente (single-register): un único SN74HC595N con 8 salidas Qa-Qh,
 * cada una con un significado fijo. Ver checkpoint.md Sección 2 para la tabla
 * completa y la justificación de cada decisión — este archivo es la
 * implementación directa de esa tabla, no repito aquí toda la justificación.
 *
 * Mapeo de bits (bit 0 = LSB = primero en salir = Qa, bit 7 = MSB = Qh):
 *   Bit 0 (Qa) — Blanco:   Heartbeat del kernel, parpadeo constante 1000ms.
 *   Bit 1 (Qb) — Azul 1:   Salud de teclado+OLED. Fijo=OK, parpadeo 500ms=falla.
 *   Bit 2 (Qc) — Azul 2:   Salud del enlace ESP32. Fijo=OK, parpadeo 500ms=falla.
 *   Bit 3 (Qd) — Rojo 1:   SHUTDOWN (500ms) o Alarma Permanente (200ms). Ver abajo.
 *   Bit 4 (Qe) — Verde:    Barra térmica — parpadeo 500ms=COLD, fijo=LOW o superior.
 *   Bit 5 (Qf) — Amarillo: Barra térmica — fijo=MEDIO o superior. Sin parpadeo propio
 *                           (no hay más LEDs disponibles para darle un estado "activo"
 *                           distinguible del "ya superado", decisión explícita del usuario).
 *   Bit 6 (Qg) — Naranja:  Barra térmica — parpadeo 500ms=HIGH, fijo=CRITICAL (overtemp).
 *   Bit 7 (Qh) — Rojo 2:   Causa de CRITICAL — parpadeo 500ms=sobretemperatura
 *                           (con Qe/Qf/Qg congelados en fijo), fijo=falla de sensor NTC
 *                           (con Qe/Qf/Qg completamente apagados, ver discussion.md §4.3).
 *
 * Durante SHUTDOWN (SystemState.shutdown_requested == true): TODO se apaga
 * excepto Qd, que parpadea a 500ms hasta que termina la secuencia de apagado
 * ordenado de todos los hilos (ya no hay escritura a NVS que esperar, ver
 * checkpoint.md Sección 4). Esta condición tiene prioridad sobre cualquier
 * otra — incluso sobre Alarma Permanente, que solo aplica en operación normal.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "led_representation_manager.h"
#include "../drivers/shift_register.h"
#include "../state/control_state.h"
#include "../state/system_state.h"
#include "../state/telemetry_state.h"

LOG_MODULE_REGISTER(led_representation_manager, LOG_LEVEL_WRN);

#define STACK_SIZE      1024
#define THREAD_PRIORITY 4       /* Media prioridad */
#define PERIOD_MS       50      /* 20Hz: suficiente resolución para blinks de 200/500/1000ms */

#define BIT_QA (1U << 0)
#define BIT_QB (1U << 1)
#define BIT_QC (1U << 2)
#define BIT_QD (1U << 3)
#define BIT_QE (1U << 4)
#define BIT_QF (1U << 5)
#define BIT_QG (1U << 6)
#define BIT_QH (1U << 7)

static bool sr_ready = false;

/*
 * Devuelve true durante la primera mitad de cada período `blink_period_ms`,
 * generando un parpadeo simétrico 50/50. `tick` es un contador incremental
 * de ciclos de PERIOD_MS del propio hilo (no un timestamp absoluto), lo cual
 * es suficiente porque todos los patrones de este sistema son parpadeos
 * periódicos simples, no secuencias con fase relativa entre sí que dependan
 * de un origen de tiempo compartido.
 */
static bool blink_phase(uint32_t tick, uint32_t blink_period_ms)
{
	uint32_t ticks_per_period = blink_period_ms / PERIOD_MS;
	if (ticks_per_period == 0) {
		ticks_per_period = 1;
	}
	return (tick % ticks_per_period) < (ticks_per_period / 2);
}

/* Arma el byte completo a enviar al registro para un ciclo dado. Separado en
 * su propia función (en vez de vivir dentro del while(1)) para poder
 * probarlo mentalmente/leerlo de corrido sin el ruido del bucle del hilo. */
static uint8_t build_frame(uint32_t tick, const SystemState *sys,
			   const ControlState *ctrl, const TelemetryState *tel)
{
	uint8_t frame = 0;

	if (sys->shutdown_requested) {
		/* SHUTDOWN tiene prioridad absoluta: todo apagado menos Qd. */
		if (blink_phase(tick, 500)) {
			frame |= BIT_QD;
		}
		return frame;
	}

	/* Qa — heartbeat del kernel, siempre activo salvo en SHUTDOWN. */
	if (blink_phase(tick, 1000)) {
		frame |= BIT_QA;
	}

	/* Qb — salud combinada de teclado + OLED. */
	bool ui_fault = (tel->error_log_flags & (ERROR_FLAG_OLED_I2C | ERROR_FLAG_KEYPAD)) != 0;
	if (ui_fault) {
		if (blink_phase(tick, 500)) {
			frame |= BIT_QB;
		}
	} else {
		frame |= BIT_QB;
	}

	/* Qc — salud del enlace ESP32. */
	bool esp32_fault = (tel->error_log_flags & ERROR_FLAG_ESP32_LINK) != 0;
	if (esp32_fault) {
		if (blink_phase(tick, 500)) {
			frame |= BIT_QC;
		}
	} else {
		frame |= BIT_QC;
	}

	/* Qd — Alarma Permanente únicamente (SHUTDOWN ya se resolvió arriba).
	 * Parpadea indefinidamente a 200ms mientras system_enabled == false;
	 * no hay forma de apagarlo por software (ver discussion.md §4.5). */
	if (!sys->system_enabled) {
		if (blink_phase(tick, 200)) {
			frame |= BIT_QD;
		}
	}

	/* Qe-Qh — barra térmica progresiva. */
	if (ctrl->current_threshold_code == THRESHOLD_CRITICAL &&
	    ctrl->critical_cause == CRITICAL_CAUSE_SENSOR_FAULT) {
		/* Falla de sensor: la barra entera se apaga (ninguna lectura es
		 * confiable), Qh queda fijo como única señal de esta causa. */
		frame |= BIT_QH;
	} else {
		switch (ctrl->current_threshold_code) {
		case THRESHOLD_COLD:
			if (blink_phase(tick, 500)) {
				frame |= BIT_QE;
			}
			break;
		case THRESHOLD_LOW:
			frame |= BIT_QE;
			break;
		case THRESHOLD_MEDIUM:
			frame |= BIT_QE | BIT_QF;
			break;
		case THRESHOLD_HIGH:
			frame |= BIT_QE | BIT_QF;
			if (blink_phase(tick, 500)) {
				frame |= BIT_QG;
			}
			break;
		case THRESHOLD_CRITICAL:
			if (ctrl->keep_alive_revoked) {
				/* ESTADO ESCALADO (OVERTMP, >20s): Efecto baliza entre barra térmica y Qh */
				if (blink_phase(tick, 500)) {
					frame |= BIT_QE | BIT_QF | BIT_QG; /* Barra encendida, Qh apagado */
				} else {
					frame |= BIT_QH; /* Barra apagada, Qh encendido */
				}
			} else {
				/* ESTADO INICIAL (CRITIC, <20s): Barra fija, Qh parpadeando como advertencia */
				frame |= BIT_QE | BIT_QF | BIT_QG;
				if (blink_phase(tick, 500)) {
					frame |= BIT_QH;
				}
			}
			break;
		}
	}

	return frame;
}

static void led_representation_manager_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("led_representation_manager thread started");
	sr_ready = shift_register_init();
	if (!sr_ready) {
		LOG_ERR("Registro de desplazamiento no disponible — LEDs desactivados");
	}

	uint32_t tick = 0;

	while (1) {
		if (sr_ready) {
			SystemState sys;
			ControlState ctrl;
			TelemetryState tel;

			system_state_get(&sys);
			control_state_get(&ctrl);
			telemetry_state_get(&tel);

			uint8_t frame = build_frame(tick, &sys, &ctrl, &tel);
			shift_register_write(frame);
		}

		tick++;
		k_sleep(K_MSEC(PERIOD_MS));
	}
}

K_THREAD_DEFINE(led_manager_tid, STACK_SIZE, led_representation_manager_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
