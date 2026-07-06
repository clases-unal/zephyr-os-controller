/**
 * @file matrix_keypad.h
 * @brief Escaneo no bloqueante de teclado matricial 4×4.
 *
 * Layout de teclas:
 * Fila\\Col  C0   C1   C2   C3
 * R0      1    2    3    A
 * R1      4    5    6    B
 * R2      7    8    9    C
 * R3      * 0    #    D
 *
 * Pines (definidos en el overlay, nucleo_l476rg.overlay):
 * Filas (salida):             PC7 (R0), PA9 (R1), PA8 (R2), PB10 (R3)
 * Columnas (entrada pull-up): PB4 (C0), PB5 (C1), PB3 (C2), PA10 (C3)
 *
 * El driver lee estos pines desde DeviceTree usando:
 * DT_NODELABEL(row_0..row_3) y DT_NODELABEL(col_0..col_3).
 *
 * @note Para modificar el mapeo del teclado, cambia solo el overlay
 * zephyr/boards/nucleo_l476rg.overlay. El código no necesita otro cambio.
 */

#ifndef MATRIX_KEYPAD_H
#define MATRIX_KEYPAD_H

#include <stdbool.h>

/**
 * @brief Inicializa los GPIOs correspondientes a las filas y columnas del teclado.
 * * Se debe llamar una sola vez antes de invocar a matrix_keypad_scan().
 *
 * @return true si todos los pines definidos en el DeviceTree están listos y se
 * configuraron correctamente, false de lo contrario.
 */
bool matrix_keypad_init(void);

/**
 * @brief Escanea la matriz del teclado de forma no bloqueante.
 *
 * Aplica la técnica de bajar (LOW) una fila a la vez y leer el estado de
 * todas las columnas. El tiempo de ejecución es rápido (~1ms por llamada).
 *
 * @param out_key Puntero donde se almacenará el carácter de la tecla pulsada,
 * si la hubiera.
 * @return true si se detectó una tecla presionada en este ciclo, false si no.
 */
bool matrix_keypad_scan(char *out_key);

#endif /* MATRIX_KEYPAD_H */