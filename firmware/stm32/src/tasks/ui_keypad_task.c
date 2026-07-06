/**
 * @file ui_keypad_task.c
 * @brief Pantalla OLED (SSD1306) + teclado matricial 4×4.
 *
 * @details
 * Responsabilidades:
 * - Mostrar en el OLED: temperatura actual, nivel térmico activo, duty cycle
 * del ventilador, y estado del sistema (ON / ALARMA / mensaje de falla).
 * - Leer el teclado cada 20ms y procesar pulsaciones para editar los 4
 * umbrales de temperatura en ConfigState (bajo, medio, alto, crítico).
 * - Timeout de 30s: si no hay actividad, vuelve al modo monitoreo.
 *
 * TEMA VISUAL: Fondo oscuro por defecto para el SSD1306 (píxeles apagados = negro,
 * píxeles encendidos = blanco).
 */

#include <zephyr/kernel.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_keypad_task.h"
#include "../drivers/matrix_keypad.h"
#include "../state/control_state.h"
#include "../state/config_state.h"
#include "../state/system_state.h"
#include "../state/telemetry_state.h"

LOG_MODULE_REGISTER(ui_keypad_task, LOG_LEVEL_INF);

/* ── Configuración del hilo ──────────────────────────────────────────────── */
#define STACK_SIZE        2048
#define THREAD_PRIORITY   4
#define SCAN_PERIOD_MS    20
#define TIMEOUT_TICKS     (30000 / SCAN_PERIOD_MS)  /* 30s de inactividad en modo edición */

/* ── Tipos ────────────────────────────────────────────────────────────────── */
typedef enum {
	UI_MODE_MONITOR,
	UI_MODE_EDIT_LOW,
	UI_MODE_EDIT_MEDIUM,
	UI_MODE_EDIT_HIGH,
	UI_MODE_EDIT_CRITICAL,
	UI_MODE_SHUTDOWN,
} ui_mode_t;

/* ── Estado del módulo (file-scope) ──────────────────────────────────────── */
static const struct device *display_dev = DEVICE_DT_GET(DT_NODELABEL(ssd1306));
static bool display_ok = false;
static bool keypad_ok  = false;

/* Bandera de una sola escritura solicitada externamente para iniciar configuración */
volatile bool flag_request_config = false;

/* ── Búfer para almacenar los números tecleados antes de guardar ─────────────*/
static char edit_buf[8] = {0};
static uint8_t edit_idx = 0;

/* ── API pública ──────────────────────────────────────────────────────────── */

/**
 * @brief Solicita el ingreso al modo configuración desde cualquier hilo externo.
 */
void ui_request_config_mode(void)
{
	flag_request_config = true;
}

/* ── Inicialización real del display ─────────────────────────────────────── */

/**
 * @brief Inicializa el hardware del display OLED y la API CFB (Character Framebuffer).
 *
 * @return true si el dispositivo inicializó correctamente, false en caso contrario.
 */
static bool display_init(void)
{
	if (!device_is_ready(display_dev)) {
		LOG_ERR("SSD1306 no listo");
		return false;
	}

	if (display_blanking_off(display_dev) != 0) {
		return false;
	}
	if (cfb_framebuffer_init(display_dev) != 0) {
		return false;
	}

	/* Fijamos la fuente más pequeña (0) obligatoriamente */
	if (cfb_framebuffer_set_font(display_dev, 0) != 0) {
		LOG_ERR("No se pudo cargar la fuente");
		return false;
	}

	/* Fondo oscuro: NO se invierte la polaridad nativa del SSD1306. */
	cfb_framebuffer_clear(display_dev, true);
	LOG_INF("Display OLED listo (fondo oscuro)");
	return true;
}

/* ── Helpers de display ──────────────────────────────────────────────────── */

/**
 * @brief Limpia el framebuffer del display de manera lógica (en memoria RAM).
 */
static void ui_display_clear(void)
{
	if (!display_ok) {
		return;
	}
	cfb_framebuffer_clear(display_dev, false);
}

/**
 * @brief Imprime una cadena en una posición determinada en el framebuffer.
 *
 * @param str Cadena de texto a imprimir.
 * @param col Coordenada X (columna) en píxeles.
 * @param row Coordenada Y (fila) en píxeles.
 */
static void display_print(const char *str, int col, int row)
{
	if (!display_ok) {
		return;
	}
	cfb_print(display_dev, str, col, row);
}

/**
 * @brief Finaliza el dibujado enviando el framebuffer al hardware del display.
 */
static void display_flush(void)
{
	if (!display_ok) {
		return;
	}
	cfb_framebuffer_finalize(display_dev);
}

/* ── Renderizado principal ───────────────────────────────────────────────── */

/**
 * @brief Dibuja en memoria la vista principal (Modo Monitoreo) y la envía al display.
 *
 * Muestra temperatura, nivel crítico, PWM del ventilador y el estado de la alarma
 * o módulos en falla de forma resumida en las 4 líneas principales de la pantalla.
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
	bool keypad_err = (tel.error_log_flags & ERROR_FLAG_KEYPAD) != 0;
	bool esp32_err  = (tel.error_log_flags & ERROR_FLAG_ESP32_LINK) != 0;

	char line[16]; /* Buffer ajustado a lo que cabe en 128px con la fuente 0 */
	ui_display_clear();

	/* Fila 0: Temp */
	snprintf(line, sizeof(line), "Temp: %.1f%s",
		 (double)ctrl.current_temperature,
		 ntc_err ? "E" : "C");
	display_print(line, 0, 0);

	/* Fila 1: Lvl */
	static const char *threshold_names[] = { "COLD", "LOW", "MEDIUM", "HIGH", "CRITIC" };

	if (ctrl.current_threshold_code == THRESHOLD_CRITICAL) {
		if (ctrl.critical_cause == CRITICAL_CAUSE_SENSOR_FAULT) {
			display_print("Lvl: S.FAULT", 0, 16);
		} else if (ctrl.keep_alive_revoked) { 
			/* Pasaron los 20 segundos de gracia: Escalada a OVERTMP */
			display_print("Lvl: OVERTMP", 0, 16);
		} else {
			/* Acaba de cruzar el umbral, aún está en los 20s de gracia */
			display_print("Lvl: CRITIC", 0, 16);
		}
	} else {
		/* Niveles normales */
		snprintf(line, sizeof(line), "Lvl: %s", threshold_names[ctrl.current_threshold_code]);
		display_print(line, 0, 16);
	}

	/* Fila 2: PWM */
	snprintf(line, sizeof(line), "PWM: %u%%", ctrl.fan_pwm_duty_cycle);
	display_print(line, 0, 32);

	/* Fila 3: Sys */
	if (!sys.system_enabled) {
		display_print("Sys: ALARMA", 0, 48);
	} else if (keypad_err) {
		display_print("Sys: NO KEY", 0, 48);
	} else if (esp32_err) {
		display_print("Sys: NO ESP", 0, 48);
	} else {
		display_print("Sys: ON", 0, 48);
	}

	display_flush();
}

/**
 * @brief Renderiza la vista de edición para modificar un umbral específico.
 *
 * @param mode Modo de edición actual (ej. UI_MODE_EDIT_LOW).
 * @param current_val Valor actual numérico (o el tecleado en el búfer) a mostrar en pantalla.
 */
static void render_edit(ui_mode_t mode, float current_val)
{
	static const char *labels[] = {
		[UI_MODE_EDIT_LOW]      = "CONF: LOW",
		[UI_MODE_EDIT_MEDIUM]   = "CONF: MED",
		[UI_MODE_EDIT_HIGH]     = "CONF: HIGH",
		[UI_MODE_EDIT_CRITICAL] = "CONF: CRIT",
	};

	char line[16];
	ui_display_clear();

	display_print(labels[mode], 0, 0);
	snprintf(line, sizeof(line), "Val: %.1f", (double)current_val);
	display_print(line, 0, 16);
	
	/* Ayudas visuales */
	display_print("A:+ B:- C:OK", 0, 32);
	display_print("#:Nxt *:Exit", 0, 48);

	display_flush();
}

/* ── Helpers de acceso y teclado ─────────────────────────────────────────── */

/**
 * @brief Evalúa la tecla pulsada y actualiza el búfer o cambia de modo.
 *
 * Se encarga de la lógica de edición de números, incrementos (+5/-5), validación
 * del orden ascendente estricto (Low < Med < High < Critical) al guardar con la tecla C,
 * y la navegación entre menús.
 *
 * @param key Carácter representando la tecla física presionada.
 * @param mode Modo de interfaz actual (pantalla en la que se encuentra el usuario).
 * @return El nuevo modo de interfaz después de procesar la tecla.
 */
static ui_mode_t process_key_edit(char key, ui_mode_t mode)
{
    float current_val = 0.0f;
    ConfigState cfg;
    config_state_get(&cfg); 

    if (edit_idx > 0) {
        current_val = atof(edit_buf);
    } else {
        if (mode == UI_MODE_EDIT_LOW) current_val = cfg.threshold_low;
        else if (mode == UI_MODE_EDIT_MEDIUM) current_val = cfg.threshold_medium;
        else if (mode == UI_MODE_EDIT_HIGH) current_val = cfg.threshold_high;
        else if (mode == UI_MODE_EDIT_CRITICAL) current_val = cfg.threshold_critical;
    }
    
    if ((key >= '0' && key <= '9') || key == '.') {
        if (edit_idx < sizeof(edit_buf) - 1) {
            edit_buf[edit_idx++] = key;
            edit_buf[edit_idx] = '\0';
        }
    } 
    else if (key == 'A') {
        current_val += 5.0f;
        snprintf(edit_buf, sizeof(edit_buf), "%.1f", (double)current_val);
        edit_idx = strlen(edit_buf);
    } 
    else if (key == 'B') {
        current_val -= 5.0f;
        snprintf(edit_buf, sizeof(edit_buf), "%.1f", (double)current_val);
        edit_idx = strlen(edit_buf);
    } 
    else if (key == 'C') {
        if (mode == UI_MODE_EDIT_LOW) cfg.threshold_low = current_val;
        else if (mode == UI_MODE_EDIT_MEDIUM) cfg.threshold_medium = current_val;
        else if (mode == UI_MODE_EDIT_HIGH) cfg.threshold_high = current_val;
        else if (mode == UI_MODE_EDIT_CRITICAL) cfg.threshold_critical = current_val;

        /* Regla estricta: L < M < H < C */
        if (cfg.threshold_low < cfg.threshold_medium && 
            cfg.threshold_medium < cfg.threshold_high && 
            cfg.threshold_high < cfg.threshold_critical) {
            
            config_state_set_thresholds(cfg.threshold_low, cfg.threshold_medium, 
                                        cfg.threshold_high, cfg.threshold_critical);

            ui_display_clear();
            display_print("GUARDADO OK", 0, 16);
            display_flush();
            k_msleep(1000);
            
            edit_idx = 0;
            memset(edit_buf, 0, sizeof(edit_buf));
        } else {
            ui_display_clear();
            display_print("ERROR DE ORDEN", 0, 16);
            display_print("L< M< H< C", 0, 32);
            display_flush();
            k_msleep(2000);
        }
    } 
    else if (key == '#') {
        edit_idx = 0;
        memset(edit_buf, 0, sizeof(edit_buf));

        if (mode == UI_MODE_EDIT_LOW) return UI_MODE_EDIT_MEDIUM;
        else if (mode == UI_MODE_EDIT_MEDIUM) return UI_MODE_EDIT_HIGH;
        else if (mode == UI_MODE_EDIT_HIGH) return UI_MODE_EDIT_CRITICAL;
        else if (mode == UI_MODE_EDIT_CRITICAL) return UI_MODE_EDIT_LOW;
    } 
    else if (key == '*') {
        edit_idx = 0;
        memset(edit_buf, 0, sizeof(edit_buf));
        return UI_MODE_MONITOR; 
    }
    
    return mode;
}

/**
 * @brief Hilo principal de interfaz de usuario.
 *
 * Mantiene la renderización de la pantalla y el escaneo del teclado periódicamente.
 * Gestiona los diferentes modos de vista (monitoreo general, edición de cada variable
 * y apagado) así como la lógica antirrebote al interactuar con el teclado.
 *
 * @param p1 Parámetro no usado (requerido por Zephyr).
 * @param p2 Parámetro no usado.
 * @param p3 Parámetro no usado.
 */
static void ui_keypad_task_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	k_msleep(200);

	display_ok = display_init();
	telemetry_state_set_error_flag(ERROR_FLAG_OLED_I2C, !display_ok);

	/* --- MENSAJE DE BIENVENIDA --- */
	if (display_ok) {
		ui_display_clear();
		display_print("INICIANDO...", 0, 0);
		display_print("Sistema", 0, 32);
		display_print("Termico", 0, 48);
		display_flush();
		k_msleep(2500);
	}
	/* ----------------------------- */

	keypad_ok = matrix_keypad_init();
	if (!keypad_ok) {
		telemetry_state_set_error_flag(ERROR_FLAG_KEYPAD, true);
	}

	ui_mode_t mode    = UI_MODE_MONITOR;
	uint32_t  timeout = TIMEOUT_TICKS;

	while (1) {
		SystemState sys;
		system_state_get(&sys);

		/* Detectar petición de apagado general */
		if (sys.shutdown_requested && mode != UI_MODE_SHUTDOWN) {
			mode = UI_MODE_SHUTDOWN;
			if (display_ok) {
				ui_display_clear();
				display_print("APAGANDO...", 0, 16);
				display_print("Hasta luego!", 0, 32);
				display_flush();
				
				k_msleep(1500); 
				
				ui_display_clear();
				display_flush();
				display_blanking_on(display_dev);
			}
		}

		if (mode == UI_MODE_SHUTDOWN) {
			k_msleep(100);
			continue;
		}

		if (flag_request_config) {
			flag_request_config = false;
			if (display_ok) {
				if (mode == UI_MODE_MONITOR) {
					mode = UI_MODE_EDIT_LOW;
					timeout = TIMEOUT_TICKS;
				} else {
					mode = UI_MODE_MONITOR;
				}
				edit_idx = 0;
				memset(edit_buf, 0, sizeof(edit_buf));
			}
		}

		/* 1. Pintar la pantalla */
		if (display_ok) {
			if (mode == UI_MODE_MONITOR) {
				render_monitor();
			} else {
				ConfigState cfg;
				config_state_get(&cfg);
				float current_val = 0.0f;
				
				if (edit_idx > 0) {
					current_val = atof(edit_buf);
				} else {
					if (mode == UI_MODE_EDIT_LOW) current_val = cfg.threshold_low;
					else if (mode == UI_MODE_EDIT_MEDIUM) current_val = cfg.threshold_medium;
					else if (mode == UI_MODE_EDIT_HIGH) current_val = cfg.threshold_high;
					else if (mode == UI_MODE_EDIT_CRITICAL) current_val = cfg.threshold_critical;
				}

				render_edit(mode, current_val);
			}
		}

		/* 2. Leer el teclado */
        static char last_key = '\0';
        char key;
        bool key_pressed = keypad_ok && matrix_keypad_scan(&key);

        if (key_pressed) {
            timeout = TIMEOUT_TICKS;
            
            if (key != last_key) {
                last_key = key; 

                if (mode != UI_MODE_MONITOR && mode != UI_MODE_SHUTDOWN) {
                    mode = process_key_edit(key, mode);
                }
            }
        } else {
            last_key = '\0';
        }

        k_msleep(20);
	}
}

K_THREAD_DEFINE(ui_keypad_tid, STACK_SIZE, ui_keypad_task_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);