/**
 * ============================================================
 *  CEREBRO ESP32 - Carro RC con BLE (Servidor GATT)
 * ============================================================
 *  Proyecto  : Carro a Control Remoto IoT
 *  MCU       : ESP32 (cualquier variante con BLE)
 *  IDE       : Arduino IDE 2.x
 *  Librería  : ESP32 BLE Arduino (incluida en el core de ESP32)
 *
 *  MODOS DE OPERACIÓN:
 *    - MODO_SIMULACION = true  → Sin hardware; datos ficticios por Serial.
 *    - MODO_SIMULACION = false → Lee sensor H0200K real y maneja puente H real.
 *
 *  PROTOCOLO BLE (GATT):
 *    Servicio Principal UUID : 4fafc201-1fb5-459e-8fcc-c5c9c331914b
 *    Característica Control  : beb5483e-36e1-4688-b7f5-ea07361b26a8  [WRITE]
 *    Característica Telemetría: 1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e  [NOTIFY]
 *
 *  FORMATO DE PAQUETES:
 *    Control   → [Byte0: Dirección] [Byte1: Velocidad 0-255]
 *    Telemetría → [Byte0: RPM_HIGH] [Byte1: RPM_LOW]  (uint16_t big-endian)
 *
 *  DIRECCIÓN (Byte 0):
 *    0x46 = 'F' = Adelante   |  0x42 = 'B' = Atrás
 *    0x4C = 'L' = Izquierda  |  0x52 = 'R' = Derecha
 *    0x53 = 'S' = Stop
 *
 *  PINES (ver sección PIN LAYOUT más abajo)
 * ============================================================
 */

// ──────────────────────────────────────────────────────────────
//  LIBRERÍAS
// ──────────────────────────────────────────────────────────────
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>   // Descriptor necesario para NOTIFY

// ──────────────────────────────────────────────────────────────
//  MODO DE OPERACIÓN  ← CAMBIA AQUÍ
// ──────────────────────────────────────────────────────────────
#define MODO_SIMULACION  false   // true = simulación | false = hardware real

// ──────────────────────────────────────────────────────────────
//  ╔══════════════════════════════════════╗
//  ║          PIN LAYOUT (ESP32)          ║
//  ╚══════════════════════════════════════╝
//
//  PUENTE H (L298N o L293D)
//  ┌────────────────┬──────────┬────────────────────────────────┐
//  │ Señal          │ Pin ESP32│ Descripción                    │
//  ├────────────────┼──────────┼────────────────────────────────┤
//  │ ENA (Motor A)  │  GPIO 25 │ PWM velocidad motor izquierdo  │
//  │ IN1 (Motor A)  │  GPIO 26 │ Dirección motor izquierdo      │
//  │ IN2 (Motor A)  │  GPIO 27 │ Dirección motor izquierdo      │
//  │ ENB (Motor B)  │  GPIO 14 │ PWM velocidad motor derecho    │
//  │ IN3 (Motor B)  │  GPIO 12 │ Dirección motor derecho        │
//  │ IN4 (Motor B)  │  GPIO 13 │ Dirección motor derecho        │
//  ├────────────────┼──────────┼────────────────────────────────┤
//  │ VCC Puente H   │  VIN/5V  │ Alimentación lógica puente H   │
//  │ GND            │  GND     │ Tierra común ESP32 + puente H  │
//  │ OUT_A+ / OUT_A-│  Motor L │ Terminales motor izquierdo     │
//  │ OUT_B+ / OUT_B-│  Motor R │ Terminales motor derecho       │
//  └────────────────┴──────────┴────────────────────────────────┘
//
//  SENSOR RPM H0200K (sensor de efecto Hall, salida digital)
//  ┌────────────────┬──────────┬────────────────────────────────┐
//  │ Señal          │ Pin ESP32│ Descripción                    │
//  ├────────────────┼──────────┼────────────────────────────────┤
//  │ VCC sensor     │  3.3V    │ Alimentación sensor            │
//  │ GND sensor     │  GND     │ Tierra sensor                  │
//  │ OUT (señal)    │  GPIO 34 │ Pulsos digitales (INPUT_PULLUP)│
//  └────────────────┴──────────┴────────────────────────────────┘
//
//  NOTAS DE CABLEADO:
//  • Los pines 34-39 del ESP32 son SOLO ENTRADA, ideales para sensores.
//  • Si usas L298N: VCC_Motor del puente conecta a tu batería (6-12V).
//  • Añade condensadores 100nF en paralelo a los motores contra EMI.
//  • Resistencia pull-up 10kΩ entre OUT del sensor y 3.3V si hay ruido.
// ──────────────────────────────────────────────────────────────

// Pines Puente H
const int PIN_ENA = 25;  // PWM Motor A (izquierdo)
const int PIN_IN1 = 26;
const int PIN_IN2 = 27;
const int PIN_ENB = 33;  // PWM Motor IZQUIERDO — pin separado
const int PIN_IN3 = 12;
const int PIN_IN4 = 14;

// Pin Sensor RPM H0200K
const int PIN_SENSOR_RPM = 34;

// ──────────────────────────────────────────────────────────────
//  UUIDs GATT
// ──────────────────────────────────────────────────────────────
#define UUID_SERVICIO        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define UUID_CHAR_CONTROL    "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define UUID_CHAR_TELEMETRIA "1c95d5e3-d8f7-413a-bf3d-7a2e5d7be87e"

// ──────────────────────────────────────────────────────────────
//  COMANDOS DE DIRECCIÓN (Byte 0)
// ──────────────────────────────────────────────────────────────
#define CMD_ADELANTE  0x46  // 'F'
#define CMD_ATRAS     0x42  // 'B'
#define CMD_IZQUIERDA 0x4C  // 'L'
#define CMD_DERECHA   0x52  // 'R'
#define CMD_STOP      0x53  // 'S'

// ──────────────────────────────────────────────────────────────
//  CANALES PWM (LEDC - ESP32)
// ──────────────────────────────────────────────────────────────
const int PWM_CANAL_A    = 0;
const int PWM_CANAL_B    = 1;
const int PWM_FRECUENCIA = 1000;  // Hz
const int PWM_RESOLUCION = 8;     // bits → 0-255

// ──────────────────────────────────────────────────────────────
//  WATCHDOG - TIEMPO MÁXIMO SIN COMANDO (ms)
// ──────────────────────────────────────────────────────────────
const unsigned long WATCHDOG_TIMEOUT_MS = 1500;

// ──────────────────────────────────────────────────────────────
//  INTERVALOS DE REFRESCO (ms)
// ──────────────────────────────────────────────────────────────
const unsigned long INTERVALO_TELEMETRIA_MS = 250;

// ──────────────────────────────────────────────────────────────
//  SENSOR RPM - VARIABLES DE INTERRUPCIÓN
// ──────────────────────────────────────────────────────────────
#if !MODO_SIMULACION
volatile unsigned long contadorPulsos = 0;   // Pulsos desde última medición
const int IMANES_POR_VUELTA = 2;             // Ajusta según el motor/sensor

// ISR - Contador de pulsos (en IRAM para mayor velocidad)
void IRAM_ATTR contarPulso() {
  contadorPulsos++;
}
#endif

// ──────────────────────────────────────────────────────────────
//  VARIABLES DE ESTADO GLOBALES
// ──────────────────────────────────────────────────────────────
BLEServer*          pServidor       = nullptr;
BLECharacteristic*  pCharControl    = nullptr;
BLECharacteristic*  pCharTelemetria = nullptr;

bool     dispositivoConectado = false;
bool     hayNuevoComando      = false;

// Estado motor
uint8_t  velocidadActual      = 0;     // 0-255 (valor PWM)
uint8_t  direccionActual      = CMD_STOP;

// Timers no bloqueantes
unsigned long ultimoComando_ms    = 0;
unsigned long ultimaTelemetria_ms = 0;

// RPM calculada
uint16_t rpmActual = 0;

// ──────────────────────────────────────────────────────────────
//  DECLARACIONES ADELANTADAS (forward declarations)
//  Necesarias porque las clases Callback se definen antes que
//  las funciones que invocan (regla de C++ estricto).
// ──────────────────────────────────────────────────────────────
void aplicarMotores();
String interpretarDireccion(uint8_t cmd);

// ──────────────────────────────────────────────────────────────
//  CALLBACK: CONEXIÓN / DESCONEXIÓN BLE
// ──────────────────────────────────────────────────────────────
class CallbackConexion : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    dispositivoConectado = true;
    ultimoComando_ms     = millis();  // Reset watchdog al conectar
    Serial.println();
    Serial.println("══════════════════════════════════════");
    Serial.println("  ✔ DISPOSITIVO CONECTADO por BLE");
    Serial.println("══════════════════════════════════════");
  }

  void onDisconnect(BLEServer* pServer) override {
    dispositivoConectado = false;
    // Seguridad: forzar parada al desconectar
    velocidadActual  = 0;
    direccionActual  = CMD_STOP;
    aplicarMotores();
    Serial.println();
    Serial.println("══════════════════════════════════════");
    Serial.println("  ✖ DISPOSITIVO DESCONECTADO");
    Serial.println("  → Motores detenidos por seguridad.");
    Serial.println("══════════════════════════════════════");
    // Re-advertise para permitir nueva conexión
    pServer->startAdvertising();
    Serial.println("  BLE re-advertising activo...");
  }
};

// ──────────────────────────────────────────────────────────────
//  CALLBACK: ESCRITURA EN CARACTERÍSTICA DE CONTROL
// ──────────────────────────────────────────────────────────────
class CallbackControl : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCaracteristica) override {
    // Obtener datos crudos
    String valorRaw = pCaracteristica->getValue();

    // Validar longitud estricta = 2 bytes
    if (valorRaw.length() != 2) {
      Serial.print("  ⚠ Paquete inválido. Se esperaban 2 bytes, se recibieron: ");
      Serial.println(valorRaw.length());
      return;
    }

    uint8_t byteDireccion = (uint8_t)valorRaw[0];
    uint8_t byteVelocidad = (uint8_t)valorRaw[1];

    // Actualizar estado
    direccionActual  = byteDireccion;
    velocidadActual  = byteVelocidad;
    ultimoComando_ms = millis();  // Reset watchdog
    hayNuevoComando  = true;

    // Imprimir comando recibido
    Serial.print("  → Comando recibido: ");
    Serial.print(interpretarDireccion(byteDireccion));
    Serial.print(" | Velocidad (PWM): ");
    Serial.print(byteVelocidad);
    Serial.print(" (0x");
    if (byteVelocidad < 16) Serial.print("0");
    Serial.print(byteVelocidad, HEX);
    Serial.println(")");

    // Aplicar a hardware (o simular)
    aplicarMotores();
  }
};

// ──────────────────────────────────────────────────────────────
//  FUNCIÓN: INTERPRETAR DIRECCIÓN
// ──────────────────────────────────────────────────────────────
String interpretarDireccion(uint8_t cmd) {
  switch (cmd) {
    case CMD_ADELANTE:  return "ADELANTE  [0x46]";
    case CMD_ATRAS:     return "ATRAS     [0x42]";
    case CMD_IZQUIERDA: return "IZQUIERDA [0x4C]";
    case CMD_DERECHA:   return "DERECHA   [0x52]";
    case CMD_STOP:      return "STOP      [0x53]";
    default:
      return "DESCONOCIDO [0x" + String(cmd, HEX) + "]";
  }
}

// ──────────────────────────────────────────────────────────────
//  FUNCIÓN: APLICAR MOTORES
//  En simulación solo imprime; en hardware real mueve el puente H.
// ──────────────────────────────────────────────────────────────
void aplicarMotores() {
#if MODO_SIMULACION
  // ── MODO SIMULACIÓN ──────────────────────────────────────────
  Serial.print("  [SIM] Puente H → ");
  switch (direccionActual) {
    case CMD_ADELANTE:
      Serial.print("IN1=HIGH, IN2=LOW, IN3=HIGH, IN4=LOW");
      break;
    case CMD_ATRAS:
      Serial.print("IN1=LOW, IN2=HIGH, IN3=LOW, IN4=HIGH");
      break;
    case CMD_IZQUIERDA:
      // Motor izquierdo atrás, motor derecho adelante → gira izquierda
      Serial.print("IN1=LOW, IN2=HIGH, IN3=HIGH, IN4=LOW");
      break;
    case CMD_DERECHA:
      // Motor izquierdo adelante, motor derecho atrás → gira derecha
      Serial.print("IN1=HIGH, IN2=LOW, IN3=LOW, IN4=HIGH");
      break;
    case CMD_STOP:
    default:
      Serial.print("IN1=LOW, IN2=LOW, IN3=LOW, IN4=LOW");
      break;
  }
  Serial.print(" | ENA=ENB=PWM:");
  Serial.println(velocidadActual);

#else
  // ── MODO HARDWARE REAL ───────────────────────────────────────
  switch (direccionActual) {
    case CMD_ADELANTE:
      digitalWrite(PIN_IN1, HIGH); digitalWrite(PIN_IN2, LOW);
      digitalWrite(PIN_IN3, HIGH); digitalWrite(PIN_IN4, LOW);
      break;
    case CMD_ATRAS:
      digitalWrite(PIN_IN1, LOW);  digitalWrite(PIN_IN2, HIGH);
      digitalWrite(PIN_IN3, LOW);  digitalWrite(PIN_IN4, HIGH);
      break;
    case CMD_IZQUIERDA:
      digitalWrite(PIN_IN1, HIGH); digitalWrite(PIN_IN2, LOW);  // Motor L adelante
      digitalWrite(PIN_IN3, LOW);  digitalWrite(PIN_IN4, HIGH); // Motor R atrás
      break;

    case CMD_DERECHA:
      digitalWrite(PIN_IN1, LOW);  digitalWrite(PIN_IN2, HIGH); // Motor L atrás
      digitalWrite(PIN_IN3, HIGH); digitalWrite(PIN_IN4, LOW);  // Motor R adelante
      break;
    case CMD_STOP:
    default:
      digitalWrite(PIN_IN1, LOW); digitalWrite(PIN_IN2, LOW);
      digitalWrite(PIN_IN3, LOW); digitalWrite(PIN_IN4, LOW);
      break;
  }
  // Aplicar velocidad PWM a ambos motores
  ledcWrite(PIN_ENA, velocidadActual);
  ledcWrite(PIN_ENB, velocidadActual);
#endif
}

// ──────────────────────────────────────────────────────────────
//  FUNCIÓN: LEER / SIMULAR RPM
// ──────────────────────────────────────────────────────────────
uint16_t leerRPM() {
#if MODO_SIMULACION
  // Simular RPM proporcional a la velocidad (más realista que aleatoria)
  if (velocidadActual == 0 || direccionActual == CMD_STOP) {
    return 0;
  }
  // Base proporcional al PWM + pequeña variación aleatoria
  uint16_t base = map(velocidadActual, 0, 255, 0, 500);
  int variacion = random(-20, 21);
  return (uint16_t)constrain((int)base + variacion, 0, 600);

#else
  // ── HARDWARE REAL: Sensor H0200K ────────────────────────────
  // Capturamos y reseteamos el contador de pulsos de forma atómica
  noInterrupts();
  unsigned long pulsos = contadorPulsos;
  contadorPulsos = 0;
  interrupts();

  // Fórmula: RPM = (pulsos / imanes) * (60000 / intervalo_ms)
  // intervalo_ms = INTERVALO_TELEMETRIA_MS (cada vez que llamamos)
  float rpm = ((float)pulsos / IMANES_POR_VUELTA) *
              (60000.0f / INTERVALO_TELEMETRIA_MS);
  return (uint16_t)constrain((int)rpm, 0, 65535);
#endif
}

// ──────────────────────────────────────────────────────────────
//  FUNCIÓN: NOTIFICAR TELEMETRÍA BLE
// ──────────────────────────────────────────────────────────────
void enviarTelemetria() {
  if (!dispositivoConectado) return;

  rpmActual = leerRPM();

  // Descomponer uint16_t en Byte Alto y Byte Bajo (big-endian)
  uint8_t payload[2];
  payload[0] = (rpmActual >> 8) & 0xFF;   // Byte alto
  payload[1] =  rpmActual       & 0xFF;   // Byte bajo

  pCharTelemetria->setValue(payload, 2);
  pCharTelemetria->notify();

  Serial.print("  [TEL] RPM enviados: ");
  Serial.print(rpmActual);
  Serial.print(" → Bytes [0x");
  if (payload[0] < 16) Serial.print("0");
  Serial.print(payload[0], HEX);
  Serial.print(", 0x");
  if (payload[1] < 16) Serial.print("0");
  Serial.print(payload[1], HEX);
  Serial.println("]");
}

// ──────────────────────────────────────────────────────────────
//  WATCHDOG: VERIFICAR PÉRDIDA DE SEÑAL
// ──────────────────────────────────────────────────────────────
void verificarWatchdog() {
  if (!dispositivoConectado) return;

  unsigned long ahora = millis();
  if ((ahora - ultimoComando_ms) > WATCHDOG_TIMEOUT_MS && velocidadActual != 0) {
    Serial.println();
    Serial.println("  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println("  !ALERTA! Pérdida de señal.");
    Serial.println("  Forzando detención de motores.");
    Serial.println("  !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println();
    velocidadActual  = 0;
    direccionActual  = CMD_STOP;
    aplicarMotores();
  }
}

// ══════════════════════════════════════════════════════════════
//  SETUP
// ══════════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);  // Pequeña espera para que el monitor serial se estabilice

  Serial.println();
  Serial.println("╔══════════════════════════════════════════╗");
  Serial.println("║   CEREBRO ESP32 - Carro RC BLE           ║");
#if MODO_SIMULACION
  Serial.println("║   MODO: *** SIMULACIÓN ***               ║");
#else
  Serial.println("║   MODO: *** HARDWARE REAL ***            ║");
#endif
  Serial.println("╚══════════════════════════════════════════╝");

  // ── CONFIGURAR PINES (solo en modo hardware) ──────────────────
#if !MODO_SIMULACION
  // Pines de dirección del puente H
  pinMode(PIN_IN1, OUTPUT); pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT); pinMode(PIN_IN4, OUTPUT);

  // Pines PWM - canal LEDC del ESP32
  ledcAttach(PIN_ENA, PWM_FRECUENCIA, PWM_RESOLUCION);
  ledcAttach(PIN_ENB, PWM_FRECUENCIA, PWM_RESOLUCION);

  // Estado inicial seguro
  aplicarMotores();

  // Sensor RPM - interrupción en flanco de bajada
  pinMode(PIN_SENSOR_RPM, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_SENSOR_RPM),
                  contarPulso, FALLING);

  Serial.println("  Pines de hardware configurados.");
#else
  Serial.println("  Pines de hardware OMITIDOS (simulación).");
#endif

  // ── INICIALIZAR BLE ──────────────────────────────────────────
  BLEDevice::init("ESP32-RC-Car");
  Serial.println("  BLEDevice inicializado como: ESP32-RC-Car");

  // Crear servidor BLE
  pServidor = BLEDevice::createServer();
  pServidor->setCallbacks(new CallbackConexion());

  // Crear servicio principal
  BLEService* pServicio = pServidor->createService(UUID_SERVICIO);

  // ── CARACTERÍSTICA DE CONTROL (WRITE) ─────────────────────────
  pCharControl = pServicio->createCharacteristic(
    UUID_CHAR_CONTROL,
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_WRITE_NR  // Write Without Response también
  );
  pCharControl->setCallbacks(new CallbackControl());

  // ── CARACTERÍSTICA DE TELEMETRÍA (NOTIFY) ────────────────────
  pCharTelemetria = pServicio->createCharacteristic(
    UUID_CHAR_TELEMETRIA,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  // BLE2902 es el descriptor estándar para habilitar notificaciones
  pCharTelemetria->addDescriptor(new BLE2902());

  // Iniciar servicio
  pServicio->start();
  Serial.println("  Servicio GATT iniciado.");
  Serial.println("    UUID Servicio   : " UUID_SERVICIO);
  Serial.println("    UUID Control    : " UUID_CHAR_CONTROL);
  Serial.println("    UUID Telemetría : " UUID_CHAR_TELEMETRIA);

  // ── ADVERTISING BLE ──────────────────────────────────────────
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(UUID_SERVICIO);
  pAdvertising->setScanResponse(true);
  // Intervalos de advertising para mejor compatibilidad con iOS/Android
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();

  Serial.println("  BLE Advertising activo. Esperando conexión...");
  Serial.println("───────────────────────────────────────────");

  // Inicializar timers
  ultimoComando_ms    = millis();
  ultimaTelemetria_ms = millis();
}

// ══════════════════════════════════════════════════════════════
//  LOOP PRINCIPAL (NO BLOQUEANTE)
// ══════════════════════════════════════════════════════════════
void loop() {
  unsigned long ahora = millis();

  // 1. WATCHDOG: verificar pérdida de señal cada ciclo
  verificarWatchdog();

  // 2. TELEMETRÍA: enviar RPM cada INTERVALO_TELEMETRIA_MS
  if ((ahora - ultimaTelemetria_ms) >= INTERVALO_TELEMETRIA_MS) {
    ultimaTelemetria_ms = ahora;
    enviarTelemetria();
  }

  // Sin delay() - el loop corre libre para máxima responsividad
}
