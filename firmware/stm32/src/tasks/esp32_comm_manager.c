/**
 * @file esp32_comm_manager.c
 * @brief Hilo de comunicación con el ESP32 por UART3.
 *
 * @details
 * Qué hace:
 * - Envía telemetría periódica (según timer o salto brusco de temperatura).
 * - Envía un paquete de diagnóstico una vez al arrancar.
 * - Envía un paquete de configuración cada vez que el usuario cambia los
 * umbrales desde el teclado.
 * - Envía un HEARTBEAT propio cada 5s.
 * - LEE continuamente lo que llega por UART y lo alimenta al parser de paquetes.
 * Cualquier trama válida recibida cuenta como "el ESP32 sigue vivo".
 * - Si no llega NINGUNA trama válida durante ESP32_TIMEOUT_MS, se declara
 * la conexión perdida y se activa una bandera de reenvío pendiente para cuando reconecte.
 *
 * Cómo lo hace:
 * - Un único hilo hace TX y RX de forma no bloqueante: intenta leer con uart_poll_in() 
 * y aparte revisa si toca enviar telemetría/heartbeat según temporizadores.
 *
 * Qué recibe / qué entrega:
 * - Recibe el estado actual del sistema desde los estados compartidos, y
 * tramas crudas desde el UART físico.
 * - Entrega paquetes serializados al ESP32, y entrega hacia el resto del
 * sistema el estado real del enlace.
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
/* Tolerancia antes de declarar el enlace caído: ~2.4x el período de heartbeat */
#define ESP32_TIMEOUT_MS       12000

static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart3));

static uint8_t tx_buf[4 + sizeof(payload_diagnostic_t) + 2 + 4];  /* trama máxima esperada */
static uint8_t seq_counter = 0;
static bool uart_ok = false;

/* Bandera de una sola escritura: se pone en true al confirmar una edición de umbrales. */
static volatile bool config_changed_pending = false;

/**
 * @brief Marca una bandera para solicitar el envío de la nueva configuración.
 */
void esp32_comm_manager_notify_config_changed(void)
{
	config_changed_pending = true;
}

/**
 * @brief Envía una secuencia de bytes a través del UART mediante polling.
 *
 * @param data Puntero al buffer de datos a enviar.
 * @param len Cantidad de bytes a enviar.
 */
static void send_bytes(const uint8_t *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		uart_poll_out(uart_dev, data[i]);
	}
}

/**
 * @brief Construye y envía un paquete de diagnóstico al ESP32.
 *
 * Recoge información sobre el conteo de reinicios y los errores registrados
 * en los distintos módulos (NTC, OLED, ESP32) enviándolos por UART.
 */
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

/**
 * @brief Construye y envía un paquete con la configuración actual de umbrales.
 */
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

/**
 * @brief Construye y envía un paquete de heartbeat al ESP32.
 */
static void send_heartbeat(void)
{
	size_t len = uart_packet_build(tx_buf, sizeof(tx_buf),
				       PACKET_TYPE_HEARTBEAT, seq_counter++,
				       NULL, 0);
	if (len > 0) {
		send_bytes(tx_buf, len);
	}
}

/**
 * @brief Construye y envía un paquete de telemetría regular.
 *
 * @param temperature Temperatura actual en grados Celsius.
 * @param duty Porcentaje actual del ciclo de trabajo del ventilador (0-100).
 * @param threshold Nivel térmico activo actual.
 * @param error_flags Banderas de error activas en el sistema.
 */
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

/**
 * @brief Lee los bytes disponibles en el UART sin bloquear y los pasa al parser.
 *
 * @param parser Puntero a la estructura de estado del parser de UART.
 * @return true si se ha recibido al menos una trama válida y completa, false de lo contrario.
 */
static bool poll_receive(uart_rx_parser_t *parser)
{
	bool got_valid_frame = false;
	uint8_t byte;

	/* uart_poll_in() retorna 0 si hay un byte disponible, -1 si no hay nada */
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
		}
	}

	return got_valid_frame;
}

/**
 * @brief Hilo principal de comunicación con el ESP32.
 *
 * Ejecuta un bucle periódico donde lee el puerto UART en busca de mensajes
 * y transmite telemetría, configuraciones o heartbeats cuando los temporizadores
 * lo indican o el usuario realiza cambios. Evalúa continuamente la "salud" del
 * enlace de comunicación.
 *
 * @param p1 Parámetro no usado (requerido por Zephyr).
 * @param p2 Parámetro no usado.
 * @param p3 Parámetro no usado.
 */
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
	 * válida del ESP32 se declara la conexión viva. */
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