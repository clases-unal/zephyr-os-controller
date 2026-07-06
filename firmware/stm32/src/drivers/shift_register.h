/**
 * @file shift_register.h
 * @brief Driver genérico para un SN74HC595N (8 salidas) operado vía SPI.
 *
 * @details
 * Este driver delega el significado visual de los LEDs en capas superiores
 * (ej. led_representation_manager.c). Su única responsabilidad es desplazar
 * un byte mediante el periférico de hardware SPI y activar el flanco del
 * LATCH para hacer el reflejo visible de un solo golpe.
 */

#ifndef SHIFT_REGISTER_H
#define SHIFT_REGISTER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Inicializa el bus SPI y el pin GPIO de LATCH del Shift Register.
 *
 * Se debe ejecutar una vez de manera preliminar a la escritura de bits.
 *
 * @return true si los periféricos (SPI, GPIO) indicados por el DeviceTree
 * se prepararon con éxito. false ante un error de validación del hardware.
 */
bool shift_register_init(void);

/**
 * @brief Transmite un bloque de 8 bits (un byte) y efectúa el pulso de enganche (LATCH).
 *
 * El bit 0 (LSB) del parámetro `value` se reflejará físicamente en el pin Q0.
 * El bit 7 (MSB) de `value` corresponderá a Q7 de forma secuencial.
 *
 * @param value Byte (uint8_t) con la combinación de estados lógicos a representar en paralelo.
 */
void shift_register_write(uint8_t value);

#endif /* SHIFT_REGISTER_H */