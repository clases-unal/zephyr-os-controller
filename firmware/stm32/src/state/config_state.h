/**
 * @file config_state.h
 * @brief Parámetros editables por el usuario vía HMI (ConfigState).
 *
 * @details
 * Protegido por config_mutex. Contiene los umbrales de temperatura 
 * utilizados por cooling_manager para la toma de decisiones.
 * * MEJORA FUTURA: Los 4 umbrales de esta estructura son el caso de uso más 
 * obvio para persistencia en memoria no volátil (NVS).
 */

#ifndef CONFIG_STATE_H
#define CONFIG_STATE_H

#include <zephyr/kernel.h>

/**
 * @brief Estructura que define los umbrales de activación térmica.
 */
typedef struct {
	float threshold_low;      /**< Umbral térmico para nivel LOW. */
	float threshold_medium;   /**< Umbral térmico para nivel MEDIUM. */
	float threshold_high;     /**< Umbral térmico para nivel HIGH. */
	float threshold_critical; /**< Umbral de sobretemperatura para CRITICAL. */
} ConfigState;

/**
 * @brief Inicializa los umbrales por defecto y el mutex asociado.
 */
void config_state_init(void);

/**
 * @brief Obtiene una copia segura de los parámetros de configuración.
 * * @param out Puntero a la estructura donde se copiará el estado actual.
 */
void config_state_get(ConfigState *out);

/**
 * @brief Modifica todos los umbrales térmicos garantizando la exclusión mutua.
 * * @param low Nuevo valor para el umbral LOW.
 * @param medium Nuevo valor para el umbral MEDIUM.
 * @param high Nuevo valor para el umbral HIGH.
 * @param critical Nuevo valor para el umbral CRITICAL.
 */
void config_state_set_thresholds(float low, float medium, float high, float critical);

#endif /* CONFIG_STATE_H */