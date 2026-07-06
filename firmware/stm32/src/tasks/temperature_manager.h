/**
 * @file temperature_manager.h
 * @brief Gestor de Temperatura: ADC, Steinhart-Hart, filtrado.
 *
 * Módulo encargado de la inicialización y lectura del sensor de temperatura.
 * Cada módulo de tasks/ expone únicamente una función de inicialización,
 * mientras que el hilo se registra estáticamente en el archivo .c correspondiente.
 */

#ifndef TEMPERATURE_MANAGER_H
#define TEMPERATURE_MANAGER_H

/**
 * @brief Inicializa las variables internas y el hardware del sensor NTC.
 *
 * Configura los buffers de filtro y realiza el primer intento de vinculación
 * con el ADC subyacente.
 */
void temperature_manager_init(void);

#endif /* TEMPERATURE_MANAGER_H */