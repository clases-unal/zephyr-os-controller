/**
 * @file cooling_manager.h
 * @brief Determinación de umbral térmico y control PWM del ventilador.
 *
 * Expone la interfaz de inicialización para el subsistema de refrigeración.
 */

#ifndef COOLING_MANAGER_H
#define COOLING_MANAGER_H

#include <stdbool.h>

/**
 * @brief Verifica que el dispositivo PWM esté listo y configurado.
 *
 * Se llama internamente desde el propio hilo de refrigeración al arrancar.
 * No requiere ser invocada manualmente desde main.c.
 *
 * @return true si el periférico PWM está listo para usarse, false en caso contrario.
 */
bool cooling_manager_init(void);

#endif /* COOLING_MANAGER_H */