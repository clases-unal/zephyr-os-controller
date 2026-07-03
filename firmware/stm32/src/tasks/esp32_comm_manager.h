#ifndef ESP32_COMM_MANAGER_H
#define ESP32_COMM_MANAGER_H

/*
 * Llamar cuando el usuario confirma un cambio de umbrales desde el teclado
 * (tecla 'D' en ui_keypad_task.c) para que el nuevo ConfigState se envíe al
 * ESP32 de inmediato, sin esperar a la próxima reconexión. Segura de llamar
 * desde cualquier hilo — solo marca una bandera que el hilo de comunicación
 * revisa en su propio bucle, no toca el UART directamente.
 */
void esp32_comm_manager_notify_config_changed(void);

#endif
