/**
 * @file heater_simulation_task.c
 * @brief Hilo que simula y controla una fuente de calor externa (keep-alive).
 *
 * @details
 * Qué hace:
 * - Activa y desactiva un pin de GPIO para simular una resistencia de calentamiento.
 * - Solo emite calor cuando system_enabled está activo Y la autorización no fue
 * revocada externamente.
 *
 * Cómo lo hace:
 * - Mantiene el pin activo de forma FIJA mientras tenga permiso.
 * - El hilo consulta SystemState para verificar si el sistema está habilitado.
 * - Consulta también una bandera interna "authorized" que otro módulo
 * puede bajar para forzar el corte del keep-alive.
 *
 * Qué recibe / qué entrega:
 * - Recibe el estado de habilitación del sistema y la autorización externa.
 * - Entrega una señal de calor simulada al entorno físico vía GPIO.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include "heater_simulation_task.h"
#include "../state/system_state.h"

LOG_MODULE_REGISTER(heater_simulation_task, LOG_LEVEL_INF);

#define STACK_SIZE       1024
#define THREAD_PRIORITY  6       /* Baja prioridad */

static const struct gpio_dt_spec heater =
	GPIO_DT_SPEC_GET(DT_NODELABEL(heater_pin), gpios);

/* Volátil: se escribe desde el hilo de cooling_manager y se lee desde este hilo. */
static volatile bool authorized = true;

/**
 * @brief Modifica la bandera de autorización del keep-alive térmico.
 *
 * @param new_authorized Nuevo estado de autorización (true para conceder, false para revocar).
 */
void heater_simulation_set_authorized(bool new_authorized)
{
	if (new_authorized != authorized) {
		LOG_WRN("Autorizacion de keep-alive cambiada a: %s",
			new_authorized ? "AUTORIZADO" : "REVOCADO");
	}
	authorized = new_authorized;
}

/**
 * @brief Inicializa el puerto GPIO asociado al "heater" configurándolo como salida inactiva por defecto.
 */
void heater_simulation_task_init(void)
{
	if (!gpio_is_ready_dt(&heater)) {
		LOG_ERR("Pin de heater no listo — verificar overlay (PA4)");
		return;
	}
	gpio_pin_configure_dt(&heater, GPIO_OUTPUT_INACTIVE);
}

/**
 * @brief Hilo principal de la simulación del actuador de calor.
 *
 * Revisa el estado general y los permisos de autorización cíclicamente
 * fijando el nivel del GPIO acorde.
 *
 * @param p1 Parámetro no usado (requerido por Zephyr).
 * @param p2 Parámetro no usado.
 * @param p3 Parámetro no usado.
 */
static void heater_simulation_task_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("heater_simulation_task thread started");
	heater_simulation_task_init();

	while (1) {
		SystemState sys;
		system_state_get(&sys);

		/* may_run es true si no hay alarma permanente y si la temperatura 
		 * no ha revocado la autorización */
		bool may_run = sys.system_enabled && authorized;

		if (may_run) {
			gpio_pin_set_dt(&heater, 1);
		} else {
			gpio_pin_set_dt(&heater, 0);
		}
		
		/* Espera corta para liberar la CPU */
		k_sleep(K_MSEC(100)); 
	}
}

K_THREAD_DEFINE(heater_sim_tid, STACK_SIZE, heater_simulation_task_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);