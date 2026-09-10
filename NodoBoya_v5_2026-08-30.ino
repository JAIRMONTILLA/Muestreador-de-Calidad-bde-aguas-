// ======================================================================
// MUESTREADOR DE CALIDAD DE AGUAS -- NODO 2 / BOYA (ESP32 #2)
// Semillero SENSORAMA - Universidad Libre
// Hecho por Ing. Jair E.
// ======================================================================
//
// *** VERSION 3 -- hardware real, segun el esquema de conexion confirmado ***
//
// Esta version reemplaza los supuestos de la v2 (sonda Atlas por I2C,
// ultrasonicos UART) por el hardware que SI esta en tu plano:
//   - Sonda de pH PH-4502C (analogica, con divisor 10k/15k de proteccion)
//   - Sensor de conductividad electrica (analogico)
//   - Sensor de turbidez/solidos (analogico)
//   - 2x DS18B20 (temperatura, bus OneWire con pull-up 4.7k a 3.3V)
//   - 3x HC-SR04 (ultrasonicos: columna total, calado boya, carro del
//     ascensor) con Trigger COMPARTIDO en un solo pin y Echo individual
//     por sensor (cada Echo con divisor 1k/1.8k, porque el HC-SR04
//     responde a 5V y el GPIO del ESP32 no tolera mas de 3.3V)
//   - A4988 alimentado con VDD=3.3V desde el ESP32 (logica compatible
//     directa, sin level shifter)
//   - 2 reles (bomba de purga / bomba de muestreo), con la polaridad de
//     disparo configurable mas abajo (RELAY_ACTIVO_EN_LOW)
//
// PENDIENTE DE VERIFICAR ANTES DE ENERGIZAR (ver el analisis de tu
// esquema para el detalle completo):
//   1. Que el sensor de conductividad y el de turbidez NO entreguen mas
//      de 3.3V a fondo de escala -- si tu modulo llega a 5V, hace falta
//      un divisor igual que el del pH antes de conectarlo al GPIO.
//   2. La polaridad real de disparo de los reles (RELAY_ACTIVO_EN_LOW).
//   3. Las constantes de calibracion de pH, conductividad y turbidez
//      (todas marcadas AJUSTAR -- son formulas lineales de partida, no
//      calibracion real).
//   4. T_PURGA_MS: calibrar con el caudal real de la bomba de purga.
//   5. Confirmar que el VMOT del A4988 este conectado al riel de 12V
//      directamente (no a traves de ningun GPIO), con el capacitor de
//      220uF lo mas cerca posible de sus pines VMOT/GND.
//
// El modo SIMULAR_SONDA sigue disponible para probar todo el ciclo sin
// la sonda fisica conectada.
// ======================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_idf_version.h>
#include <OneWire.h>
#include <DallasTemperature.h>   // libreria "DallasTemperature" (Miles Burton) -- instalar desde el Gestor de Librerias

// ---------- Modo de banco de pruebas ----------
#define SIMULAR_SONDA       true   // true = genera lecturas de sonda de ejemplo
#define SIN_BATERIA_FISICA  true   // true = banco de pruebas con fuente de laboratorio
                                    // (12V/5V/3.3V), sin bateria real conectada
#define RELAY_ACTIVO_EN_LOW true   // Modulo Songle/Tongling JQC-3FF-S-Z 1 canal:
                                    // casi siempre activo en LOW (IN=LOW cierra el
                                    // rele). Si al probar queda al reves, cambiar
                                    // a false -- no hay que tocar nada mas del codigo.

// ======================================================================
// PINES -- segun el esquema de conexion confirmado (ver
// Esquema_Conexion_Diagrama_Pines.md, seccion Nodo 2)
// ======================================================================
// Ascensor GT2 (NEMA17 + A4988). VDD del driver = 3.3V desde el ESP32.
#define PIN_STEP           4    // D4
#define PIN_DIR            16   // RX2 -- HIGH=subir / LOW=bajar (verificar sentido real)
#define PIN_ENABLE         17   // TX2 -- LOW=activo / HIGH=motor libre
#define PIN_ENDSTOP_HOME   33   // D33, pull-up interno
#define PIN_ENDSTOP_MAX    32   // D32, pull-up interno

#define PASOS_POR_CM     800   // AJUSTAR: recalcular con el diametro real de la polea GT2
#define VEL_HOMING_US   1200   // us/semipulso -- homing lento
#define VEL_MOVER_US     700   // us/semipulso -- velocidad nominal
#define PROF_MIN_CM       10
#define PROF_MAX_CM       90   // recorrido de demo/banco de pruebas (AJUSTAR para campo)
#define PORCENTAJE_PROF_AUTO 0.60f  // P1/P3: profundidad = 60% de columna_total_cm
#define TIMEOUT_HOMING_MS  45000UL
#define TIMEOUT_MOVER_MS   90000UL

// Dos bombas peristalticas por rele (sin valvula de 3 vias por ahora):
// Relay 1 = bomba de purga por barrido (camara), Relay 2 = bomba de
// muestreo (llena el recipiente de 1 L). Ver RELAY_ACTIVO_EN_LOW arriba.
#define PIN_BOMBA_PURGA     18   // D18 -- Relay 1
#define PIN_BOMBA_MUESTREO  19   // D19 -- Relay 2

// ---- Sonda de pH PH-4502C (analogica) ----
// Divisor de proteccion 10k/15k en la linea PO -> D35 (factor 0.6).
#define PIN_PH  35   // D35, input-only, ADC1
#define DIVISOR_PH 0.6f   // V_real_sensor = V_leido_ESP32 / DIVISOR_PH
// AJUSTAR: calibrar con soluciones buffer pH 4 y pH 7 (2 puntos), formula
// lineal ph = PH_PENDIENTE * voltajeReal + PH_INTERCEPTO
#define PH_PENDIENTE   -5.70f
#define PH_INTERCEPTO  21.34f

// ---- Sensor de conductividad electrica (analogico) ----
// *** VERIFICAR con multimetro que este modulo no supere 3.3V a fondo de
// escala antes de conectarlo -- si llega a 5V, hace falta un divisor
// igual que el del pH antes de usar este pin. ***
#define PIN_CONDUCTIVIDAD 34   // D34, input-only, ADC1
#define COND_ADC_A_US_CM  350.0f  // AJUSTAR: calibrar con solucion patron conocida

// ---- Sensor de turbidez / solidos (analogico) ----
// *** Mismo aviso que la conductividad: verificar el voltaje real antes
// de conectar. ***
#define PIN_TURBIDEZ  39   // VN, input-only, ADC1
#define TURBIDEZ_ADC_A_NTU 400.0f  // AJUSTAR: calibrar con patrones turbidimetricos

// ---- 2x DS18B20 (temperatura, bus OneWire) ----
// Pull-up 4.7k a 3.3V en el bus (ya esta en el esquema).
#define PIN_ONEWIRE_TEMP  25   // D25

// Bateria (para cuando se conecte una real; hoy no aplica, ver SIN_BATERIA_FISICA)
#define PIN_BATERIA_ADC 36   // VP -- AJUSTAR si se conecta una bateria real
#define DIVISOR_BATERIA  4.0f // AJUSTAR: V_bat_real = V_leido * DIVISOR_BATERIA

// ---- 3x HC-SR04 (ultrasonicos) ----
// Trigger COMPARTIDO en un solo pin (D26); cada sensor tiene su propio
// Echo, con divisor 1k/1.8k de proteccion (HC-SR04 responde a 5V).
// US1: profundidad total del cuerpo de agua ("pecho del rio")
// US2: calado de la boya (que tan hundida esta)
// US3: posicion del carro del ascensor (referencia cruzada con la sonda)
#define PIN_TRIG_COMUN  26   // D26 -- comun a los 3 sensores
#define PIN_ECHO_US1    13   // D13 -- columna total
#define PIN_ECHO_US2    14   // D14 -- calado boya
#define PIN_ECHO_US3    27   // D27 -- carro ascensor
#define HCSR04_TIMEOUT_US 30000UL   // ~5 m de alcance maximo
#define HCSR04_PAUSA_ENTRE_MS 15    // pausa entre sensores para evitar cruce de ecos

// LED de estado
#define PIN_LED_BOYA 2

// Duraciones del ciclo
// T_PURGA_MS = tiempo para que la bomba de purga mueva 3x el volumen del
// recipiente de 1 L. AJUSTAR con el caudal real medido de tu bomba
// (ejemplo: si la bomba mueve 60 ml/min, 3 L requieren 3000 ms * 60 = ~180 s;
// el valor de abajo es solo un punto de partida, mide el caudal real).
#define T_PURGA_MS        60000UL
#define T_MUESTREO_MS     20000UL   // tiempo para llenar el recipiente de 1 L
#define T_ESTABILIZAR_MS   2000UL
#define INTERVALO_MEDICION_COLUMNA_MS 700UL  // cada cuanto se envian las 3 lecturas ultrasonicas

// ======================================================================
// PROTOCOLOS Y ERRORES -- COPIA EXACTA del Nodo Tierra (no modificar)
// ======================================================================
enum Protocolo : uint8_t {
  PROTOCOLO_P1_AUTOMATICO   = 1,
  PROTOCOLO_P2_CONFIGURABLE = 2,
  PROTOCOLO_P3_REACTIVO     = 3,
};

#define ERR_BATERIA_BAJA     (1 << 0)
#define ERR_SENSOR_PH        (1 << 1)
#define ERR_SENSOR_OD        (1 << 2)
#define ERR_SENSOR_COND      (1 << 3)
#define ERR_SENSOR_TURBIDEZ  (1 << 4)
#define ERR_ASCENSOR_ATASCO  (1 << 5)
#define ERR_ESPNOW_PERDIDA   (1 << 6)

// ---- Bitmask de estado_dispositivos: 1=OK/conectado, 0=falla o no verificado ----
#define BIT_ASCENSOR_OK        (1 << 0)
#define BIT_BOMBA_PURGA_OK     (1 << 1)
#define BIT_BOMBA_MUESTREO_OK  (1 << 2)
#define BIT_SENSOR_PH_OK       (1 << 3)
#define BIT_SENSOR_COND_OK     (1 << 4)
#define BIT_SENSOR_TURBIDEZ_OK (1 << 5)
#define BIT_SENSOR_TEMP_OK     (1 << 6)
#define BIT_US1_OK             (1 << 7)   // columna total
#define BIT_US2_OK             (1 << 8)   // calado boya
#define BIT_US3_OK             (1 << 9)   // posicion carro

// ---------- Telemetria que envia la Boya al Nodo Tierra ----------
// IDENTICO byte a byte a NodoTierra_TodoEnUno.ino -- NO CAMBIAR sin
// cambiar tambien el Nodo Tierra, o el ESP-NOW deja de funcionar.
typedef struct __attribute__((packed)) {
  uint32_t timestamp;
  float    latitud;
  float    longitud;
  float    profundidad_cm;
  float    ph;
  float    conductividad_us;
  float    oxigeno_disuelto;
  float    turbidez_ntu;
  float    temperatura_c;
  uint8_t  bateria_pct;
  uint8_t  protocolo_activo;
  uint8_t  muestras_ciclo;
  uint16_t error_flags;
  uint16_t paquetes_perdidos;
  // ---- Campos agregados en v2 (agregados AL FINAL para no romper el
  //      orden de los campos anteriores; deben coincidir con el mismo
  //      orden en NodoTierra_TodoEnUno.ino) ----
  float    columna_total_cm;    // US1: profundidad total del cuerpo de agua
  float    calado_boya_cm;      // US2: calado de la boya
  float    pos_carro_cm;        // US3: posicion del carro del ascensor
  uint16_t estado_dispositivos; // bitmask BIT_* -- ver mas abajo
  // ---- Campos v3 (trazado del lecho / plano cartesiano) ----
  float    gps_avance_m;     // avance horizontal (metros) -- simulado hasta
                              // que se instale el modulo GPS real
  uint8_t  estado_fsm;       // fase actual del ciclo (mismos valores que EstadoBoya)
} DatosBoya_t;

// ---------- Comando que el Nodo Tierra envia a la Boya ----------
typedef struct __attribute__((packed)) {
  uint8_t  comando;
  uint8_t  protocolo_nuevo;
  uint8_t  num_puntos_prof;
  uint16_t profundidades_cm[5];
  uint8_t  muestras_por_ciclo;
} ComandoTierra_t;

// MAC del Nodo Tierra (confirmada: f0:24:f9:43:5d:a4, tomada de tu
// archivo NodoTierra_TodoEnUno.ino)
static uint8_t MAC_NODO_TIERRA[6] = {0xF0, 0x24, 0xF9, 0x43, 0x5D, 0xA4};
// Referencia: esta Boya deberia tener MAC 38:18:2b:8a:a8:88 segun tu
// comentario en el sketch anterior -- confirmalo con WiFi.macAddress().

// ======================================================================
// ESTADOS DE LA FSM
// ======================================================================
enum EstadoBoya : uint8_t {
  FSM_MEDIR_COLUMNA = 0,   // estado "de reposo": mide y transmite los 3
                           // sensores ultrasonicos en vivo, esperando
                           // que el Nodo 1 elija un protocolo
  FSM_BAJAR,
  FSM_ESTABILIZAR,
  FSM_LEER_SONDA,
  FSM_PURGAR,
  FSM_LLENAR_RECIPIENTE,
  FSM_SUBIR_HOME,
  FSM_REGISTRAR_ENVIAR,
  FSM_ERROR,
};
static const char* NOMBRE_ESTADO[] = {
  "MEDIR_COLUMNA", "BAJAR", "ESTABILIZAR", "LEER_SONDA", "PURGAR",
  "LLENAR_RECIPIENTE", "SUBIR_HOME", "REGISTRAR_ENVIAR", "ERROR"
};

// ======================================================================
// VARIABLES GLOBALES
// ======================================================================
EstadoBoya g_estado = FSM_MEDIR_COLUMNA;
DatosBoya_t g_datos = {};

uint8_t  g_protocoloActivo   = PROTOCOLO_P1_AUTOMATICO;
uint8_t  g_numPuntosProf     = 1;
uint16_t g_profundidadesCm[5] = {60, 0, 0, 0, 0};
uint8_t  g_muestrasPorCiclo  = 1;

uint16_t g_paquetesPerdidos  = 0;
uint32_t g_ultimoCicloMillis = 0;
uint32_t g_ultimaMedicionColumna = 0;
#define INTERVALO_P1_MS (60UL * 60UL * 1000UL)   // P1: cada hora

// --- Comunicacion con Tierra (patron seguro: la interrupcion SOLO copia
//     bytes y levanta una bandera; la logica real corre en loop()) ---
static volatile bool comandoNuevo = false;
static ComandoTierra_t comandoRecibido;

// ======================================================================
// ESP-NOW
// ======================================================================
static void procesarComandoRecibido(const uint8_t* data, int len) {
  if (len == sizeof(ComandoTierra_t)) {
    memcpy((void*)&comandoRecibido, data, sizeof(ComandoTierra_t));
    comandoNuevo = true;
  }
}

#if ESP_IDF_VERSION_MAJOR >= 5
void IRAM_ATTR onComandoRecibido(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  procesarComandoRecibido(data, len);
}
#else
void IRAM_ATTR onComandoRecibido(const uint8_t* mac, const uint8_t* data, int len) {
  procesarComandoRecibido(data, len);
}
#endif

// Confirmacion real de entrega (esto es lo que faltaba en el firmware
// completo -- el sketch de prueba minima si lo tenia, por eso ahi si se
// veia "ENTREGADO OK"). ESP_NOW_SEND_SUCCESS solo confirma que el paquete
// salio del radio Wi-Fi de esta placa; no garantiza que el otro extremo
// lo haya procesado, pero ya es mucha mas informacion que no tener nada.
#if ESP_IDF_VERSION_MAJOR >= 5
void onEspNowEnviado(const wifi_tx_info_t* info, esp_now_send_status_t status) {
#else
void onEspNowEnviado(const uint8_t* mac, esp_now_send_status_t status) {
#endif
  Serial.println(status == ESP_NOW_SEND_SUCCESS
                    ? "  [ESP-NOW] entregado OK"
                    : "  [ESP-NOW] FALLO el envio");
}

void espnow_iniciar() {
  WiFi.mode(WIFI_STA);
  delay(100);   // margen para que el radio Wi-Fi termine de levantar antes de leer la MAC
  Serial.print("MAC de esta Boya: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ERROR: no se pudo iniciar ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(onComandoRecibido);
  esp_now_register_send_cb(onEspNowEnviado);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, MAC_NODO_TIERRA, 6);
  peer.channel = 0;
  peer.encrypt = false;
  if (!esp_now_is_peer_exist(MAC_NODO_TIERRA)) {
    esp_now_add_peer(&peer);
  }
  Serial.println("ESP-NOW OK.");
}

bool espnow_enviar_datos(const DatosBoya_t& d) {
  Serial.println("[ESP-NOW] enviando telemetria...");
  esp_err_t r = esp_now_send(MAC_NODO_TIERRA, (const uint8_t*)&d, sizeof(d));
  if (r != ESP_OK) {
    Serial.printf("  [ESP-NOW] esp_now_send() devolvio error inmediato: %d\n", r);
  }
  return r == ESP_OK;
}

// ======================================================================
// ASCENSOR (NEMA17 + A4988) -- logica tomada de tu sketch anterior,
// solo con los pines/constantes marcados como AJUSTAR arriba
// ======================================================================
void ascensor_iniciar() {
  pinMode(PIN_STEP,   OUTPUT); digitalWrite(PIN_STEP, LOW);
  pinMode(PIN_DIR,    OUTPUT);
  pinMode(PIN_ENABLE, OUTPUT); digitalWrite(PIN_ENABLE, HIGH); // libre
  pinMode(PIN_ENDSTOP_HOME, INPUT_PULLUP);
  pinMode(PIN_ENDSTOP_MAX,  INPUT_PULLUP);   // pull-up interno, segun el esquema
}

bool ascensor_homing() {
  Serial.println("Ascensor: HOMING...");
  digitalWrite(PIN_DIR, HIGH);   // subir
  digitalWrite(PIN_ENABLE, LOW);

  uint32_t t0 = millis();
  while (digitalRead(PIN_ENDSTOP_HOME) == HIGH) {
    digitalWrite(PIN_STEP, HIGH); delayMicroseconds(VEL_HOMING_US);
    digitalWrite(PIN_STEP, LOW);  delayMicroseconds(VEL_HOMING_US);
    yield();   // *** CRITICO: sin esto, un bucle largo (hasta 45 s) sin
               // ceder el CPU dispara el Task Watchdog del ESP32 y la
               // placa se reinicia sola a mitad del homing -- si el
               // endstop nunca esta conectado o nunca se activa, esto
               // provocaba un reinicio en bucle que impedia llegar a la
               // parte del codigo que transmite por ESP-NOW.
    if (millis() - t0 > TIMEOUT_HOMING_MS) {
      digitalWrite(PIN_ENABLE, HIGH);
      Serial.println("TIMEOUT en HOMING -- revisar endstop superior");
      return false;
    }
  }
  for (int i = 0; i < 20; i++) {   // asentar contra el tope
    digitalWrite(PIN_STEP, HIGH); delayMicroseconds(VEL_HOMING_US);
    digitalWrite(PIN_STEP, LOW);  delayMicroseconds(VEL_HOMING_US);
    yield();
  }
  digitalWrite(PIN_ENABLE, HIGH);
  Serial.println("Ascensor: HOMING OK");
  return true;
}

bool ascensor_moverCm(float cm, bool bajar) {
  long pasos = (long)(cm * PASOS_POR_CM);
  digitalWrite(PIN_DIR, bajar ? LOW : HIGH);
  digitalWrite(PIN_ENABLE, LOW);

  uint32_t t0 = millis();
  for (long i = 0; i < pasos; i++) {
    if (!bajar && digitalRead(PIN_ENDSTOP_HOME) == LOW) break;
    if (bajar  && digitalRead(PIN_ENDSTOP_MAX)  == LOW) {
      Serial.println("ENDSTOP FONDO activado -- deteniendo");
      digitalWrite(PIN_ENABLE, HIGH);
      return false;
    }
    digitalWrite(PIN_STEP, HIGH); delayMicroseconds(VEL_MOVER_US);
    digitalWrite(PIN_STEP, LOW);  delayMicroseconds(VEL_MOVER_US);
    yield();   // mismo motivo que en ascensor_homing()
    if (millis() - t0 > TIMEOUT_MOVER_MS) {
      digitalWrite(PIN_ENABLE, HIGH);
      Serial.println("TIMEOUT moviendo el ascensor");
      return false;
    }
  }
  digitalWrite(PIN_ENABLE, HIGH);
  return true;
}

// ======================================================================
// HIDRAULICA -- 2 bombas por rele, SIN valvula de 3 vias.
// Purga por barrido: se corre la bomba de purga sola (sin enrutar nada)
// el tiempo necesario para desplazar 3x el volumen del recipiente.
// Luego la bomba de muestreo llena el recipiente de 1 L.
// La polaridad de disparo del rele se controla con RELAY_ACTIVO_EN_LOW.
// ======================================================================
inline void rele_escribir(uint8_t pin, bool encender) {
  bool nivel = RELAY_ACTIVO_EN_LOW ? !encender : encender;
  digitalWrite(pin, nivel ? HIGH : LOW);
}

void hidraulica_iniciar() {
  pinMode(PIN_BOMBA_PURGA, OUTPUT);    rele_escribir(PIN_BOMBA_PURGA, false);
  pinMode(PIN_BOMBA_MUESTREO, OUTPUT); rele_escribir(PIN_BOMBA_MUESTREO, false);
}

bool hidraulica_purgar() {
  Serial.println("Hidraulica: PURGA POR BARRIDO (3x volumen del recipiente)");
  rele_escribir(PIN_BOMBA_PURGA, true);
  delay(T_PURGA_MS);
  rele_escribir(PIN_BOMBA_PURGA, false);
  delay(300);
  return true;   // sin sensor de flujo todavia -- no hay forma de verificar falla real
}

bool hidraulica_llenarRecipiente() {
  Serial.println("Hidraulica: LLENANDO RECIPIENTE (1 L)");
  rele_escribir(PIN_BOMBA_MUESTREO, true);
  delay(T_MUESTREO_MS);
  rele_escribir(PIN_BOMBA_MUESTREO, false);
  delay(T_ESTABILIZAR_MS);   // esperar a que se disipe ruido electrico antes de leer ADC
  return true;
}

// ======================================================================
// SENSORES ANALOGICOS: pH (PH-4502C), conductividad, turbidez
// + 2x DS18B20 (temperatura, OneWire)
// ======================================================================
OneWire busOneWire(PIN_ONEWIRE_TEMP);
DallasTemperature sensoresTemp(&busOneWire);

void sensores_iniciar() {
  analogSetAttenuation(ADC_11db);   // permite leer hasta ~3.3V en los ADC1
  sensoresTemp.begin();
  Serial.printf("DS18B20 detectados en el bus: %d (se esperaban 2)\n",
                sensoresTemp.getDeviceCount());
}

// pH-4502C con divisor de proteccion 10k/15k (factor DIVISOR_PH=0.6).
// AJUSTAR PH_PENDIENTE/PH_INTERCEPTO con 2 puntos de calibracion real
// (buffers pH 4 y pH 7).
bool ph_leer(float &valorPh) {
  int raw = analogRead(PIN_PH);
  float vLeido = raw * (3.3f / 4095.0f);
  float vReal = vLeido / DIVISOR_PH;   // deshace el divisor de proteccion
  valorPh = PH_PENDIENTE * vReal + PH_INTERCEPTO;
  return (raw > 0 && raw < 4095);   // fuera de rango = probable desconexion
}

// AJUSTAR: formula lineal de partida -- calibrar con solucion patron real.
bool conductividad_leer(float &valorUsCm) {
  int raw = analogRead(PIN_CONDUCTIVIDAD);
  float v = raw * (3.3f / 4095.0f);
  valorUsCm = v * COND_ADC_A_US_CM;
  return (raw > 0 && raw < 4095);
}

// AJUSTAR: formula lineal de partida -- calibrar con patrones turbidimetricos.
bool turbidez_leer(float &valorNtu) {
  int raw = analogRead(PIN_TURBIDEZ);
  float v = raw * (3.3f / 4095.0f);
  float ntu = (3.3f - v) * TURBIDEZ_ADC_A_NTU;
  valorNtu = constrain(ntu, 0.0f, 3000.0f);
  return (raw > 0 && raw < 4095);
}

// Promedia los 2 DS18B20 del bus OneWire (si solo responde uno, usa ese).
bool temperatura_leer(float &valorC) {
  sensoresTemp.requestTemperatures();
  float t0 = sensoresTemp.getTempCByIndex(0);
  float t1 = sensoresTemp.getTempCByIndex(1);
  bool ok0 = (t0 != DEVICE_DISCONNECTED_C);
  bool ok1 = (t1 != DEVICE_DISCONNECTED_C);
  if (ok0 && ok1) valorC = (t0 + t1) / 2.0f;
  else if (ok0)   valorC = t0;
  else if (ok1)   valorC = t1;
  else             { valorC = 25.0f; return false; }
  return true;
}

// Lee los 4 parametros activos (pH, conductividad, turbidez, temperatura).
// Si SIMULAR_SONDA esta activo, genera valores de ejemplo (para probar el
// resto del sistema sin los sensores fisicos conectados). El oxigeno
// disuelto (od) no esta instalado por ahora, siempre se reporta en 0.
// bitsOk acumula los BIT_SENSOR_*_OK de los sensores que si respondieron.
uint16_t sonda_leerTodo(float &ph, float &ec, float &od, float &tempC, float &ntu, uint16_t &bitsOk) {
  uint16_t errores = 0;
  od = 0.0f;   // sensor de OD no instalado todavia

#if SIMULAR_SONDA
  float t = millis() / 1000.0f;
  ph    = 7.2f + 0.3f * sin(t / 11.0f);
  ec    = 350.0f + 20.0f * sin(t / 13.0f);
  tempC = 24.0f + 1.5f * sin(t / 17.0f);
  ntu   = 4.0f + 1.0f * sin(t / 7.0f);
  bitsOk |= BIT_SENSOR_PH_OK | BIT_SENSOR_COND_OK | BIT_SENSOR_TEMP_OK | BIT_SENSOR_TURBIDEZ_OK;
  Serial.println("SIMULAR_SONDA activo -- valores de ejemplo, no reales");
#else
  if (ph_leer(ph))              bitsOk |= BIT_SENSOR_PH_OK;
  else { errores |= ERR_SENSOR_PH;   ph = 0; }

  if (conductividad_leer(ec))   bitsOk |= BIT_SENSOR_COND_OK;
  else { errores |= ERR_SENSOR_COND; ec = 0; }

  if (temperatura_leer(tempC))  bitsOk |= BIT_SENSOR_TEMP_OK;

  if (turbidez_leer(ntu))       bitsOk |= BIT_SENSOR_TURBIDEZ_OK;
  else errores |= ERR_SENSOR_TURBIDEZ;
#endif

  return errores;
}

// ======================================================================
// BATERIA
// ======================================================================
uint8_t bateria_leerPct() {
#if SIN_BATERIA_FISICA
  return 100;   // banco de pruebas con fuente de laboratorio, sin bateria real
#else
  int raw = analogRead(PIN_BATERIA_ADC);
  float vBat = raw * (3.3f / 4095.0f) * DIVISOR_BATERIA;
  // AJUSTAR segun tu pack LiPo 3S real (aprox 9.0V vacio -- 12.6V lleno)
  int pct = (int)((vBat - 9.0f) * 100.0f / (12.6f - 9.0f));
  return (uint8_t)constrain(pct, 0, 100);
#endif
}

// ======================================================================
// SENSORES ULTRASONICOS US1/US2/US3 (HC-SR04, trigger/echo)
// Trigger COMPARTIDO en un solo pin; cada sensor tiene su propio Echo.
// Se disparan uno a la vez (nunca simultaneo) para evitar que el eco de
// un sensor se cruce con el pulso de otro.
// ======================================================================
void ultrasonicos_iniciar() {
  pinMode(PIN_TRIG_COMUN, OUTPUT);
  digitalWrite(PIN_TRIG_COMUN, LOW);
  pinMode(PIN_ECHO_US1, INPUT);
  pinMode(PIN_ECHO_US2, INPUT);
  pinMode(PIN_ECHO_US3, INPUT);
}

// Dispara el trigger comun y mide el tiempo de eco de UN sensor puntual.
// pulseIn() ya bloquea hasta que el eco de ESE sensor vuelve (o hace
// timeout), asi que no hace falta un delay adicional para "esperar" el
// eco -- pero si conviene una pequena pausa despues, para que el eco
// anterior se apague del todo antes de disparar el siguiente sensor.
bool hcsr04_leerCm(uint8_t pinEcho, float &cm) {
  digitalWrite(PIN_TRIG_COMUN, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG_COMUN, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG_COMUN, LOW);

  unsigned long duracion = pulseIn(pinEcho, HIGH, HCSR04_TIMEOUT_US);
  delay(HCSR04_PAUSA_ENTRE_MS);   // deja que el eco se disipe antes del siguiente sensor

  if (duracion == 0) return false;   // timeout, sin eco (fuera de rango o desconectado)
  cm = duracion / 58.0f;             // formula estandar del HC-SR04
  return true;
}

// ======================================================================
// UTILIDAD LED
// ======================================================================
// ======================================================================
// GPS -- simulado hasta que se instale el modulo real. Genera un avance
// horizontal con tendencia a crecer (como si la Boya se desplazara a lo
// largo de un transecto), con pasos aleatorios para que se vea vivo en
// el plano cartesiano del Nodo 1 mientras se prueba sin hardware GPS.
// ======================================================================
float gps_simular_avance_m() {
  static float avance = 0.0f;
  static unsigned long ultimoPaso = 0;
  if (millis() - ultimoPaso > 500) {
    ultimoPaso = millis();
    avance += ((float)random(-30, 61)) / 100.0f;   // -0.30 a +0.60 m, sesgado a avanzar
    if (avance < 0) avance = 0;
  }
  return avance;
}

void parpadear(int n, int ms) {
  for (int i = 0; i < n; i++) {
    digitalWrite(PIN_LED_BOYA, HIGH); delay(ms);
    digitalWrite(PIN_LED_BOYA, LOW);  delay(ms);
  }
}

// ======================================================================
// APLICAR COMANDOS RECIBIDOS DEL NODO TIERRA
// ======================================================================
void aplicarComando(const ComandoTierra_t& cmd) {
  if (cmd.comando == 1) {   // cambiar protocolo
    g_protocoloActivo = cmd.protocolo_nuevo;
    Serial.printf("Comando: nuevo protocolo=%d\n", g_protocoloActivo);

    if (g_protocoloActivo == PROTOCOLO_P2_CONFIGURABLE) {
      g_numPuntosProf = constrain(cmd.num_puntos_prof, 1, 5);
      for (int i = 0; i < g_numPuntosProf; i++) g_profundidadesCm[i] = cmd.profundidades_cm[i];
      g_muestrasPorCiclo = constrain(cmd.muestras_por_ciclo, 1, 5);
    }
    // P1 y P3 usan la profundidad automatica (60% de columna_total_cm,
    // ver PORCENTAJE_PROF_AUTO); P3 ademas necesitaria umbrales de disparo
    // que hoy no viajan en ComandoTierra_t -- pendiente si se quiere
    // ese detalle fino.
    if (g_estado == FSM_MEDIR_COLUMNA) {
      g_estado = FSM_BAJAR;   // el Nodo 1 ya eligio protocolo -> arrancar el ciclo
    }
  }
  // comando==2 (iniciar ciclo manual) -- mismo efecto: forzar un ciclo
  if (cmd.comando == 2 && g_estado == FSM_MEDIR_COLUMNA) {
    g_estado = FSM_BAJAR;
  }
}

// ======================================================================
// FSM PRINCIPAL
// MedirColumna (en vivo, esperando protocolo) -> Bajar -> Estabilizar ->
// Leer sonda -> Purgar (barrido) -> Llenar recipiente -> Subir HOME ->
// Registrar y enviar -> vuelve a MedirColumna (monitoreo continuo)
// ======================================================================
void fsm_ejecutar() {
  static EstadoBoya estadoAnterior = (EstadoBoya)255;
  if (g_estado != estadoAnterior) {
    const char* nombreAnterior = (estadoAnterior == (EstadoBoya)255) ? "INICIO" : NOMBRE_ESTADO[estadoAnterior];
    Serial.printf("FSM: %s -> %s\n", nombreAnterior, NOMBRE_ESTADO[g_estado]);
    estadoAnterior = g_estado;

    // Aviso inmediato de cambio de fase, para que el Nodo 1 muestre en
    // vivo que actuador esta activo (ascensor bajando, sonda midiendo,
    // purgando, etc.) sin esperar a que termine toda la fase.
    g_datos.estado_fsm = (uint8_t)g_estado;
    g_datos.timestamp = millis() / 1000;
    if (!espnow_enviar_datos(g_datos)) g_paquetesPerdidos++;
  }

  switch (g_estado) {

    case FSM_MEDIR_COLUMNA: {
      // Mide y transmite en vivo con los 3 sensores ultrasonicos mientras
      // se espera que el Nodo 1 elija un protocolo de trabajo. Tambien es
      // aqui donde P1 automatico dispara solo cada hora.
      if (millis() - g_ultimaMedicionColumna > INTERVALO_MEDICION_COLUMNA_MS) {
        g_ultimaMedicionColumna = millis();

        uint16_t bitsUS = 0;
        float col, calado, carro;
        if (hcsr04_leerCm(PIN_ECHO_US1, col))    { g_datos.columna_total_cm = col;    bitsUS |= BIT_US1_OK; }
        if (hcsr04_leerCm(PIN_ECHO_US2, calado)) { g_datos.calado_boya_cm   = calado; bitsUS |= BIT_US2_OK; }
        if (hcsr04_leerCm(PIN_ECHO_US3, carro))  { g_datos.pos_carro_cm     = carro;  bitsUS |= BIT_US3_OK; }

        g_datos.estado_dispositivos = (g_datos.estado_dispositivos &
            ~(BIT_US1_OK | BIT_US2_OK | BIT_US3_OK)) | bitsUS;
        g_datos.estado_dispositivos |= BIT_BOMBA_PURGA_OK | BIT_BOMBA_MUESTREO_OK; // sin sensor de falla, se asumen OK
        g_datos.timestamp = millis() / 1000;
        g_datos.bateria_pct = bateria_leerPct();
        g_datos.protocolo_activo = g_protocoloActivo;
        g_datos.paquetes_perdidos = g_paquetesPerdidos;
        g_datos.gps_avance_m = gps_simular_avance_m();   // AJUSTAR: reemplazar por GPS real
        g_datos.estado_fsm = (uint8_t)g_estado;

        if (!espnow_enviar_datos(g_datos)) g_paquetesPerdidos++;
      }

      if (g_protocoloActivo == PROTOCOLO_P1_AUTOMATICO &&
          millis() - g_ultimoCicloMillis > INTERVALO_P1_MS) {
        g_estado = FSM_BAJAR;
      }
      break;
    }

    case FSM_BAJAR: {
      float profObjetivo;
      if (g_protocoloActivo == PROTOCOLO_P2_CONFIGURABLE && g_profundidadesCm[0] > 0) {
        profObjetivo = g_profundidadesCm[0];
      } else {
        // P1/P3: 60% de la profundidad medida del cuerpo de agua (US1)
        profObjetivo = g_datos.columna_total_cm * PORCENTAJE_PROF_AUTO;
      }
      profObjetivo = constrain(profObjetivo, (float)PROF_MIN_CM, (float)PROF_MAX_CM);

      bool ok = ascensor_moverCm(profObjetivo, true);
      g_datos.estado_dispositivos = (g_datos.estado_dispositivos & ~BIT_ASCENSOR_OK) |
                                     (ok ? BIT_ASCENSOR_OK : 0);
      if (!ok) {
        g_datos.error_flags |= ERR_ASCENSOR_ATASCO;
        g_estado = FSM_ERROR;
        break;
      }
      g_datos.profundidad_cm = profObjetivo;
      g_estado = FSM_ESTABILIZAR;
      break;
    }

    case FSM_ESTABILIZAR:
      delay(1000);   // estabilizacion mecanica antes de medir
      g_estado = FSM_LEER_SONDA;
      break;

    case FSM_LEER_SONDA: {
      float ph, ec, od, tempC, ntu;
      uint16_t bitsOk = 0;
      uint16_t err = sonda_leerTodo(ph, ec, od, tempC, ntu, bitsOk);
      g_datos.ph = ph;
      g_datos.conductividad_us = ec;
      g_datos.oxigeno_disuelto = od;   // siempre 0: sensor no instalado
      g_datos.temperatura_c = tempC;
      g_datos.turbidez_ntu = ntu;
      g_datos.error_flags = (g_datos.error_flags & ~(ERR_SENSOR_PH | ERR_SENSOR_COND |
                              ERR_SENSOR_TURBIDEZ)) | err;
      g_datos.estado_dispositivos = (g_datos.estado_dispositivos &
          ~(BIT_SENSOR_PH_OK | BIT_SENSOR_COND_OK | BIT_SENSOR_TURBIDEZ_OK | BIT_SENSOR_TEMP_OK)) | bitsOk;
      g_estado = FSM_PURGAR;
      break;
    }

    case FSM_PURGAR: {
      bool ok = hidraulica_purgar();
      g_datos.estado_dispositivos = (g_datos.estado_dispositivos & ~BIT_BOMBA_PURGA_OK) |
                                     (ok ? BIT_BOMBA_PURGA_OK : 0);
      g_estado = FSM_LLENAR_RECIPIENTE;
      break;
    }

    case FSM_LLENAR_RECIPIENTE: {
      // Por ahora un solo recipiente de 1 L: cada "muestra" es una pasada
      // adicional de la bomba de muestreo hacia el mismo recipiente.
      uint8_t nMuestras = g_muestrasPorCiclo > 0 ? g_muestrasPorCiclo : 1;
      bool ok = true;
      for (uint8_t i = 0; i < nMuestras; i++) {
        ok = hidraulica_llenarRecipiente() && ok;
      }
      g_datos.estado_dispositivos = (g_datos.estado_dispositivos & ~BIT_BOMBA_MUESTREO_OK) |
                                     (ok ? BIT_BOMBA_MUESTREO_OK : 0);
      g_datos.muestras_ciclo = nMuestras;
      g_estado = FSM_SUBIR_HOME;
      break;
    }

    case FSM_SUBIR_HOME: {
      bool ok = ascensor_homing();
      g_datos.estado_dispositivos = (g_datos.estado_dispositivos & ~BIT_ASCENSOR_OK) |
                                     (ok ? BIT_ASCENSOR_OK : 0);
      g_estado = FSM_REGISTRAR_ENVIAR;
      break;
    }

    case FSM_REGISTRAR_ENVIAR: {
      g_datos.timestamp = millis() / 1000;
      g_datos.latitud = 0.0f;    // sin GPS instalado -- AJUSTAR si se agrega
      g_datos.longitud = 0.0f;
      g_datos.bateria_pct = bateria_leerPct();
      if (g_datos.bateria_pct < 20) g_datos.error_flags |= ERR_BATERIA_BAJA;
      g_datos.protocolo_activo = g_protocoloActivo;
      g_datos.paquetes_perdidos = g_paquetesPerdidos;

      if (!espnow_enviar_datos(g_datos)) {
        g_paquetesPerdidos++;
        Serial.println("ESP-NOW: fallo el envio (se reintenta en el proximo ciclo)");
      } else {
        Serial.println("ESP-NOW: datos enviados al Nodo Tierra");
      }

      g_ultimoCicloMillis = millis();
      g_estado = FSM_MEDIR_COLUMNA;   // sigue monitoreando en vivo entre ciclos
      break;
    }

    case FSM_ERROR:
      Serial.println("FSM_ERROR -- parpadeo y reintento");
      parpadear(10, 150);
      delay(3000);
      g_estado = FSM_MEDIR_COLUMNA;
      break;
  }
}

// ======================================================================
// SETUP / LOOP
// ======================================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("========================================");
  Serial.println("NODO 2 -- BOYA -- version v4 (2026-08-23)");
  Serial.println("con: yield() en homing/movimiento, confirmacion");
  Serial.println("     de envio ESP-NOW, hardware real del esquema");
  Serial.println("     (PH-4502C, 2xDS18B20, 3xHC-SR04, sin I2C)");
  Serial.println("========================================");

  pinMode(PIN_LED_BOYA, OUTPUT);
  digitalWrite(PIN_LED_BOYA, LOW);

  ascensor_iniciar();
  hidraulica_iniciar();
  ultrasonicos_iniciar();
  sensores_iniciar();   // ADC (pH/conductividad/turbidez) + DS18B20

  espnow_iniciar();

  Serial.println("Homing inicial...");
  ascensor_homing();

  // Arranca midiendo y transmitiendo con los 3 ultrasonicos en vivo,
  // esperando a que el Nodo 1 elija un protocolo de trabajo.
  g_estado = FSM_MEDIR_COLUMNA;

  Serial.println("Setup completo.");
}

void loop() {
  // Aplicar comando nuevo del Nodo Tierra (fuera de la interrupcion)
  if (comandoNuevo) {
    comandoNuevo = false;
    aplicarComando(comandoRecibido);
  }

  fsm_ejecutar();
}
