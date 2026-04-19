# IoT-FinalProyect

## 1. Visión del Proyecto
**Problema:** Dificultad para el monitoreo y control remoto de variables críticas (velocidad y dirección) en prototipos de robótica móvil de bajo costo.
**Usuarios:** Estudiantes de Ingeniería Informática y desarrolladores de sistemas embebidos.
**Solución:** Un vehículo a escala basado en el microcontrolador ESP32, controlado por una interfaz web, capaz de reportar telemetría (velocidad en RPM) mediante sensores de herradura HC-020K.

## 2. Arquitectura y Restricciones

### Arquitectura del Sistema
El sistema se basa en una arquitectura cliente-servidor donde el ESP32 actúa como punto de acceso (AP) o estación, sirviendo una interfaz HTML/JS. Los comandos de control se envían al puente H L298N para manejar los motores DC.

<div align="center">
  <img src="https://github.com/user-attachments/assets/601c8477-0472-4feb-b1f7-25917699dbcf" width="80%" alt="Diagrama de Arquitectura Smart Car">
  <p><i>Figura 1: Diagrama de bloques de la arquitectura del sistema</i></p>
</div>


### Presupuesto Económico
| Componente | Cantidad | Costo Unitario (COP) | Total (COP) |
| :--- | :---: | :--- | :--- |
| Kit Chasis (Estructura, 2 Motores, Ruedas, Rueda Loca, Portapilas x4) | 1 | $40,000 | $40,000 |
| Microcontrolador ESP32 DevKit V1 | 1 | $35,000 | $35,000 |
| Puente H L298N | 1 | $18,000 | $18,000 |
| Sensor de velocidad (MH-Sensor-Series) | 1 | $8,000 | $8,000 |
| Pilas (AA o Recargables) | 4 | $5,000 | $20,000 |
| **Total Invertido** | - | - | **$121,000** |

### Restricciones de Recursos
* **Hardware:** El ESP32 tiene pines limitados; se debe mapear cuidadosamente el Pinout para evitar conflictos con los pines de "boot" al usar el Puente H y los encoders.
* **Energía:** Los motores consumen picos de corriente altos. Se requiere un sistema de alimentación dual o regulado para evitar que el ESP32 se reinicie por caídas de tensión.
* **Software:** El uso de `delay()` está prohibido para no bloquear la recepción de paquetes Wi-Fi y la lectura de los pulsos del sensor.

## 3. Reporte del Spike Arquitectónico (Release 1)
**Incertidumbre Crítica:** ¿Es capaz el ESP32 de procesar interrupciones de alta frecuencia del HC-020K mientras gestiona el tráfico de red de la aplicación web?

**Resultados de la Investigación:**
1.  **Compatibilidad Mecánica:** Se validó que los discos encoders de 20 ranuras encajan perfectamente en el chasis adquirido.
2.  **Estrategia de Firmware:** Se determinó que la lectura del sensor se realizará mediante `attachInterrupt()` en flanco de subida. Para la comunicación, se utilizará la librería `ESPAsyncWebServer`, la cual permite manejar peticiones sin detener el bucle principal de ejecución.
3.  **Conclusión:** La arquitectura propuesta es viable. El ESP32 posee dos núcleos, lo que permitiría, en caso de necesidad, dedicar uno exclusivamente a la telemetría y otro a la conectividad.

## 4. Planeación Ágil (MVP)

### Features Principales
* **Must-Have (MVP):**
    * Control de dirección (Adelante, Atrás, Izquierda, Derecha).
    * Lectura y cálculo de RPM mediante interrupciones.
    * Interfaz web básica para visualización de datos.
* **Nice-to-Have:**
    * Control de velocidad mediante PWM.
    * Indicadores visuales (LEDs) de estado de conexión.
    * Modo de frenado automático ante pérdida de señal.

---
*Este proyecto es desarrollado para el curso de Internet de las cosas - Universidad de La Sabana, 2026.*
