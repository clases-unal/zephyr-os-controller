/**
 * @file matrix_keypad.c
 * @brief Implementación del escaneo de teclado matricial 4×4.
 *
 * @details
 * Técnica: activar una fila a la vez (LOW), leer todas las columnas.
 * Las columnas tienen pull-up interno → reposo en HIGH, presionado en LOW.
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree/gpio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include "matrix_keypad.h"

#define ROWS 4
#define COLS 4

/* Nodos definidos en el DeviceTree para las filas */
static const struct gpio_dt_spec row_pins[ROWS] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(row_0), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(row_1), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(row_2), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(row_3), gpios),
};

/* Nodos definidos en el DeviceTree para las columnas */
static const struct gpio_dt_spec col_pins[COLS] = {
	GPIO_DT_SPEC_GET(DT_NODELABEL(col_0), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(col_1), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(col_2), gpios),
	GPIO_DT_SPEC_GET(DT_NODELABEL(col_3), gpios),
};

LOG_MODULE_REGISTER(matrix_keypad, LOG_LEVEL_WRN);

/* Matriz lógica que asocia filas y columnas con caracteres */
static const char key_map[ROWS][COLS] = {
	{'1', '2', '3', 'A'},
	{'4', '5', '6', 'B'},
	{'7', '8', '9', 'C'},
	{'*', '0', '#', 'D'},
};

bool matrix_keypad_init(void)
{
	/* Configuración de las filas como salidas, inactivas en nivel ALTO */
	for (int r = 0; r < ROWS; r++) {
		if (!device_is_ready(row_pins[r].port)) {
			LOG_ERR("Row pin %d no listo", r);
			return false;
		}
		gpio_pin_configure_dt(&row_pins[r], GPIO_OUTPUT_HIGH);
	}

	/* Configuración de las columnas como entradas con Pull-Up interno */
	for (int c = 0; c < COLS; c++) {
		if (!device_is_ready(col_pins[c].port)) {
			LOG_ERR("Col pin %d no listo", c);
			return false;
		}
		gpio_pin_configure_dt(&col_pins[c], GPIO_INPUT | GPIO_PULL_UP);
	}
	
	LOG_INF("Teclado matricial inicializado correctamente");
	return true;
}

bool matrix_keypad_scan(char *out_key)
{
	bool key_pressed = false;

	/* Barrido: Se baja a LOW una fila a la vez */
	for (int r = 0; r < ROWS; r++) {
		gpio_pin_set_dt(&row_pins[r], 0);

		/* Pequeña pausa para permitir el asentamiento del voltaje */
		k_busy_wait(50); 

		/* Se leen todas las columnas en la fila activa */
		for (int c = 0; c < COLS; c++) {
			if (gpio_pin_get_dt(&col_pins[c]) == 0) { /* 0 = tecla pulsada por pull-up */
				*out_key = key_map[r][c];
				key_pressed = true;
				break;
			}
		}

		/* Se restaura la fila a HIGH antes de probar la siguiente */
		gpio_pin_set_dt(&row_pins[r], 1);

		if (key_pressed) {
			break;
		}
	}

	return key_pressed;
}