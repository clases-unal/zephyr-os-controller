/**
 * @file ui_keypad_task.h
 * @brief Pantalla OLED + teclado matricial (modo edición de umbrales).
 */

#ifndef UI_KEYPAD_TASK_H
#define UI_KEYPAD_TASK_H

/**
 * @brief Solicita entrar al modo de edición de umbrales en la interfaz de usuario.
 *
 * Arranca en UI_MODE_EDIT_LOW. Pensada para ser llamada desde otro hilo 
 * (por ejemplo, al detectar una pulsación media del botón físico).
 *
 * @note Es segura de llamar desde cualquier hilo porque solo marca una bandera 
 * de una sola escritura que ui_keypad_task revisa y limpia en su propio bucle, 
 * nunca toca el display directamente.
 */
void ui_request_config_mode(void);

#endif /* UI_KEYPAD_TASK_H */