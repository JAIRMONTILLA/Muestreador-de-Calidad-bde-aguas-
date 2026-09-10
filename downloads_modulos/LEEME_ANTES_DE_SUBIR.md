# 📦 DESCARGA DE MÓDULOS ESP32 - SISTEMA OCEANOGRÁFICO

## ✅ Archivos Disponibles para Descarga

Se han generado **dos archivos .ino monolíticos** (todo en uno) listos para subir directamente desde Arduino IDE:

### 1. Nodo Tierra (Unidad Principal con Pantalla)
- **Archivo:** `NodoTierra_v17_Monolitico.ino`
- **Tamaño:** ~9.7 KB
- **Funciones:**
  - Pantalla TFT para visualización del plano cartesiano
  - Simulación de lecho marino irregular (5 armónicos)
  - Selección de punto de muestreo con botones físicos
  - Ajuste de profundidad objetivo
  - Envío de comandos vía ESP-NOW a la boya
  - Máquina de estados completa (IDLE, MOVIMIENTO, SELECCIÓN, MUESTREO)

### 2. Nodo Boya (Unidad Flotante con Sensores)
- **Archivo:** `NodoBoya_v6_Monolitico.ino`
- **Tamaño:** ~7.5 KB
- **Funciones:**
  - Recepción de comandos ESP-NOW del Nodo Tierra
  - Lectura simulada de sensores (profundidad, temperatura DS18B20)
  - Ejecución automática de protocolo de muestreo
  - Telemetría periódica
  - Perfil de lecho local sincronizado

---

## 📥 Opciones de Descarga

### Opción A: Descargar Paquete Completo (Recomendado)
**Archivo ZIP:** `Modulos_ESP32_v1.zip` (6.4 KB)
- Contiene ambos archivos `.ino`
- Listo para descomprimir y usar

### Opción B: Descargar por Módulo Individual
Cada archivo `.ino` es completamente independiente y autocontenido:
- `NodoTierra_v17_Monolitico.ino`
- `NodoBoya_v6_Monolitico.ino`

---

## 🚀 Instrucciones de Subida (Arduino IDE)

### Paso 1: Preparación
1. Abra Arduino IDE
2. Instale el core ESP32 (si no lo tiene):
   - Archivo → Preferencias → URLs adicionales: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
   - Herramientas → Placa → Gestor de placas → Busque "ESP32" → Instalar

### Paso 2: Instalar Librerías Requeridas

#### Para Nodo Tierra:
```
Herramientas → Gestionar Bibliotecas → Buscar e instalar:
- Adafruit GFX Library
- Adafruit ILI9341
```

#### Para Nodo Boya:
```
Herramientas → Gestionar Bibliotecas → Buscar e instalar:
- DallasTemperature
- OneWire
```

### Paso 3: Cargar Código

#### Para Nodo Tierra:
1. Abra `NodoTierra_v17_Monolitico.ino` en Arduino IDE
2. Conecte el ESP32 de la unidad tierra vía USB
3. Seleccione:
   - Placa: "ESP32 Dev Module"
   - Puerto: (el que corresponda a su dispositivo)
4. Presione el botón **Subir** (flecha derecha)
5. Espere a que termine (aparecerá "Done uploading")

#### Para Nodo Boya:
1. Abra `NodoBoya_v6_Monolitico.ino` en Arduino IDE
2. Conecte el ESP32 de la boya vía USB
3. Seleccione:
   - Placa: "ESP32 Dev Module"
   - Puerto: (el que corresponda a su dispositivo)
4. Presione el botón **Subir**
5. Espere a que termine

---

## 🔍 Verificación Post-Subida

### Monitor Serie (115200 baudios)

#### Nodo Tierra debe mostrar:
```
Iniciando Sistema...
Sistema Iniciado. Esperando comando...
```

#### Nodo Boya debe mostrar:
```
Iniciando Nodo Boya v6...
Boya lista. Esperando comandos del Nodo Tierra...
Telemetría - Prof: 3.XX Temp: 18.XX
```

---

## ⚙️ Configuración Importante

### Direcciones MAC (ESP-NOW)

En el código encontrará estas líneas que **DEBEN COINCIDIR** entre ambos módulos:

**En NodoTierra_v17_Monolitico.ino:**
```cpp
uint8_t boyaAddress[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
```

**En NodoBoya_v6_Monolitico.ino:**
```cpp
uint8_t tierraAddress[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
```

> **NOTA:** Estas son direcciones de ejemplo. Para producción, reemplace con las MAC reales de sus dispositivos ESP32. Puede obtenerlas ejecutando un sketch que imprima `WiFi.macAddress()`.

---

## 🎯 Flujo de Operación

1. **Encienda ambos nodos**
2. **En Nodo Tierra:**
   - Use los botones LEFT/RIGHT/UP/DOWN para moverse en el plano
   - Presione OK para entrar en modo "SELEC. PUNTO"
   - Ajuste la posición fina con los botones
   - Presione OK para fijar el punto
   - Ajuste la profundidad con UP/DOWN
   - Presione OK para iniciar muestreo

3. **En Nodo Boya:**
   - Recibirá automáticamente el comando
   - Ejecutará el protocolo de muestreo
   - Mostrará logs en Monitor Serie

---

## 🛠️ Solución de Problemas

| Problema | Solución |
|----------|----------|
| Error "esp_now_init failed" | Verifique que WiFi esté en modo STA antes de init |
| No hay comunicación entre nodos | Verifique que las MAC addresses coincidan exactamente |
| Pantalla TFT en blanco | Revise conexiones de pines (CS, DC, RST) |
| Botones no responden | Verifique que estén en INPUT_PULLUP y conectados a GND |

---

## 📝 Notas Técnicas

- **Versión del Firmware:** v17.1 (Tierra) / v6.1 (Boya)
- **Fecha de Release:** 2026-09-10
- **Tipo:** Monolítico (todo el código en un solo archivo .ino)
- **Protocolo:** ESP-NOW (sin necesidad de router WiFi)
- **Arquitectura:** FSM (Máquina de Estados Finitos)

---

## 🔄 Próximos Pasos Sugeridos

1. Reemplazar direcciones MAC de ejemplo con las reales
2. Conectar sensores físicos (DS18B20, MS5837, GPS)
3. Implementar debounce avanzado para botones
4. Agregar modo deep-sleep para ahorro energético
5. Implementar logging a SD card

---

**Generado automáticamente por el sistema de gestión de releases**
Para sugerencias o actualizaciones, ejecute el script de generación de releases.
