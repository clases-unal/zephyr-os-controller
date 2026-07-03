/*
 * shift_register.h — Driver genérico para un SN74HC595N (8 salidas) vía SPI.
 *
 * Este driver no sabe nada sobre LEDs, colores ni significados — solo sabe
 * mandar un byte por SPI y pulsar el LATCH para que el registro lo saque a
 * sus 8 pines Q0-Q7 en paralelo. La interpretación de qué bit significa qué
 * LED vive en tasks/led_representation_manager.c, no aquí — así el driver es
 * reutilizable si el proyecto necesita otro registro en el futuro.
 */

#ifndef SHIFT_REGISTER_H
#define SHIFT_REGISTER_H

#include <stdint.h>
#include <stdbool.h>

/* Inicializa el bus SPI y el pin de LATCH. Llamar una vez antes de cualquier
 * shift_register_write(). Retorna false si el hardware no está listo. */
bool shift_register_init(void);

/*
 * Envía un byte completo al registro y pulsa LATCH para que quede reflejado
 * en las salidas Q0-Q7. El bit 0 (LSB) de `value` corresponde a Q0 y el bit 7
 * (MSB) a Q7 — este es el orden que usa led_representation_manager.c para
 * mapear Qa..Qh a bits 0..7 respectivamente.
 */
void shift_register_write(uint8_t value);

#endif /* SHIFT_REGISTER_H */
