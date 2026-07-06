/**
 * @file power_status_manager.h
 * @brief Interfaz para la gestión del pulsador físico y de estados de energía.
 */

#ifndef POWER_STATUS_MANAGER_H
#define POWER_STATUS_MANAGER_H

#include <stdbool.h>

/**
 * @brief Inicializa el gestor de estados de energía y el pulsador físico.
 *
 * Configura el pin GPIO asociado al botón de usuario y establece
 * las interrupciones requeridas.
 *
 * @return true si la configuración inicial del GPIO y su interrupción fue exitosa, false en caso contrario.
 */
bool power_status_manager_init(void);

#endif /* POWER_STATUS_MANAGER_H */