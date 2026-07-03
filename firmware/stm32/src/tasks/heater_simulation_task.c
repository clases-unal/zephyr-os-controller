/*
 * heater_simulation_task.c — Hilo que simula una fuente de calor externa.
 *
 * Qué hace:
 * - Activa y desactiva un pin de GPIO para simular una
 * resistencia de calentamiento.
 * - Solo emite calor cuando system_enabled está activo Y la autorización no fue
 * revocada externamente (ver heater_simulation_set_authorized()).
 *
 * Cómo lo hace:
 * - Mantiene el pin activo de forma FIJA mientras tenga permiso.
 * - El hilo consulta SystemState para verificar si el sistema está habilitado.
 * - Consulta también una bandera interna "authorized" que otro módulo
 * (cooling_manager, ante CRITICAL por sobretemperatura sostenida) puede
 * bajar para forzar el corte del keep-alive sin tener acceso directo al GPIO.
 * - Si cualquiera de las dos condiciones falla, mantiene el GPIO en estado inactivo.
 *
 * Qué recibe / qué entrega:
 * - Recibe el estado de habilitación del sistema desde SystemState y la
 * autorización externa desde heater_simulation_set_authorized().
 * - Entrega una señal de calor simulada al entorno físico (GPIO) y, vía la
 * temperatura del NTC, afecta al comportamiento del ventilador.
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

/* Volátil: se escribe desde el hilo de cooling_manager y se lee desde este
 * hilo. No necesita mutex propio porque es un bool de una sola escritura por
 * evento (revocar/restaurar), no una estructura compuesta. */
static volatile bool authorized = true;

void heater_simulation_set_authorized(bool new_authorized)
{
	if (new_authorized != authorized) {
		LOG_WRN("Autorizacion de keep-alive cambiada a: %s",
			new_authorized ? "AUTORIZADO" : "REVOCADO");
	}
	authorized = new_authorized;
}

void heater_simulation_task_init(void)
{
	if (!gpio_is_ready_dt(&heater)) {
		LOG_ERR("Pin de heater no listo — verificar overlay (PA4)");
		return;
	}
	gpio_pin_configure_dt(&heater, GPIO_OUTPUT_INACTIVE);
}

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
		
		/* Espera corta para liberar la CPU, ya no controla un pulso
		 * sino la frecuencia de actualización del pin */
		k_sleep(K_MSEC(100)); 
	}
}

K_THREAD_DEFINE(heater_sim_tid, STACK_SIZE, heater_simulation_task_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);