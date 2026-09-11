/*
 * ============================================================================
 * PROYECTO: SISTEMA DE MUESTREO OCEANOGRÁFICO - NODO TIERRA
 * VERSIÓN: v17.1 (Monolítico Autocontenido)
 * FECHA: 2026-09-10
 * DESCRIPCIÓN: Unidad principal con pantalla TFT, selección de punto de muestreo
 *              sobre lecho irregular y control de protocolos vía ESP-NOW.
 * ============================================================================
 * INSTRUCCIONES DE INSTALACIÓN:
 * 1. Copie este código completo en un nuevo sketch de Arduino IDE.
 * 2. Guarde el archivo como "NodoTierra_v17.ino".
 * 3. Instale las librerías externas requeridas desde el Gestor de Librerías:
 *    - Adafruit GFX Library
 *    - Adafruit ILI9341 (o la específica para su pantalla TFT)
 *    - EspNow (viene con el core ESP32)
 * 4. Configure el puerto y la placa (ESP32 Dev Module) y suba.
 * ============================================================================
 */

#include <WiFi.h>
#include <esp_now.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h> // Ajustar según el modelo exacto de su pantalla

// ============================================================================
// CONFIGURACIÓN DE HARDWARE (PINES)
// ============================================================================
#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4
// Definir pines de botones según su esquema
#define BTN_UP   35
#define BTN_DOWN 34
#define BTN_OK   39
#define BTN_LEFT 36
#define BTN_RIGHT 33

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// ============================================================================
// CONSTANTES Y ESTRUCTURAS DE DATOS (Reemplaza archivos .h)
// ============================================================================

// MAC Address del Nodo Boya (DEBE COINCIDIR CON LA BOYA)
uint8_t boyaAddress[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}; 

// Definición de Estados (Sincronizado con Boya)
typedef enum {
  ESTADO_IDLE = 0,
  ESTADO_MOVIMIENTO = 1,
  ESTADO_SELECCION_PUNTO = 2,
  ESTADO_PROFUNDIDAD = 3,
  ESTADO_MUESTREO = 4,
  ESTADO_RETORNO = 5,
  ESTADO_ERROR = 99
} EstadoFSM;

// Estructura de Paquete ESP-NOW
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

// Callback para recibir datos
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {
  if (len == sizeof(struct_message)) {
    memcpy(&incomingData, incomingData, sizeof(struct_message));
    // Aquí se procesaría la respuesta de la boya (ej: confirmación de llegada)
  }
}

// ============================================================================
// VARIABLES GLOBALES DE SIMULACIÓN
// ============================================================================
float currentX = 0.0;
float currentY = 0.0;
float targetX = 0.0;
float targetY = 0.0;
float currentDepth = 0.0;
float lechoProfile[100]; // Perfil del lecho generado
EstadoFSM currentState = ESTADO_IDLE;
bool puntoSeleccionado = false;

// ============================================================================
// FUNCIONES DE UTILIDAD Y MATEMÁTICAS
// ============================================================================

// Genera un perfil de lecho irregular usando suma de senoidales (Ruido 1/f simplificado)
void generarPerfilLecho() {
  for (int i = 0; i < 100; i++) {
    float x = (float)i / 10.0;
    // Suma de 5 armónicos para irregularidad natural
    float y = 5.0 * sin(x * 0.5) + 
              2.5 * sin(x * 1.2) + 
              1.2 * sin(x * 3.5) + 
              0.8 * sin(x * 7.1) + 
              0.4 * sin(x * 15.3);
    lechoProfile[i] = y;
  }
}

// Obtiene la profundidad del lecho en una coordenada X simulada
float obtenerProfundidadLecho(float xSim) {
  int index = (int)(xSim * 10) % 100;
  if (index < 0) index = 0;
  return lechoProfile[index];
}

// Simula avance GPS
void gps_simular_avance_m(float *x, float *y, float dx, float dy) {
  *x += dx;
  *y += dy;
}

// ============================================================================
// FUNCIONES DE INTERFAZ GRÁFICA (TFT)
// ============================================================================

void dibujarPlanoCartesiano() {
  tft.fillScreen(ILI9341_BLACK);
  
  // Dibujar ejes
  tft.drawLine(160, 0, 160, 240, ILI9341_WHITE); // Eje Y
  tft.drawLine(0, 120, 320, 120, ILI9341_WHITE); // Eje X
  
  // Dibujar perfil del lecho (simulado en la parte inferior)
  tft.drawLine(0, 200, 320, 200, ILI9341_BLUE); // Línea base agua
  
  // Representación visual simplificada del lecho
  for(int i=0; i<320; i+=5) {
     float prof = obtenerProfundidadLecho(i/10.0);
     int yLecho = 200 + (int)(prof * 5); 
     if(yLecho > 235) yLecho = 235;
     tft.drawLine(i, yLecho, i+4, 239, ILI9341_BROWN);
  }

  // Dibujar posición actual (Barco/Boya)
  int px = 160 + (int)(currentX * 10);
  int py = 120 - (int)(currentY * 10);
  
  // Limites pantalla
  if(px < 0) px = 0; if(px > 320) px = 320;
  if(py < 0) py = 0; if(py > 240) py = 240;

  tft.fillCircle(px, py, 5, ILI9341_GREEN);
  tft.drawCircle(px, py, 8, ILI9341_RED);
  
  // Cursor de selección si estamos en modo selección
  if(currentState == ESTADO_SELECCION_PUNTO) {
     tft.drawCircle(px, py, 12, ILI9341_YELLOW);
  }
}

void dibujarUI() {
  tft.setCursor(5, 5);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(1);
  tft.print("Est: ");
  
  switch(currentState) {
    case ESTADO_IDLE: tft.println("ESPERA"); break;
    case ESTADO_MOVIMIENTO: tft.println("MOVIMIENTO"); break;
    case ESTADO_SELECCION_PUNTO: tft.println("SELEC. PUNTO"); break;
    case ESTADO_PROFUNDIDAD: tft.println("AJUSTE PROF."); break;
    case ESTADO_MUESTREO: tft.println("MUESTREANDO"); break;
    default: tft.println("ERROR");
  }
  
  tft.setCursor(5, 20);
  tft.printf("X:%.1f Y:%.1f", currentX, currentY);
  
  if(puntoSeleccionado) {
    tft.setTextColor(ILI9341_GREEN);
    tft.setCursor(180, 20);
    tft.println("PUNTO FIJADO");
  }
}

// ============================================================================
// LÓGICA PRINCIPAL
// ============================================================================

void setup() {
  Serial.begin(115200);
  
  // Inicializar TFT
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);
  tft.setTextColor(ILI9341_WHITE);
  tft.setTextSize(2);
  tft.println("Iniciando Sistema...");
  
  // Configurar Botones
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);

  // Generar terreno
  generarPerfilLecho();

  // Inicializar WiFi y ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error iniciando ESP-NOW");
    return;
  }
  
  esp_now_register_recv_cb(OnDataRecv);
  
  // Registrar par
  esp_now_peer_info_t peerInfo;
  memcpy(peerInfo.peer_addr, boyaAddress, 6);
  peerInfo.channel = 0;  
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Fallo al agregar par");
    return;
  }
  
  Serial.println("Sistema Iniciado. Esperando comando...");
  delay(2000);
  currentState = ESTADO_MOVIMIENTO; // Estado inicial de prueba
}

void loop() {
  // Lectura de botones simple (sin debounce avanzado para brevedad)
  bool upPressed = !digitalRead(BTN_UP);
  bool downPressed = !digitalRead(BTN_DOWN);
  bool okPressed = !digitalRead(BTN_OK);
  bool leftPressed = !digitalRead(BTN_LEFT);
  bool rightPressed = !digitalRead(BTN_RIGHT);

  // Máquina de Estados Simplificada para Demostración
  switch (currentState) {
    
    case ESTADO_MOVIMIENTO:
      if (rightPressed) currentX += 0.5;
      if (leftPressed) currentX -= 0.5;
      if (upPressed) currentY += 0.5;
      if (downPressed) currentY -= 0.5;
      
      if (okPressed) {
        currentState = ESTADO_SELECCION_PUNTO;
        puntoSeleccionado = false;
      }
      break;

    case ESTADO_SELECCION_PUNTO:
      // Movimiento fino para selección
      if (rightPressed) currentX += 0.1;
      if (leftPressed) currentX -= 0.1;
      if (upPressed) currentY += 0.1;
      if (downPressed) currentY -= 0.1;
      
      if (okPressed) {
        puntoSeleccionado = true;
        currentState = ESTADO_PROFUNDIDAD;
        // Enviar señal a la boya de que hay un punto seleccionado
        myData.estado = ESTADO_SELECCION_PUNTO;
        myData.posX = currentX;
        myData.posY = currentY;
        esp_now_send(boyaAddress, (uint8_t *) &myData, sizeof(myData));
      }
      break;

    case ESTADO_PROFUNDIDAD:
       if (puntoSeleccionado) {
         if (upPressed) currentDepth += 0.5;
         if (downPressed) currentDepth -= 0.5;
         if (currentDepth < 0) currentDepth = 0;
         
         // Verificar profundidad del lecho en ese punto
         float fondo = obtenerProfundidadLecho(currentX);
         if (currentDepth > fondo) currentDepth = fondo;
         
         if (okPressed) {
           currentState = ESTADO_MUESTREO;
           myData.estado = ESTADO_MUESTREO;
           myData.profundidadObjetivo = currentDepth;
           esp_now_send(boyaAddress, (uint8_t *) &myData, sizeof(myData));
         }
       }
       break;
       
    case ESTADO_MUESTREO:
       tft.fillScreen(ILI9341_BLUE);
       tft.setCursor(50, 100);
       tft.println("MUESTREANDO...");
       delay(5000); // Simular tiempo de muestreo
       currentState = ESTADO_IDLE;
       tft.fillScreen(ILI9341_BLACK);
       break;
  }

  // Actualizar Pantalla
  dibujarPlanoCartesiano();
  dibujarUI();
  
  delay(100); // Pequeña pausa para estabilidad
}
