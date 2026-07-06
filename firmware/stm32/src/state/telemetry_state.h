/**
 * @file telemetry_state.h
 * @brief Diagnóstico e histórico de fallas (TelemetryState).
 *
 * @details
 * Protegido por telemetry_mutex.
 */

#ifndef TELEMETRY_STATE_H
#define TELEMETRY_STATE_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

/* Banderas de error */
#define ERROR_FLAG_NTC_SENSOR     (1U << 0)
#define ERROR_FLAG_OLED_I2C       (1U << 1)
#define ERROR_FLAG_KEYPAD         (1U << 2)
#define ERROR_FLAG_ESP32_LINK     (1U << 3)

/**
 * @brief Registro histórico de telemetría y salud del dispositivo.
 */
typedef struct {
	uint32_t error_log_flags;         /**< Máscara de bits de errores activos. */
	uint32_t system_boot_count;       /**< Cantidad de veces que arrancó el sistema. */
	uint32_t error_count[4];          /**< Contador de fallas por módulo específico. */
	uint8_t ntc_consecutive_failures; /**< Contador de fallas consecutivas (NTC). */
} TelemetryState;

/**
 * @brief Inicializa los contadores de fallas a cero y el mutex.
 */
void telemetry_state_init(void);

/**
 * @brief Copia en variable local los datos telemétricos usando exclusión mutua.
 * @param out Puntero a la variable de destino.
 */
void telemetry_state_get(TelemetryState *out);

/**
 * @brief Activa o desactiva una bandera de error específica en la bitmask.
 * @param flag Bandera a alterar (ej. ERROR_FLAG_NTC_SENSOR).
 * @param active true para establecer (set), false para limpiar (clear).
 */
void telemetry_state_set_error_flag(uint32_t flag, bool active);

/**
 * @brief Suma un inicio más al conteo del histórico de arranques del equipo.
 */
void telemetry_state_increment_boot_count(void);

#endif /* TELEMETRY_STATE_H */