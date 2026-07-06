/**
 * @file ntc_sensor.h
 * @brief Driver de lectura del termistor NTC vía ADC empleando la ecuación Beta.
 *
 * Especificaciones de hardware asumidas:
 * NTC: 10kΩ @25°C, B25/50 = 3470K ±1%.
 * Resistencia fija del divisor: 10kΩ.
 * Pin asignado: PA0 (ADC1, canal 5) — verificar zephyr/boards/nucleo_l476rg.overlay
 */

#ifndef NTC_SENSOR_H
#define NTC_SENSOR_H

#include <stdbool.h>

/**
 * @brief Inicializa el canal analógico a digital (ADC) para el termistor.
 *
 * Debe llamarse una vez antes del primer intento de lectura (read).
 *
 * @return true si el dispositivo ADC se inicializó de acuerdo al overlay. false
 * si el periférico no se encuentra listo.
 */
bool ntc_sensor_init(void);

/**
 * @brief Obtiene una lectura de la temperatura actual en grados Celsius.
 *
 * Efectúa el muestreo del ADC, halla la resistencia del divisor de tensión
 * y luego aplica la ecuación Beta térmica.
 *
 * @param out_temperature Puntero de salida en el que se almacenará el valor en °C.
 * @return true si la lectura fue exitosa. false si se detectó un valor anormal,
 * lo cual típicamente señala un problema físico (cable abierto o corto eléctrico).
 */
bool ntc_sensor_read_celsius(float *out_temperature);

#endif /* NTC_SENSOR_H */