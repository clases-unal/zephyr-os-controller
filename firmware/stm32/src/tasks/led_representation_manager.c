/**
 * @file led_representation_manager.c
 * @brief Hilo visualizador del estado general del sistema vía LEDs.
 *
 * @details
 * Diseño vigente (single-register): un único SN74HC595N con 8 salidas Qa-Qh,
 * cada una con un significado fijo:
 * Bit 0 (Qa) — Blanco:   Heartbeat del kernel, parpadeo constante 1000ms.
 * Bit 1 (Qb) — Azul 1:   Salud de teclado+OLED. Fijo=OK, parpadeo 500ms=falla.
 * Bit 2 (Qc) — Azul 2:   Salud del enlace ESP32. Fijo=OK, parpadeo 500ms=falla.
 * Bit 3 (Qd) — Rojo 1:   SHUTDOWN (500ms) o Alarma Permanente (200ms).
 * Bit 4 (Qe) — Verde:    Barra térmica — parpadeo 500ms=COLD, fijo=LOW o superior.
 * Bit 5 (Qf) — Amarillo: Barra térmica — fijo=MEDIO o superior.
 * Bit 6 (Qg) — Naranja:  Barra térmica — parpadeo 500ms=HIGH, fijo=CRITICAL (overtemp).
 * Bit 7 (Qh) — Rojo 2:   Causa de CRITICAL — parpadeo 500ms=sobretemperatura, fijo=falla de sensor NTC.
 *
 * Durante SHUTDOWN, TODO se apaga excepto Qd, que parpadea a 500ms indicando el
 * proceso de apagado. 
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

/**
 * @brief Evalúa si un LED debe encenderse en el ciclo actual para generar un parpadeo simétrico 50/50.
 *
 * @param tick Contador iterativo de ciclos del hilo principal.
 * @param blink_period_ms Período total en milisegundos del parpadeo (encendido + apagado).
 * @return true en la primera mitad del período (LED ON), false en la segunda mitad (LED OFF).
 */
static bool blink_phase(uint32_t tick, uint32_t blink_period_ms)
{
	uint32_t ticks_per_period = blink_period_ms / PERIOD_MS;
	if (ticks_per_period == 0) {
		ticks_per_period = 1;
	}
	return (tick % ticks_per_period) < (ticks_per_period / 2);
}

/**
 * @brief Construye el byte con el estado lógico de los 8 LEDs para el ciclo actual.
 *
 * Recopila la información de los diferentes estados del sistema y aplica la
 * lógica requerida para encender, apagar o parpadear los bits correspondientes
 * basándose en el tick de tiempo actual.
 *
 * @param tick Contador iterativo de tiempo.
 * @param sys Puntero de solo lectura al estado del sistema general.
 * @param ctrl Puntero de solo lectura al estado de control y refrigeración.
 * @param tel Puntero de solo lectura al estado telemétrico y de errores.
 * @return El byte (uint8_t) que debe ser enviado al registro de desplazamiento (Shift Register).
 */
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

	/* Qa — heartbeat del kernel. */
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

	/* Qd — Alarma Permanente. Parpadea indefinidamente a 200ms mientras system_enabled == false */
	if (!sys->system_enabled) {
		if (blink_phase(tick, 200)) {
			frame |= BIT_QD;
		}
	}

	/* Qe-Qh — barra térmica progresiva. */
	if (ctrl->current_threshold_code == THRESHOLD_CRITICAL &&
	    ctrl->critical_cause == CRITICAL_CAUSE_SENSOR_FAULT) {
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
			if (ctrl->keep_alive_revoked) {
				/* Transición de bajada (Seguimos en sobretemperatura bloqueada):
				 * - Qg deja de titilar (se anula / apaga por completo).
				 * - Qe, Qf y Qh siguen titilando sincrónicamente a 500ms. */
				if (blink_phase(tick, 500)) {
					frame |= BIT_QE | BIT_QF | BIT_QH;
				}
			} else {
				/* Comportamiento normal en HIGH (funcionamiento ordinario):
				 * - Qe, Qf y Qg se quedan fijos. */
				frame |= BIT_QE | BIT_QF | BIT_QG;
			}
			break;
		case THRESHOLD_CRITICAL:
			if (ctrl->keep_alive_revoked) {
				/* SOBRETEMPERATURA POR AGOTAMIENTO DEL TEMPORIZADOR (>20s):
				 * - Toda la barra térmica (Qe, Qf, Qg) y Qh titilan simétricamente. */
				if (blink_phase(tick, 500)) {
					frame |= BIT_QE | BIT_QF | BIT_QG | BIT_QH; 
				}
			} else {
				/* ESTADO CRÍTICO INICIAL (RECIÉN LLEGADO, <20s):
				 * - Qe y Qf fijos.
				 * - Qg (Alto) titila a la misma frecuencia que Qh (500ms). */
				frame |= BIT_QE | BIT_QF;
				if (blink_phase(tick, 500)) {
					frame |= BIT_QG | BIT_QH;
				}
			}
			break;
		}
	}

	return frame;
}

/**
 * @brief Hilo que gestiona la escritura física hacia el registro de LEDs.
 *
 * Se ejecuta periódicamente y transfiere los bytes de datos formados por
 * 'build_frame()' al hardware en base al estado del sistema para representar
 * alarmas, métricas y salud.
 *
 * @param p1 Parámetro no usado (requerido por Zephyr).
 * @param p2 Parámetro no usado.
 * @param p3 Parámetro no usado.
 */
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