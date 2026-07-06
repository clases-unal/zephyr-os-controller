/**
 * @file system_state.h
 * @brief Coordinación global de alta prioridad (SystemState).
 *
 * @details
 * Protegido por sys_mutex. Banderas vitales que controlan si el sistema 
 * entero puede continuar o si debe bloquearse / apagarse.
 */

#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <zephyr/kernel.h>
#include <stdbool.h>

/**
 * @brief Estado de habilitación y ciclo de energía general.
 */
typedef struct {
	bool system_enabled;     /**< false = Alarma Permanente (requiere intervención física). */
	bool shutdown_requested; /**< true = Se inició la solicitud de apagado ordenado general. */
} SystemState;

/**
 * @brief Inicializa las banderas de sistema y el respectivo mutex.
 */
void system_state_init(void);

/**
 * @brief Lee con seguridad el estado de sistema general.
 * @param out Puntero donde alojar el estado del sistema copiado.
 */
void system_state_get(SystemState *out);

/**
 * @brief Modifica la bandera de habilitación del sistema (útil para toggles).
 * @param enabled true para sistema activo, false para forzar Alarma Permanente.
 */
void system_state_set_enabled(bool enabled);

/**
 * @brief Emite globalmente la solicitud irrevocable de un SHUTDOWN.
 */
void system_state_request_shutdown(void);

#endif /* SYSTEM_STATE_H */