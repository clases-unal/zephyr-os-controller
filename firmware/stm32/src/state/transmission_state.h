/*
 * transmission_state.h — Estado de comunicación con el ESP32 (TransmissionState)
 * Protegido por transmission_mutex.
 */

#ifndef TRANSMISSION_STATE_H
#define TRANSMISSION_STATE_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
	bool esp32_connected;
	int64_t last_telemetry_sent_ms;   /* k_uptime_get() en el último envío */
	bool resend_pending;
} TransmissionState;

void transmission_state_init(void);
void transmission_state_get(TransmissionState *out);
void transmission_state_set_connected(bool connected);
void transmission_state_mark_sent(int64_t uptime_ms);
void transmission_state_set_resend_pending(bool pending);

#endif /* TRANSMISSION_STATE_H */
