/**
 * @file ntc_sensor.c
 * @brief Implementación para medir la temperatura con un sensor NTC de 10K.
 *
 * @details
 * Conversión usada: ecuación Beta (suficiente para el rango de operación):
 * 1/T = 1/T0 + (1/B) * ln(R/R0)
 *
 * Con T0 = 298.15 K (25°C), R0 = 10000 Ω, B = 3470 K.
 *
 * TOPOLOGÍA ASUMIDA:
 * Divisor con NTC entre VDD y el nodo ADC, resistencia fija entre ADC y GND:
 * VDD(3.3V) ---[NTC]---o(PA0/ADC)---[R_fija 10k]--- GND
 *
 * Formula:  R_ntc = R_fija * (VDD / V_adc - 1)
 */

#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include "ntc_sensor.h"

LOG_MODULE_REGISTER(ntc_sensor, LOG_LEVEL_WRN);

/* Nodos y parámetros de hardware desde DeviceTree */
#define ADC_NODE      DT_NODELABEL(adc1)
static const struct device *adc_dev = DEVICE_DT_GET(ADC_NODE);

#define ADC_CHANNEL_ID 5
#define ADC_RESOLUTION 12

/* Constantes para Steinhart-Hart / Modelo Beta */
#define NTC_R_FIXED_OHMS 10000.0f
#define NTC_R0_OHMS      10000.0f
#define NTC_BETA_K       3470.0f
#define T0_KELVIN        298.15f
#define VDD_VOLTS        3.3f

static struct adc_channel_cfg channel_cfg = {
	.gain             = ADC_GAIN_1,
	.reference        = ADC_REF_INTERNAL,
	.acquisition_time = ADC_ACQ_TIME_DEFAULT,
	.channel_id       = ADC_CHANNEL_ID,
	.differential     = 0,
};

static int16_t sample_buffer;

bool ntc_sensor_init(void)
{
	if (!device_is_ready(adc_dev)) {
		LOG_ERR("ADC no esta listo en ntc_sensor_init");
		return false;
	}

	int err = adc_channel_setup(adc_dev, &channel_cfg);
	if (err != 0) {
		LOG_ERR("Error al configurar canal ADC: %d", err);
		return false;
	}
	return true;
}

bool ntc_sensor_read_celsius(float *out_temperature)
{
	struct adc_sequence sequence = {
		.channels    = BIT(ADC_CHANNEL_ID),
		.buffer      = &sample_buffer,
		.buffer_size = sizeof(sample_buffer),
		.resolution  = ADC_RESOLUTION,
	};

	int err = adc_read(adc_dev, &sequence);
	if (err != 0) {
		LOG_ERR("adc_read fallo: %d", err);
		return false;
	}

	/*
	 * Para simplificar y dado que dependemos del divisor usando 3.3V, 
	 * evitamos compensaciones complejas de Zephyr.
	 * v_adc = (raw / 4095.0) * VDD
	 */
	float v_adc = ((float)sample_buffer / 4095.0f) * VDD_VOLTS;

	/* Evitar división por cero si el ADC satura, indica un circuito abierto o corto */
	if (v_adc <= 0.01f || v_adc >= (VDD_VOLTS - 0.01f)) {
		LOG_WRN("Lectura ADC fuera de rango fisico (%.3f V) — posible falla NTC", (double)v_adc);
		return false;
	}

	/* TOPOLOGÍA A (NTC arriba, hacia VDD) */
	float r_ntc = NTC_R_FIXED_OHMS * (VDD_VOLTS / v_adc - 1.0f);

	if (r_ntc <= 0.0f) {
		return false;
	}

	/* Ecuación Beta (Kelvin) */
	float ln_r = logf(r_ntc / NTC_R0_OHMS);
	float temp_k = 1.0f / ((1.0f / T0_KELVIN) + (1.0f / NTC_BETA_K) * ln_r);

	/* Pasar a Celsius */
	*out_temperature = temp_k - 273.15f;

	return true;
}