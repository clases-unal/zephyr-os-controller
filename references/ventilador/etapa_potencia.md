# 1. Regulador de Voltaje L78S05CV (Según tu Figura 5)

- **Pin 1 ($V_I$ - Izquierda):**
  - Conectar a los +12V de la fuente de potencia.
  - Coloca un capacitor de 0.33 µF entre este pin y la Tierra de 12V.

- **Pin 2 (GND - Centro):**
  - Conectar a la Tierra (GND) de la fuente de 12V.

- **Pin 3 ($V_O$ - Derecha):**
  - Salida de +5V.
  - Coloca un capacitor de 0.1 µF entre este pin y la Tierra de 12V.
  - Esta línea alimentará el optoacoplador.

---

# 2. Optoacoplador 6N137 (Según tus Figuras 1 y 2)

> **(Este componente mantiene separadas las dos tierras del sistema)**

- **Pin 1:**
  - No se conecta a nada (NC).

- **Pin 2 (Anode):**
  - Conectar al pin de salida PWM (3.3V) de tu microcontrolador a través de una resistencia de 220 Ω.

- **Pin 3 (Cathode):**
  - Conectar a la Tierra (GND) del microcontrolador.
  - *(Tierra lógica, aislada de los 12V).*

- **Pin 4:**
  - No se conecta a nada (NC).

- **Pin 5 (GND):**
  - Conectar a la Tierra (GND) de la fuente de 12V.

- **Pin 6 ($V_O$):**
  - Conectar directamente al Pin 2 (INPUT) del driver TC4429.
  - Además, conecta una resistencia de pull-up ($R_L$ de 1 kΩ o 2.2 kΩ) desde este pin 6 hacia el pin 8 ($V_{CC}$).

- **Pin 7 ($V_E$):**
  - Conectar directamente al Pin 8 ($V_{CC}$) para mantener el chip habilitado.

- **Pin 8 ($V_{CC}$):**
  - Conectar al Pin 3 (+5V) del regulador L78S.
  - Coloca un capacitor de 0.1 µF entre este pin 8 y el pin 5 (GND).

---

# 3. Driver de MOSFET TC4429CPA (Según tu Figura 4-1)

- **Pin 1 ($V_{DD}$):**
  - Conectar a los +12V de la fuente de potencia.
  - Unir externamente con un cable al Pin 8.
  - Coloca un capacitor cerámico de 0.1 µF desde este pin 1 hacia el pin 4 (GND).

- **Pin 2 (INPUT):**
  - Conectar directamente al Pin 6 ($V_O$) del optoacoplador 6N137.

- **Pin 3:**
  - No se conecta a nada (NC).

- **Pin 4 (GND):**
  - Conectar a la Tierra (GND) de la fuente de 12V.
  - Unir externamente con un cable al Pin 5.

- **Pin 5 (GND):**
  - Conectar a la Tierra (GND) de la fuente de 12V.
  - Unir externamente con un cable al Pin 4.

- **Pin 6 (OUTPUT):**
  - Unir con un cable al Pin 7.
  - Esta unión (pines 6 y 7) va directo a la Compuerta (Gate / Pin 1) del MOSFET IRFZ44Z.

- **Pin 7 (OUTPUT):**
  - Unir al Pin 6.

- **Pin 8 ($V_{DD}$):**
  - Conectar a los +12V de la fuente de potencia.
  - Unir al Pin 1.
  - Coloca un capacitor cerámico de 0.1 µF desde este pin 8 hacia el pin 5 (GND).
  - También conecta aquí el capacitor electrolítico de 4.7 µF (o 10 µF) hacia la línea de Tierra.

---

# 4. MOSFET IRFZ44Z (Visto de frente, de izquierda a derecha)

- **Pin 1 (Gate):**
  - Conectar a la salida conjunta del driver (Pines 6 y 7 del TC4429).

- **Pin 2 (Drain):**
  - Conectar al cable negativo (negro) del ventilador de 12V.

- **Pin 3 (Source):**
  - Conectar a la Tierra (GND) de la fuente de 12V.

---

# 5. Ventilador de 12V y Diodo de Protección (Obligatorio)

- **Cable Positivo (Rojo):**
  - Conectar directamente a los +12V de la fuente de potencia.

- **Cable Negativo (Negro):**
  - Conectar al Pin 2 (Drain) del MOSFET.

- **Diodo de protección (ej. 1N4007):**
  - Conéctalo en paralelo con los cables del ventilador.
  - El extremo que tiene la franja blanca (Cátodo) se conecta al cable positivo (+12V).
  - El extremo completamente negro (Ánodo) se conecta al cable negativo (Drain del MOSFET).
