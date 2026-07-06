/**
 * @file temperature_manager.c
 * @brief Hilo responsable de leer la temperatura del NTC y publicarla.
 *
 * Qué hace:
 * - Inicializa el ADC/NTC al arrancar.
 * - Lee la temperatura periódicamente desde el sensor.
 * - Filtra las muestras para reducir ruido.
 * - Publica la lectura en ControlState.current_temperature.
 *
 * Cómo lo hace:
 * - Usa ntc_sensor_init() y ntc_sensor_read_celsius().
 * - Aplica un promedio móvil simple sobre un buffer pequeño para suavizar la lectura.
 * - Cuando el sensor falla repetidamente, marca un error en TelemetryState.
 *
 * Qué recibe / qué entrega:
 * - Recibe información de hardware a través del ADC y del driver del NTC.
 * - Entrega una temperatura filtrada y válida en ControlState.
 * - Entrega señales de error a TelemetryState.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "temperature_manager.h"
#include "../drivers/ntc_sensor.h"
#include "../state/control_state.h"
#include "../state/telemetry_state.h"

LOG_MODULE_REGISTER(temperature_manager, LOG_LEVEL_INF);

#define STACK_SIZE 1024
#define THREAD_PRIORITY 2       /* Alta prioridad */

#define PERIOD_MS            500
#define FILTER_WINDOW        5
#define MAX_CONSECUTIVE_FAILURES 5
#define INIT_RETRY_MS        2000   /* tiempo entre reintentos de init del ADC */

static float filter_buffer[FILTER_WINDOW];
static uint8_t filter_index;
static uint8_t filter_filled_count;
static bool ntc_ready = false;

/**
 * @brief Reinicia a cero los valores y el estado del buffer de filtro móvil.
 *
 * Esta función es útil para limpiar el historial de lecturas, especialmente
 * tras una falla del sensor o durante la inicialización, evitando promediar
 * con datos antiguos e incorrectos.
 */
static void filter_reset(void)
{
	for (int i = 0; i < FILTER_WINDOW; i++) {
		filter_buffer[i] = 0.0f;
	}
	filter_index = 0;
	filter_filled_count = 0;
}

/**
 * @brief Inserta una nueva lectura y calcula el promedio móvil.
 *
 * Aplica un suavizado a las lecturas del sensor utilizando una ventana deslizante.
 * Retorna el promedio exacto de los datos almacenados, ajustando el cálculo
 * cuando la ventana aún no está completamente llena (en los primeros ciclos).
 *
 * @param new_sample Nueva medición de temperatura cruda del sensor en °C.
 * @return La temperatura promediada actual en °C.
 */
static float apply_moving_average(float new_sample)
{
	filter_buffer[filter_index] = new_sample;
	filter_index = (filter_index + 1) % FILTER_WINDOW;
	if (filter_filled_count < FILTER_WINDOW) {
		filter_filled_count++;
	}
	float sum = 0.0f;
	for (int i = 0; i < filter_filled_count; i++) {
		sum += filter_buffer[i];
	}
	return sum / (float)filter_filled_count;
}

/**
 * @brief Intenta inicializar el hardware del sensor NTC.
 *
 * Llamado al boot y periódicamente cada INIT_RETRY_MS si la inicialización falló.
 * Permite que el sistema se recupere si el ADC no estaba listo en el primer
 * intento (por ej. por orden de inicialización del kernel).
 *
 * @return true si el ADC quedó listo para leer, false de lo contrario.
 */
static bool try_init_ntc(void)
{
	LOG_INF("Intentando inicializar NTC/ADC...");
	bool ok = ntc_sensor_init();
	if (ok) {
		LOG_INF("NTC listo");
	} else {
		LOG_ERR("ntc_sensor_init fallo — se reintentara en %d ms", INIT_RETRY_MS);
	}
	return ok;
}

/**
 * @brief Inicializa las variables internas y el hardware del sensor NTC.
 */
void temperature_manager_init(void)
{
	filter_reset();
	ntc_ready = try_init_ntc();
}

/**
 * @brief Hilo principal de gestión de la lectura de temperatura.
 *
 * Tareas principales por ciclo:
 * 1. Verifica y reintenta la inicialización del NTC si este falló.
 * 2. Solicita la temperatura del hardware.
 * 3. Si la lectura es exitosa, reinicia el contador de errores, aplica el promedio
 * móvil y publica el dato en el estado global.
 * 4. Si la lectura falla, incrementa un contador y publica un error a la telemetría
 * cuando sobrepasa el límite de tolerancia (MAX_CONSECUTIVE_FAILURES).
 *
 * @param p1 Parámetro no usado (requerido por la firma de hilos de Zephyr).
 * @param p2 Parámetro no usado.
 * @param p3 Parámetro no usado.
 */
static void temperature_manager_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("temperature_manager thread started");
	temperature_manager_init();

	uint8_t consecutive_failures = 0;

	while (1) {
		/* Si el ADC nunca se inicializó, reintentar antes de leer */
		if (!ntc_ready) {
			ntc_ready = try_init_ntc();
			if (!ntc_ready) {
				telemetry_state_set_error_flag(ERROR_FLAG_NTC_SENSOR, true);
				k_sleep(K_MSEC(INIT_RETRY_MS));
				continue;
			}
			filter_reset();
			consecutive_failures = 0;
		}

		float raw_temperature;
		bool ok = ntc_sensor_read_celsius(&raw_temperature);

		if (ok) {
			consecutive_failures = 0;
			telemetry_state_set_error_flag(ERROR_FLAG_NTC_SENSOR, false);
			float filtered = apply_moving_average(raw_temperature);
			control_state_set_temperature(filtered);
			LOG_INF("NTC: raw=%.2f filtered=%.2f C",
				(double)raw_temperature, (double)filtered);
		} else {
			consecutive_failures++;
			LOG_WRN("Lectura NTC fallida (%u/%u consecutivas)",
				consecutive_failures, MAX_CONSECUTIVE_FAILURES);

			if (consecutive_failures >= MAX_CONSECUTIVE_FAILURES) {
				telemetry_state_set_error_flag(ERROR_FLAG_NTC_SENSOR, true);
				LOG_ERR("NTC en falla — failsafe activo en cooling_manager");
				/* Forzar re-init en el próximo ciclo para intentar recuperación */
				ntc_ready = false;
				consecutive_failures = 0;
			}
		}

		k_sleep(K_MSEC(PERIOD_MS));
	}
}

K_THREAD_DEFINE(temperature_manager_tid, STACK_SIZE, temperature_manager_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);