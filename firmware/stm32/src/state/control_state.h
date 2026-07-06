/**
 * @file control_state.h
 * @brief Estado de control térmico activo (ControlState).
 *
 * @details
 * Protegido por control_mutex. Dominio: Todo lo relacionado al lazo de 
 * control térmico en sí (temperatura medida, salida PWM, umbral activo).
 */

#ifndef CONTROL_STATE_H
#define CONTROL_STATE_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Códigos de clasificación térmica (el orden importa para comparaciones >=).
 */
typedef enum {
	THRESHOLD_COLD = 0,
	THRESHOLD_LOW,
	THRESHOLD_MEDIUM,
	THRESHOLD_HIGH,
	THRESHOLD_CRITICAL,
} threshold_code_t;

/**
 * @brief Identifica la causa por la cual el sistema entró en estado CRITICAL.
 */
typedef enum {
	CRITICAL_CAUSE_NONE = 0,     /**< El sistema no está en estado crítico. */
	CRITICAL_CAUSE_OVERTEMP,     /**< Superado el límite threshold_critical. */
	CRITICAL_CAUSE_SENSOR_FAULT, /**< Falla técnica detectada en el sensor NTC. */
} critical_cause_t;

/**
 * @brief Estado global de control y actuación térmica.
 */
typedef struct {
	float current_temperature;       /**< Temperatura filtrada actual en °C. */
	uint8_t fan_pwm_duty_cycle;      /**< Porcentaje de ciclo de trabajo PWM (0-100). */
	threshold_code_t current_threshold_code; /**< Nivel térmico activo. */
	critical_cause_t critical_cause;         /**< Causa de la criticidad (si aplica). */
	uint32_t time_in_critical_overtemp_ms;   /**< Tiempo sostenido en CRITICAL por temperatura. */
	bool keep_alive_revoked;         /**< Estado de revocación de la planta de calor externa. */
} ControlState;

/**
 * @brief Inicializa la estructura de estado de control y su mutex.
 */
void control_state_init(void);

/**
 * @brief Obtiene una copia de solo lectura del estado de control actual.
 * @param out Puntero donde se copiarán los datos.
 */
void control_state_get(ControlState *out);

/**
 * @brief Actualiza el valor de temperatura del sistema de manera segura.
 * @param temperature_celsius Nueva lectura filtrada en grados Celsius.
 */
void control_state_set_temperature(float temperature_celsius);

/**
 * @brief Actualiza la consigna del ciclo de trabajo del ventilador.
 * @param duty_cycle Nuevo valor de trabajo (0-100%).
 */
void control_state_set_fan_duty(uint8_t duty_cycle);

/**
 * @brief Define el nivel térmico actual y si hay causa crítica activa.
 * @param code Nuevo código de umbral.
 * @param cause Causa de entrada a estado crítico (CRITICAL_CAUSE_NONE en situación normal).
 */
void control_state_set_threshold(threshold_code_t code, critical_cause_t cause);

/**
 * @brief Establece los milisegundos que el sistema lleva sostenido en sobretemperatura.
 * @param ms Tiempo medido en milisegundos.
 */
void control_state_set_time_in_critical(uint32_t ms);

/**
 * @brief Marca si la autorización externa del actuador de calor ha sido retirada.
 * @param revoked true si la revocación está activa.
 */
void control_state_set_keep_alive_revoked(bool revoked);

#endif /* CONTROL_STATE_H */