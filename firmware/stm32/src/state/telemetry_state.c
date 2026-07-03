/*
 * telemetry_state.c — Implementación de TelemetryState + telemetry_mutex
 *
 * NOTA SOBRE PERSISTENCIA: system_boot_count, error_count[] y
 * ntc_consecutive_failures "deberían" persistir entre reinicios según el
 * diseño original (discussion.md Sección 8), pero se decidió explícitamente
 * NO implementar NVS por ahora (ver checkpoint.md Sección 4) — el costo de
 * implementarlo bien no se justificaba dentro del tiempo disponible. Por eso
 * estos campos viven solo en RAM y se reinician a 0 en cada boot. Queda
 * documentado como mejora futura en docs/04-design-decisions.md, junto con
 * la lista exacta de qué campos necesitarían NVS el día que se retome.
 */

#include "telemetry_state.h"

static TelemetryState state;
static struct k_mutex telemetry_mutex;

void telemetry_state_init(void)
{
	k_mutex_init(&telemetry_mutex);
	state.error_log_flags = 0;
	state.system_boot_count = 0;
	for (int i = 0; i < 4; i++) {
		state.error_count[i] = 0;
	}
	state.ntc_consecutive_failures = 0;
}

void telemetry_state_get(TelemetryState *out)
{
	k_mutex_lock(&telemetry_mutex, K_FOREVER);
	*out = state;
	k_mutex_unlock(&telemetry_mutex);
}

void telemetry_state_set_error_flag(uint32_t flag, bool active)
{
	k_mutex_lock(&telemetry_mutex, K_FOREVER);
	if (active) {
		state.error_log_flags |= flag;
	} else {
		state.error_log_flags &= ~flag;
	}
	k_mutex_unlock(&telemetry_mutex);
}

void telemetry_state_increment_boot_count(void)
{
	k_mutex_lock(&telemetry_mutex, K_FOREVER);
	state.system_boot_count++;
	k_mutex_unlock(&telemetry_mutex);
}
