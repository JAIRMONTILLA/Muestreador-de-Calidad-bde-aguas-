# Guía de Subida de Código - Boya Ecológica ALBUS

## Archivos Generados

### 1. Nodo Tierra (Unidad en Tierra - Pantalla TFT)
- **Archivo:** `NodoTierra_v17_2026-09-01.ino`
- **Dispositivo:** ESP32 DevKit V1 con pantalla TFT 1.8" ST7735
- **MAC:** f0:24:f9:43:5d:a4
- **Función:** Interfaz de usuario, selección de punto de muestreo, visualización de plano cartesiano con lecho irregular

### 2. Nodo Boya (Unidad Flotante)
- **Archivo:** `NodoBoya_v6_2026-09-01.ino`
- **Dispositivo:** ESP32 DevKit V1 con sensores
- **MAC:** 38:18:2b:8a:a8:88
- **Función:** Muestreo de agua, medición de sensores, comunicación ESP-NOW

---

## Instrucciones de Subida con Arduino IDE

### Requisitos Previos

1. **Instalar Core ESP32 en Arduino IDE:**
   - Ir a `Archivo > Preferencias`
   - En "URLs adicionales para Gestor de Tarjetas", agregar:
     ```
     https://espressif.github.io/arduino-esp32/package_esp32_index.json
     ```
   - Ir a `Herramientas > Placa > Gestor de Tarjetas`
   - Buscar "esp32" e instalar "esp32 by Espressif Systems" (versión 2.0.9 o 3.0.7)

2. **Instalar Librerías Requeridas:**
   - `Adafruit GFX Library` (Herramientas > Administrar Bibliotecas)
   - `Adafruit ST7735 and ST7789 Library`
   - `DallasTemperature` (para la Boya)
   - `OneWire` (para la Boya)

3. **Configuración de Puertos:**
   - Conectar el ESP32 vía USB
   - Identificar el puerto en `Herramientas > Puerto`
   - En Linux: `/dev/ttyUSB0` o `/dev/ttyACM0`
   - En Windows: `COM3`, `COM4`, etc.

---

## Procedimiento de Subida

### Paso 1: Subir Código al Nodo Tierra

1. **Abrir el archivo:**
   ```
   Archivo > Abrir > /workspace/NodoTierra_v17_2026-09-01.ino
   ```

2. **Configurar la placa:**
   ```
   Herramientas > Placa > esp32 > ESP32 Dev Module
   Herramientas > Puerto > [Seleccionar tu puerto]
   Herramientas > Upload Speed > 921600
   ```

3. **Subir el código:**
   - Presionar el botón de subir (flecha derecha)
   - Mantener presionado el botón BOOT del ESP32 si la subida falla
   - Esperar a que aparezca "Done uploading"

4. **Verificar funcionamiento:**
   - Abrir Monitor Serie (115200 baudios)
   - Debería mostrar inicialización de pantalla y ESP-NOW

---

### Paso 2: Subir Código a la Boya

1. **Desconectar el Nodo Tierra y conectar la Boya**

2. **Abrir el archivo:**
   ```
   Archivo > Abrir > /workspace/NodoBoya_v6_2026-09-01.ino
   ```

3. **Configurar la placa:**
   ```
   Herramientas > Placa > esp32 > ESP32 Dev Module
   Herramientas > Puerto > [Seleccionar tu puerto]
   Herramientas > Upload Speed > 921600
   ```

4. **Subir el código:**
   - Presionar el botón de subir
   - Mantener presionado BOOT si es necesario
   - Esperar "Done uploading"

5. **Verificar funcionamiento:**
   - Abrir Monitor Serie (115200 baudios)
   - Debería mostrar:
     ```
     NODO BOYA v6 - Semillero SENSORAMA
     Compatible con Nodo Tierra v17
     [LECHO] Perfil irregular generado (5 armonicos)
     [SETUP] Completado - esperando comandos del Nodo Tierra
     ```

---

## Flujo de Operación

### 1. Encendido Inicial
- Encender ambos nodos (Tierra y Boya)
- Ambos se inicializan en modo "MEDIR_COLUMNA"
- La Boya comienza a enviar telemetría cada 700ms

### 2. Selección de Punto de Muestreo (Nodo Tierra)
1. En el menú principal, presionar OK para entrar al plano cartesiano
2. Usar UP/DOWN para ajustar la profundidad deseada
3. La posición X se actualiza automáticamente según el avance GPS simulado
4. Presionar OK para confirmar el punto de muestreo
5. El sistema muestra: "Punto de muestreo seleccionado: X=X.Xm Prof=XXcm"

### 3. Selección de Protocolo (Nodo Tierra)
Una vez confirmado el punto:
1. El sistema permite seleccionar el protocolo:
   - **P1 Automático:** Por tiempo y profundidad variable (cada hora)
   - **P2 Configurable:** Definido por el investigador (1-5 profundidades)
   - **P3 Reactivo:** Disparado por umbrales (pH < 6 o turbidez > 20)

2. Usar UP/DOWN para navegar entre protocolos
3. Presionar OK para confirmar
4. Si es P2, configurar las profundidades específicas

### 4. Ejecución del Muestreo (Boya)
- La Boya recibe el comando vía ESP-NOW
- Ejecuta la secuencia:
  1. BAJAR a la profundidad seleccionada
  2. ESTABILIZAR (2 segundos)
  3. LEER_SONDA (pH, conductividad, turbidez, temperatura)
  4. PURGAR (60 segundos, ajustable)
  5. LLENAR_RECIPIENTE (20 segundos, ajustable)
  6. SUBIR_HOME
  7. REGISTRAR_ENVIAR datos al Nodo Tierra

### 5. Visualización de Datos
- El Nodo Tierra muestra en tiempo real:
  - Estado actual de la FSM
  - Valores de sensores
  - Posición en el plano cartesiano
  - Perfil del lecho irregular
  - Iconos de estado de dispositivos

---

## Solución de Problemas

### Error: "Failed to connect to ESP32"
- Mantener presionado el botón BOOT mientras se conecta el USB
- Soltar BOOT después de que aparezca "Connecting..."
- Verificar que el cable USB sea de datos (no solo carga)

### Error: "esp_now_init failed"
- Verificar que WiFi esté desactivado en otros dispositivos cercanos
- Reiniciar ambos ESP32
- Confirmar que las MAC addresses estén correctamente configuradas

### Error: "Pantalla no muestra nada"
- Verificar conexiones de pines (TFT_CS=21, TFT_DC=22, TFT_RST=5)
- Confirmar que el backlight tenga 3.3V
- Revisar que las librerías Adafruit estén instaladas

### Error: "Datos no coinciden entre nodos"
- Verificar que ambas estructuras `DatosBoya_t` sean idénticas
- Confirmar que los enums `EstadoBoya` y `ESTADO_FSM_*` coincidan
- Revisar que el atributo `__attribute__((packed))` esté presente

---

## Comandos ESP-NOW Disponibles

### Comando 1: Cambiar Protocolo
```cpp
ComandoTierra_t cmd;
cmd.comando = 1;
cmd.protocolo_nuevo = PROTOCOLO_P2_CONFIGURABLE;
cmd.num_puntos_prof = 3;
cmd.profundidades_cm[0] = 30;  // cm
cmd.profundidades_cm[1] = 60;
cmd.profundidades_cm[2] = 90;
cmd.muestras_por_ciclo = 2;
```

### Comando 2: Iniciar Ciclo Manual
```cpp
ComandoTierra_t cmd;
cmd.comando = 2;
// Los demás campos se ignoran
```

---

## Notas Importantes

1. **Compatibilidad de Versiones:**
   - Nodo Tierra v17 ↔ Nodo Boya v6 (compatibles)
   - Las estructuras deben coincidir byte a byte
   - Los estados FSM deben tener los mismos valores numéricos

2. **Perfil de Lecho Irregular:**
   - Generado con 5 armónicos sinusoidales
   - Rango: 0-10 metros de avance horizontal
   - Profundidad base: 50cm ± variaciones
   - Se reinicia cíclicamente para simulación continua

3. **Modo Prueba:**
   - Mantener BTN_OK presionado al encender el Nodo Tierra
   - Genera datos simulados sin necesidad de la Boya real
   - Útil para probar la UI y el plano cartesiano

4. **Calibración de Sensores:**
   - pH: usar buffers 4.0 y 7.0 para ajustar PH_PENDIENTE y PH_INTERCEPTO
   - Conductividad: usar solución patrón conocida (ej. 440 µS/cm)
   - Turbidez: usar patrones NTU certificados

---

## Descarga de Códigos

Los archivos están disponibles en `/workspace/`:

```bash
# Listar archivos
ls -la /workspace/*.ino

# Copiar a tu computadora (Linux/Mac)
cp /workspace/NodoTierra_v17_2026-09-01.ino ~/Documentos/ALBUS/
cp /workspace/NodoBoya_v6_2026-09-01.ino ~/Documentos/ALBUS/

# O descargar vía SCP (desde otra máquina)
scp usuario@servidor:/workspace/NodoTierra_v17_2026-09-01.ino ./
scp usuario@servidor:/workspace/NodoBoya_v6_2026-09-01.ino ./
```

---

## Contacto y Soporte

- **Semillero:** SENSORAMA - Universidad Libre
- **Responsable:** Ing. Jair E.
- **Documentación adicional:** Ver archivos .docx en `/workspace/`
