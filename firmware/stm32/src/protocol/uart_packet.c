/*
 * uart_packet.c — Construcción de tramas, CRC16-CCITT, y parseo de recepción.
 */

#include <string.h>
#include "uart_packet.h"

uint16_t uart_packet_crc16(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFFFu;

	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (int bit = 0; bit < 8; bit++) {
			if (crc & 0x8000u) {
				crc = (crc << 1) ^ 0x1021u;
			} else {
				crc <<= 1;
			}
		}
	}
	return crc;
}

size_t uart_packet_build(uint8_t *buf, size_t buf_size,
			 packet_type_t type, uint8_t seq,
			 const void *payload, uint8_t payload_len)
{
	/* Tamaño total: SOF + TYPE + SEQ + LEN + payload + CRC(2) */
	size_t total = 4u + payload_len + 2u;

	if (payload_len > UART_PACKET_MAX_PAYLOAD || total > buf_size) {
		return 0;
	}

	buf[0] = UART_PACKET_SOF;
	buf[1] = (uint8_t)type;
	buf[2] = seq;
	buf[3] = payload_len;

	if (payload_len > 0 && payload != NULL) {
		memcpy(&buf[4], payload, payload_len);
	}

	/* CRC sobre TYPE + SEQ + LEN + PAYLOAD (índices 1 a 3+payload_len) */
	uint16_t crc = uart_packet_crc16(&buf[1], 3u + payload_len);
	buf[4 + payload_len]     = (uint8_t)(crc >> 8);
	buf[4 + payload_len + 1] = (uint8_t)(crc & 0xFFu);

	return total;
}

void uart_packet_parser_init(uart_rx_parser_t *parser)
{
	parser->state = RX_WAIT_SOF;
	parser->payload_idx = 0;
}

/*
 * Máquina de estados de un solo byte por llamada. Reinicia a RX_WAIT_SOF en
 * cualquier situación anómala (LEN fuera de rango, CRC inválido) en vez de
 * intentar "recuperar" la trama a medias — con un protocolo tan simple no
 * vale la pena la complejidad de intentar resincronizar de otra forma que no
 * sea "descartar todo y esperar el próximo 0xAA".
 */
bool uart_packet_parser_feed(uart_rx_parser_t *parser, uint8_t byte,
			     packet_type_t *out_type, uint8_t *out_seq,
			     const uint8_t **out_payload, uint8_t *out_payload_len)
{
	switch (parser->state) {
	case RX_WAIT_SOF:
		if (byte == UART_PACKET_SOF) {
			parser->state = RX_WAIT_TYPE;
		}
		/* Si no es SOF, seguimos esperando — esto es lo que permite
		 * resincronizar solo tras basura en la línea. */
		break;

	case RX_WAIT_TYPE:
		parser->type = byte;
		parser->state = RX_WAIT_SEQ;
		break;

	case RX_WAIT_SEQ:
		parser->seq = byte;
		parser->state = RX_WAIT_LEN;
		break;

	case RX_WAIT_LEN:
		if (byte > UART_PACKET_MAX_PAYLOAD) {
			/* LEN corrupto o desalineado: descartar y resincronizar */
			parser->state = RX_WAIT_SOF;
			break;
		}
		parser->len = byte;
		parser->payload_idx = 0;
		parser->state = (byte == 0) ? RX_WAIT_CRC_HI : RX_WAIT_PAYLOAD;
		break;

	case RX_WAIT_PAYLOAD:
		parser->payload[parser->payload_idx++] = byte;
		if (parser->payload_idx >= parser->len) {
			parser->state = RX_WAIT_CRC_HI;
		}
		break;

	case RX_WAIT_CRC_HI:
		parser->crc_hi = byte;
		parser->state = RX_WAIT_CRC_LO;
		break;

	case RX_WAIT_CRC_LO: {
		uint16_t received_crc = ((uint16_t)parser->crc_hi << 8) | byte;

		/* Reconstruir TYPE+SEQ+LEN+PAYLOAD para verificar el CRC —
		 * mismo cálculo que uart_packet_build() del lado emisor. */
		uint8_t crc_input[3 + UART_PACKET_MAX_PAYLOAD];
		crc_input[0] = parser->type;
		crc_input[1] = parser->seq;
		crc_input[2] = parser->len;
		memcpy(&crc_input[3], parser->payload, parser->len);

		uint16_t computed_crc = uart_packet_crc16(crc_input, 3u + parser->len);

		parser->state = RX_WAIT_SOF; /* Listos para la próxima trama, pase lo que pase */

		if (computed_crc != received_crc) {
			return false;
		}

		*out_type = (packet_type_t)parser->type;
		*out_seq = parser->seq;
		*out_payload = parser->payload;
		*out_payload_len = parser->len;
		return true;
	}
	}

	return false;
}
