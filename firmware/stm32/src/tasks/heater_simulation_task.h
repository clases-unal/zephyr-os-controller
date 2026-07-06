/**
 * @file heater_simulation_task.h
 * @brief Interfaz para el hilo de simulación de fuente de calor.
 */

#ifndef HEATER_SIMULATION_TASK_H
#define HEATER_SIMULATION_TASK_H

#include <stdbool.h>

/**
 * @brief Inicializa la configuración del pin GPIO utilizado para simular calor.
 */
void heater_simulation_task_init(void);

/**
 * @brief Autoriza o revoca la línea keep-alive (PA4) hacia la planta térmica externa.
 *
 * Pensada para que cooling_manager pueda cortar la autorización cuando el
 * sistema permanece en CRITICAL por sobretemperatura más allá del tiempo de
 * tolerancia, sin que ningún otro módulo tenga que tocar el GPIO directamente.
 *
 * @param authorized true = autorizado (sujeto igualmente a system_enabled).
 * false = revocado (pin se mantiene en LOW hasta ser reautorizado).
 */
void heater_simulation_set_authorized(bool authorized);

#endif /* HEATER_SIMULATION_TASK_H */