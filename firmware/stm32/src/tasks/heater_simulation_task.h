#ifndef HEATER_SIMULATION_TASK_H
#define HEATER_SIMULATION_TASK_H

#include <stdbool.h>

void heater_simulation_task_init(void);

/*
 * Autoriza o revoca la línea keep-alive (PA4) hacia la planta térmica externa.
 * Pensada para que cooling_manager pueda cortar la autorización cuando el
 * sistema permanece en CRITICAL por sobretemperatura más allá del tiempo de
 * tolerancia (ver checkpoint.md Sección 3.3 / discussion.md §9.1.3), sin que
 * ningún otro módulo tenga que tocar el GPIO directamente — la propiedad del
 * pin se queda dentro de este archivo, igual que el resto del proyecto separa
 * "quién decide" de "quién controla el hardware físico".
 *
 * true  = autorizado (comportamiento normal, sujeto igualmente a system_enabled)
 * false = revocado — el pin se mantiene en LOW sin importar el resto de la
 *         lógica, hasta que se vuelva a llamar con true.
 */
void heater_simulation_set_authorized(bool authorized);

#endif
