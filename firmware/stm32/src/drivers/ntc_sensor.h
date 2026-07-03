/*
 * ntc_sensor.h — Driver de lectura del termistor NTC vía ADC + ecuación Beta
 *
 * NTC: 10kΩ @25°C, B25/50 = 3470K ±1% (00-project-decisions-and-procedure.md DEC-H-003)
 * Resistencia fija del divisor: 10kΩ
 * Pin: PA0 (ADC1, canal 5) — ver zephyr/boards/nucleo_l476rg.overlay
 */

#ifndef NTC_SENSOR_H
#define NTC_SENSOR_H

#include <stdbool.h>

/*
 * Inicializa el canal ADC (debe llamarse una vez antes del primer read).
 * Retorna false si el dispositivo ADC no está listo (overlay mal aplicado).
 */
bool ntc_sensor_init(void);

/*
 * Lee el ADC, convierte a resistencia y aplica la ecuación Beta para obtener
 * temperatura en °C. Retorna false si la lectura está fuera del rango físico
 * esperado (sensor en corto o cable abierto — discussion.md Sección 3.1).
 */
bool ntc_sensor_read_celsius(float *out_temperature);

#endif /* NTC_SENSOR_H */
