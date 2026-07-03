/*
 * temperature_manager.h — Temperature Manager: ADC, Steinhart-Hart, filtrado
 *
 * Cada módulo de tasks/ expone únicamente una función de inicialización (si la
 * necesita); el hilo en sí se registra de forma estática en el .c con
 * K_THREAD_DEFINE, por lo que normalmente no hay más API pública que exponer aquí.
 */

#ifndef TEMPERATURE_MANAGER_H
#define TEMPERATURE_MANAGER_H

void temperature_manager_init(void);

#endif /* TEMPERATURE_MANAGER_H */
