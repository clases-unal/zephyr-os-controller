/*
 * cooling_manager.c — Hilo de decisión térmica y control de ventilador.
 *
 * Qué hace:
 * - Lee la temperatura actual desde ControlState.
 * - Clasifica esa temperatura en uno de 5 niveles (COLD/LOW/MEDIUM/HIGH/CRITICAL)
 *   contra los 4 umbrales configurables en ConfigState, aplicando histéresis
 *   asimétrica (discussion.md §4.2: subir es inmediato, bajar requiere cruzar
 *   umbral - 2°C).
 * - Detecta la entrada a CRITICAL por dos causas independientes: sobretemperatura
 *   (temperature >= threshold_critical) o falla de sensor NTC (bandera de
 *   TelemetryState). Ver checkpoint.md Sección 3 para el diseño completo.
 * - Mientras CRITICAL-por-sobretemperatura se sostiene más de 20s sin que la
 *   temperatura baje lo suficiente, revoca la autorización de la planta externa
 *   (línea keep-alive) como medida de seguridad adicional — resuelve el punto
 *   pendiente de discussion.md §9.1.3.
 * - Convierte el nivel térmico activo en un duty cycle y lo aplica al ventilador.
 * - En caso de fallo del sensor NTC, entra en failsafe y fuerza el ventilador a
 *   máxima velocidad.
 *
 * NOTA DE ARQUITECTURA: discussion.md §5.3 ubica la clasificación de umbrales
 * dentro de Temperature Manager, no aquí. El código ya traía esta lógica en
 * Cooling Manager desde antes de esta sesión de cambios y esa parte ya
 * funcionaba, así que se extendió en el mismo lugar para no arriesgar lo que
 * ya estaba probado — queda anotado como desviación consciente respecto al
 * documento de arquitectura, a reconciliar en docs/02-firmware-architecture.md.
 *
 * Qué recibe / qué entrega:
 * - Recibe la temperatura medida y la configuración de umbrales desde los
 *   estados compartidos.
 * - Entrega el umbral activo, la causa de CRITICAL (si aplica) y el duty cycle
 *   a ControlState para que otros módulos (LEDs, OLED, telemetría) lo reflejen.
 * - Entrega la señal de PWM al ventilador real y, cuando corresponde, revoca el
 *   keep-alive de la planta externa a través de heater_simulation_task.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

#include "cooling_manager.h"
#include "../state/control_state.h"
#include "../state/config_state.h"
#include "../state/telemetry_state.h"
#include "heater_simulation_task.h"

LOG_MODULE_REGISTER(cooling_manager, LOG_LEVEL_INF);

#define STACK_SIZE 1024
#define THREAD_PRIORITY 2       /* Alta prioridad — ver docs/02-firmware-architecture.md */
#define PERIOD_MS 1000

/* Mapeo umbral -> duty cycle (%). TODO: ajustar estos valores contra el
 * comportamiento esperado real (discussion.md no fija porcentajes exactos
 * salvo la tabla conceptual 0/30/60/100 de la Sección 3.1 — aquí se usan
 * valores ya presentes en el código previo a esta sesión, sin tocarlos). */
#define DUTY_COLD    40
#define DUTY_LOW     60
#define DUTY_MEDIUM  80
#define DUTY_HIGH   100
#define DUTY_CRITICAL 100

/* Margen de histéresis fijo, igual para los 4 umbrales (discussion.md §4.2). */
#define HYSTERESIS_MARGIN_C 2.0f

/* Tiempo máximo tolerado en CRITICAL-por-sobretemperatura antes de revocar el
 * keep-alive de la planta externa. Valor corto a propósito para que sea
 * observable en una demostración en vivo, siguiendo el mismo criterio que el
 * temporizador de estabilidad del NTC (15s) — ver checkpoint.md Sección 3.3. */
#define CRITICAL_OVERTEMP_TIMEOUT_MS 20000

static const struct pwm_dt_spec fan_pwm = PWM_DT_SPEC_GET(DT_PATH(zephyr_user));

bool cooling_manager_init(void)
{
	if (!pwm_is_ready_dt(&fan_pwm)) {
		LOG_ERR("PWM del ventilador no listo — revisar nucleo_l476rg.overlay");
		return false;
	}
	return true;
}

/*
 * Evaluador genérico de histéresis: reemplaza tener 4 bloques if/else casi
 * idénticos (uno por umbral) por una sola tabla ordenada que se recorre una
 * vez. Para cada umbral, si el estado ACTUAL ya está en ese nivel o por
 * encima, el umbral efectivo para permanecer ahí baja en HYSTERESIS_MARGIN_C
 * (hace falta enfriarse más para bajar). Si el estado actual está por debajo,
 * el umbral efectivo es el exacto (subir es inmediato al cruzarlo). Esto
 * implementa exactamente la regla asimétrica de discussion.md §4.2 para los
 * 4 umbrales a la vez, sin duplicar la lógica por cada uno.
 */
static threshold_code_t classify_with_hysteresis(float temperature,
						  threshold_code_t current_state,
						  const ConfigState *cfg)
{
	struct {
		float value;
		threshold_code_t level;
	} boundaries[] = {
		{ cfg->threshold_low,      THRESHOLD_LOW },
		{ cfg->threshold_medium,   THRESHOLD_MEDIUM },
		{ cfg->threshold_high,     THRESHOLD_HIGH },
		{ cfg->threshold_critical, THRESHOLD_CRITICAL },
	};

	threshold_code_t new_state = THRESHOLD_COLD;

	for (size_t i = 0; i < ARRAY_SIZE(boundaries); i++) {
		float effective = boundaries[i].value;

		if (current_state >= boundaries[i].level) {
			/* Ya estábamos en este nivel o superior: hace falta bajar
			 * más para abandonarlo (histéresis en la dirección de bajada). */
			effective -= HYSTERESIS_MARGIN_C;
		}

		if (temperature >= effective) {
			new_state = boundaries[i].level;
		}
	}

	return new_state;
}

static uint8_t duty_for_threshold(threshold_code_t code)
{
	switch (code) {
	case THRESHOLD_CRITICAL: return DUTY_CRITICAL;
	case THRESHOLD_HIGH:     return DUTY_HIGH;
	case THRESHOLD_MEDIUM:   return DUTY_MEDIUM;
	case THRESHOLD_LOW:      return DUTY_LOW;
	case THRESHOLD_COLD:
	default:                 return DUTY_COLD;
	}
}

static void apply_duty_cycle(uint8_t duty_percent)
{
	uint32_t pulse_ns = (fan_pwm.period * duty_percent) / 100;

	int err = pwm_set_pulse_dt(&fan_pwm, pulse_ns);
	if (err != 0) {
		LOG_ERR("pwm_set_pulse_dt fallo: %d", err);
	}
}

static void cooling_manager_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("cooling_manager thread started");

	bool pwm_ok = cooling_manager_init();
	if (!pwm_ok) {
		LOG_ERR("cooling_manager continuara sin control de PWM funcional");
	}

	threshold_code_t last_threshold = THRESHOLD_COLD;
	uint32_t time_in_critical_overtemp_ms = 0;

	while (1) {
		ControlState control;
		ConfigState config;
		TelemetryState telemetry;

		control_state_get(&control);
		config_state_get(&config);
		telemetry_state_get(&telemetry);

		bool ntc_failed = (telemetry.error_log_flags & ERROR_FLAG_NTC_SENSOR) != 0;

		uint8_t duty;
		threshold_code_t threshold;
		critical_cause_t cause;

		if (ntc_failed) {
			/* Falla de sensor: CRITICAL inmediato, sin pasar por la
			 * clasificación por temperatura (esa temperatura ya no es
			 * confiable). Failsafe: ventilador a máxima velocidad. */
			threshold = THRESHOLD_CRITICAL;
			cause = CRITICAL_CAUSE_SENSOR_FAULT;
			duty = DUTY_CRITICAL;
			LOG_WRN("Failsafe activo (NTC en falla): ventilador forzado a %u%%", duty);
		} else {
			threshold = classify_with_hysteresis(control.current_temperature,
							      last_threshold, &config);
			cause = (threshold == THRESHOLD_CRITICAL) ? CRITICAL_CAUSE_OVERTEMP
								   : CRITICAL_CAUSE_NONE;
			duty = duty_for_threshold(threshold);
		}

		/* --- Temporizador de tolerancia en CRITICAL-por-sobretemperatura --- */
		if (threshold == THRESHOLD_CRITICAL && cause == CRITICAL_CAUSE_OVERTEMP) {
			time_in_critical_overtemp_ms += PERIOD_MS;

			if (time_in_critical_overtemp_ms >= CRITICAL_OVERTEMP_TIMEOUT_MS) {
				LOG_ERR("CRITICAL por sobretemperatura sostenido %u ms — "
					"revocando keep-alive de la planta externa",
					time_in_critical_overtemp_ms);
				heater_simulation_set_authorized(false);
				control_state_set_keep_alive_revoked(true);
			}
		} else {
			if (time_in_critical_overtemp_ms >= CRITICAL_OVERTEMP_TIMEOUT_MS) {
				/* Se recuperó tras haber revocado el keep-alive: restaurar. */
				LOG_INF("CRITICAL por sobretemperatura resuelto — "
					"restaurando autorizacion de keep-alive");
				heater_simulation_set_authorized(true);
				control_state_set_keep_alive_revoked(false);
			}
			time_in_critical_overtemp_ms = 0;
		}
		control_state_set_time_in_critical(time_in_critical_overtemp_ms);

		control_state_set_threshold(threshold, cause);
		control_state_set_fan_duty(duty);
		last_threshold = threshold;

		if (pwm_ok) {
			apply_duty_cycle(duty);
		}

		LOG_INF("T=%.2fC umbral=%d causa=%d duty=%u%% t_critico=%ums",
			(double)control.current_temperature, threshold, cause, duty,
			time_in_critical_overtemp_ms);

		k_sleep(K_MSEC(PERIOD_MS));
	}
}

K_THREAD_DEFINE(cooling_manager_tid, STACK_SIZE, cooling_manager_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);