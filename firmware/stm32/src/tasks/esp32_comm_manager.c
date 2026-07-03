/*
 * esp32_comm_manager.c — Hilo de comunicación con el ESP32 por UART3.
 *
 * Qué hace (versión extendida — antes de esta sesión el hilo SOLO transmitía,
 * nunca leía nada de vuelta y `esp32_connected` quedaba fijo en true desde el
 * boot sin importar si el ESP32 seguía ahí o no):
 * - Envía telemetría periódica (según timer o salto brusco de temperatura).
 * - Envía un paquete de diagnóstico una vez al arrancar.
 * - Envía un paquete de configuración cada vez que el usuario cambia los
 *   umbrales desde el teclado (antes nunca se enviaba, ver checkpoint.md §5).
 * - Envía un HEARTBEAT propio cada 5s.
 * - LEE continuamente lo que llega por UART y lo alimenta al parser de
 *   src/protocol/uart_packet.c. Cualquier trama válida recibida (sea cual
 *   sea su tipo) cuenta como "el ESP32 sigue vivo" y actualiza el timestamp
 *   de última recepción.
 * - Si no llega NINGUNA trama válida durante ESP32_TIMEOUT_MS, se declara
 *   la conexión perdida: TransmissionState.esp32_connected pasa a false,
 *   se marca ERROR_FLAG_ESP32_LINK (el LED Qc del registro de desplazamiento
 *   lo refleja automáticamente) y se activa resend_pending para que, al
 *   reconectar, se reenvíen diagnóstico + configuración actual sin esperar
 *   al próximo ciclo normal.
 *
 * Cómo lo hace:
 * - Un único hilo hace TX y RX de forma no bloqueante: en cada vuelta del
 *   bucle intenta leer un byte con uart_poll_in() (retorna de inmediato si
 *   no hay nada) y, aparte, revisa si toca enviar telemetría/heartbeat según
 *   temporizadores. No se usan dos hilos separados para TX/RX — un solo
 *   hilo alternando es suficiente para las tasas de esta aplicación (un
 *   paquete cada 200ms-5s) y evita cualquier necesidad de sincronizar dos
 *   hilos accediendo al mismo periférico UART.
 *
 * Qué recibe / qué entrega:
 * - Recibe el estado actual del sistema desde los estados compartidos, y
 *   tramas crudas desde el UART físico.
 * - Entrega paquetes serializados al ESP32, y entrega hacia el resto del
 *   sistema (TelemetryState, TransmissionState) el estado real del enlace.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <math.h>

#include "esp32_comm_manager.h"
#include "../protocol/uart_packet.h"
#include "../state/control_state.h"
#include "../state/config_state.h"
#include "../state/telemetry_state.h"
#include "../state/transmission_state.h"

LOG_MODULE_REGISTER(esp32_comm_manager, LOG_LEVEL_INF);

#define STACK_SIZE       2048    /* Mayor que otros: maneja buffers de trama */
#define THREAD_PRIORITY  6       /* Baja prioridad */

#define LOOP_PERIOD_MS         20     /* Frecuencia del bucle TX/RX combinado */
#define TELEMETRY_PERIOD_MS    2000
#define TEMP_DELTA_THRESHOLD   0.5f   /* °C — fuerza envío aunque no haya expirado el timer */
#define HEARTBEAT_PERIOD_MS    5000
/* Tolerancia antes de declarar el enlace caído: ~2.4x el período de
 * heartbeat, suficiente para absorber una pérdida ocasional de un paquete
 * sin generar falsos positivos, pero corto para que sea observable en una
 * demostración en vivo. */
#define ESP32_TIMEOUT_MS       12000

static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart3));

static uint8_t tx_buf[4 + sizeof(payload_diagnostic_t) + 2 + 4];  /* trama máxima esperada */
static uint8_t seq_counter = 0;
static bool uart_ok = false;

/* Bandera de una sola escritura, igual patrón que heater_simulation_task.c
 * (authorized): se pone en true desde ui_keypad_task.c al confirmar una
 * edición de umbrales, y este hilo la revisa y limpia en su propio bucle.
 * No necesita mutex por el mismo motivo que allí — un ciclo de retraso en el
 * peor caso (LOOP_PERIOD_MS = 20ms) es intrascendente para este propósito. */
static volatile bool config_changed_pending = false;

void esp32_comm_manager_notify_config_changed(void)
{
	config_changed_pending = true;
}

static void send_bytes(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(uart_dev, data[i]);
	}
}

static void send_diagnostic(void)
{
	TelemetryState tel;
	telemetry_state_get(&tel);

	payload_diagnostic_t diag = {
		.boot_count        = tel.system_boot_count,
		.error_count_ntc   = tel.error_count[0],
		.error_count_oled  = tel.error_count[1],
		.error_count_esp32 = tel.error_count[2],
	};

	size_t len = uart_packet_build(tx_buf, sizeof(tx_buf),
				       PACKET_TYPE_DIAGNOSTIC, seq_counter++,
				       &diag, sizeof(diag));
	if (len > 0) {
		send_bytes(tx_buf, len);
		LOG_INF("Diagnostico enviado (boot=%u)", tel.system_boot_count);
	}
}

static void send_config(void)
{
	ConfigState cfg;
	config_state_get(&cfg);

	payload_config_t payload = {
		.threshold_low_cdeg      = (int16_t)(cfg.threshold_low * 100.0f),
		.threshold_medium_cdeg   = (int16_t)(cfg.threshold_medium * 100.0f),
		.threshold_high_cdeg     = (int16_t)(cfg.threshold_high * 100.0f),
		.threshold_critical_cdeg = (int16_t)(cfg.threshold_critical * 100.0f),
	};

	size_t len = uart_packet_build(tx_buf, sizeof(tx_buf),
				       PACKET_TYPE_CONFIG, seq_counter++,
				       &payload, sizeof(payload));
	if (len > 0) {
		send_bytes(tx_buf, len);
		LOG_INF("Configuracion enviada: %.1f/%.1f/%.1f/%.1f",
			(double)cfg.threshold_low, (double)cfg.threshold_medium,
			(double)cfg.threshold_high, (double)cfg.threshold_critical);
	}
}

static void send_heartbeat(void)
{
	size_t len = uart_packet_build(tx_buf, sizeof(tx_buf),
				       PACKET_TYPE_HEARTBEAT, seq_counter++,
				       NULL, 0);
	if (len > 0) {
		send_bytes(tx_buf, len);
	}
}

static void send_telemetry(float temperature, uint8_t duty, uint8_t threshold,
			   uint8_t error_flags)
{
	payload_telemetry_t payload = {
		.temperature_cdeg = (int16_t)(temperature * 100.0f),
		.fan_duty_pct     = duty,
		.threshold_code   = threshold,
		.error_flags      = (uint8_t)error_flags,
	};

	size_t len = uart_packet_build(tx_buf, sizeof(tx_buf),
				       PACKET_TYPE_TELEMETRY, seq_counter++,
				       &payload, sizeof(payload));
	if (len > 0) {
		send_bytes(tx_buf, len);
		transmission_state_mark_sent(k_uptime_get());
	}
}

/* Procesa todos los bytes disponibles en el UART sin bloquear. Retorna true
 * si se recibió al menos una trama válida en esta pasada (sin importar el
 * tipo) — eso es lo único que le importa al llamador para saber si el ESP32
 * "dio señales de vida" en este ciclo. */
static bool poll_receive(uart_rx_parser_t *parser)
{
	bool got_valid_frame = false;
	uint8_t byte;

	/* uart_poll_in() retorna 0 si hay un byte disponible, -1 si no hay nada
	 * — por eso el bucle es "while == 0", no "while > 0". */
	while (uart_poll_in(uart_dev, &byte) == 0) {
		packet_type_t type;
		uint8_t seq;
		const uint8_t *payload;
		uint8_t payload_len;

		bool complete = uart_packet_parser_feed(parser, byte, &type, &seq,
							&payload, &payload_len);
		if (complete) {
			got_valid_frame = true;
			LOG_INF("Trama recibida del ESP32: tipo=0x%02x seq=%u len=%u",
				type, seq, payload_len);
			/* No hay todavía ningún tipo de trama que el ESP32 deba
			 * iniciar por su cuenta salvo el heartbeat de eco — si en
			 * el futuro el ESP32 necesita mandar comandos (por ej.
			 * cambiar modo remoto), este es el punto donde se
			 * despacharía por switch(type). */
		}
	}

	return got_valid_frame;
}

static void esp32_comm_manager_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INF("esp32_comm_manager thread started");

	if (!device_is_ready(uart_dev)) {
		LOG_ERR("USART3 no listo — verificar overlay (PC10=TX/PC11=RX) y CONFIG_SERIAL=y");
		uart_ok = false;
		telemetry_state_set_error_flag(ERROR_FLAG_ESP32_LINK, true);
	} else {
		uart_ok = true;
		LOG_INF("USART3 listo");
	}

	uart_rx_parser_t parser;
	uart_packet_parser_init(&parser);

	/* Arranca como "no conectado": recién al recibir la primera trama
	 * válida del ESP32 se declara la conexión viva. Antes de esta sesión
	 * esto se fijaba en true incondicionalmente al boot, lo cual no
	 * reflejaba la realidad si el ESP32 ni siquiera estaba conectado. */
	transmission_state_set_connected(false);
	telemetry_state_set_error_flag(ERROR_FLAG_ESP32_LINK, true);
	bool was_connected = false;

	/* Dar tiempo a los otros hilos para que inicialicen su estado */
	k_sleep(K_MSEC(500));

	if (uart_ok) {
		send_diagnostic();
	}

	float last_temp_sent = -999.0f;
	int64_t last_telemetry_ms = 0;
	int64_t last_heartbeat_ms = 0;
	int64_t last_rx_ms = k_uptime_get();

	while (1) {
		k_sleep(K_MSEC(LOOP_PERIOD_MS));

		if (!uart_ok) {
			continue;
		}

		int64_t now_ms = k_uptime_get();

		/* --- Recepción: cualquier trama válida cuenta como "vivo" --- */
		if (poll_receive(&parser)) {
			last_rx_ms = now_ms;
		}

		bool connected_now = (now_ms - last_rx_ms) < ESP32_TIMEOUT_MS;

		if (connected_now != was_connected) {
			transmission_state_set_connected(connected_now);
			telemetry_state_set_error_flag(ERROR_FLAG_ESP32_LINK, !connected_now);

			if (connected_now) {
				LOG_INF("ESP32 reconectado — reenviando diagnostico y configuracion");
				send_diagnostic();
				send_config();
				transmission_state_set_resend_pending(false);
			} else {
				LOG_WRN("ESP32 sin responder por %d ms — enlace declarado caido",
					(int)ESP32_TIMEOUT_MS);
				transmission_state_set_resend_pending(true);
			}
			was_connected = connected_now;
		}

		/* --- Configuración modificada por el usuario: enviar de inmediato --- */
		if (config_changed_pending && connected_now) {
			send_config();
			config_changed_pending = false;
		}

		/* --- Heartbeat propio, independiente de si hay telemetría nueva --- */
		if ((now_ms - last_heartbeat_ms) >= HEARTBEAT_PERIOD_MS) {
			send_heartbeat();
			last_heartbeat_ms = now_ms;
		}

		/* --- Telemetría: por timer o por salto de temperatura --- */
		ControlState ctrl;
		TelemetryState tel;
		control_state_get(&ctrl);
		telemetry_state_get(&tel);

		bool timer_expired  = (now_ms - last_telemetry_ms) >= TELEMETRY_PERIOD_MS;
		bool delta_exceeded = fabsf(ctrl.current_temperature - last_temp_sent)
				      >= TEMP_DELTA_THRESHOLD;

		if (timer_expired || delta_exceeded) {
			send_telemetry(ctrl.current_temperature,
				       ctrl.fan_pwm_duty_cycle,
				       (uint8_t)ctrl.current_threshold_code,
				       (uint8_t)(tel.error_log_flags & 0xFF));

			last_temp_sent   = ctrl.current_temperature;
			last_telemetry_ms = now_ms;

			LOG_INF("Telemetria enviada: T=%.2f duty=%u umbral=%u",
				(double)ctrl.current_temperature,
				ctrl.fan_pwm_duty_cycle,
				ctrl.current_threshold_code);
		}
	}
}

K_THREAD_DEFINE(esp32_comm_tid, STACK_SIZE, esp32_comm_manager_thread,
		 NULL, NULL, NULL, THREAD_PRIORITY, 0, 0);
