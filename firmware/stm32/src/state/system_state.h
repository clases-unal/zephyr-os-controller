/*
 * system_state.h — Coordinación global de alta prioridad (SystemState)
 * Protegido por sys_mutex. Este es el ÚLTIMO mutex en el orden de adquisición
 * (ver docs/02-firmware-architecture.md Sección 2) — si un hilo necesita sys_mutex
 * junto con otro, sys_mutex se adquiere al final.
 *
 * Dominio: las dos banderas de más alto nivel del sistema, las que casi todos
 * los demás hilos consultan en cada ciclo para decidir si deben actuar con
 * normalidad, quedarse quietos, o iniciar su propia secuencia de apagado.
 */

#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <zephyr/kernel.h>
#include <stdbool.h>

typedef struct {
	/* false = Alarma Permanente (discussion.md §4.5): el sistema quedó
	 * inhabilitado tras una falla que requiere intervención humana (p. ej.
	 * falla sostenida del NTC). No hay ningún camino de software para
	 * volver a poner esto en true — se recupera únicamente con un ciclo de
	 * energía físico después de que la persona corrija la causa. */
	bool system_enabled;

	/* true = se solicitó un apagado ordenado (botón físico, pulsación
	 * larga). Cada hilo que controla hardware (PWM, keep-alive, registro de
	 * LEDs) debe revisar esta bandera y llevar su propia salida a un estado
	 * seguro cuando se active. No implica ninguna escritura a memoria no
	 * volátil — esa idea se descartó (ver checkpoint.md Sección 4); esto es
	 * puramente "todos los hilos terminan limpio y el sistema queda
	 * inactivo", no una preparación para apagar la alimentación desde el
	 * propio firmware. */
	bool shutdown_requested;
} SystemState;

void system_state_init(void);
void system_state_get(SystemState *out);
void system_state_set_enabled(bool enabled);
void system_state_request_shutdown(void);

#endif /* SYSTEM_STATE_H */
