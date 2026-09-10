import xml.etree.ElementTree as ET
from xml.dom import minidom

# Configuración de pines extraída del código generado anteriormente
# Nodo Boya
boya_pins = {
    "ESP32": {"type": "mcu", "label": "ESP32 DevKit V1"},
    "DHT22": {"pin": "GPIO 4", "type": "sensor", "label": "Temp/Humedad Aire"},
    "DS18B20": {"pin": "GPIO 5", "type": "sensor", "label": "Temp Agua"},
    "TURBIDEZ": {"pin": "GPIO 34 (ADC)", "type": "sensor", "label": "Sensor Turbidez"},
    "PH": {"pin": "GPIO 35 (ADC)", "type": "sensor", "label": "Sensor pH"},
    "BOMBA_1": {"pin": "GPIO 26", "type": "actuator", "label": "Bomba Peristáltica 1"},
    "BOMBA_2": {"pin": "GPIO 27", "type": "actuator", "label": "Bomba Peristáltica 2"},
    "MOTOR_NEMA": {"pins": ["GPIO 12", "GPIO 13", "GPIO 14", "GPIO 15"], "type": "actuator", "label": "Motor NEMA (Driver)"},
    "LED_STATUS": {"pin": "GPIO 2", "type": "indicator", "label": "LED Estado"},
    "BTN_CONF": {"pin": "GPIO 0", "type": "input", "label": "Botón Config"}
}

# Nodo Tierra
tierra_pins = {
    "ESP32": {"type": "mcu", "label": "ESP32 DevKit V1"},
    "TFT_CS": {"pin": "GPIO 5", "type": "display", "label": "TFT CS"},
    "TFT_DC": {"pin": "GPIO 4", "type": "display", "label": "TFT DC"},
    "TFT_MOSI": {"pin": "GPIO 23", "type": "display", "label": "TFT MOSI"},
    "TFT_CLK": {"pin": "GPIO 18", "type": "display", "label": "TFT CLK"},
    "TFT_RST": {"pin": "GPIO 2", "type": "display", "label": "TFT RST"},
    "SDA": {"pin": "GPIO 21", "type": "comm", "label": "I2C SDA (Opcional)"},
    "SCL": {"pin": "GPIO 22", "type": "comm", "label": "I2C SCL (Opcional)"},
    "BTN_UP": {"pin": "GPIO 34", "type": "input", "label": "Botón Arriba"},
    "BTN_DOWN": {"pin": "GPIO 35", "type": "input", "label": "Botón Abajo"},
    "BTN_OK": {"pin": "GPIO 36", "type": "input", "label": "Botón OK"},
    "BTN_LEFT": {"pin": "GPIO 39", "type": "input", "label": "Botón Izq"},
    "BTN_RIGHT": {"pin": "GPIO 13", "type": "input", "label": "Botón Der"}
}

def create_svg_component(x, y, label, pin_info, color="#ffffff"):
    # Componente base
    g = ET.Element('g')
    
    # Caja del componente
    rect = ET.SubElement(g, 'rect', {
        'x': str(x), 'y': str(y), 'width': '140', 'height': '60',
        'fill': color, 'stroke': '#000000', 'stroke-width': '2', 'rx': '5'
    })
    
    # Texto etiqueta principal
    text_label = ET.SubElement(g, 'text', {
        'x': str(x + 70), 'y': str(y + 25),
        'font-family': 'Arial', 'font-size': '12', 'font-weight': 'bold',
        'text-anchor': 'middle', 'fill': '#000000'
    })
    text_label.text = label
    
    # Texto PIN
    if isinstance(pin_info, dict):
        if 'pin' in pin_info:
            pin_text = ET.SubElement(g, 'text', {
                'x': str(x + 70), 'y': str(y + 45),
                'font-family': 'Courier', 'font-size': '10',
                'text-anchor': 'middle', 'fill': '#333333'
            })
            pin_text.text = f"Pin: {pin_info['pin']}"
        elif 'pins' in pin_info:
            pins_str = ", ".join(pin_info['pins'])
            pin_text = ET.SubElement(g, 'text', {
                'x': str(x + 70), 'y': str(y + 45),
                'font-family': 'Courier', 'font-size': '9',
                'text-anchor': 'middle', 'fill': '#333333'
            })
            pin_text.text = f"Pins: {pins_str}"
            
    return g

def create_connection_line(x1, y1, x2, y2, color="#0000FF", label=""):
    line_group = ET.Element('g')
    
    # Línea principal
    line = ET.SubElement(line_group, 'line', {
        'x1': str(x1), 'y1': str(y1), 'x2': str(x2), 'y2': str(y2),
        'stroke': color, 'stroke-width': '2'
    })
    
    # Círculos en extremos
    ET.SubElement(line_group, 'circle', {'cx': str(x1), 'cy': str(y1), 'r': '3', 'fill': color})
    ET.SubElement(line_group, 'circle', {'cx': str(x2), 'cy': str(y2), 'r': '3', 'fill': color})
    
    if label:
        # Calcular punto medio para etiqueta
        mid_x = (x1 + x2) / 2
        mid_y = (y1 + y2) / 2
        text = ET.SubElement(line_group, 'text', {
            'x': str(mid_x + 5), 'y': str(mid_y - 5),
            'font-family': 'Arial', 'font-size': '9', 'fill': color
        })
        text.text = label
        
    return line_group

def generate_schematic(pins_data, filename, title):
    svg = ET.Element('svg', {
        'xmlns': 'http://www.w3.org/2000/svg',
        'width': '800', 'height': '1000', 'viewBox': '0 0 800 1000'
    })
    
    # Fondo
    ET.SubElement(svg, 'rect', {'width': '100%', 'height': '100%', 'fill': '#f4f4f4'})
    
    # Título
    title_elem = ET.SubElement(svg, 'text', {
        'x': '400', 'y': '40', 'font-family': 'Arial', 'font-size': '20', 
        'font-weight': 'bold', 'text-anchor': 'middle'
    })
    title_elem.text = title
    
    # Posicionar ESP32 central
    esp_x, esp_y = 330, 400
    mcu_box = create_svg_component(esp_x, esp_y, "ESP32 DEV KIT", {"pin": "MCU"}, "#FFD700")
    svg.append(mcu_box)
    
    # Layout de componentes alrededor del ESP32
    sensors_y = 100
    actuators_y = 700
    left_x = 50
    right_x = 600
    
    sensor_count = 0
    actuator_count = 0
    
    for name, info in pins_data.items():
        if info['type'] == 'mcu':
            continue
            
        if info['type'] in ['sensor', 'input', 'display', 'comm', 'indicator']:
            # Lado izquierdo o arriba
            if sensor_count % 2 == 0:
                cx, cy = left_x, sensors_y + (sensor_count // 2) * 90
            else:
                cx, cy = right_x, sensors_y + (sensor_count // 2) * 90
            
            box = create_svg_component(cx, cy, info['label'], info, "#ADD8E6")
            svg.append(box)
            
            # Conectar al ESP32
            # Punto de conexión en el componente (centro abajo)
            start_x, start_y = cx + 70, cy + 60
            # Punto de conexión en ESP32 (variar para no superponer)
            end_x = esp_x + 70 if cx > esp_x else esp_x
            end_y = esp_y - 10 - (sensor_count * 5)
            
            line_color = "#0000FF" if info['type'] == 'sensor' else "#008000"
            conn = create_connection_line(start_x, start_y, end_x, end_y, line_color, info.get('pin', '') if isinstance(info.get('pin'), str) else "")
            svg.append(conn)
            sensor_count += 1
            
        elif info['type'] == 'actuator':
            # Abajo
            cx = 100 + (actuator_count % 3) * 200
            cy = actuators_y + (actuator_count // 3) * 100
            
            box = create_svg_component(cx, cy, info['label'], info, "#FFB6C1")
            svg.append(box)
            
            # Conectar al ESP32
            start_x, start_y = cx + 70, cy
            end_x = esp_x + 20 + (actuator_count * 15)
            end_y = esp_y + 60
            
            conn = create_connection_line(start_x, start_y, end_x, end_y, "#FF0000", "Signal")
            svg.append(conn)
            
            # Línea de Energía (Simulada)
            power_line = ET.SubElement(svg, 'line', {
                'x1': str(cx + 140), 'y1': str(cy + 10), 'x2': str(cx + 140), 'y2': str(cy - 40),
                'stroke': '#000000', 'stroke-width': '3', 'stroke-dasharray': '5,5'
            })
            power_label = ET.SubElement(svg, 'text', {
                'x': str(cx + 145), 'y': str(cy - 50), 'font-family': 'Arial', 'font-size': '10', 'fill': '#000000'
            })
            power_label.text = "VIN (5V/12V)"
            
            actuator_count += 1

    # Leyenda
    legend_y = 900
    ET.SubElement(svg, 'text', {'x': '50', 'y': str(legend_y), 'font-family': 'Arial', 'font-size': '14', 'font-weight': 'bold'}).text = "Leyenda:"
    
    items = [
        ("#ADD8E6", "Sensores / Entradas"),
        ("#FFB6C1", "Actuadores (Motores/Bombas)"),
        ("#FFD700", "Microcontrolador"),
        ("#0000FF", "Señal Digital"),
        ("#008000", "Señal Analógica/Datos"),
        ("#FF0000", "Control Actuador"),
        ("#000000 (Discontinua)", "Alimentación Externa")
    ]
    
    for i, (color, label) in enumerate(items):
        x = 50 + (i % 4) * 190
        y = legend_y + 25 + (i // 4) * 30
        ET.SubElement(svg, 'rect', {'x': str(x), 'y': str(y-15), 'width': '20', 'height': '20', 'fill': color, 'stroke': '#000'})
        ET.SubElement(svg, 'text', {'x': str(x+25), 'y': str(y), 'font-family': 'Arial', 'font-size': '12'}).text = label

    # Guardar XML bonito
    xml_str = minidom.parseString(ET.tostring(svg)).toprettyxml(indent="   ")
    with open(filename, 'w') as f:
        f.write(xml_str)
    print(f"Generado: {filename}")

# Generar diagramas
generate_schematic(boya_pins, '/workspace/Diagrama_Cableado_NodoBoya.svg', 'Esquema de Conexiones - NODO BOYA (v6)')
generate_schematic(tierra_pins, '/workspace/Diagrama_Cableado_NodoTierra.svg', 'Esquema de Conexiones - NODO TIERRA (v17)')

print("Diagramas SVG generados exitosamente.")
