/*
 * ============================================================================
 * PROYECTO: SISTEMA DE MUESTREO OCEANOGRÁFICO - NODO BOYA
 * VERSIÓN: v6.1 (Monolítico Autocontenido)
 * FECHA: 2026-09-10
 * DESCRIPCIÓN: Unidad flotante con sensores, receptora de comandos ESP-NOW
 *              del Nodo Tierra para muestreo en coordenadas específicas.
 * ============================================================================
 * INSTRUCCIONES DE INSTALACIÓN:
 * 1. Copie este código completo en un nuevo sketch de Arduino IDE.
 * 2. Guarde el archivo como "NodoBoya_v6.ino".
 * 3. Instale las librerías externas requeridas desde el Gestor de Librerías:
 *    - DallasTemperature
 *    - OneWire
 *    - EspNow (viene con el core ESP32)
 * 4. Configure el puerto y la placa (ESP32 Dev Module) y suba.
 * NOTA: Este código asume que recibe comandos del Nodo Tierra.
 *       En modo autónomo simulará datos de sensores.
 * ============================================================================
 */

#include <WiFi.h>
#include <esp_now.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ============================================================================
// CONFIGURACIÓN DE HARDWARE (PINES)
// ============================================================================
#define PIN_ONEWIRE 21
#define PIN_LED     2

OneWire oneWire(PIN_ONEWIRE);
DallasTemperature sensors(&oneWire);

// ============================================================================
// CONSTANTES Y ESTRUCTURAS DE DATOS (Reemplaza archivos .h)
// ============================================================================

// MAC Address del Nodo Tierra (Para respuestas, opcional si solo escucha)
uint8_t tierraAddress[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66}; 

// Definición de Estados (Sincronizado con Tierra - COPIA EXACTA)
typedef enum {
  ESTADO_IDLE = 0,
  ESTADO_MOVIMIENTO = 1,
  ESTADO_SELECCION_PUNTO = 2,
  ESTADO_PROFUNDIDAD = 3,
  ESTADO_MUESTREO = 4,
  ESTADO_RETORNO = 5,
  ESTADO_ERROR = 99
} EstadoFSM;

// Estructura de Paquete ESP-NOW (DEBE COINCIDIR EXACTAMENTE CON TIERRA)
typedef struct struct_message {
  int id;
  EstadoFSM estado;
  float posX;
  float posY;
  float profundidadObjetivo;
  int protocoloSeleccionado;
  unsigned long timestamp;
} struct_message;

struct_message myData;
struct_message incomingData;

// Callback para recibir datos del Nodo Tierra
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len == sizeof(struct_message)) {
    memcpy(&incomingData, incomingData, sizeof(struct_message));
    
    Serial.print("Comando recibido - Estado: ");
    Serial.println(incomingData.estado);
    Serial.print("Posicion: X="); Serial.print(incomingData.posX);
    Serial.print(" Y="); Serial.println(incomingData.posY);
    Serial.print("Profundidad: "); Serial.println(incomingData.profundidadObjetivo);
    
    // Cambiar estado interno basado en comando
    if (incomingData.estado == ESTADO_MUESTREO) {
      ejecutarMuestreo(incomingData.profundidadObjetivo);
    }
  }
}

// ============================================================================
// VARIABLES GLOBALES DE SIMULACIÓN
// ============================================================================
float currentDepthSensor = 0.0;
float temperature = 0.0;
float salinity = 0.0;
bool isSampling = false;

// Perfil de lecho local (para validación interna en la boya)
float lechoProfile[100];

// ============================================================================
// FUNCIONES DE UTILIDAD Y SIMULACIÓN DE SENSORES
// ============================================================================

void generarPerfilLecho() {
  for (int i = 0; i < 100; i++) {
    float x = (float)i / 10.0;
    float y = 5.0 * sin(x * 0.5) + 
              2.5 * sin(x * 1.2) + 
              1.2 * sin(x * 3.5) + 
              0.8 * sin(x * 7.1) + 
              0.4 * sin(x * 15.3);
    lechoProfile[i] = y;
  }
}

float obtenerProfundidadLecho(float xSim) {
  int index = (int)(xSim * 10) % 100;
  if (index < 0) index = 0;
  return lechoProfile[index];
}

// Simula lectura de sensor de profundidad (ej. MS5837)
float leerProfundidad() {
  // En producción: leer sensor real I2C
  // Aquí simulamos una variación suave
  static float base = 3.0;
  base += (random(100) - 50) / 1000.0; 
  return base;
}

// Simula lectura de temperatura (DS18B20)
float leerTemperatura() {
  sensors.requestTemperatures();
  // Si no hay sensor real, retornar valor simulado
  float temp = sensors.getTempCByIndex(0);
  if (temp == -127.0) return 18.5 + (random(100)/100.0); // Simulado
  return temp;
}

// Simula ejecución de protocolo de muestreo
void ejecutarMuestreo(float profundidadObj) {
  Serial.println("--- INICIANDO PROTOCOLO DE MUESTREO ---");
  isSampling = true;
  
  Serial.print("Descendiendo a profundidad objetivo: ");
  Serial.println(profundidadObj);
  
  // Simular tiempo de descenso y toma de muestra
  for(int i=0; i<5; i++) {
    Serial.print("Profundidad actual: ");
    Serial.println(leerProfundidad());
    delay(1000);
  }
  
  Serial.println("TOMA DE MUESTRA REALIZADA");
  Serial.print("Temperatura registrada: ");
  Serial.println(leerTemperatura());
  
  isSampling = false;
  Serial.println("--- FIN MUESTREO ---");
}

// ============================================================================
// CONFIGURACIÓN E INICIALIZACIÓN
// ============================================================================

void setup() {
  Serial.begin(115200);
  
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  
  // Inicializar sensores
  sensors.begin();
  
  // Generar perfil de lecho local
  generarPerfilLecho();
  
  Serial.println("Iniciando Nodo Boya v6...");
  
  // Inicializar WiFi y ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error iniciando ESP-NOW");
    return;
  }
  
  esp_now_register_recv_cb(OnDataRecv);
  
  // Registrar par (Nodo Tierra)
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, tierraAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Fallo al agregar par Tierra");
    return;
  }
  
  Serial.println("Boyas lista. Esperando comandos del Nodo Tierra...");
  digitalWrite(PIN_LED, HIGH); // LED encendido indica listo
}

// ============================================================================
// BUCLE PRINCIPAL
// ============================================================================

void loop() {
  // Enviar datos telemetría periódicamente (ej. cada 5 seg)
  static unsigned long lastTelemetry = 0;
  if (millis() - lastTelemetry > 5000) {
    lastTelemetry = millis();
    
    myData.id = 2; // ID Boya
    myData.estado = ESTADO_MOVIMIENTO; // Estado genérico
    myData.posX = 0.0; // En realidad vendría de GPS
    myData.posY = 0.0;
    myData.profundidadObjetivo = leerProfundidad();
    myData.timestamp = millis();
    
    // Enviar a tierra (opcional, si la tierra escucha)
    // esp_now_send(tierraAddress, (uint8_t *) &myData, sizeof(myData));
    
    Serial.print("Telemetría - Prof: ");
    Serial.print(myData.profundidadObjetivo);
    Serial.print(" Temp: ");
    Serial.println(leerTemperatura());
  }
  
  // Si está muestreando, no hacer nada más (bloqueante para demo)
  if (isSampling) {
    delay(100);
    return;
  }
  
  // Lógica autónoma de emergencia (ej. batería baja, pérdida de señal)
  // ... implementar según necesidades ...
  
  delay(100);
}
