/*
 * power_status_manager.c — Pulsador físico (ISR + debounce), gestión de SHUTDOWN
 *
 * Pin: PC13 (botón azul de usuario de la Nucleo-L476RG).
 * Si tu diseño usa otro pin, cambia la declaración en el overlay y el
 * alias "sw0" o el nodo gpio_keys en el overlay.
 *
 * Comportamiento:
 *  - Detecta flanco de bajada (botón presionado) vía ISR de GPIO.
 *  - Aplica debounce por software: ignora flancos adicionales durante
 *    DEBOUNCE_MS tras el primero.
 *  - Pulsación corta (< LONG_PRESS_MS): toggle system_enabled.
 *  - Pulsación larga (>= LONG_PRESS_MS): solicita SHUTDOWN ordenado.
 *
 * Comunicación con el resto del sistema: solo escribe SystemState.
 * Otros módulos leen SystemState para reaccionar al shutdown.
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree/gpio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>

#include "power_status_manager.h"
#include "../state/system_state.h"
#include "../state/telemetry_state.h"

LOG_MODULE_REGISTER(power_status_manager, LOG_LEVEL_INF);

#define STACK_SIZE      1024
#define THREAD_PRIORITY 2       /* Alta prioridad — ver docs/02-firmware-architecture.md */

#define DEBOUNCE_MS    50
#define LONG_PRESS_MS 3000

extern void ui_request_config_mode(void);

/* El botón del usuario se obtiene desde DeviceTree usando el alias sw0.
 * Esto centraliza la asignación de pines en el soporte de la placa y evita
 * duplicar la dirección física del botón en el código. */
static const struct gpio_dt_spec user_button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb;

/* Semáforo que la ISR señala cuando detecta un flanco válido */
static K_SEM_DEFINE(button_sem, 0, 1);

/* Timestamp del flanco de bajada, para medir duración de pulsación */
static int64_t press_timestamp_ms;

static void button_isr(const struct device *dev, struct gpio_callback *cb,
		       uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	/* Guardar timestamp y señalar al hilo — no hacer lógica pesada en ISR */
	press_timestamp_ms = k_uptime_get();
	k_sem_give(&button_sem);
}

bool power_status_manager_init(void)
{
	if (!gpio_is_ready_dt(&user_button)) {
		LOG_ERR("GPIO del pulsador no listo — verifica el alias sw0 en DeviceTree");
		return false;
	}

	// CAMBIO 1: Configurar con PULL_UP interno explícito
	int err = gpio_pin_configure_dt(&user_button, GPIO_INPUT | GPIO_PULL_UP);
	if (err < 0) {
		LOG_ERR("Error al configurar el pin del boton: %d", err);
		return false;
	}

	// CAMBIO 2: Cambiar a EDGE_TO_ACTIVE (detecta la caída a GND basándose en la configuración lógica del DTS)
	err = gpio_pin_interrupt_configure_dt(&user_button, GPIO_INT_EDGE_TO_ACTIVE);
	if (err < 0) {
		LOG_ERR("Error al configurar la interrupcion del boton: %d", err);
		return false;
	}

	gpio_init_callback(&button_cb, button_isr, BIT(user_button.pin));
	gpio_add_callback(user_button.port, &button_cb);

	LOG_INF("Pulsador listo en %s pin %u (Pull-Up / Activo en Bajo)", user_button.port->name, user_button.pin);
	return true;
}

static void power_status_manager_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    LOG_INF("power_status_manager thread started");

    bool ok = power_status_manager_init();
    if (!ok) {
        LOG_ERR("power_status_manager: init fallo, hilo desactivado");
        return;
    }

    while (1) {
        k_sem_take(&button_sem, K_FOREVER);
        k_msleep(DEBOUNCE_MS); /* Ignorar rebote inicial */

        /* gpio_pin_get_dt devuelve 1 si el botón está en su estado activo (presionado) */
        if (gpio_pin_get_dt(&user_button) == 1) {
            int held_time = DEBOUNCE_MS;

            /* Bucle que cuenta el tiempo mientras el botón SIGA presionado.
             * Detenemos el conteo máximo en 5000ms para no bloquear el hilo de más. */
            while (gpio_pin_get_dt(&user_button) == 1 && held_time < 5000) {
                k_msleep(50);
                held_time += 50;
            }

            /* --- EVALUACIÓN DE LAS VENTANAS DE TIEMPO --- */
            
            if (held_time >= 5000) {
                // Mayor a 5s: APAGADO
                LOG_WRN("Pulsacion larga detectada (%d ms) — solicitando SHUTDOWN", held_time);
                system_state_request_shutdown();
                telemetry_state_increment_boot_count();
                k_msleep(100);  // Dar tiempo al log UART
                sys_poweroff(); // Apagar hardware
            } 
            else if (held_time >= 1000 && held_time <= 2000) {
                // Entre 1s y 2s: MODO CONFIGURACIÓN
                LOG_INF("Pulsacion media (%d ms) — Iniciando config UI", held_time);
                ui_request_config_mode();
            } 
            else if (held_time <= 500) {
                // Menor o igual a 500ms: APAGAR/ENCENDER PROCESO TÉRMICO
                SystemState sys;
                system_state_get(&sys);
                bool new_state = !sys.system_enabled;
                system_state_set_enabled(new_state);
                LOG_INF("Pulsacion corta (%d ms) — system_enabled=%d", held_time, new_state);
            } 
            else {
                // Tiempos intermedios (501ms-999ms y 2001ms-4999ms): IGNORAR
                LOG_INF("Pulsacion ignorada por estar en un rango intermedio (%d ms)", held_time);
            }
        }
        
        /* Limpiar cualquier otra interrupción que haya ocurrido mientras mediamos */
        k_sem_reset(&button_sem);
    }
}

K_THREAD_DEFINE(power_status_manager_tid, STACK_SIZE, power_status_manager_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
