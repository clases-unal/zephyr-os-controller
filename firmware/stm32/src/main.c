/*
 * main.c — Punto de entrada del firmware y orquestador del arranque.
 *
 * Este archivo no implementa lógica de control por sí solo. Su responsabilidad es
 * preparar el entorno compartido del sistema antes de que otros hilos comiencen a
 * trabajar. En otras palabras, main() crea las condiciones iniciales para que el
 * resto del firmware pueda operar de forma coordinada.
 *
 * Flujo de arranque:
 * 1. Se inicializan las estructuras de estado global (ControlState, ConfigState,
 *    TelemetryState, SystemState y TransmissionState). Estas estructuras son el
 *    "estado común" del sistema y se usan por múltiples hilos.
 * 2. Se incrementa el contador de arranques, útil para depuración y registro.
 * 3. El programa queda en un bucle de mantenimiento muy simple. El trabajo real
 *    se ejecuta en hilos creados con K_THREAD_DEFINE() desde los archivos de
 *    tasks/; main() no necesita hacer un while(1) con lógica de negocio.
 *
 * Relación entre main() y los hilos:
 * - Cada hilo es un proceso autónomo que se encarga de una parte del sistema.
 * - temperature_manager lee el sensor y publica temperatura.
 * - cooling_manager decide el umbral y controla el ventilador.
 * - heater_simulation_task simula carga térmica.
 * - led_representation_manager refleja el estado en LEDs.
 * - power_status_manager escucha el botón físico.
 * - ui_keypad_task gestiona la pantalla/teclado.
 * - esp32_comm_manager comunica con el ESP32 por UART.
 *
 * Por qué este archivo es tan corto:
 * - El diseño del proyecto está orientado a concurrencia: cada tarea tiene su
 *   propio hilo y su propio ciclo de trabajo.
 * - main() solo debe dejar el sistema en un estado consistente antes de que los
 *   hilos comiencen a correr.
 *
 * Importante:
 * - Si se agregan nuevos hilos o se cambia el orden de inicialización, conviene
 *   revisar que las estructuras de estado estén preparadas antes de que cualquier
 *   hilo intente leer o escribir en ellas.
 *
 * TODO: evaluar si conviene mover la creación de hilos a un punto explícito desde
 * main() para eliminar la condición de carrera teórica con K_THREAD_DEFINE().
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "state/control_state.h"
#include "state/transmission_state.h"
#include "state/config_state.h"
#include "state/telemetry_state.h"
#include "state/system_state.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("Sistema de Control Termico de Alta Disponibilidad — boot");

	control_state_init();
	transmission_state_init();
	config_state_init();
	telemetry_state_init();
	system_state_init();

	telemetry_state_increment_boot_count();

	LOG_INF("Estado global inicializado. Hilos de tasks/ deberian estar activos.");

	/* main() no implementa control ni supervisión de negocio; su única tarea en
	 * este punto es mantener el sistema vivo mientras los hilos hacen el trabajo
	 * real. */
	while (1) {
		k_sleep(K_MSEC(1000));
	}
}
