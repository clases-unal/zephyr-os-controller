/*
 * ui_keypad_task.c — Pantalla OLED (SSD1306) + teclado matricial 4×4
 *
 * Responsabilidades:
 *  - Mostrar en el OLED: temperatura actual, nivel térmico activo (incluyendo
 *    la causa específica cuando está en CRITICAL), duty cycle del ventilador,
 *    y estado del sistema (ON / ALARMA PERMANENTE / mensaje de falla de módulo).
 *  - Leer el teclado cada 20ms y procesar pulsaciones para editar los 4
 *    umbrales de temperatura en ConfigState (bajo, medio, alto, crítico).
 *  - Timeout de 30s: si no hay actividad de teclado en modo edición, la
 *    pantalla vuelve a la vista principal (modo monitoreo).
 *
 * Display: SSD1306 128×64 vía I2C3 (PC0=SCL, PC1=SDA), usando la API CFB de
 * Zephyr. Si el display no está conectado, el hilo continúa sin él (modo
 * degradado) — la falla se reporta también en ERROR_FLAG_OLED_I2C para que
 * el LED Qb del registro de desplazamiento la refleje.
 *
 * CORRECCIÓN respecto a la versión anterior: el framebuffer nunca se
 * inicializaba de verdad — `display_ok` quedaba hardcodeado en `false` para
 * evitar un problema de heap no configurado (ver prj.conf,
 * CONFIG_HEAP_MEM_POOL_SIZE). Esa era la causa raíz de que la OLED pareciera
 * "no encender" incluso con el cableado correcto: nunca se le pedía nada.
 */

#include <zephyr/kernel.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <stdio.h>

#include "ui_keypad_task.h"
#include "../drivers/matrix_keypad.h"
#include "../state/control_state.h"
#include "../state/config_state.h"
#include "../state/system_state.h"
#include "../state/telemetry_state.h"

LOG_MODULE_REGISTER(ui_keypad_task, LOG_LEVEL_INF);

#define STACK_SIZE        2048
#define THREAD_PRIORITY   4       /* Media prioridad */
#define SCAN_PERIOD_MS    20      /* Escaneo de teclado a 50Hz */
#define TIMEOUT_TICKS     (30000 / SCAN_PERIOD_MS)  /* 30s en ticks */

static const struct device *display_dev = DEVICE_DT_GET(DT_NODELABEL(ssd1306));
static bool display_ok  = false;
static bool keypad_ok   = false;

/* Bandera para solicitar el cambio a modo edición desde otro hilo */
volatile bool flag_request_config = false;

void ui_request_config_mode(void)
{
	flag_request_config = true;
}

/* ── Modos de la UI ──────────────────────────────────────────────────────── */
typedef enum {
	UI_MODE_MONITOR,        /* Vista principal: temperatura, umbral, duty */
	UI_MODE_EDIT_LOW,
	UI_MODE_EDIT_MEDIUM,
	UI_MODE_EDIT_HIGH,
	UI_MODE_EDIT_CRITICAL,
} ui_mode_t;

/* ── Inicialización real del display ─────────────────────────────────────── */
static bool display_init(void)
{
	if (!device_is_ready(display_dev)) {
		LOG_ERR("SSD1306 no listo — verificar overlay (I2C3: PC0=SCL, PC1=SDA)");
		return false;
	}

	/* display_blanking_off(): el controlador SSD1306 arranca con la salida
	 * en blanco (blanking) por diseño, para que el framebuffer se pueda
	 * preparar antes de mostrar nada en pantalla. Sin este paso, aunque el
	 * framebuffer se dibuje correctamente, físicamente nunca se ve nada. */
	if (display_blanking_off(display_dev) != 0) {
		LOG_ERR("display_blanking_off fallo");
		return false;
	}

	/* cfb_framebuffer_init() reserva el buffer de pantalla en heap — por eso
	 * prj.conf necesita CONFIG_HEAP_MEM_POOL_SIZE distinto de cero. Si esto
	 * falla, casi siempre es por heap insuficiente, no por el bus I2C. */
	if (cfb_framebuffer_init(display_dev) != 0) {
		LOG_ERR("cfb_framebuffer_init fallo — revisar CONFIG_HEAP_MEM_POOL_SIZE en prj.conf");
		return false;
	}

	/* --- AÑADIR ESTAS TRES LÍNEAS --- */
	if (cfb_framebuffer_set_font(display_dev, 0) != 0) {
		LOG_ERR("No se pudo cargar la fuente del OLED");
		return false;
	}
	/* -------------------------------- */

	cfb_framebuffer_clear(display_dev, true);

	cfb_framebuffer_clear(display_dev, true);
	cfb_framebuffer_invert(display_dev);
	LOG_INF("Display OLED listo");
	return true;
}

/* ── Helpers de display ──────────────────────────────────────────────────── */
static void ui_display_clear(void)
{
	if (!display_ok) return;
	cfb_framebuffer_clear(display_dev, false);
}

static void display_print(const char *str, int col, int row)
{
	if (!display_ok) return;
	cfb_print(display_dev, str, col, row);
}

static void display_flush(void)
{
	if (!display_ok) return;
	cfb_framebuffer_finalize(display_dev);
}

/*
 * Layout de pantalla (128x64, 4 líneas de 16px de alto cada una — ya
 * funcionaba así antes de este cambio, se conserva la misma disposición):
 *   Fila 0  (y=0):  temperatura actual + marca de error de NTC
 *   Fila 1  (y=16): nivel térmico activo, o causa específica si es CRITICAL
 *   Fila 2  (y=32): duty cycle del ventilador
 *   Fila 3  (y=48): estado global del sistema (prioridad: Alarma Permanente >
 *                    falla de módulo auxiliar > "SISTEMA: ON")
 */
static void render_monitor(void)
{
	ControlState ctrl;
	SystemState  sys;
	TelemetryState tel;

	control_state_get(&ctrl);
	system_state_get(&sys);
	telemetry_state_get(&tel);

	bool ntc_err    = (tel.error_log_flags & ERROR_FLAG_NTC_SENSOR) != 0;
	bool oled_err   = (tel.error_log_flags & ERROR_FLAG_OLED_I2C) != 0;
	bool keypad_err = (tel.error_log_flags & ERROR_FLAG_KEYPAD) != 0;
	bool esp32_err  = (tel.error_log_flags & ERROR_FLAG_ESP32_LINK) != 0;

	char line[22];

	ui_display_clear();

	snprintf(line, sizeof(line), "T: %.1fC %s",
		 (double)ctrl.current_temperature,
		 ntc_err ? "[ERR]" : "");
	display_print(line, 0, 0);

	/* Fila 1: en CRITICAL se prioriza mostrar la causa específica (única
	 * forma textual de distinguirla, ver discussion.md §7.3) por encima del
	 * nombre genérico del umbral. */
	if (ctrl.current_threshold_code == THRESHOLD_CRITICAL) {
		const char *cause_text = (ctrl.critical_cause == CRITICAL_CAUSE_SENSOR_FAULT)
					  ? "CRIT: FALLA NTC"
					  : "CRIT: SOBRETEMP";
		display_print(cause_text, 0, 16);
	} else {
		static const char *threshold_names[] = { "FRIO", "BAJO", "MEDIO", "ALTO" };
		snprintf(line, sizeof(line), "Umbral: %s",
			 threshold_names[ctrl.current_threshold_code]);
		display_print(line, 0, 16);
	}

	snprintf(line, sizeof(line), "Fan: %u%%", ctrl.fan_pwm_duty_cycle);
	display_print(line, 0, 32);

	/* Fila 3: un solo mensaje a la vez, por prioridad. Las fallas de NTC y
	 * de la propia OLED NO se listan aquí a propósito (discussion.md §7.3):
	 * NTC ya tiene representación completa en la fila 1 al escalar a
	 * CRITICAL, y la OLED no puede reportar su propia falla. */
	if (!sys.system_enabled) {
		display_print("ALARMA PERMANENTE", 0, 48);
	} else if (keypad_err) {
		display_print("Teclado no disp.", 0, 48);
	} else if (esp32_err) {
		display_print("ESP32 desconectado", 0, 48);
	} else {
		display_print("SISTEMA: ON ", 0, 48);
	}

	ARG_UNUSED(oled_err); /* La OLED nunca puede reportar su propia falla en sí misma */

	display_flush();
}

static void render_edit(ui_mode_t mode, float current_val)
{
	static const char *labels[] = {
		[UI_MODE_EDIT_LOW]      = "Umbral BAJO",
		[UI_MODE_EDIT_MEDIUM]   = "Umbral MEDIO",
		[UI_MODE_EDIT_HIGH]     = "Umbral ALTO",
		[UI_MODE_EDIT_CRITICAL] = "Umbral CRITICO",
	};

	char line[22];
	ui_display_clear();
	display_print("CONFIGURACION", 0, 0);
	display_print(labels[mode], 0, 16);
	snprintf(line, sizeof(line), "Valor: %.1f C", (double)current_val);
	display_print(line, 0, 32);
	display_print("A=+1 B=-1 D=OK *=Sal", 0, 48);
	display_flush();
}

/* ── Helpers de acceso a campos según el modo ────────────────────────────── */
static float *field_for_mode(ui_mode_t mode, ConfigState *cfg)
{
	switch (mode) {
	case UI_MODE_EDIT_LOW:      return &cfg->threshold_low;
	case UI_MODE_EDIT_MEDIUM:   return &cfg->threshold_medium;
	case UI_MODE_EDIT_HIGH:     return &cfg->threshold_high;
	case UI_MODE_EDIT_CRITICAL: return &cfg->threshold_critical;
	default:                    return NULL;
	}
}

/* ── Procesamiento de teclas ─────────────────────────────────────────────── */
static ui_mode_t process_key_monitor(char key)
{
	switch (key) {
	case '1': return UI_MODE_EDIT_LOW;
	case '2': return UI_MODE_EDIT_MEDIUM;
	case '3': return UI_MODE_EDIT_HIGH;
	case '4': return UI_MODE_EDIT_CRITICAL;
	default:  return UI_MODE_MONITOR;
	}
}

static ui_mode_t process_key_edit(char key, ui_mode_t mode)
{
	ConfigState cfg;
	config_state_get(&cfg);

	float *target = field_for_mode(mode, &cfg);
	if (!target) return UI_MODE_MONITOR;

	switch (key) {
	case 'A': *target += 1.0f; break;
	case 'B': *target -= 1.0f; break;
	case 'D':
		/* Validación obligatoria antes de confirmar (discussion.md §7.4):
		 * low < medium < high < critical. Si falla, se rechaza el cambio
		 * y se mantienen los límites previos — no se llega a escribir
		 * en ConfigState. */
		if (cfg.threshold_low < cfg.threshold_medium &&
		    cfg.threshold_medium < cfg.threshold_high &&
		    cfg.threshold_high < cfg.threshold_critical) {
			config_state_set_thresholds(cfg.threshold_low, cfg.threshold_medium,
						     cfg.threshold_high, cfg.threshold_critical);
			LOG_INF("Umbrales actualizados: %.1f / %.1f / %.1f / %.1f",
				(double)cfg.threshold_low, (double)cfg.threshold_medium,
				(double)cfg.threshold_high, (double)cfg.threshold_critical);
			return UI_MODE_MONITOR;
		} else {
			LOG_WRN("Configuracion invalida rechazada: %.1f / %.1f / %.1f / %.1f",
				(double)cfg.threshold_low, (double)cfg.threshold_medium,
				(double)cfg.threshold_high, (double)cfg.threshold_critical);
			/* Se mantiene en modo edición del mismo campo para que el
			 * usuario corrija; el mensaje de error visual se puede
			 * agregar como una variante de render_edit() si se
			 * necesita más adelante. */
			return mode;
		}
	case '*':
		return UI_MODE_MONITOR;   /* Cancelar sin guardar */
	default:
		break;
	}

	return mode;
}

/* ── Hilo principal ──────────────────────────────────────────────────────── */
static void ui_keypad_task_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	k_msleep(200); /* Esperar a que main() inicialice los estados globales */

	LOG_INF("ui_keypad_task thread started");

	display_ok = display_init();
	telemetry_state_set_error_flag(ERROR_FLAG_OLED_I2C, !display_ok);

	keypad_ok = matrix_keypad_init();
	if (!keypad_ok) {
		LOG_ERR("Teclado matricial no listo — verificar overlay (filas/columnas)");
		telemetry_state_set_error_flag(ERROR_FLAG_KEYPAD, true);
	}

	ui_mode_t mode    = UI_MODE_MONITOR;
	uint32_t  timeout = TIMEOUT_TICKS;

	while (1) {
		// --- AÑADIR ESTE BLOQUE ---
		if (flag_request_config) {
			flag_request_config = false;
			if (display_ok) {
				mode = UI_MODE_EDIT_LOW; // O la constante que uses para editar
				timeout = TIMEOUT_TICKS;
			}
		}
		// 1. Pintar la pantalla ANTES de esperar
		if (display_ok) {
			if (mode == UI_MODE_MONITOR) {
				render_monitor(); // <-- Usamos tu función original
			} else {
				// Buscar el valor del umbral actual para mostrarlo en la edición
				ConfigState cfg;
				config_state_get(&cfg);
				float current_val = 0.0f;
				
				if (mode == UI_MODE_EDIT_LOW) current_val = cfg.threshold_low;
				else if (mode == UI_MODE_EDIT_MEDIUM) current_val = cfg.threshold_medium;
				else if (mode == UI_MODE_EDIT_HIGH) current_val = cfg.threshold_high;
				else if (mode == UI_MODE_EDIT_CRITICAL) current_val = cfg.threshold_critical;

				render_edit(mode, current_val); // <-- Usamos tu función original
			}
		}

		// 2. Leer el teclado
		char key;
		bool key_pressed = keypad_ok && matrix_keypad_scan(&key);

		if (key_pressed) {
			timeout = TIMEOUT_TICKS;

			if (mode == UI_MODE_MONITOR) {
				// Usamos tu función que ya procesa la 'A' para saltar a edición
				mode = process_key_monitor(key); 
			} else {
				// Modo edición normal
				mode = process_key_edit(key, mode);
			}
		} else if (mode != UI_MODE_MONITOR) {
			// 3. Cuenta el timeout solo si estás editando
			if (timeout > 0) {
				timeout--;
			} else {
				LOG_INF("Timeout de edicion alcanzado. Volviendo a Monitoreo.");
				mode = UI_MODE_MONITOR;
			}
		}

		k_msleep(20);
	}}

K_THREAD_DEFINE(ui_keypad_tid, STACK_SIZE, ui_keypad_task_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
