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
 * CORRECCIÓN HISTÓRICA (ya resuelta): en una versión anterior el framebuffer
 * nunca se inicializaba de verdad — `display_ok` quedaba hardcodeado en
 * `false` para evitar un problema de heap no configurado (ver prj.conf,
 * CONFIG_HEAP_MEM_POOL_SIZE). Esa era la causa raíz de que la OLED pareciera
 * "no encender" incluso con el cableado correcto: nunca se le pedía nada.
 *
 * TEMA VISUAL — fondo oscuro (fix de esta sesión): antes se llamaba a
 * cfb_framebuffer_invert() en display_init(), lo que invierte la polaridad
 * normal del SSD1306 y produce fondo blanco con texto negro. La polaridad
 * NATIVA del controlador ya es "fondo oscuro" (píxeles apagados = negro,
 * píxeles encendidos = el texto en blanco), que es el resultado deseado —
 * por eso ahora simplemente NO se invierte nada. Si en el futuro se prefiere
 * el esquema invertido, basta con volver a llamar cfb_framebuffer_invert()
 * una vez en display_init().
 *
 * BUG "aparece en modo edición al arrancar" (fix de esta sesión): esto NO
 * es un problema de este archivo ni del teclado — es causado por un pulso
 * espurio del botón físico de usuario (PC13) durante el arranque, que
 * power_status_manager.c interpretaba como una pulsación media real y por
 * eso llamaba a ui_request_config_mode() sin que el usuario tocara nada.
 * La explicación completa y la corrección están en power_status_manager.c.
 * Este archivo solo necesitaba que display_ok funcionara de verdad (arriba)
 * para que el síntoma fuera visible en primer lugar.
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

/* Bandera de una sola escritura: power_status_manager.c la activa cuando
 * detecta una pulsación media (1-2s) del botón físico, este hilo la consume
 * en su propio bucle y la limpia. Ver ui_keypad_task.h para el prototipo
 * público — antes se declaraba con "extern" directamente en el .c que la
 * llamaba, ahora vive en el header como corresponde a cualquier función
 * pública de este módulo. */
volatile bool flag_request_config = false;

/* ── Búfer para almacenar los números tecleados antes de guardar ─────────────*/
static char edit_buf[8] = {0};
static uint8_t edit_idx = 0;

/* ── API pública ──────────────────────────────────────────────────────────── */
void ui_request_config_mode(void)
{
	flag_request_config = true;
}

/* ── Inicialización real del display ─────────────────────────────────────── */
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

	/* Fondo oscuro: NO se invierte la polaridad nativa del SSD1306 (ver
	 * nota de cabecera del archivo). cfb_framebuffer_clear(..., true)
	 * limpia y refresca de una vez el panel físico con todo en negro. */
	cfb_framebuffer_clear(display_dev, true);
	LOG_INF("Display OLED listo (fondo oscuro)");
	return true;
}

/* ── Helpers de display ──────────────────────────────────────────────────── */
static void ui_display_clear(void)
{
	if (!display_ok) {
		return;
	}
	cfb_framebuffer_clear(display_dev, false);
}

static void display_print(const char *str, int col, int row)
{
	if (!display_ok) {
		return;
	}
	cfb_print(display_dev, str, col, row);
}

static void display_flush(void)
{
	if (!display_ok) {
		return;
	}
	cfb_framebuffer_finalize(display_dev);
}

/* ── Renderizado principal ───────────────────────────────────────────────── */
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

	/* Fila 0: Temp (Max 12 chars: "Temp: 100.0C") */
	snprintf(line, sizeof(line), "Temp: %.1f%s",
		 (double)ctrl.current_temperature,
		 ntc_err ? "E" : "C");
	display_print(line, 0, 0);

	/* Fila 1: Lvl (Max 12 chars: "Lvl: SOBRET") */
	if (ctrl.current_threshold_code == THRESHOLD_CRITICAL) {
		const char *cause_text = (ctrl.critical_cause == CRITICAL_CAUSE_SENSOR_FAULT)
					  ? "Lvl: FALLA" : "Lvl: SOBRET";
		display_print(cause_text, 0, 16);
	} else {
		static const char *threshold_names[] = { "FRIO", "BAJO", "MED", "ALTO" };
		snprintf(line, sizeof(line), "Lvl: %s", threshold_names[ctrl.current_threshold_code]);
		display_print(line, 0, 16);
	}

	/* Fila 2: PWM (Max 12 chars: "PWM: 100%") */
	snprintf(line, sizeof(line), "PWM: %u%%", ctrl.fan_pwm_duty_cycle);
	display_print(line, 0, 32);

	/* Fila 3: Sys (Max 12 chars: "Sys: ALARMA") */
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
	
	/* Ayudas visuales actualizadas al nuevo mapa de teclado */
	display_print("A:+ B:- C:OK", 0, 32);
	display_print("#:Nxt *:Exit", 0, 48);

	display_flush();
}

/* ── Helpers de acceso y teclado ─────────────────────────────────────────── */
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

static ui_mode_t process_key_monitor(char key)
{
	/* Ya no procesamos teclas '1', '2', '3' o '4' para entrar a configuración. 
	 * El acceso es exclusivo mediante pulsación media del botón PC13. */
	return UI_MODE_MONITOR;
}

static ui_mode_t process_key_edit(char key, ui_mode_t mode)
{
	ConfigState cfg;
	config_state_get(&cfg);
	float *target = field_for_mode(mode, &cfg);
	if (!target) {
		return UI_MODE_MONITOR;
	}

	switch (key) {
	case 'A': *target += 1.0f; break;
	case 'B': *target -= 1.0f; break;
	case 'D':
		if (cfg.threshold_low < cfg.threshold_medium &&
		    cfg.threshold_medium < cfg.threshold_high &&
		    cfg.threshold_high < cfg.threshold_critical) {
			config_state_set_thresholds(cfg.threshold_low, cfg.threshold_medium,
						     cfg.threshold_high, cfg.threshold_critical);
			return UI_MODE_MONITOR;
		} else {
			return mode; /* Orden inválido: rechazado, se queda en edición */
		}
	case '*':
		return UI_MODE_MONITOR;
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
		k_msleep(2500); /* Mostrar por 2.5 segundos */
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
				
				/* Mostramos el mensaje durante 1.5s (de los 2s que nos da power_status_manager) */
				k_msleep(1500); 
				
				/* Limpiar la pantalla y apagar físicamente el controlador SSD1306 */
				ui_display_clear();
				display_flush();
				display_blanking_on(display_dev);
			}
		}

		/* Si está apagando, bloquear cualquier otra acción y renderizado */
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
				/* Limpiar el búfer al entrar o salir de edición */
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
				
				/* Si el usuario ha presionado números, previsualizamos su entrada */
				if (edit_idx > 0) {
					current_val = atof(edit_buf);
				} else {
					/* Si no ha tecleado nada, mostramos el umbral real de la RAM */
					if (mode == UI_MODE_EDIT_LOW) current_val = cfg.threshold_low;
					else if (mode == UI_MODE_EDIT_MEDIUM) current_val = cfg.threshold_medium;
					else if (mode == UI_MODE_EDIT_HIGH) current_val = cfg.threshold_high;
					else if (mode == UI_MODE_EDIT_CRITICAL) current_val = cfg.threshold_critical;
				}

				render_edit(mode, current_val);
			}
		}

		/* 2. Leer el teclado */
		static char last_key = '\0'; /* Memoria de la tecla presionada (Antirrebote) */
		char key;
		bool key_pressed = keypad_ok && matrix_keypad_scan(&key);

		if (key_pressed) {
			timeout = TIMEOUT_TICKS; /* Reiniciar inactividad con cualquier tecla */
			
			/* --- LÓGICA ANTIRREBOTE (EDGE DETECTION) --- */
			/* Solo procesar si la tecla es diferente a la del ciclo anterior */
			if (key != last_key) {
				last_key = key; /* Guardar estado de la tecla para no repetir */

				if (mode != UI_MODE_MONITOR && mode != UI_MODE_SHUTDOWN) {
					
					/* 2.1 Obtener el valor actual que vemos en pantalla */
					float current_val = 0.0f;
					ConfigState cfg;
					config_state_get(&cfg); /* Estado real en RAM */

					/* Priorizar lo que el usuario está editando temporalmente */
					if (edit_idx > 0) {
						current_val = atof(edit_buf);
					} else {
						/* Si no hay edición en curso, mostrar valor real de RAM */
						if (mode == UI_MODE_EDIT_LOW) current_val = cfg.threshold_low;
						else if (mode == UI_MODE_EDIT_MEDIUM) current_val = cfg.threshold_medium;
						else if (mode == UI_MODE_EDIT_HIGH) current_val = cfg.threshold_high;
						else if (mode == UI_MODE_EDIT_CRITICAL) current_val = cfg.threshold_critical;
					}

					/* 2.2 Procesar la acción de la tecla */
					
					/* Escritura manual con números */
					if ((key >= '0' && key <= '9') || key == '.') {
						if (edit_idx < sizeof(edit_buf) - 1) {
							edit_buf[edit_idx++] = key;
							edit_buf[edit_idx] = '\0';
						}
					} 
					/* A: Aumentar 5 grados */
					else if (key == 'A') {
						current_val += 5.0f;
						snprintf(edit_buf, sizeof(edit_buf), "%.1f", (double)current_val);
						edit_idx = strlen(edit_buf);
					} 
					/* B: Disminuir 5 grados */
					else if (key == 'B') {
						current_val -= 5.0f;
						snprintf(edit_buf, sizeof(edit_buf), "%.1f", (double)current_val);
						edit_idx = strlen(edit_buf);
					} 
					/* C: Confirmar y Guardar (Validando lógica térmica) */
					else if (key == 'C') {
						if (mode == UI_MODE_EDIT_LOW) cfg.threshold_low = current_val;
						else if (mode == UI_MODE_EDIT_MEDIUM) cfg.threshold_medium = current_val;
						else if (mode == UI_MODE_EDIT_HIGH) cfg.threshold_high = current_val;
						else if (mode == UI_MODE_EDIT_CRITICAL) cfg.threshold_critical = current_val;

						/* Regla estricta: L < M < H < C */
						if (cfg.threshold_low < cfg.threshold_medium && 
						    cfg.threshold_medium < cfg.threshold_high && 
						    cfg.threshold_high < cfg.threshold_critical) {
							
							/* Guardar permanentemente en RAM */
							config_state_set_thresholds(cfg.threshold_low, cfg.threshold_medium, 
														cfg.threshold_high, cfg.threshold_critical);

							ui_display_clear();
							display_print("GUARDADO OK", 0, 16);
							display_flush();
							k_msleep(1000);
							
							/* Limpiar búfer para leer el nuevo valor confirmado */
							edit_idx = 0;
							memset(edit_buf, 0, sizeof(edit_buf));
						} else {
							/* Rechazar cambios por romper la escala de temperaturas */
							ui_display_clear();
							display_print("ERROR DE ORDEN", 0, 16);
							display_print("L< M< H< C", 0, 32);
							display_flush();
							k_msleep(2000);
						}
					} 
					/* #: Pasar al siguiente menú de forma cíclica */
					else if (key == '#') {
						/* Descartar cualquier cambio temporal no guardado */
						edit_idx = 0;
						memset(edit_buf, 0, sizeof(edit_buf));

						if (mode == UI_MODE_EDIT_LOW) mode = UI_MODE_EDIT_MEDIUM;
						else if (mode == UI_MODE_EDIT_MEDIUM) mode = UI_MODE_EDIT_HIGH;
						else if (mode == UI_MODE_EDIT_HIGH) mode = UI_MODE_EDIT_CRITICAL;
						else if (mode == UI_MODE_EDIT_CRITICAL) mode = UI_MODE_EDIT_LOW;
					} 
					/* *: Salir al monitor principal */
					else if (key == '*') {
						edit_idx = 0;
						memset(edit_buf, 0, sizeof(edit_buf));
						mode = UI_MODE_MONITOR; 
					}
					/* NOTA: La tecla 'D' simplemente cae al vacío y no hace nada */
				}
			}
		} else {
			/* --- REINICIO DE ESTADO AL SOLTAR --- */
			/* Permite volver a registrar pulsaciones una vez levantado el dedo */
			last_key = '\0';
		}

		k_msleep(20);
	}
}

K_THREAD_DEFINE(ui_keypad_tid, STACK_SIZE, ui_keypad_task_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
