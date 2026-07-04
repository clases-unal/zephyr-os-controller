/*
 * power_status_manager.c — Pulsador físico (ISR + debounce), gestión de SHUTDOWN
 *
 * Pin: PC13 (botón azul de usuario B1 de la Nucleo-L476RG).
 * Si tu diseño usa otro pin, cambia la declaración en el overlay y el
 * alias "sw0" o el nodo gpio_keys en el overlay.
 *
 * Comportamiento:
 *  - Detecta flanco de bajada (botón presionado) vía ISR de GPIO.
 *  - Aplica debounce por software: ignora flancos adicionales durante
 *    DEBOUNCE_MS tras el primero.
 *  - Pulsación corta (<= 500ms): toggle system_enabled.
 *  - Pulsación media (1000-2000ms): solicita modo configuración (OLED/teclado).
 *  - Pulsación larga (>= 5000ms): solicita SHUTDOWN ordenado y apaga.
 *  - Cualquier otra duración: se ignora (zonas muertas entre umbrales).
 *
 * Comunicación con el resto del sistema: solo escribe SystemState (y, para
 * el modo configuración, llama a la función pública de ui_keypad_task).
 * Otros módulos leen SystemState para reaccionar al shutdown.
 *
 * BUG DE ARRANQUE CORREGIDO EN ESTA SESIÓN — "la OLED abre en modo edición
 * sin que nadie toque el teclado ni el botón":
 * PC13 en el STM32L4 vive en el dominio de respaldo (compartido con la
 * funcionalidad de tamper/wakeup del RTC), que se estabiliza más lento que
 * el resto de los GPIO normales tras la energización — es un comportamiento
 * documentado del fabricante, no un defecto de esta placa en particular.
 * Durante ese breve período la línea puede leerse como "presionada" (activa)
 * sin que haya dedo alguno sobre el botón, típicamente por un lapso de hasta
 * uno o dos segundos. Como el sistema aquí ya distingue rangos de duración,
 * ese pulso fantasma caía justo dentro de la ventana de 1000-2000ms —
 * exactamente la que dispara el modo configuración — y por eso el síntoma
 * era tan específico y repetible.
 * La corrección NO cambia el pin ni su configuración eléctrica (sigue siendo
 * PC13, pull-up interno, EDGE_TO_ACTIVE): simplemente el hilo ignora
 * cualquier pulsación detectada durante los primeros BOOT_SETTLE_MS después
 * de que el pulsador queda listo, tiempo más que suficiente para que la
 * línea se estabilice y muy por debajo de lo que tardaría una persona en
 * presionar el botón a propósito recién arrancado el sistema.
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree/gpio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/poweroff.h>

#include "power_status_manager.h"
#include "ui_keypad_task.h"
#include "../state/system_state.h"
#include "../state/telemetry_state.h"

LOG_MODULE_REGISTER(power_status_manager, LOG_LEVEL_INF);

/* ── Configuración del hilo y temporización ──────────────────────────────── */
#define STACK_SIZE      1024
#define THREAD_PRIORITY 2       /* Alta prioridad — ver docs/02-firmware-architecture.md */

#define DEBOUNCE_MS       50
#define SHORT_PRESS_MAX_MS  500   /* <= esto: toggle system_enabled */
#define MEDIUM_PRESS_MIN_MS 1000  /* Ventana [MIN, MAX]: modo configuración */
#define MEDIUM_PRESS_MAX_MS 2000
#define LONG_PRESS_MS     5000    /* >= esto: shutdown ordenado */

/* Ventana de gracia tras el arranque durante la cual se ignora cualquier
 * pulsación detectada (ver nota de cabecera sobre el comportamiento de PC13
 * en el dominio de respaldo del STM32L4). 2s da margen amplio frente al
 * peor caso de asentamiento de la línea sin ser perceptible para el usuario
 * real, que no va a presionar el botón en el primer instante tras energizar
 * la placa. */
#define BOOT_SETTLE_MS    2000

/* ── Estado del módulo (file-scope) ──────────────────────────────────────── */
static const struct gpio_dt_spec user_button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb;

/* Semáforo que la ISR señala cuando detecta un flanco válido */
static K_SEM_DEFINE(button_sem, 0, 1);

/* Timestamp del flanco de bajada. Guardado por la ISR pero actualmente sin
 * lector (la medición real de duración se hace por polling en el hilo, ver
 * el bucle held_time más abajo) — queda como uno de los pocos símbolos
 * "definidos pero no usados" del proyecto; se deja intacto en vez de
 * eliminarlo porque no afecta el comportamiento y podría reutilizarse si
 * más adelante se prefiere medir la duración por timestamp en vez de por
 * polling. */
static int64_t press_timestamp_ms;

/* ── ISR ──────────────────────────────────────────────────────────────────── */
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

/* ── Inicialización ───────────────────────────────────────────────────────── */
bool power_status_manager_init(void)
{
	if (!gpio_is_ready_dt(&user_button)) {
		LOG_ERR("GPIO del pulsador no listo — verifica el alias sw0 en DeviceTree");
		return false;
	}

	int err = gpio_pin_configure_dt(&user_button, GPIO_INPUT | GPIO_PULL_UP);
	if (err < 0) {
		LOG_ERR("Error al configurar el pin del boton: %d", err);
		return false;
	}

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

/* ── Hilo principal ───────────────────────────────────────────────────────── */
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

	/* Marca de tiempo de referencia para la ventana de gracia de arranque
	 * (ver nota de cabecera). Se toma justo después de que el pulsador
	 * queda configurado y listo para generar interrupciones. */
	int64_t boot_ready_ms = k_uptime_get();

	while (1) {
		k_sem_take(&button_sem, K_FOREVER);
		k_msleep(DEBOUNCE_MS); /* Ignorar rebote inicial */

		/* Descartar cualquier pulsación (real o fantasma) detectada
		 * dentro de la ventana de gracia post-arranque. */
		if ((k_uptime_get() - boot_ready_ms) < BOOT_SETTLE_MS) {
			LOG_INF("Pulsacion ignorada: dentro de la ventana de asentamiento post-arranque");
			k_sem_reset(&button_sem);
			continue;
		}

		/* gpio_pin_get_dt devuelve 1 si el botón está en su estado activo (presionado) */
		if (gpio_pin_get_dt(&user_button) == 1) {
			int held_time = DEBOUNCE_MS;

			/* Bucle que cuenta el tiempo mientras el botón SIGA presionado.
			 * Detenemos el conteo máximo en LONG_PRESS_MS para no bloquear
			 * el hilo de más. */
			while (gpio_pin_get_dt(&user_button) == 1 && held_time < LONG_PRESS_MS) {
				k_msleep(50);
				held_time += 50;
			}

			/* --- Evaluación de las ventanas de tiempo --- */
			if (held_time >= LONG_PRESS_MS) {
				LOG_WRN("Pulsacion larga detectada (%d ms) — solicitando SHUTDOWN", held_time);
				system_state_request_shutdown();
				telemetry_state_increment_boot_count();
				k_msleep(2000);  /* Dar tiempo al log UART */
				sys_poweroff();  /* Apagar hardware */
			} else if (held_time >= MEDIUM_PRESS_MIN_MS && held_time <= MEDIUM_PRESS_MAX_MS) {
				LOG_INF("Pulsacion media (%d ms) — Iniciando config UI", held_time);
				ui_request_config_mode();
			} else if (held_time <= SHORT_PRESS_MAX_MS) {
				SystemState sys;
				system_state_get(&sys);
				bool new_state = !sys.system_enabled;
				system_state_set_enabled(new_state);
				LOG_INF("Pulsacion corta (%d ms) — system_enabled=%d", held_time, new_state);
			} else {
				LOG_INF("Pulsacion ignorada por estar en un rango intermedio (%d ms)", held_time);
			}
		}

		/* Limpiar cualquier otra interrupción que haya ocurrido mientras mediamos */
		k_sem_reset(&button_sem);
	}
}

K_THREAD_DEFINE(power_status_manager_tid, STACK_SIZE, power_status_manager_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
