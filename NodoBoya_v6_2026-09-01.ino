// ======================================================================
// MUESTREADOR DE CALIDAD DE AGUAS -- NODO 2 / BOYA (ESP32 #2)
// Semillero SENSORAMA - Universidad Libre
// Hecho por Ing. Jair E.
// ======================================================================
//
// *** VERSION 6 -- Compatible con Nodo Tierra v17 (plano cartesiano + lecho irregular) ***
//
// CAMBIOS PRINCIPALES vs v5:
//   - Enum EstadoBoya actualizado para coincidir EXACTAMENTE con los 
//     #define ESTADO_FSM_* del Nodo Tierra v17
//   - Simulación GPS mejorada con perfil de lecho irregular (5 armónicos)
//   - Campos v3 (gps_avance_m, estado_fsm) totalmente operativos
//   - Compatibilidad total con la UI de selección de punto de muestreo
//
// HARDWARE:
//   - Sonda de pH PH-4502C (analogica, divisor 10k/15k)
//   - Sensor de conductividad electrica (analogico)
//   - Sensor de turbidez/solidos (analogico)
//   - 2x DS18B20 (temperatura, OneWire con pull-up 4.7k)
//   - 3x HC-SR04 (ultrasonicos: columna total, calado boya, carro ascensor)
//   - A4988 (NEMA17, alimentado con VMOT=12V, VDD=3.3V desde ESP32)
//   - 2 reles (bomba purga / bomba muestreo)
//
// LIBRERIAS REQUERIDAS (Arduino IDE > Administrar bibliotecas):
//   - DallasTemperature (Miles Burton)
//   - OneWire
//
// MAC Boya:   38:18:2b:8a:a8:88
// MAC Tierra: f0:24:f9:43:5d:a4
// ======================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_idf_version.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ---------- Modo de banco de pruebas ----------
#define SIMULAR_SONDA       true   // true = genera lecturas de sonda de ejemplo
#define SIN_BATERIA_FISICA  true   // true = banco de pruebas con fuente de laboratorio
#define RELAY_ACTIVO_EN_LOW true   // Modulo Songle/Tongling JQC-3FF-S-Z activo en LOW

// ======================================================================
// PINES -- segun el esquema de conexion confirmado
// ======================================================================
// Ascensor GT2 (NEMA17 + A4988)
#define PIN_STEP           4    // D4
#define PIN_DIR            16   // RX2
#define PIN_ENABLE         17   // TX2
#define PIN_ENDSTOP_HOME   33   // D33
#define PIN_ENDSTOP_MAX    32   // D32

#define PASOS_POR_CM     800   // AJUSTAR: recalcular con diametro real de polea GT2
#define VEL_HOMING_US   1200   // us/semipulso - homing lento
#define VEL_MOVER_US     700   // us/semipulso - velocidad nominal
#define PROF_MIN_CM       10
#define PROF_MAX_CM       90   // recorrido de demo/banco de pruebas
#define PORCENTAJE_PROF_AUTO 0.60f  // P1/P3: 60% de columna_total_cm
#define TIMEOUT_HOMING_MS  45000UL
#define TIMEOUT_MOVER_MS   90000UL

// Bombas peristalticas por rele
#define PIN_BOMBA_PURGA     18   // D18 - Relay 1
#define PIN_BOMBA_MUESTREO  19   // D19 - Relay 2

// Sonda de pH PH-4502C (analogica)
#define PIN_PH  35   // D35, ADC1
#define DIVISOR_PH 0.6f   // V_real_sensor = V_leido_ESP32 / DIVISOR_PH
#define PH_PENDIENTE   -5.70f   // AJUSTAR con buffer pH 4 y 7
#define PH_INTERCEPTO  21.34f

// Sensor de conductividad electrica (analogico)
#define PIN_CONDUCTIVIDAD 34   // D34, ADC1
#define COND_ADC_A_US_CM  350.0f  // AJUSTAR con solucion patron

// Sensor de turbidez (analogico)
#define PIN_TURBIDEZ  39   // VN, ADC1
#define TURBIDEZ_ADC_A_NTU 400.0f  // AJUSTAR con patrones turbidimetricos

// 2x DS18B20 (temperatura, OneWire)
#define PIN_ONEWIRE_TEMP  25   // D25

// Bateria (para cuando se conecte una real)
#define PIN_BATERIA_ADC 36   // VP
#define DIVISOR_BATERIA  4.0f

// 3x HC-SR04 (ultrasonicos)
#define PIN_TRIG_COMUN  26   // D26 - comun a los 3 sensores
#define PIN_ECHO_US1    13   // D13 - columna total
#define PIN_ECHO_US2    14   // D14 - calado boya
#define PIN_ECHO_US3    27   // D27 - carro ascensor
#define HCSR04_TIMEOUT_US 30000UL
#define HCSR04_PAUSA_ENTRE_MS 15

// LED de estado
#define PIN_LED_BOYA 2

// Duraciones del ciclo
#define T_PURGA_MS        60000UL
#define T_MUESTREO_MS     20000UL
#define T_ESTABILIZAR_MS   2000UL
#define INTERVALO_MEDICION_COLUMNA_MS 700UL

// ======================================================================
// PROTOCOLOS Y ERRORES -- COPIA EXACTA del Nodo Tierra v17
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

// Bitmask de estado_dispositivos
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

// ======================================================================
// ESTADOS DE LA FSM -- DEBE COINCIDIR EXACTAMENTE CON NodoTierra v17
// ======================================================================
// El Nodo Tierra usa #define ESTADO_FSM_* con valores 0-8
// Este enum DEBE mantener el mismo orden y valores
enum EstadoBoya : uint8_t {
  FSM_MEDIR_COLUMNA = 0,   // 0 -> ESTADO_FSM_MEDIR_COLUMNA
  FSM_BAJAR,               // 1 -> ESTADO_FSM_BAJAR
  FSM_ESTABILIZAR,         // 2 -> ESTADO_FSM_ESTABILIZAR
  FSM_LEER_SONDA,          // 3 -> ESTADO_FSM_LEER_SONDA
  FSM_PURGAR,              // 4 -> ESTADO_FSM_PURGAR
  FSM_LLENAR_RECIPIENTE,   // 5 -> ESTADO_FSM_LLENAR_RECIPIENTE
  FSM_SUBIR_HOME,          // 6 -> ESTADO_FSM_SUBIR_HOME
  FSM_REGISTRAR_ENVIAR,    // 7 -> ESTADO_FSM_REGISTRAR_ENVIAR
  FSM_ERROR,               // 8 -> ESTADO_FSM_ERROR
};

static const char* NOMBRE_ESTADO[] = {
  "MEDIR_COLUMNA", "BAJAR", "ESTABILIZAR", "LEER_SONDA", "PURGAR",
  "LLENAR_RECIPIENTE", "SUBIR_HOME", "REGISTRAR_ENVIAR", "ERROR"
};

// ======================================================================
// ESTRUCTURAS COMPARTIDAS -- DEBEN COINCIDIR BYTE A BYTE CON NODO TIERRA
// ======================================================================
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
  // Campos v2 (ultrasonicos + bitmask)
  float    columna_total_cm;
  float    calado_boya_cm;
  float    pos_carro_cm;
  uint16_t estado_dispositivos;
  // Campos v3 (plano cartesiano / lecho irregular)
  float    gps_avance_m;     // avance horizontal (metros)
  uint8_t  estado_fsm;       // fase actual (debe coincidir con EstadoBoya)
} DatosBoya_t;

typedef struct __attribute__((packed)) {
  uint8_t  comando;
  uint8_t  protocolo_nuevo;
  uint8_t  num_puntos_prof;
  uint16_t profundidades_cm[5];
  uint8_t  muestras_por_ciclo;
} ComandoTierra_t;

// MAC del Nodo Tierra (receptor)
static uint8_t MAC_NODO_TIERRA[6] = {0xF0, 0x24, 0xF9, 0x43, 0x5D, 0xA4};

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
#define INTERVALO_P1_MS (60UL * 60UL * 1000UL)   // cada hora
#define INTERVALO_P3_MS (15UL * 60UL * 1000UL)   // cada 15 min (reactivo)

// Simulacion GPS / plano cartesiano
float g_gps_simulado_m = 0.0f;
uint32_t g_ultimoAvanceGPS = 0;
#define VELOCIDAD_AVANCE_SIMULADO_CM_S 2.5f  // cm/s simulado

// Perfil de lecho irregular (5 armonicos para simulacion realista)
float g_lecho_irregular[100];  // perfil de profundidad vs posicion
bool g_lecho_generado = false;

// OneWire y DallasTemperature
OneWire oneWire(PIN_ONEWIRE_TEMP);
DallasTemperature sensors(&oneWire);

// ======================================================================
// FUNCIONES AUXILIARES
// ======================================================================

void buzzer_corto() {
  // Reservado para futuro buzzer en la Boya (opcional)
}

// Generar perfil de lecho irregular con 5 armonicos
void generarPerfilLecho() {
  if (g_lecho_generado) return;
  
  for (int i = 0; i < 100; i++) {
    float x = (float)i / 100.0f * 10.0f;  // 0 a 10 metros
    // Suma de 5 armonicos para crear perfil irregular realista
    g_lecho_irregular[i] = 50.0f                          // profundidad base (cm)
                         + 15.0f * sin(x * 0.5f)          // armonico 1: variacion larga
                         + 8.0f * sin(x * 1.3f)           // armonico 2
                         + 5.0f * sin(x * 2.7f)           // armonico 3
                         + 3.0f * sin(x * 4.1f)           // armonico 4
                         + 2.0f * sin(x * 6.9f);          // armonico 5: variacion corta
  }
  g_lecho_generado = true;
}

// Obtener profundidad del lecho en una posicion X dada
float obtenerProfundidadLecho(float avance_m) {
  generarPerfilLecho();
  int indice = (int)(avance_m / 10.0f * 100.0f);
  indice = constrain(indice, 0, 99);
  return g_lecho_irregular[indice];
}

// Simular avance GPS con perfil de lecho irregular
float gps_simular_avance_m() {
  uint32_t ahora = millis();
  if (ahora - g_ultimoAvanceGPS >= 1000) {  // actualizar cada segundo
    g_ultimoAvanceGPS = ahora;
    g_gps_simulado_m += VELOCIDAD_AVANCE_SIMULADO_CM_S / 100.0f;  // convertir cm a m
    
    // Reiniciar ciclo cada 10 metros (rango del perfil)
    if (g_gps_simulado_m >= 10.0f) {
      g_gps_simulado_m = 0.0f;
    }
  }
  return g_gps_simulado_m;
}

// Leer ultrasonicos con timeout y divisor de voltaje
float leerUltrasonico(int pinEcho) {
  digitalWrite(PIN_TRIG_COMUN, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG_COMUN, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG_COMUN, LOW);
  
  unsigned long duracion = pulseIn(pinEcho, HIGH, HCSR04_TIMEOUT_US);
  
  if (duracion == 0) return -1.0f;  // timeout
  
  // Divisor 1k/1.8k: factor = (1+1.8)/1.8 = 1.556
  // El HC-SR04 responde a 5V, pero el ESP32 solo lee hasta 3.3V
  // La lectura real es correcta porque pulseIn mide tiempo, no voltaje
  float distancia_cm = duracion * 343.0f / 2.0f / 10000.0f;  // cm
  
  return distancia_cm;
}

// Leer los 3 ultrasonicos y actualizar estado_dispositivos
void medirColumnaAgua() {
  static uint16_t intentosFallidos = 0;
  
  delay(HCSR04_PAUSA_ENTRE_MS);
  float us1 = leerUltrasonico(PIN_ECHO_US1);  // columna total
  
  delay(HCSR04_PAUSA_ENTRE_MS);
  float us2 = leerUltrasonico(PIN_ECHO_US2);  // calado boya
  
  delay(HCSR04_PAUSA_ENTRE_MS);
  float us3 = leerUltrasonico(PIN_ECHO_US3);  // carro ascensor
  
  // Validar lecturas
  bool us1_ok = (us1 > 0 && us1 < 500);
  bool us2_ok = (us2 > 0 && us2 < 200);
  bool us3_ok = (us3 > 0 && us3 < 200);
  
  if (us1_ok) {
    g_datos.columna_total_cm = us1;
    g_datos.estado_dispositivos |= BIT_US1_OK;
  } else {
    g_datos.estado_dispositivos &= ~BIT_US1_OK;
    intentosFallidos++;
  }
  
  if (us2_ok) {
    g_datos.calado_boya_cm = us2;
    g_datos.estado_dispositivos |= BIT_US2_OK;
  } else {
    g_datos.estado_dispositivos &= ~BIT_US2_OK;
  }
  
  if (us3_ok) {
    g_datos.pos_carro_cm = us3;
    g_datos.estado_dispositivos |= BIT_US3_OK;
  } else {
    g_datos.estado_dispositivos &= ~BIT_US3_OK;
  }
  
  // Calcular profundidad real (columna total - calado boya)
  if (us1_ok && us2_ok) {
    g_datos.profundidad_cm = g_datos.columna_total_cm - g_datos.calado_boya_cm;
  }
  
  // Actualizar GPS simulado para el plano cartesiano
  g_datos.gps_avance_m = gps_simular_avance_m();
  
  // Actualizar profundidad del lecho en la posicion actual
  float profLecho = obtenerProfundidadLecho(g_datos.gps_avance_m);
  // La profundidad medida debe coincidir aproximadamente con el lecho
  // (esto es para validar la simulacion)
  
  Serial.printf("[US] Columna=%.1fcm Calado=%.1fcm Prof=%.1fcm Lecho=%.1fcm PosX=%.2fm\n",
                g_datos.columna_total_cm, g_datos.calado_boya_cm,
                g_datos.profundidad_cm, profLecho, g_datos.gps_avance_m);
}

// Leer sonda completa (pH, conductividad, turbidez, temperatura)
void leerSondaCompleta() {
  // pH
  #if SIMULAR_SONDA
    g_datos.ph = 6.5f + (float)random(-10, 10) / 10.0f;  // 6.4 - 6.6
    g_datos.conductividad_us = 450.0f + (float)random(-20, 20);
    g_datos.turbidez_ntu = 12.0f + (float)random(-3, 3);
    g_datos.temperatura_c = 22.0f + (float)random(-5, 5) / 10.0f;
    g_datos.error_flags = 0;
    g_datos.estado_dispositivos |= (BIT_SENSOR_PH_OK | BIT_SENSOR_COND_OK | 
                                     BIT_SENSOR_TURBIDEZ_OK | BIT_SENSOR_TEMP_OK);
  #else
    // Lectura real de pH
    int adcPH = analogRead(PIN_PH);
    float voltajePH = adcPH * (3.3f / 4095.0f) / DIVISOR_PH;
    g_datos.ph = PH_PENDIENTE * voltajePH + PH_INTERCEPTO;
    
    // Validar pH
    if (g_datos.ph < 0 || g_datos.ph > 14) {
      g_datos.error_flags |= ERR_SENSOR_PH;
      g_datos.estado_dispositivos &= ~BIT_SENSOR_PH_OK;
    } else {
      g_datos.estado_dispositivos |= BIT_SENSOR_PH_OK;
    }
    
    // Conductividad
    int adcCond = analogRead(PIN_CONDUCTIVIDAD);
    g_datos.conductividad_us = adcCond * COND_ADC_A_US_CM / 4095.0f;
    g_datos.estado_dispositivos |= BIT_SENSOR_COND_OK;
    
    // Turbidez
    int adcTurb = analogRead(PIN_TURBIDEZ);
    g_datos.turbidez_ntu = adcTurb * TURBIDEZ_ADC_A_NTU / 4095.0f;
    g_datos.estado_dispositivos |= BIT_SENSOR_TURBIDEZ_OK;
    
    // Temperatura (DS18B20)
    sensors.requestTemperatures();
    g_datos.temperatura_c = sensors.getTempCByIndex(0);
    if (g_datos.temperatura_c == -127.0f) {
      g_datos.error_flags |= ERR_SENSOR_TURBIDEZ;  // reusar bandera
      g_datos.estado_dispositivos &= ~BIT_SENSOR_TEMP_OK;
    } else {
      g_datos.estado_dispositivos |= BIT_SENSOR_TEMP_OK;
    }
    
    g_datos.error_flags &= ~(ERR_SENSOR_PH | ERR_SENSOR_COND | ERR_SENSOR_TURBIDEZ);
  #endif
  
  g_datos.oxigeno_disuelto = 0.0f;  // sensor no instalado todavia
  
  Serial.printf("[SONDA] pH=%.2f Cond=%.1f uS/cm Turb=%.1f NTU Temp=%.1f C\n",
                g_datos.ph, g_datos.conductividad_us, 
                g_datos.turbidez_ntu, g_datos.temperatura_c);
}

// Callback de recepcion ESP-NOW (comandos desde el Nodo Tierra)
#if ESP_IDF_VERSION_MAJOR >= 5
  // Core esp32 3.x (ESP-IDF 5.x)
  void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
#else
  // Core esp32 2.x (ESP-IDF 4.x)
  void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int len) {
#endif
  if (len != sizeof(ComandoTierra_t)) return;
  
  ComandoTierra_t cmd;
  memcpy(&cmd, data, sizeof(cmd));
  
  Serial.printf("[ESP-NOW RX] Comando=%d Proto=%d Puntos=%d Muestras=%d\n",
                cmd.comando, cmd.protocolo_nuevo, cmd.num_puntos_prof, cmd.muestras_por_ciclo);
  
  if (cmd.comando == 1) {  // cambiar protocolo
    if (cmd.protocolo_nuevo >= 1 && cmd.protocolo_nuevo <= 3) {
      g_protocoloActivo = cmd.protocolo_nuevo;
      g_numPuntosProf = cmd.num_puntos_prof;
      g_muestrasPorCiclo = cmd.muestras_por_ciclo;
      
      for (int i = 0; i < cmd.num_puntos_prof; i++) {
        g_profundidadesCm[i] = cmd.profundidades_cm[i];
      }
      
      Serial.printf("[PROTOCOLO] Nuevo=%d Puntos=%d\n", g_protocoloActivo, g_numPuntosProf);
    }
  } else if (cmd.comando == 2) {  // iniciar ciclo manual
    if (g_estado == FSM_MEDIR_COLUMNA) {
      g_estado = FSM_BAJAR;
      Serial.println("[MANUAL] Iniciando ciclo de muestreo");
    }
  }
}

// Inicializar ESP-NOW
bool iniciarESPNOW() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Error inicializando");
    return false;
  }
  
  esp_now_register_recv_cb(OnDataRecv);
  
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, MAC_NODO_TIERRA, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("[ESP-NOW] Error agregando peer");
    return false;
  }
  
  Serial.println("[ESP-NOW] Inicializado correctamente");
  return true;
}

// Enviar telemetria al Nodo Tierra
void enviarTelemetria() {
  g_datos.timestamp = millis() / 1000;
  g_datos.protocolo_activo = g_protocoloActivo;
  g_datos.muestras_ciclo = g_muestrasPorCiclo;
  g_datos.bateria_pct = SIN_BATERIA_FISICA ? 100 : analogRead(PIN_BATERIA_ADC) * 100 / 4095;
  g_datos.estado_fsm = (uint8_t)g_estado;  // CRUCIAL: debe coincidir con EstadoBoya
  
  // Lat/Lon simuladas (ajustar para ubicacion real)
  g_datos.latitud = 6.2442f;   // Medellin (ejemplo)
  g_datos.longitud = -75.5736f;
  
  int resultado = esp_now_send(MAC_NODO_TIERRA, (uint8_t*)&g_datos, sizeof(g_datos));
  
  if (resultado == ESP_OK) {
    g_paquetesPerdidos = 0;
  } else {
    g_paquetesPerdidos++;
    g_datos.error_flags |= ERR_ESPNOW_PERDIDA;
  }
  
  g_datos.paquetes_perdidos = g_paquetesPerdidos;
  
  Serial.printf("[ESP-NOW TX] Enviado %d bytes, Estado=%d (%s)\n",
                sizeof(g_datos), g_estado, NOMBRE_ESTADO[g_estado]);
}

// Configurar pines
void configurarPines() {
  // Ascensor
  pinMode(PIN_STEP, OUTPUT);
  pinMode(PIN_DIR, OUTPUT);
  pinMode(PIN_ENABLE, OUTPUT);
  pinMode(PIN_ENDSTOP_HOME, INPUT_PULLUP);
  pinMode(PIN_ENDSTOP_MAX, INPUT_PULLUP);
  digitalWrite(PIN_ENABLE, HIGH);  // motor deshabilitado inicialmente
  
  // Bombas
  pinMode(PIN_BOMBA_PURGA, OUTPUT);
  pinMode(PIN_BOMBA_MUESTREO, OUTPUT);
  digitalWrite(PIN_BOMBA_PURGA, RELAY_ACTIVO_EN_LOW ? HIGH : LOW);
  digitalWrite(PIN_BOMBA_MUESTREO, RELAY_ACTIVO_EN_LOW ? HIGH : LOW);
  
  // Ultrasonicos
  pinMode(PIN_TRIG_COMUN, OUTPUT);
  pinMode(PIN_ECHO_US1, INPUT);
  pinMode(PIN_ECHO_US2, INPUT);
  pinMode(PIN_ECHO_US3, INPUT);
  digitalWrite(PIN_TRIG_COMUN, LOW);
  
  // LED
  pinMode(PIN_LED_BOYA, OUTPUT);
  digitalWrite(PIN_LED_BOYA, LOW);
  
  // Sensores analogicos
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);  // 0-3.9V rango
  
  Serial.println("[PINES] Configurados correctamente");
}

// ======================================================================
// SETUP
// ======================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n========================================");
  Serial.println("NODO BOYA v6 - Semillero SENSORAMA");
  Serial.println("Compatible con Nodo Tierra v17");
  Serial.println("========================================");
  
  configurarPines();
  
  // Inicializar sensores de temperatura
  sensors.begin();
  sensors.setResolution(12);
  
  // Inicializar ESP-NOW
  if (!iniciarESPNOW()) {
    Serial.println("[ERROR] No se pudo iniciar ESP-NOW");
    g_datos.error_flags |= ERR_ESPNOW_PERDIDA;
  }
  
  // Generar perfil de lecho irregular
  generarPerfilLecho();
  Serial.println("[LECHO] Perfil irregular generado (5 armonicos)");
  
  // Estado inicial
  g_estado = FSM_MEDIR_COLUMNA;
  g_datos.estado_fsm = (uint8_t)g_estado;
  g_datos.error_flags = 0;
  g_datos.estado_dispositivos = 0;
  
  // Medicion inicial de columna
  medirColumnaAgua();
  
  // Parpadeo LED de inicio
  digitalWrite(PIN_LED_BOYA, HIGH);
  delay(200);
  digitalWrite(PIN_LED_BOYA, LOW);
  
  Serial.println("[SETUP] Completado - esperando comandos del Nodo Tierra");
  Serial.println("[INFO] Usa el Nodo Tierra para seleccionar punto de muestreo y protocolo");
}

// ======================================================================
// LOOP
// ======================================================================
void loop() {
  uint32_t ahora = millis();
  
  // Maquina de estados
  switch (g_estado) {
    case FSM_MEDIR_COLUMNA:
      // Estado de reposo: medir y transmitir ultrasonicos continuamente
      if (ahora - g_ultimaMedicionColumna >= INTERVALO_MEDICION_COLUMNA_MS) {
        g_ultimaMedicionColumna = ahora;
        medirColumnaAgua();
        enviarTelemetria();
        
        // Blink LED suave
        digitalWrite(PIN_LED_BOYA, (ahora / 500) % 2);
      }
      
      // Verificar si hay comando para iniciar ciclo manual
      // (se procesa en el callback ESP-NOW)
      break;
      
    case FSM_BAJAR:
      Serial.println("[FSM] Bajando a profundidad objetivo...");
      // Logica de bajada del ascensor (simplificada para demo)
      delay(2000);  // simular tiempo de bajada
      g_estado = FSM_ESTABILIZAR;
      break;
      
    case FSM_ESTABILIZAR:
      Serial.println("[FSM] Estabilizando lectura...");
      delay(T_ESTABILIZAR_MS);
      g_estado = FSM_LEER_SONDA;
      break;
      
    case FSM_LEER_SONDA:
      Serial.println("[FSM] Leyendo sonda...");
      leerSondaCompleta();
      g_estado = FSM_PURGAR;
      break;
      
    case FSM_PURGAR:
      Serial.println("[FSM] Purgando camara...");
      digitalWrite(PIN_BOMBA_PURGA, RELAY_ACTIVO_EN_LOW ? LOW : HIGH);
      delay(min(T_PURGA_MS, 5000UL));  // demo: solo 5 segundos
      digitalWrite(PIN_BOMBA_PURGA, RELAY_ACTIVO_EN_LOW ? HIGH : LOW);
      g_estado = FSM_LLENAR_RECIPIENTE;
      break;
      
    case FSM_LLENAR_RECIPIENTE:
      Serial.println("[FSM] Llenando recipiente...");
      digitalWrite(PIN_BOMBA_MUESTREO, RELAY_ACTIVO_EN_LOW ? LOW : HIGH);
      delay(min(T_MUESTREO_MS, 3000UL));  // demo: solo 3 segundos
      digitalWrite(PIN_BOMBA_MUESTREO, RELAY_ACTIVO_EN_LOW ? HIGH : LOW);
      g_estado = FSM_SUBIR_HOME;
      break;
      
    case FSM_SUBIR_HOME:
      Serial.println("[FSM] Subiendo a home...");
      delay(2000);  // simular tiempo de subida
      g_estado = FSM_REGISTRAR_ENVIAR;
      break;
      
    case FSM_REGISTRAR_ENVIAR:
      Serial.println("[FSM] Registrando y enviando datos...");
      enviarTelemetria();
      g_ultimoCicloMillis = ahora;
      g_estado = FSM_MEDIR_COLUMNA;
      break;
      
    case FSM_ERROR:
      Serial.println("[FSM] ERROR - reiniciando...");
      g_datos.error_flags |= ERR_ASCENSOR_ATASCO;
      delay(5000);
      g_estado = FSM_MEDIR_COLUMNA;
      break;
  }
  
  // Verificar intervalo de protocolo automatico (P1)
  if (g_protocoloActivo == PROTOCOLO_P1_AUTOMATICO) {
    if (ahora - g_ultimoCicloMillis >= INTERVALO_P1_MS) {
      if (g_estado == FSM_MEDIR_COLUMNA) {
        Serial.println("[P1 AUTOMATICO] Iniciando ciclo programado...");
        g_estado = FSM_BAJAR;
      }
    }
  }
  
  // Verificar intervalo de protocolo reactivo (P3)
  if (g_protocoloActivo == PROTOCOLO_P3_REACTIVO) {
    if (ahora - g_ultimoCicloMillis >= INTERVALO_P3_MS) {
      // Verificar umbrales (ejemplo: pH < 6 o turbidez > 20)
      bool condicionActivada = (g_datos.ph < 6.0f || g_datos.turbidez_ntu > 20.0f);
      if (condicionActivada && g_estado == FSM_MEDIR_COLUMNA) {
        Serial.println("[P3 REACTIVO] Umbral superado - iniciando ciclo...");
        g_estado = FSM_BAJAR;
      }
    }
  }
  
  // Pequeño delay para evitar bloqueo total
  delay(10);
}
