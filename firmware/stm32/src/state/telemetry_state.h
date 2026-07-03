/*
 * telemetry_state.h — Diagnóstico e histórico de fallas (TelemetryState)
 * Protegido por telemetry_mutex.
 */

#ifndef TELEMETRY_STATE_H
#define TELEMETRY_STATE_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

/* Máscara de bits — cada bit representa una falla independiente.
 * TODO: confirmar el set completo de banderas contra discussion.md Sección 3.1. */
#define ERROR_FLAG_NTC_SENSOR     (1U << 0)
#define ERROR_FLAG_OLED_I2C       (1U << 1)
#define ERROR_FLAG_KEYPAD         (1U << 2)
#define ERROR_FLAG_ESP32_LINK     (1U << 3)

typedef struct {
	uint32_t error_log_flags;
	uint32_t system_boot_count;     /* Persiste en NVS — ver discussion.md Sección 8 */
	uint32_t error_count[4];        /* Persiste en NVS */
	uint8_t ntc_consecutive_failures; /* Persiste en NVS */
} TelemetryState;

void telemetry_state_init(void);
void telemetry_state_get(TelemetryState *out);
void telemetry_state_set_error_flag(uint32_t flag, bool active);
void telemetry_state_increment_boot_count(void);

#endif /* TELEMETRY_STATE_H */
