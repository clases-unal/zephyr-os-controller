/**
 * @file transmission_state.h
 * @brief Estado de comunicación con el ESP32.
 * * @details Protegido por transmission_mutex.
 */

#ifndef TRANSMISSION_STATE_H
#define TRANSMISSION_STATE_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Variables de estado del enlace UART con ESP32.
 */
typedef struct {
	bool esp32_connected;           /**< Status vivo de la conexión con el SoC externo. */
	int64_t last_telemetry_sent_ms; /**< Timestamp (uptime_ms) de la última transmisión. */
	bool resend_pending;            /**< Bandera para forzar envío de config/diagnóstico. */
} TransmissionState;

/** @brief Inicializa los valores y mutex de transmisión. */
void transmission_state_init(void);

/** @brief Adquiere copia segura del estado del canal. */
void transmission_state_get(TransmissionState *out);

/** @brief Modifica el indicador de conexión (Enlazado/Caído). */
void transmission_state_set_connected(bool connected);

/** @brief Marca el momento de la última telemetría enviada. */
void transmission_state_mark_sent(int64_t uptime_ms);

/** @brief Modifica si hay un paquete de diagnóstico o reconfiguración pendiente de retransmisión. */
void transmission_state_set_resend_pending(bool pending);

#endif /* TRANSMISSION_STATE_H */