/*
 * shift_register.c — Implementación del driver del SN74HC595N.
 *
 * Secuencia por cada shift_register_write() (ver datasheet SN74HC595N):
 *  1. Con LATCH (RCLK) en bajo, se saca el byte completo por MOSI vía SPI —
 *     el propio periférico SPI genera los 8 pulsos de SCK necesarios.
 *  2. Se pulsa LATCH de bajo a alto: ese flanco de subida es lo que copia el
 *     contenido del registro de desplazamiento interno hacia el registro de
 *     salida (Q0-Q7), haciendo visible el nuevo valor en los LEDs de golpe,
 *     sin parpadeos intermedios mientras se desplazan los bits.
 *  3. Se vuelve a poner LATCH en bajo, listo para la siguiente escritura.
 *
 * El LATCH se maneja como GPIO manual (no como NSS de SPI) porque su
 * temporización exacta (flanco de subida DESPUÉS de que terminó toda la
 * trama) es más simple de garantizar así que confiando en el comportamiento
 * automático de NSS del periférico SPI del STM32.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "shift_register.h"

LOG_MODULE_REGISTER(shift_register, LOG_LEVEL_INF);

static const struct device *spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi1));
static const struct gpio_dt_spec latch =
	GPIO_DT_SPEC_GET(DT_NODELABEL(shiftreg_latch), gpios);

/* Configuración SPI: el SN74HC595N acepta hasta ~25-30 MHz según variante,
 * muy por encima de lo que necesitamos para refrescar LEDs (se actualizan
 * como mucho un par de veces por segundo) — 1 MHz da margen de sobra sin
 * preocuparse por integridad de señal en cables de protoboard largos. Modo
 * SPI 0 (CPOL=0, CPHA=0): el SN74HC595 mete el bit en SER con SRCLK en bajo
 * y lo captura en el flanco de subida de SRCLK, que es exactamente el modo 0. */
static const struct spi_config spi_cfg = {
	.frequency = 1000000,
	.operation = SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER,
	.slave = 0,
};

static bool ready = false;

bool shift_register_init(void)
{
	if (!device_is_ready(spi_dev)) {
		LOG_ERR("SPI1 no listo — verificar overlay (PA5=SCK, PA7=MOSI)");
		return false;
	}
	if (!gpio_is_ready_dt(&latch)) {
		LOG_ERR("Pin de LATCH no listo — verificar overlay (PA6)");
		return false;
	}

	gpio_pin_configure_dt(&latch, GPIO_OUTPUT_INACTIVE);
	ready = true;
	LOG_INF("Registro de desplazamiento listo (SPI1 + LATCH en PA6)");
	return true;
}

void shift_register_write(uint8_t value)
{
	if (!ready) {
		return;
	}

	struct spi_buf tx_buf = {
		.buf = &value,
		.len = 1,
	};
	struct spi_buf_set tx_bufs = {
		.buffers = &tx_buf,
		.count = 1,
	};

	int err = spi_write(spi_dev, &spi_cfg, &tx_bufs);
	if (err != 0) {
		LOG_ERR("spi_write fallo: %d", err);
		return;
	}

	/* Pulso de LATCH: flanco de subida copia el dato hacia las salidas. */
	gpio_pin_set_dt(&latch, 1);
	/* El SN74HC595N solo necesita unos pocos ns de ancho de pulso; unos
	 * cuantos microsegundos son innecesariamente generosos pero garantizan
	 * margen frente a cualquier jitter del kernel sin costar nada perceptible. */
	k_busy_wait(2);
	gpio_pin_set_dt(&latch, 0);
}
