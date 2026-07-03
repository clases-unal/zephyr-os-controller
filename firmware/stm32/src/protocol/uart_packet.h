/*
 * uart_packet.h — Protocolo UART STM32 <-> ESP32
 *
 * Formato de trama (igual en ambos sentidos, el protocolo es simétrico):
 *   [SOF 1B][TYPE 1B][SEQ 1B][LEN 1B][PAYLOAD nB][CRC16 2B]
 *   SOF = 0xAA
 *   CRC16-CCITT sobre TYPE+SEQ+LEN+PAYLOAD (no incluye SOF ni el CRC mismo)
 *
 * Este archivo ofrece dos mitades independientes:
 *   - Construcción de tramas para enviar (uart_packet_build) — ya existía.
 *   - Parseo de tramas recibidas byte a byte (uart_packet_parser_*) — nuevo,
 *     agregado para que esp32_comm_manager pueda leer heartbeats/paquetes que
 *     el ESP32 envíe de vuelta, en vez de solo transmitir a ciegas.
 */

#ifndef UART_PACKET_H
#define UART_PACKET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define UART_PACKET_SOF       0xAAu
#define UART_PACKET_MAX_PAYLOAD 64u

typedef enum {
	PACKET_TYPE_TELEMETRY  = 0x01,  /* temperatura, duty, umbral — cada 2s */
	PACKET_TYPE_CONFIG     = 0x02,  /* umbrales configurados por HMI */
	PACKET_TYPE_DIAGNOSTIC = 0x03,  /* boot count, error flags — solo en boot */
	PACKET_TYPE_HEARTBEAT  = 0x04,  /* "sigo vivo" — sin payload, cada 5s en ambos sentidos */
} packet_type_t;

/* Payload de telemetría dinámica (tipo 0x01) */
typedef struct __attribute__((packed)) {
	int16_t  temperature_cdeg;  /* temperatura × 100, ej. 2530 = 25.30°C */
	uint8_t  fan_duty_pct;
	uint8_t  threshold_code;
	uint8_t  error_flags;
} payload_telemetry_t;

/* Payload de configuración (tipo 0x02) */
typedef struct __attribute__((packed)) {
	int16_t  threshold_low_cdeg;
	int16_t  threshold_medium_cdeg;
	int16_t  threshold_high_cdeg;
	int16_t  threshold_critical_cdeg;
} payload_config_t;

/* Payload de diagnóstico (tipo 0x03) */
typedef struct __attribute__((packed)) {
	uint32_t boot_count;
	uint32_t error_count_ntc;
	uint32_t error_count_oled;
	uint32_t error_count_esp32;
} payload_diagnostic_t;

/* PACKET_TYPE_HEARTBEAT no tiene payload (payload_len = 0) — su único
 * propósito es existir y llegar con CRC válido, no transporta datos. */

/* Calcula CRC16-CCITT (polinomio 0x1021, init 0xFFFF) */
uint16_t uart_packet_crc16(const uint8_t *data, size_t len);

/*
 * Construye una trama completa en buf[].
 * Retorna la longitud total de la trama, o 0 si payload_len > MAX_PAYLOAD.
 */
size_t uart_packet_build(uint8_t *buf, size_t buf_size,
			 packet_type_t type, uint8_t seq,
			 const void *payload, uint8_t payload_len);

/* ── Recepción byte a byte ───────────────────────────────────────────────
 * Diseñado para alimentarse desde uart_poll_in() en un bucle no bloqueante:
 * cada byte que llega se pasa a uart_packet_parser_feed(). La función
 * devuelve true únicamente cuando terminó de ensamblar una trama completa Y
 * el CRC verificó correcto — en ese momento *out_type/*out_seq/*out_payload/
 * *out_payload_len quedan poblados con la trama recibida. En cualquier otro
 * caso (trama incompleta, CRC inválido, byte de resincronización) devuelve
 * false y el parser se reinicia solo internamente, sin necesitar que quien
 * lo llama haga nada especial — así el llamador no tiene que preocuparse por
 * manejar tramas corruptas o desalineadas, solo por leer el resultado cuando
 * la función devuelve true.
 */
typedef struct {
	enum {
		RX_WAIT_SOF,
		RX_WAIT_TYPE,
		RX_WAIT_SEQ,
		RX_WAIT_LEN,
		RX_WAIT_PAYLOAD,
		RX_WAIT_CRC_HI,
		RX_WAIT_CRC_LO,
	} state;
	uint8_t type;
	uint8_t seq;
	uint8_t len;
	uint8_t payload[UART_PACKET_MAX_PAYLOAD];
	uint8_t payload_idx;
	uint8_t crc_hi;
} uart_rx_parser_t;

void uart_packet_parser_init(uart_rx_parser_t *parser);

bool uart_packet_parser_feed(uart_rx_parser_t *parser, uint8_t byte,
			     packet_type_t *out_type, uint8_t *out_seq,
			     const uint8_t **out_payload, uint8_t *out_payload_len);

#endif /* UART_PACKET_H */
