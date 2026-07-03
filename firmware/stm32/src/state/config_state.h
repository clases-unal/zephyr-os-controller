/*
 * config_state.h — Parámetros editables por el usuario vía HMI (ConfigState)
 * Protegido por config_mutex.
 */

#ifndef CONFIG_STATE_H
#define CONFIG_STATE_H

#include <zephyr/kernel.h>

typedef struct {
	float threshold_low;
	float threshold_medium;
	float threshold_high;
	/* Umbral de CRITICAL por sobretemperatura. Se agregó junto con el
	 * rediseño de LEDs de un solo registro de desplazamiento — antes CRITICAL
	 * solo existía como concepto documentado, sin un valor de temperatura
	 * real que lo disparara. Editable por teclado igual que los otros tres,
	 * con el mismo margen de histéresis fijo de 2°C (discussion.md §4.2). */
	float threshold_critical;
	/* TODO: modo de operación — definir enum cuando se cierre el diseño de UI/teclado */
} ConfigState;

void config_state_init(void);
void config_state_get(ConfigState *out);
void config_state_set_thresholds(float low, float medium, float high, float critical);

#endif /* CONFIG_STATE_H */
