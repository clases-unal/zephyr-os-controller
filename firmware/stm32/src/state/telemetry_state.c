/*
 * telemetry_state.c — Implementación de TelemetryState + telemetry_mutex
 *
 * ══════════════════════════════════════════════════════════════════════════
 * MEJORA FUTURA — Persistencia en NVS (deliberadamente NO implementada)
 * ══════════════════════════════════════════════════════════════════════════
 * Decisión: este proyecto NO usa NVS (Non-Volatile Storage) por ahora. Todo
 * el estado de esta struct y de ConfigState vive solo en RAM y se reinicia
 * a sus valores por defecto en cada boot. Esto fue una decisión consciente
 * del usuario, no un olvido — se documenta aquí el planteamiento completo
 * para que retomarlo en el futuro sea un trabajo de "seguir la receta" en
 * vez de rediseñar desde cero.
 *
 * Qué se llegó a considerar y por qué no se implementó:
 *  - Kconfig ya tenía CONFIG_FLASH, CONFIG_FLASH_MAP, CONFIG_NVS y
 *    CONFIG_SETTINGS habilitados en una versión anterior de prj.conf, pero
 *    config_state.c / telemetry_state.c nunca llegaron a llamar la API real
 *    de NVS (nvs_init/nvs_read/nvs_write) — esas banderas estaban activas
 *    sin ningún efecto real, solo agregando código muerto al binario. Se
 *    retiraron explícitamente de prj.conf por esa razón (ver el comentario
 *    en la sección "Almacenamiento persistente" de ese archivo).
 *  - El costo de implementarlo bien (particionar flash, definir el mapa de
 *    IDs de NVS, manejar wear-leveling y el caso de partición corrupta/
 *    vacía en el primer boot) no se justificaba dentro del alcance y tiempo
 *    disponibles de este proyecto.
 *
 * Campos que persistirían si se retoma NVS (cada uno necesitaría su propio
 * ID de entrada NVS):
 *   - ConfigState.threshold_low / _medium / _high / _critical (4 floats) —
 *     para no perder la calibración del usuario entre reinicios.
 *   - TelemetryState.system_boot_count (contador) — historial de arranques.
 *   - TelemetryState.error_count[4] (contadores, uno por bandera de
 *     ERROR_FLAG_*) — historial acumulado de fallas por tipo.
 *   - TelemetryState.ntc_consecutive_failures — solo tendría sentido
 *     persistirlo si se quisiera sobrevivir un power-cycle en medio de una
 *     racha de fallas del sensor; de lo contrario es razonable que arranque
 *     en 0 siempre, ya que describe una condición transitoria.
 *
 * Plan de implementación si se retoma:
 *   1. Reactivar en prj.conf: CONFIG_FLASH=y, CONFIG_FLASH_MAP=y,
 *      CONFIG_NVS=y, CONFIG_SETTINGS=y (opcional esta última si se usa la
 *      API de Settings en vez de NVS directo).
 *   2. Definir una partición de flash dedicada en el overlay (storage_partition
 *      o similar) del tamaño mínimo recomendado por el driver NVS.
 *   3. En cada *_init() (config_state_init, telemetry_state_init), intentar
 *      nvs_read() de cada campo; si no existe (primer boot / partición
 *      vacía), caer a los valores por defecto actuales y escribirlos.
 *   4. En cada *_set_*() que modifique un campo persistente, llamar
 *      nvs_write() además de actualizar la copia en RAM bajo mutex — con
 *      cuidado de no escribir en cada ciclo de un hilo periódico (desgasta
 *      la flash); para contadores que cambian seguido, conviene escribir
 *      solo cada N incrementos o al detectar SHUTDOWN.
 *   5. Actualizar system_state.c: dado que ya no hay escritura a flash que
 *      esperar durante SHUTDOWN, si se reintroduce NVS aquí sí habría que
 *      agregar un paso de "flush final" antes de completar el apagado.
 * ══════════════════════════════════════════════════════════════════════════
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
