/**
 * @file esp32_comm_manager.h
 * @brief Interfaz para la gestión de comunicación con el ESP32 vía UART.
 */

#ifndef ESP32_COMM_MANAGER_H
#define ESP32_COMM_MANAGER_H

/**
 * @brief Notifica que el usuario ha confirmado un cambio de umbrales.
 *
 * Llamar cuando el usuario confirma un cambio de umbrales desde el teclado
 * (tecla 'D' en ui_keypad_task.c) para que el nuevo ConfigState se envíe al
 * ESP32 de inmediato, sin esperar a la próxima reconexión. 
 *
 * @note Es segura de llamar desde cualquier hilo — solo marca una bandera que 
 * el hilo de comunicación revisa en su propio bucle, no toca el UART directamente.
 */
void esp32_comm_manager_notify_config_changed(void);

#endif /* ESP32_COMM_MANAGER_H */