/*
 * control_state.h — Estado de control térmico (ControlState)
 * Protegido por control_mutex. Ver docs/02-firmware-architecture.md Sección 2.
 *
 * Dominio: lo relacionado al lazo de control térmico en sí (temperatura medida,
 * salida PWM hacia el ventilador, umbral activo). NO incluye configuración editable
 * por el usuario (eso es ConfigState) ni banderas de diagnóstico (eso es TelemetryState).
 */

#ifndef CONTROL_STATE_H
#define CONTROL_STATE_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

/* Códigos de umbral térmico. El orden importa: se usa para comparaciones >=.
 * THRESHOLD_CRITICAL se agregó junto con el 4to umbral configurable
 * (ConfigState.threshold_critical) — antes CRITICAL no existía como código
 * de umbral real, solo se mencionaba en la documentación sin implementación. */
typedef enum {
	THRESHOLD_COLD = 0,
	THRESHOLD_LOW,
	THRESHOLD_MEDIUM,
	THRESHOLD_HIGH,
	THRESHOLD_CRITICAL,
} threshold_code_t;

/* Causa específica de un CRITICAL activo. Necesario porque el LED Qh y la
 * pantalla OLED deben mostrar mensajes distintos según la causa (ver
 * discussion.md §4.3), aunque threshold_code_t por sí solo no distingue entre
 * ambas — ambas producen THRESHOLD_CRITICAL. */
typedef enum {
	CRITICAL_CAUSE_NONE = 0,     /* No está en CRITICAL */
	CRITICAL_CAUSE_OVERTEMP,     /* current_temperature >= threshold_critical */
	CRITICAL_CAUSE_SENSOR_FAULT, /* Lectura ADC fuera de rango físico válido */
} critical_cause_t;

typedef struct {
	float current_temperature;         /* °C, ya filtrada (promedio móvil) */
	uint8_t fan_pwm_duty_cycle;         /* 0-100 */
	threshold_code_t current_threshold_code;
	critical_cause_t critical_cause;    /* Válido solo si current_threshold_code == THRESHOLD_CRITICAL */

	/* Milisegundos continuos transcurridos desde que se entró en CRITICAL por
	 * causa OVERTEMP. Se usa para el temporizador de 20s que decide si se
	 * corta la línea keep-alive (ver cooling_manager.c y checkpoint.md §3.3).
	 * Se reinicia a 0 cada vez que se sale de CRITICAL-por-sobretemperatura. */
	uint32_t time_in_critical_overtemp_ms;

	/* true mientras el keep-alive fue revocado por el propio lazo de control
	 * térmico (timeout de CRITICAL sostenido), para que otros módulos (LED,
	 * OLED) puedan mostrarlo sin tener que re-derivar la condición ellos mismos. */
	bool keep_alive_revoked;
} ControlState;

/*
 * Inicializa la estructura y su mutex. Debe llamarse una única vez desde main()
 * antes de crear cualquier hilo que la use.
 */
void control_state_init(void);

/* Lectura segura: copia el estado actual en *out bajo el mutex. */
void control_state_get(ControlState *out);

/* Escritura segura de temperatura (llamada típicamente desde temperature_manager). */
void control_state_set_temperature(float temperature_celsius);

/* Escritura segura de duty cycle (llamada típicamente desde cooling_manager). */
void control_state_set_fan_duty(uint8_t duty_cycle);

/* Escritura segura de umbral activo y causa de CRITICAL (causa se ignora si
 * code != THRESHOLD_CRITICAL, pero se recomienda pasar CRITICAL_CAUSE_NONE
 * explícitamente en ese caso para dejarlo claro en el sitio de llamada). */
void control_state_set_threshold(threshold_code_t code, critical_cause_t cause);

/* Actualiza el contador de tiempo sostenido en CRITICAL-por-sobretemperatura. */
void control_state_set_time_in_critical(uint32_t ms);

/* Marca si el keep-alive fue revocado por el propio control térmico. */
void control_state_set_keep_alive_revoked(bool revoked);

#endif /* CONTROL_STATE_H */
