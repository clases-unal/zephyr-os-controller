/*
 * cooling_manager.h — Determinación de umbral térmico + control PWM del ventilador
 */

#ifndef COOLING_MANAGER_H
#define COOLING_MANAGER_H

#include <stdbool.h>

/*
 * Verifica que el dispositivo PWM esté listo. Se llama internamente desde el
 * propio hilo al arrancar (no requiere ser invocada desde main.c).
 */
bool cooling_manager_init(void);

#endif /* COOLING_MANAGER_H */
