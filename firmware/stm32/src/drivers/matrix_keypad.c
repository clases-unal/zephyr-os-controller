/*
 * matrix_keypad.c — Escaneo de teclado matricial 4×4
 *
 * Técnica: activar una fila a la vez (LOW), leer todas las columnas.
 * Las columnas tienen pull-up interno → reposo en HIGH, presionado en LOW.
 *
 * El teclado se define desde DeviceTree.
 * - Filas: DT_NODELABEL(row_0..row_3)
 * - Columnas: DT_NODELABEL(col_0..col_3)
 *
 * Si cambias el cableado del teclado, actualiza solo el overlay
 * zephyr/boards/nucleo_l476rg.overlay. El código del driver no necesita
 * cambios adicionales.
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree/gpio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "matrix_keypad.h"

#define ROWS 4
#define COLS 4

static const struct gpio_dt_spec row_pins[ROWS] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(row_0), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(row_1), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(row_2), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(row_3), gpios),
};

static const struct gpio_dt_spec col_pins[COLS] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(col_0), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(col_1), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(col_2), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(col_3), gpios),
};

LOG_MODULE_REGISTER(matrix_keypad, LOG_LEVEL_WRN);

static const char key_map[ROWS][COLS] = {
	{'1', '2', '3', 'A'},
	{'4', '5', '6', 'B'},
	{'7', '8', '9', 'C'},
	{'*', '0', '#', 'D'},
};

bool matrix_keypad_init(void)
{
	for (int r = 0; r < ROWS; r++) {
		if (!device_is_ready(row_pins[r].port)) {
			LOG_ERR("Row pin %d no listo", r);
			return false;
		}
		/* Filas: salida, reposo en HIGH (no activada) */
		gpio_pin_configure_dt(&row_pins[r], GPIO_OUTPUT_HIGH);
	}

	for (int c = 0; c < COLS; c++) {
		if (!device_is_ready(col_pins[c].port)) {
			LOG_ERR("Col pin %d no listo", c);
			return false;
		}
		/* Columnas: entrada con pull-up interno */
		gpio_pin_configure_dt(&col_pins[c], GPIO_INPUT | GPIO_PULL_UP);
	}

	return true;
}

bool matrix_keypad_scan(char *out_key)
{
	for (int r = 0; r < ROWS; r++) {
		/* Activar fila: ponerla en LOW */
		gpio_pin_set_dt(&row_pins[r], 0);
		k_busy_wait(10);  /* 10 µs para estabilizar señal */

		for (int c = 0; c < COLS; c++) {
			int val = gpio_pin_get_dt(&col_pins[c]);
			if (val == 0) {
				/* Columna en LOW → tecla presionada */
				gpio_pin_set_dt(&row_pins[r], 1);  /* restaurar fila */
				*out_key = key_map[r][c];
				return true;
			}
		}

		/* Desactivar fila: volver a HIGH */
		gpio_pin_set_dt(&row_pins[r], 1);
	}

	return false;
}
