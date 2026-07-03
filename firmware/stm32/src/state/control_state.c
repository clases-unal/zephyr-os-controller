/*
 * control_state.c — Implementación de ControlState + control_mutex
 *
 * Patrón usado en las 5 estructuras de estado del sistema: la struct y el k_mutex
 * son estáticos (privados a este archivo, "file scope"). Nadie fuera de este archivo
 * puede tocar los campos directamente — solo a través de las funciones get/set
 * declaradas en el .h. Esto es lo que hace cumplir la disciplina de mutex a nivel
 * de organización de archivos (ver discussion.md Sección 10).
 */

#include "control_state.h"

static ControlState state;
static struct k_mutex control_mutex;

void control_state_init(void)
{
	k_mutex_init(&control_mutex);

	state.current_temperature = 0.0f;
	state.fan_pwm_duty_cycle = 0;
	state.current_threshold_code = THRESHOLD_COLD;
	state.critical_cause = CRITICAL_CAUSE_NONE;
	state.time_in_critical_overtemp_ms = 0;
	state.keep_alive_revoked = false;
}

void control_state_get(ControlState *out)
{
	k_mutex_lock(&control_mutex, K_FOREVER);
	*out = state;
	k_mutex_unlock(&control_mutex);
}

void control_state_set_temperature(float temperature_celsius)
{
	k_mutex_lock(&control_mutex, K_FOREVER);
	state.current_temperature = temperature_celsius;
	k_mutex_unlock(&control_mutex);
}

void control_state_set_fan_duty(uint8_t duty_cycle)
{
	k_mutex_lock(&control_mutex, K_FOREVER);
	state.fan_pwm_duty_cycle = duty_cycle;
	k_mutex_unlock(&control_mutex);
}

void control_state_set_threshold(threshold_code_t code, critical_cause_t cause)
{
	k_mutex_lock(&control_mutex, K_FOREVER);
	state.current_threshold_code = code;
	state.critical_cause = (code == THRESHOLD_CRITICAL) ? cause : CRITICAL_CAUSE_NONE;
	k_mutex_unlock(&control_mutex);
}

void control_state_set_time_in_critical(uint32_t ms)
{
	k_mutex_lock(&control_mutex, K_FOREVER);
	state.time_in_critical_overtemp_ms = ms;
	k_mutex_unlock(&control_mutex);
}

void control_state_set_keep_alive_revoked(bool revoked)
{
	k_mutex_lock(&control_mutex, K_FOREVER);
	state.keep_alive_revoked = revoked;
	k_mutex_unlock(&control_mutex);
}
