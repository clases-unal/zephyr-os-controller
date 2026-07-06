/**
 * @file uart_packet.h
 * @brief Protocolo UART STM32 <-> ESP32 simétrico (Construcción y Parseo).
 *
 * @details
 * Formato de trama: [SOF 1B][TYPE 1B][SEQ 1B][LEN 1B][PAYLOAD nB][CRC16 2B]
 * SOF = 0xAA. CRC16-CCITT calculado sobre TYPE+SEQ+LEN+PAYLOAD.
 */

#ifndef UART_PACKET_H
#define UART_PACKET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define UART_PACKET_SOF         0xAAu
#define UART_PACKET_MAX_PAYLOAD 64u

/**
 * @brief Tipos de paquetes serializables en la red.
 */
typedef enum {
	PACKET_TYPE_TELEMETRY  = 0x01,  /**< Temperatura, duty, umbral. */
	PACKET_TYPE_CONFIG     = 0x02,  /**< Umbrales establecidos por HMI. */
	PACKET_TYPE_DIAGNOSTIC = 0x03,  /**< Diagnósticos y contador de fallos. */
	PACKET_TYPE_HEARTBEAT  = 0x04   /**< Trama nula para control de vida. */
} packet_type_t;

/**
 * @brief Estructura del payload de telemetría (PACKET_TYPE_TELEMETRY).
 */
typedef struct __attribute__((packed)) {
	int16_t temperature_cdeg; /**< Temperatura actual multiplicada por 100. */
	uint8_t fan_duty_pct;     /**< Ciclo de trabajo del ventilador (0-100). */
	uint8_t threshold_code;   /**< Nivel térmico activo. */
	uint8_t error_flags;      /**< Banderas de error activas (bitmask). */
} payload_telemetry_t;

/**
 * @brief Estructura del payload de configuración (PACKET_TYPE_CONFIG).
 */
typedef struct __attribute__((packed)) {
	int16_t threshold_low_cdeg;      /**< Umbral LOW multiplicado por 100. */
	int16_t threshold_medium_cdeg;   /**< Umbral MEDIUM multiplicado por 100. */
	int16_t threshold_high_cdeg;     /**< Umbral HIGH multiplicado por 100. */
	int16_t threshold_critical_cdeg; /**< Umbral CRITICAL multiplicado por 100. */
} payload_config_t;

/**
 * @brief Estructura del payload de diagnóstico (PACKET_TYPE_DIAGNOSTIC).
 */
typedef struct __attribute__((packed)) {
	uint32_t boot_count;        /**< Conteo histórico de arranques del sistema. */
	uint32_t error_count_ntc;   /**< Conteo de fallas del sensor NTC. */
	uint32_t error_count_oled;  /**< Conteo de fallas de la pantalla OLED. */
	uint32_t error_count_esp32; /**< Conteo de desconexiones del ESP32. */
} payload_diagnostic_t;

/**
 * @brief Máquina de estados para la recepción de tramas UART asíncronas.
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

/**
 * @brief Inicializa la estructura del parser RX.
 * @param parser Puntero al objeto parser a limpiar.
 */
void uart_packet_parser_init(uart_rx_parser_t *parser);

/**
 * @brief Alimenta el parser con un byte crudo. Evalúa si la trama completó exitosamente.
 * * @param parser Máquina de estados.
 * @param byte_in Byte entrante del bus serial.
 * @param out_type Parámetro de salida si finaliza bien (Tipo de paquete).
 * @param out_seq Parámetro de salida (Secuencia).
 * @param out_payload Parámetro de salida (Puntero al buffer del Payload).
 * @param out_payload_len Parámetro de salida (Longitud de la carga).
 * @return true si se ensambló una trama íntegra y el CRC16 coincide, false de lo contrario.
 */
bool uart_packet_parser_feed(uart_rx_parser_t *parser, uint8_t byte_in,
                             packet_type_t *out_type, uint8_t *out_seq,
                             const uint8_t **out_payload, uint8_t *out_payload_len);

/**
 * @brief Ensambla una trama completa para transmisión, calculándole el CRC.
 * * @param buf Buffer de memoria de salida.
 * @param buf_size Longitud máxima autorizada de escritura en buf.
 * @param type Tipo de paquete de datos.
 * @param seq ID secuencial de la transacción.
 * @param payload Puntero al contenido de los datos crudos.
 * @param payload_len Tamaño del payload a anexar.
 * @return Tamaño final del paquete entero incluyendo cabecera y CRC (0 si hay error).
 */
size_t uart_packet_build(uint8_t *buf, size_t buf_size,
                         packet_type_t type, uint8_t seq,
                         const void *payload, uint8_t payload_len);

#endif /* UART_PACKET_H */