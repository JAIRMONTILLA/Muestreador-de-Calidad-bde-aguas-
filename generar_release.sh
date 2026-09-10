#!/bin/bash
# =============================================================================
# Script de Generación de Releases - Sistema Boya Oceanográfica
# =============================================================================
# Uso: ./generar_release.sh <version> "descripcion_corta"
# Ejemplo: ./generar_release.sh 2 "Mejoras en protocolos de muestreo"
# =============================================================================

set -e  # Salir en caso de error

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Función para mostrar uso correcto
mostrar_uso() {
    echo -e "${YELLOW}Uso:${NC} $0 <version> \"descripcion_corta\""
    echo -e "${YELLOW}Ejemplo:${NC} $0 2 \"Mejoras en protocolos de muestreo\""
    exit 1
}

# Verificar argumentos
if [ $# -lt 2 ]; then
    mostrar_uso
fi

VERSION=$1
DESCRIPCION=$2
FECHA=$(date +%Y-%m-%d)
DIRECTORIO_RELEASE="v${VERSION}_${FECHA}"
NOMBRE_ZIP="SistemaCompleto_v${VERSION}_${FECHA}.zip"

# Directorios
WORKSPACE="/workspace"
RELEASES_DIR="${WORKSPACE}/releases"
RELEASE_PATH="${RELEASES_DIR}/${DIRECTORIO_RELEASE}"

echo -e "${BLUE}╔════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║   Generando Release v${VERSION} - ${FECHA}${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════╝${NC}"
echo ""

# Paso 1: Verificar archivos requeridos
echo -e "${YELLOW}[1/7] Verificando archivos requeridos...${NC}"
ARCHIVOS_INO=($(find ${WORKSPACE} -maxdepth 1 -name "*.ino" -type f))

if [ ${#ARCHIVOS_INO[@]} -eq 0 ]; then
    echo -e "${RED}❌ Error: No se encontraron archivos .ino en ${WORKSPACE}${NC}"
    exit 1
fi

echo -e "${GREEN}✓ Archivos .ino encontrados:${NC}"
for archivo in "${ARCHIVOS_INO[@]}"; do
    echo "  - $(basename $archivo)"
done

# Verificar documentación
if [ ! -f "${WORKSPACE}/GUIA_SUBIDA_CODIGO.md" ]; then
    echo -e "${YELLOW}⚠️  Advertencia: GUIA_SUBIDA_CODIGO.md no encontrado${NC}"
else
    echo -e "${GREEN}✓ Documentación encontrada${NC}"
fi
echo ""

# Paso 2: Crear directorio de release
echo -e "${YELLOW}[2/7] Creando directorio de release...${NC}"
mkdir -p "${RELEASE_PATH}"
echo -e "${GREEN}✓ Directorio creado: ${RELEASE_PATH}${NC}"
echo ""

# Paso 3: Copiar archivos
echo -e "${YELLOW}[3/7] Copiando archivos al directorio de release...${NC}"
for archivo in "${ARCHIVOS_INO[@]}"; do
    cp "${archivo}" "${RELEASE_PATH}/"
    echo -e "${GREEN}  ✓ Copiado:$(basename $archivo)${NC}"
done

if [ -f "${WORKSPACE}/GUIA_SUBIDA_CODIGO.md" ]; then
    cp "${WORKSPACE}/GUIA_SUBIDA_CODIGO.md" "${RELEASE_PATH}/"
    echo -e "${GREEN}  ✓ Copiado: GUIA_SUBIDA_CODIGO.md${NC}"
fi

if [ -f "${WORKSPACE}/RELEASES.md" ]; then
    cp "${WORKSPACE}/RELEASES.md" "${RELEASE_PATH}/"
    echo -e "${GREEN}  ✓ Copiado: RELEASES.md${NC}"
fi
echo ""

# Paso 4: Generar ZIP
echo -e "${YELLOW}[4/7] Generando archivo ZIP autocontenido...${NC}"
cd "${RELEASE_PATH}"
zip -r "../${NOMBRE_ZIP}" . > /dev/null
cd "${WORKSPACE}"
echo -e "${GREEN}✓ ZIP generado: releases/${NOMBRE_ZIP}${NC}"

# Mostrar tamaño del ZIP
TAMANO=$(ls -lh "${RELEASES_DIR}/${NOMBRE_ZIP}" | awk '{print $5}')
echo -e "${GREEN}  Tamaño: ${TAMANO}${NC}"
echo ""

# Paso 5: Actualizar RELEASES.md
echo -e "${YELLOW}[5/7] Actualizando RELEASES.md...${NC}"

# Crear entrada para RELEASES.md
ENTRADA_RELEASE="
### v${VERSION}_${FECHA}
**Fecha:** ${FECHA}
**Descripción:** ${DESCRIPCION}
**Módulos Incluidos:**
"

# Extraer nombres de archivos .ino (solo nombres, sin ruta)
for archivo in "${ARCHIVOS_INO[@]}"; do
    NOMBRE=$(basename "$archivo")
    ENTRADA_RELEASE+="- \`${NOMBRE}\`
"
done

ENTRADA_RELEASE+="
**Cambios Principales:**
- [Agregar lista de cambios principales]

**Descarga:** \`releases/${NOMBRE_ZIP}\` (${TAMANO})

---
"

# Insertar entrada después del encabezado principal
if [ -f "${WORKSPACE}/RELEASES.md" ]; then
    # Crear archivo temporal con la nueva entrada
    TEMP_FILE=$(mktemp)
    
    # Insertar después de la primera línea que contenga "Historial de Releases" o similar
    if grep -q "## 📋 Historial de Releases" "${WORKSPACE}/RELEASES.md"; then
        awk -v entrada="$ENTRADA_RELEASE" '
        /## 📋 Historial de Releases/ {
            print $0
            print entrada
            next
        }
        {print}
        ' "${WORKSPACE}/RELEASES.md" > "${TEMP_FILE}"
        mv "${TEMP_FILE}" "${WORKSPACE}/RELEASES.md"
    else
        # Si no existe la sección, agregar al final
        echo -e "$ENTRADA_RELEASE" >> "${WORKSPACE}/RELEASES.md"
    fi
    
    echo -e "${GREEN}✓ RELEASES.md actualizado${NC}"
else
    echo -e "${YELLOW}⚠️  RELEASES.md no existe, creando uno nuevo...${NC}"
    cat > "${WORKSPACE}/RELEASES.md" << EOF
# 📦 Historial de Releases - Boya Oceanográfica

## 📋 Historial de Releases
$ENTRADA_RELEASE
EOF
    echo -e "${GREEN}✓ RELEASES.md creado${NC}"
fi
echo ""

# Paso 6: Limpiar directorio temporal
echo -e "${YELLOW}[6/7] Limpiando archivos temporales...${NC}"
rm -rf "${RELEASE_PATH}"
echo -e "${GREEN}✓ Directorio temporal eliminado${NC}"
echo ""

# Paso 7: Verificación final
echo -e "${YELLOW}[7/7] Verificación final...${NC}"
if [ -f "${RELEASES_DIR}/${NOMBRE_ZIP}" ]; then
    echo -e "${GREEN}✓ Verificación exitosa${NC}"
    
    # Listar contenido del ZIP
    echo ""
    echo -e "${BLUE}Contenido del ZIP:${NC}"
    unzip -l "${RELEASES_DIR}/${NOMBRE_ZIP}" | tail -n +4 | head -n -2
else
    echo -e "${RED}❌ Error: El archivo ZIP no se generó correctamente${NC}"
    exit 1
fi
echo ""

# Resumen final
echo -e "${GREEN}╔════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║          ✅ Release v${VERSION} generada exitosamente${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${BLUE}📦 Archivo generado:${NC} releases/${NOMBRE_ZIP}"
echo -e "${BLUE}📅 Fecha:${NC} ${FECHA}"
echo -e "${BLUE}📝 Descripción:${NC} ${DESCRIPCION}"
echo -e "${BLUE}📊 Tamaño:${NC} ${TAMANO}"
echo ""
echo -e "${YELLOW}Próximos pasos recomendados:${NC}"
echo "  1. Verificar integridad del ZIP: unzip -t releases/${NOMBRE_ZIP}"
echo "  2. Probar extracción: unzip -l releases/${NOMBRE_ZIP}"
echo "  3. Respaldar en ubicación externa"
echo "  4. Actualizar documentación si es necesario"
echo ""
echo -e "${YELLOW}Para extraer y probar:${NC}"
echo "  unzip releases/${NOMBRE_ZIP} -d /tmp/release_v${VERSION}_test"
echo ""
