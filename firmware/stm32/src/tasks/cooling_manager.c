/**
 * @file cooling_manager.c
 * @brief Hilo de decisión térmica y control de ventilador.
 *
 * Qué hace:
 * - Lee la temperatura actual desde ControlState.
 * - Clasifica esa temperatura en uno de 5 niveles (COLD/LOW/MEDIUM/HIGH/CRITICAL)
 * contra los 4 umbrales configurables en ConfigState, aplicando histéresis
 * asimétrica (subir es inmediato, bajar requiere cruzar umbral - 2°C).
 * - Detecta la entrada a CRITICAL por dos causas independientes: sobretemperatura
 * o falla de sensor NTC.
 * - Mientras CRITICAL-por-sobretemperatura se sostiene más de 20s sin que la
 * temperatura baje, revoca la autorización de la planta externa (línea keep-alive)
 * como medida de seguridad adicional.
 * - Convierte el nivel térmico activo en un duty cycle y lo aplica al ventilador.
 * - En caso de fallo del sensor NTC, entra en failsafe y fuerza el ventilador a
 * máxima velocidad.
 *
 * Qué recibe / qué entrega:
 * - Recibe la temperatura medida y la configuración de umbrales desde los
 * estados compartidos.
 * - Entrega el umbral activo, la causa de CRITICAL (si aplica) y el duty cycle
 * a ControlState.
 * - Entrega la señal de PWM al ventilador real y revoca el keep-alive si es necesario.
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
#define THREAD_PRIORITY 2       /* Alta prioridad */
#define PERIOD_MS 1000

/* Mapeo umbral -> duty cycle (%). */
#define DUTY_COLD    40
#define DUTY_LOW     60
#define DUTY_MEDIUM  80
#define DUTY_HIGH   100
#define DUTY_CRITICAL 100

/* Margen de histéresis fijo, igual para los 4 umbrales. */
#define HYSTERESIS_MARGIN_C 2.0f

/* Tiempo máximo tolerado en CRITICAL-por-sobretemperatura antes de revocar el
 * keep-alive de la planta externa. */
#define CRITICAL_OVERTEMP_TIMEOUT_MS 20000

static const struct pwm_dt_spec fan_pwm = PWM_DT_SPEC_GET(DT_PATH(zephyr_user));

/**
 * @brief Inicializa el hardware necesario para el control de refrigeración.
 *
 * @return true si el periférico PWM especificado en el DeviceTree está listo, false de lo contrario.
 */
bool cooling_manager_init(void)
{
	if (!pwm_is_ready_dt(&fan_pwm)) {
		LOG_ERR("PWM del ventilador no listo — revisar nucleo_l476rg.overlay");
		return false;
	}
	return true;
}

/**
 * @brief Clasifica la temperatura actual en un nivel térmico aplicando histéresis.
 *
 * Evaluador genérico que recorre una tabla ordenada de umbrales. Si el estado
 * actual ya está en un nivel o por encima, el umbral efectivo para permanecer
 * ahí baja según HYSTERESIS_MARGIN_C (requiere enfriarse más para bajar de nivel).
 * Si está por debajo, el umbral efectivo es exacto (subir es inmediato).
 *
 * @param temperature Temperatura actual medida en grados Celsius.
 * @param current_state El nivel térmico (umbral) en el que se encontraba el sistema en el ciclo anterior.
 * @param cfg Puntero a la configuración actual del sistema (que contiene los valores de los umbrales).
 * @return El nuevo nivel térmico (threshold_code_t) evaluado.
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

/**
 * @brief Convierte un nivel térmico en un ciclo de trabajo (duty cycle) para el PWM.
 *
 * @param code Nivel térmico evaluado (threshold_code_t).
 * @return Porcentaje de duty cycle (0-100) correspondiente al nivel.
 */
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

/**
 * @brief Aplica un duty cycle en porcentaje al periférico PWM del ventilador.
 *
 * @param duty_percent Ciclo de trabajo deseado (0 a 100).
 */
static void apply_duty_cycle(uint8_t duty_percent)
{
	uint32_t pulse_ns = (fan_pwm.period * duty_percent) / 100;

	int err = pwm_set_pulse_dt(&fan_pwm, pulse_ns);
	if (err != 0) {
		LOG_ERR("pwm_set_pulse_dt fallo: %d", err);
	}
}

/**
 * @brief Hilo principal del gestor de refrigeración.
 *
 * Se ejecuta periódicamente y se encarga de:
 * 1. Obtener estados de control, configuración y telemetría.
 * 2. Determinar si el sensor NTC está en falla. Si es así, aplica el modo failsafe.
 * 3. Si el sensor está bien, clasifica la temperatura y determina la causa.
 * 4. Controla un temporizador para revocar el keep-alive si la temperatura crítica
 * se sostiene por mucho tiempo.
 * 5. Actualiza los estados y aplica el PWM al ventilador.
 *
 * @param p1 Parámetro no usado (requerido por la firma de hilos de Zephyr).
 * @param p2 Parámetro no usado.
 * @param p3 Parámetro no usado.
 */
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