/*
 * system_state.c — Implementación de SystemState + sys_mutex
 *
 * Mismo patrón de archivo-privado + mutex que el resto de los 5 estados
 * compartidos del sistema (ver control_state.c para la explicación completa
 * del patrón). Este archivo es deliberadamente el más simple de los cinco:
 * solo dos banderas booleanas, sin cálculos ni validación — cualquier lógica
 * sobre QUÉ hacer cuando cambian pertenece a quien las lee (cada tarea),
 * nunca aquí.
 */

#include "system_state.h"

static SystemState state;
static struct k_mutex sys_mutex;

void system_state_init(void)
{
	k_mutex_init(&sys_mutex);
	state.system_enabled = true;
	state.shutdown_requested = false;
}

void system_state_get(SystemState *out)
{
	k_mutex_lock(&sys_mutex, K_FOREVER);
	*out = state;
	k_mutex_unlock(&sys_mutex);
}

void system_state_set_enabled(bool enabled)
{
	k_mutex_lock(&sys_mutex, K_FOREVER);
	state.system_enabled = enabled;
	k_mutex_unlock(&sys_mutex);
}

void system_state_request_shutdown(void)
{
	k_mutex_lock(&sys_mutex, K_FOREVER);
	state.shutdown_requested = true;
	k_mutex_unlock(&sys_mutex);
}
