/*
 * ntc_sensor.c
 *
 * Conversión usada: ecuación Beta (más simple que Steinhart-Hart de 3 constantes;
 * suficiente para el rango de operación de este proyecto, ver discussion.md).
 *   1/T = 1/T0 + (1/B) * ln(R/R0)
 * T0 = 298.15 K (25°C), R0 = 10000 Ω, B = 3470 K
 *
 * SUPUESTO A VERIFICAR FÍSICAMENTE (ver overlay): divisor con NTC entre VDD y el
 * nodo ADC, resistencia fija entre el nodo ADC y GND:
 *
 *     VDD(3.3V) ---[NTC]---o(PA0/ADC)---[R_fija 10k]--- GND
 *
 * Con esa topología: V_adc sube cuando el NTC se calienta (su resistencia baja).
 * Formula:  R_ntc = R_fija * (VDD / V_adc - 1)
 *
 * Si tu cableado real tiene el NTC abajo (junto a GND) y la resistencia fija
 * arriba (junto a VDD), la fórmula correcta es la inversa:
 *     R_ntc = R_fija * (V_adc / (VDD - V_adc))
 * En ese caso, comenta la línea marcada "TOPOLOGÍA A" y descomenta "TOPOLOGÍA B"
 * más abajo.
 */

#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include "ntc_sensor.h"

LOG_MODULE_REGISTER(ntc_sensor, LOG_LEVEL_INF);

/* Referencia al canal ADC declarado en el devicetree overlay (nodo zephyr,user) */
static const struct adc_dt_spec adc_channel =
	ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

static int16_t sample_buffer;
static struct adc_sequence sequence = {
	.buffer = &sample_buffer,
	.buffer_size = sizeof(sample_buffer),
};

#define NTC_R0_OHMS     10000.0f
#define NTC_R_FIXED_OHMS 10000.0f
#define NTC_B_COEFF     3470.0f
#define NTC_T0_KELVIN   298.15f
#define VDD_VOLTS       3.3f

/* Rango físico válido — fuera de esto se asume sensor en falla (corto/abierto).
 * TODO: ajustar estos límites tras observar lecturas reales con el sensor sano. */
#define VALID_TEMP_MIN_C  -10.0f
#define VALID_TEMP_MAX_C  120.0f

bool ntc_sensor_init(void)
{
	LOG_INF("ntc_sensor_init: device ptr=%p, ready=%d",
		(void *)adc_channel.dev,
		adc_is_ready_dt(&adc_channel));

	if (!adc_is_ready_dt(&adc_channel)) {
		/* Se reemplazó el uso de LOG_PANIC por LOG_ERR para alinearlo con la API
		 * de logging disponible en esta versión de Zephyr y evitar el fallo de
		 * compilación. */
		LOG_ERR("ADC device NO LISTO. Verificar overlay y CONFIG_ADC=y en prj.conf");
		k_msleep(100);
		return false;
	}

	int err = adc_channel_setup_dt(&adc_channel);
	if (err != 0) {
		LOG_ERR("adc_channel_setup_dt FALLO: err=%d", err);
		k_msleep(100);
		return false;
	}

	err = adc_sequence_init_dt(&adc_channel, &sequence);
	if (err != 0) {
		LOG_ERR("adc_sequence_init_dt FALLO: err=%d", err);
		k_msleep(100);
		return false;
	}

	LOG_INF("ADC OK: channel_id=%d resolution=%d sequence.channels=0x%08x",
		adc_channel.channel_id, adc_channel.resolution, sequence.channels);

	return true;
}

bool ntc_sensor_read_celsius(float *out_temperature)
{
	if (sequence.channels == 0) {
		LOG_ERR("ntc_sensor_read_celsius llamado sin ntc_sensor_init() exitoso previo");
		return false;
	}

	int err = adc_read_dt(&adc_channel, &sequence);
	if (err != 0) {
		LOG_ERR("adc_read_dt failed: %d", err);
		return false;
	}

	/*
	 * Conversión manual raw → voltaje.
	 * El driver STM32 de Zephyr requiere ADC_REF_INTERNAL en el overlay, pero
	 * nuestro divisor NTC está alimentado por VDD (3.3V). Como conocemos VDD y
	 * la resolución (12 bits → 0-4095), convertimos directamente:
	 *   v_adc = (raw / 4095.0) * VDD
	 * Esto es correcto para un divisor resistivo referenciado a VDD — no usamos
	 * adc_raw_to_millivolts_dt porque asumiría la referencia interna (~1.21V).
	 */
	float v_adc = ((float)sample_buffer / 4095.0f) * VDD_VOLTS;

	/* Evitar división por cero / valores absurdos si el ADC satura */
	if (v_adc <= 0.01f || v_adc >= (VDD_VOLTS - 0.01f)) {
		LOG_WRN("Lectura ADC fuera de rango fisico (%.3f V) — posible falla NTC",
			(double)v_adc);
		return false;
	}

	/* TOPOLOGÍA A (NTC arriba, junto a VDD) — supuesto por defecto */
	float r_ntc = NTC_R_FIXED_OHMS * (VDD_VOLTS / v_adc - 1.0f);

	/* TOPOLOGÍA B (NTC abajo, junto a GND) — descomentar si tu cableado es así
	 * y comentar la línea de arriba:
	 * float r_ntc = NTC_R_FIXED_OHMS * (v_adc / (VDD_VOLTS - v_adc));
	 */

	if (r_ntc <= 0.0f) {
		LOG_WRN("Resistencia NTC calculada invalida (%.1f ohm)", (double)r_ntc);
		return false;
	}

	/* Ecuación Beta */
	float inv_t = (1.0f / NTC_T0_KELVIN) +
		      (1.0f / NTC_B_COEFF) * logf(r_ntc / NTC_R0_OHMS);
	float temperature_kelvin = 1.0f / inv_t;
	float temperature_celsius = temperature_kelvin - 273.15f;

	if (temperature_celsius < VALID_TEMP_MIN_C || temperature_celsius > VALID_TEMP_MAX_C) {
		LOG_WRN("Temperatura fuera de rango valido: %.1f C", (double)temperature_celsius);
		return false;
	}

	*out_temperature = temperature_celsius;
	return true;
}
