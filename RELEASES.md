# 📦 Sistema de Versiones y Releases - Boya Oceanográfica

## 🎯 Política de Versiones

### Estructura de Nomenclatura
```
v{MAJOR}_{YYYY-MM-DD}
```
- **MAJOR**: Número de versión principal (incrementa con cambios breaking)
- **YYYY-MM-DD**: Fecha de la release (AAAA-MM-DD)

### Ejemplos:
- `v1_2026-09-01` - Primera release oficial (01/09/2026)
- `v2_2026-09-15` - Segunda release con mejoras (15/09/2026)

---

## 📁 Estructura de Directorios

```
/workspace/
├── releases/
│   ├── v1_2026-09-01/
│   │   ├── NodoTierra_v17_2026-09-01.ino
│   │   ├── NodoBoya_v6_2026-09-01.ino
│   │   └── GUIA_SUBIDA_CODIGO.md
│   └── SistemaCompleto_v1_2026-09-01.zip  ← Descarga única
├── NodoTierra_v17_2026-09-01.ino
├── NodoBoya_v6_2026-09-01.ino
└── RELEASES.md
```

---

## 🔄 Flujo de Trabajo para Nuevas Releases

### Paso 1: Identificar Cambios Necesarios
Cuando se modifique un módulo, evaluar si otros módulos requieren actualización:

| Módulo Modificado | ¿Afecta a? | Acción Requerida |
|------------------|------------|------------------|
| **Nodo Tierra** (protocolos) | Nodo Boya | ✅ Actualizar ambos |
| **Nodo Tierra** (UI) | Ninguno | ⚠️ Solo Nodo Tierra |
| **Nodo Boya** (sensores) | Nodo Tierra | ✅ Actualizar ambos |
| **Nodo Boya** (ESP-NOW) | Nodo Tierra | ✅ Actualizar ambos |
| **Ambos** (estructura datos) | Ambos | ✅ Actualizar ambos |

### Paso 2: Crear Nueva Versión
```bash
# 1. Crear directorio de release
mkdir -p /workspace/releases/v{VERSION}_{FECHA}

# 2. Copiar archivos actualizados
cp NodoTierra_*.ino NodoBoya_*.ino GUIA_*.md /workspace/releases/v{VERSION}_{FECHA}/

# 3. Crear ZIP autocontenido
cd /workspace/releases/v{VERSION}_{FECHA}
zip -r ../SistemaCompleto_v{VERSION}_{FECHA.zip .

# 4. Verificar
ls -lh /workspace/releases/
```

### Paso 3: Documentar Cambios
Actualizar esta sección con:
- Fecha de release
- Módulos modificados
- Descripción de cambios
- Motivo de la actualización

---

## 📋 Historial de Releases

### v1_2026-09-01 (Release Inicial)
**Fecha:** 01 de Septiembre, 2026  
**Módulos Incluidos:**
- `NodoTierra_v17_2026-09-01.ino`
- `NodoBoya_v6_2026-09-01.ino`
- `GUIA_SUBIDA_CODIGO.md`

**Cambios Principales:**
- ✅ Simulación de desplazamiento con lecho irregular (5 armónicos)
- ✅ Selección de punto de muestreo en plano cartesiano
- ✅ Control de profundidad con botones arriba/abajo
- ✅ Compatibilidad total entre Nodo Tierra y Nodo Boya vía ESP-NOW
- ✅ Estados FSM sincronizados entre módulos
- ✅ Visualización de perfil de lecho en tiempo real

**Descarga:** `releases/SistemaCompleto_v1_2026-09-01.zip` (51 KB)

---

## 📥 Cómo Descargar

### Opción A: Release Completa (Recomendado)
```bash
# Descargar todo el sistema en un solo archivo
wget /workspace/releases/SistemaCompleto_v1_2026-09-01.zip
unzip SistemaCompleto_v1_2026-09-01.zip
```

### Opción B: Archivos Individuales
Cada archivo `.ino` está disponible en:
- `/workspace/NodoTierra_v17_2026-09-01.ino`
- `/workspace/NodoBoya_v6_2026-09-01.ino`

---

## 🔔 Cuándo Sugerir Actualización de Módulos

### Señales de Alerta 🚨

1. **Cambio en estructura de datos ESP-NOW**
   ```cpp
   // Si modificas esto en un lado...
   typedef struct {
       float temperatura;
       float profundidad;
       uint8_t estado;
   } DatosBoya;
   
   // ...DEBES actualizarlo en el otro lado también
   ```

2. **Nuevo estado en la FSM**
   ```cpp
   // Si agregas ESTADO_NUEVO en Nodo Tierra
   #define ESTADO_NUEVO 15
   
   // Debes agregarlo en Nodo Boya también
   enum EstadoBoya {
       ESTADO_NUEVO = 15,  // ← Agregar
   };
   ```

3. **Cambio en protocolo de comunicación**
   - Nuevos comandos
   - Modificación de tiempos de espera
   - Cambio en formato de mensajes

4. **Modificación en protocolos de muestreo**
   - Nuevas profundidades
   - Cambios en tiempos de muestreo
   - Adición/eliminación de protocolos

### Matriz de Dependencias

| Cambio en | Requiere actualizar | Prioridad |
|-----------|---------------------|-----------|
| Estructura `DatosBoya` | Ambos nodos | 🔴 CRÍTICA |
| Estados FSM | Ambos nodos | 🔴 CRÍTICA |
| Protocolos muestreo | Ambos nodos | 🟠 ALTA |
| UI Nodo Tierra | Solo Nodo Tierra | 🟡 MEDIA |
| Sensores Nodo Boya | Solo Nodo Boya | 🟡 MEDIA |
| Configuración WiFi | Solo Nodo Tierra | 🟢 BAJA |

---

## 🛠️ Comandos Útiles

### Ver releases disponibles
```bash
ls -lh /workspace/releases/
```

### Extraer una release específica
```bash
cd /workspace/releases
unzip SistemaCompleto_v1_2026-09-01.zip -d v1_extruido
```

### Comparar versiones
```bash
diff NodoTierra_v17_2026-09-01.ino NodoTierra_v18_2026-09-15.ino
```

### Limpiar archivos temporales
```bash
rm -rf /workspace/releases/v1_2026-09-01/  # Solo dejar el ZIP
```

---

## 📝 Checklist para Nueva Release

- [ ] Identificar todos los módulos afectados por los cambios
- [ ] Actualizar códigos fuente (.ino)
- [ ] Probar comunicación ESP-NOW entre módulos
- [ ] Actualizar documentación (GUIA_SUBIDA_CODIGO.md)
- [ ] Crear directorio de release con fecha
- [ ] Copiar todos los archivos necesarios
- [ ] Generar ZIP autocontenido
- [ ] Verificar integridad del ZIP
- [ ] Actualizar RELEASES.md con nueva entrada
- [ ] Eliminar directorios temporales (dejar solo ZIPs)

---

## 💡 Mejores Prácticas

1. **Nunca sobrescribir releases anteriores** - Cada release es inmutable
2. **Siempre incluir ambos módulos** - Aunque solo uno cambie, para compatibilidad
3. **Documentar TODOS los cambios** - Incluso los pequeños
4. **Probar antes de liberar** - Verificar comunicación ESP-NOW
5. **Mantener histórico** - Los ZIPs son el backup oficial
6. **Usar fechas ISO** - YYYY-MM-DD para ordenamiento correcto

---

## 🆘 Soporte

Para sugerir cambios o reportar problemas:
1. Revisar matriz de dependencias
2. Identificar módulos afectados
3. Proponer nueva versión siguiendo este documento
4. Generar release siguiendo el flujo establecido
