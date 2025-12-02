# flake8: noqa: W291, E501
# pyright: reportMissingImports=false

# ----------------------------------------------------------------------
# MÓDULO PRINCIPAL: LECTOR DE TEMPERATURA DS18B20 Y DISPLAY ILI9341
# Este script inicializa el sensor de temperatura y el display TFT,
# y utiliza una técnica de dibujo diferencial para actualizar solo
# los dígitos que cambian, eliminando el parpadeo en la pantalla.
# ----------------------------------------------------------------------

# pyright: reportMissingImports=false
from machine import Pin, SPI
import time
import onewire
import ds18x20
import ili9341
# Importamos FrameBuffer y ustruct para la renderización de texto
from micropython import const
from ustruct import pack
from framebuf import FrameBuffer, MONO_VLSB


# ----------------------------------------------------------------------
# CONFIGURACIÓN DE PINES Y COLORES
# Define las constantes de hardware (pines GPIO) para el ESP32-C6,
# y las definiciones de color en formato 565 para el display.
# ----------------------------------------------------------------------

# Reutilizamos la función de color del driver para asegurar compatibilidad
def color565(r, g, b):
    """Convierte colores RGB a la representación 565 de 16-bit."""
    return (r & 0xf8) << 8 | (g & 0xfc) << 3 | b >> 3


# Pines para el sensor
DS18B20_PIN = 4     # Pin de datos para el sensor DS18B20 (GPIO 4)

# Pines para el display ILI9341 (ESP32-C6)
SPI_BUS = 1    # Bus SPI
SCK_PIN = 6    # Clock (CLK/SCK)  
MOSI_PIN = 7   # Data (MOSI)      
MISO_PIN = 10  # Data (MISO)      
CS_PIN = 5     # Chip Select (CS)
DC_PIN = 9     # Data/Command (DC)
RST_PIN = 8    # Reset (RST)      

# Definiciones de color
COLOR_BLANCO = color565(255, 255, 255)
COLOR_NEGRO = color565(0, 0, 0)
COLOR_AZUL = color565(0, 0, 255)
COLOR_ROJO = color565(255, 0, 0)

# Constantes de escalado y limpieza
# Aumentamos a 9 para dar espacio a valores como -10.00 C (8 chars) + un espacio
MAX_TEMP_WIDTH_CHARS = 9
SCALE_TITLE = 2 # Escala para el título
SCALE_TEMP = 3  # Escala para la temperatura principal
# El string inicial se usa para forzar el primer dibujo completo
MAX_CLEAR_STR = " " * MAX_TEMP_WIDTH_CHARS


# ----------------------------------------------------------------------
# FUNCIÓN DE DIBUJO DE TEXTO ESCALADO (FRAMEBUFFER)
# Implementa el dibujo de texto simple (8x8) con escalado de píxeles
# usando FrameBuffer para pre-renderizar y luego enviar el bloque al display.
# ----------------------------------------------------------------------

# Buffer de 8x8 píxeles en formato 1-bit (MONO_VLSB)
CHAR_BUF = bytearray(8)
CHAR_FB = FrameBuffer(CHAR_BUF, 8, 8, MONO_VLSB)


def draw_text_fb(display, text, x, y, fg_color, bg_color, scale=1):
    """
    Dibuja texto simple (fuente 8x8) escalado por 'scale'.
    Optimizado para dibujar un solo carácter (len(text) == 1) en una posición fija.
    """
    char_width = 8 * scale
    char_height = 8 * scale
    
    fg_bytes = pack('>H', fg_color)
    bg_bytes = pack('>H', bg_color)
    
    for char in text:
        # 1. Dibuja el carácter en el FrameBuffer de 1-bit
        CHAR_FB.fill(0)
        CHAR_FB.text(char, 0, 0, 1)
        
        # Buffer para el bloque escalado (8*scale x 8*scale * 2 bytes/pixel)
        scaled_buffer = bytearray(char_width * char_height * 2)

        # 2. Mapea y escala el buffer de 1-bit al buffer de 16-bit
        for i in range(8):  # Fila original (0-7)
            for j in range(8):  # Columna original (0-7)
                is_on = CHAR_FB.pixel(j, i)
                color_bytes = fg_bytes if is_on else bg_bytes
                
                # Rellena el bloque de scale x scale píxeles
                for sy in range(scale):
                    for sx in range(scale):
                        pixel_x = j * scale + sx
                        pixel_y = i * scale + sy
                        
                        idx = (pixel_y * char_width + pixel_x) * 2
                        scaled_buffer[idx] = color_bytes[0]
                        scaled_buffer[idx + 1] = color_bytes[1]

        # 3. Envía el bloque escalado al display
        display.block(x, y, x + char_width - 1, y + char_height - 1, scaled_buffer)
        
        # 4. Mueve la posición X para el siguiente carácter (solo relevante si len(text)>1)
        x += char_width
        
        if x + char_width > display.width:
            x = 0
            y += char_height
        if y + char_height > display.height:
            return


def center_text_x(display, text_len, scale):
    """Calcula la coordenada X inicial para centrar texto."""
    width_pixels = text_len * 8 * scale
    return (display.width - width_pixels) // 2


# ----------------------------------------------------------------------
# INICIALIZACIÓN DE HARDWARE
# Inicializa las interfaces de comunicación SPI para el ILI9341 y OneWire
# para el sensor DS18B20, verificando la presencia del sensor.
# ----------------------------------------------------------------------
# 1. Inicialización de DS18B20
ow = onewire.OneWire(Pin(DS18B20_PIN))
ds = ds18x20.DS18X20(ow)
try:
    roms = ds.scan()
    if not roms:
        raise RuntimeError(f"No se encontró DS18B20 en GPIO {DS18B20_PIN}.")
    
    print(f"✅ Sensor DS18B20 encontrado en ID: {roms[0]}")
    sensor_id = roms[0]
except Exception as e:
    print(f"❌ ERROR: Problema al inicializar OneWire o al escanear: {e}")
    sensor_id = None
    while True:
        time.sleep(1)
# 2. Inicialización de la pantalla ILI9341
try:
    # Configuración SPI para ESP32-C6
    spi = SPI(SPI_BUS, baudrate=40000000, polarity=1, phase=1,
              sck=Pin(SCK_PIN), mosi=Pin(MOSI_PIN), miso=Pin(MISO_PIN))

    display = ili9341.Display(
        spi,
        cs=Pin(CS_PIN),
        dc=Pin(DC_PIN),
        rst=Pin(RST_PIN),
        width=320,
        height=240,
        rotation=90 # 90 grados para orientación apaisada
    )
    
    display.clear(COLOR_BLANCO)
    
    # --- POSICIONAMIENTO Y ESCALADO DEL TEXTO FIJO ---
    
    # Título (Escala 2)
    TITLE_TEXT = "Dallas 18b20"
    x_title = center_text_x(display, len(TITLE_TEXT), SCALE_TITLE)
    draw_text_fb(
        display,
        TITLE_TEXT,
        x_title, 20, COLOR_NEGRO, COLOR_BLANCO, scale=SCALE_TITLE
    )
        
    print("✅ Display ILI9341 inicializado con éxito.")
    
except Exception as e:
    print(f"❌ ERROR FATAL al inicializar el display: {e}")
    while True:
        time.sleep(1)


# ----------------------------------------------------------------------
# BUCLE PRINCIPAL DE LECTURA Y ACTUALIZACIÓN DIFERENCIAL
# Lee la temperatura del sensor, compara el nuevo string con el anterior,
# y redibuja solo los caracteres que han cambiado para evitar el parpadeo.
# ----------------------------------------------------------------------
print("--- Iniciando lectura del DS18B20 ---")

# Calculamos la posición X central para el bloque completo de 9 caracteres
start_x_block = center_text_x(display, MAX_TEMP_WIDTH_CHARS, SCALE_TEMP)
pos_temp_y = 100
char_pixel_width = 8 * SCALE_TEMP # 8 píxeles de fuente * factor de escala (24 píxeles)

# Inicializamos la cadena de la temperatura anterior a espacios.
# Esto fuerza el primer dibujo completo (como un "borrado" implícito).
ultima_temp_str_padded = MAX_CLEAR_STR

while True:
    try:
        # 1. Lectura del sensor
        ds.convert_temp()
        time.sleep_ms(750)
        temp_celsius = ds.read_temp(sensor_id)
        temp_str = f"{temp_celsius:.2f} C"
        
        print(f"[LECTURA] Temperatura: {temp_str}")
        
        # 2. Asegurar que la nueva cadena tenga el ancho máximo (rellenando con espacios a la derecha)
        # CORRECCIÓN: Reemplazar str.ljust() por padding manual compatible con MicroPython
        padding_needed = MAX_TEMP_WIDTH_CHARS - len(temp_str)
        if padding_needed > 0:
            new_temp_str_padded = temp_str + (" " * padding_needed)
        else:
            new_temp_str_padded = temp_str
        
        # 3. Comparar y actualizar solo los caracteres que han cambiado
        for i in range(MAX_TEMP_WIDTH_CHARS):
            new_char = new_temp_str_padded[i]
            old_char = ultima_temp_str_padded[i]
            
            # Solo dibuja si el carácter ha cambiado
            if new_char != old_char:
                # Calcular la posición X absoluta de este carácter
                char_x = start_x_block + (i * char_pixel_width)
                
                # Dibujar solo el carácter que cambió (incluso si es un espacio, que actúa como borrado)
                draw_text_fb(
                    display,
                    new_char,
                    char_x, pos_temp_y,
                    COLOR_NEGRO, COLOR_BLANCO, scale=SCALE_TEMP
                )
        
        # 4. Actualizar la cadena almacenada para la siguiente iteración
        ultima_temp_str_padded = new_temp_str_padded
        
        # Esperar un poco antes de la siguiente lectura
        time.sleep_ms(250)
        
    except Exception as e:
        print(f"❌ ERROR de Lectura o Display en el bucle: {e}")
        time.sleep(1)