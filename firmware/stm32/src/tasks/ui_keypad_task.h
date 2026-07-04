/*
 * ui_keypad_task.h — Pantalla OLED + teclado matricial (modo edición de umbrales)
 */

#ifndef UI_KEYPAD_TASK_H
#define UI_KEYPAD_TASK_H

/*
 * Solicita entrar al modo de edición de umbrales (arranca en UI_MODE_EDIT_LOW).
 * Pensada para ser llamada desde otro hilo (power_status_manager.c, al detectar
 * una pulsación media del botón físico) — es segura de llamar desde cualquier
 * hilo porque solo marca una bandera de una sola escritura que ui_keypad_task
 * revisa y limpia en su propio bucle, nunca toca el display directamente.
 */
void ui_request_config_mode(void);

#endif /* UI_KEYPAD_TASK_H */
