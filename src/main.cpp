

#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include "FS.h"
#include "SPIFFS.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include "esp32-hal-cpu.h"
#include "BluetoothSerial.h"

// --- Configuración de Hardware ---
#define PIN_BL 32
#define PIN_RELE1 33
#define PIN_RELE2 26
#define ONE_WIRE_BUS 27

// --- Instancias ---
TFT_eSPI tft = TFT_eSPI();
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
BluetoothSerial SerialBT;

// --- Variables de Estado (Iniciamos explícitamente en TRUE) ---
bool sistemaEstado = false;
bool pantallaEncendida = true; // Forzar estado activo
bool btActivo = false;
unsigned long lastRelayMillis = 0;
unsigned long lastTempMillis = 0;
bool estadoRele = false;

// --- Colores ---
#define COL_FONDO 0x0842
#define COL_CARD 0x10A4
#define COL_ACCENT 0x03EF
#define COL_BTN_ON 0x2661
#define COL_BTN_OFF 0x114F
#define COL_TEXTO 0xFFFF
#define COL_SUBTEXTO 0xAD75

// --- Geometría ---
const int btnY = 250;
const int btnH = 60;
const int btnW = 105;
const int btn1X = 10;
const int btn2X = 125;
int cardH = 85;

// Prototipos
void dibujarInterfazBase();
void dibujarBotonSistema(bool estado);
void dibujarBotonBL();
void touch_calibrate();
void gestionarModoEnergia(bool despertar);
void actualizarVisualReles();
void actualizarTemperaturas();
void toggleBluetooth();
void enviarReporteEstado();

void btCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
  if (event == ESP_SPP_SRV_OPEN_EVT)
  {
    Serial.println("\n[BT] ¡Cliente conectado!");
    delay(200);
    enviarReporteEstado();
  }
}

void setup()
{
  // 1. Iniciar Serial y Frecuencia Máxima
  Serial.begin(115200);
  setCpuFrequencyMhz(240);

  // 2. Configurar pines de relés
  pinMode(PIN_RELE1, OUTPUT);
  pinMode(PIN_RELE2, OUTPUT);
  digitalWrite(PIN_RELE1, LOW);
  digitalWrite(PIN_RELE2, HIGH);

  // 3. Inicializar Sensores y FS
  sensors.begin();
  if (!SPIFFS.begin(true))
    Serial.println("Error SPIFFS");

  // 4. Inicializar Pantalla (La librería podría intentar apagar el pin 32 aquí)
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  // 5. Configuración de Bluetooth
  SerialBT.register_callback(btCallback);

  // 6. Dibujar Interfaz (Aún con la luz apagada para evitar ver el "dibujado")
  touch_calibrate();
  dibujarInterfazBase();
  dibujarBotonSistema(sistemaEstado);
  dibujarBotonBL();
  actualizarVisualReles();
  actualizarTemperaturas();

  // 7. FINAL DEL SETUP: FORZAR ENCENDIDO DE LUZ
  pinMode(PIN_BL, OUTPUT);
  digitalWrite(PIN_BL, LOW); // 1 = ON
  pantallaEncendida = true;

  Serial.println("--- Sistema Iniciado y Pantalla ON ---");
}

void loop()
{
  uint16_t x, y;

  // Escucha Bluetooth
  if (btActivo && SerialBT.available())
  {
    String incoming = SerialBT.readStringUntil('\n');
    Serial.println("BT RECIBIDO: " + incoming);
  }

  // Sensores cada 2s
  if (millis() - lastTempMillis >= 2000)
  {
    lastTempMillis = millis();
    sensors.requestTemperatures();
    if (pantallaEncendida)
      actualizarTemperaturas();
  }

  // Relés cada 3s
  if (millis() - lastRelayMillis >= 3000)
  {
    lastRelayMillis = millis();
    estadoRele = !estadoRele;
    digitalWrite(PIN_RELE1, estadoRele);
    digitalWrite(PIN_RELE2, !estadoRele);
    if (pantallaEncendida)
      actualizarVisualReles();
  }

  if (tft.getTouch(&x, &y, 250))
  {
    // Esquina superior derecha: Toggle BT
    if (y < 40 && x > 180)
    {
      toggleBluetooth();
      delay(300);
      return;
    }

    if (!pantallaEncendida)
    {
      // Área de botón para despertar
      if ((x > btn2X) && (x < (btn2X + btnW)) && (y > btnY) && (y < (btnY + btnH)))
      {
        gestionarModoEnergia(true);
        delay(300);
      }
    }
    else
    {
      // Botón Táctil ON/OFF
      if ((x > btn1X) && (x < (btn1X + btnW)) && (y > btnY) && (y < (btnY + btnH)))
      {
        sistemaEstado = !sistemaEstado;
        dibujarBotonSistema(sistemaEstado);
        Serial.println("Tactil presionado: " + String(sistemaEstado ? "ON" : "OFF"));
        enviarReporteEstado();
        delay(350);
      }
      // Botón Sleep
      else if ((x > btn2X) && (x < (btn2X + btnW)) && (y > btnY) && (y < (btnY + btnH)))
      {
        gestionarModoEnergia(false);
      }
    }
    while (tft.getTouch(&x, &y, 250))
      ;
  }
}

void enviarReporteEstado()
{
  float t1 = sensors.getTempCByIndex(0);
  float t2 = sensors.getTempCByIndex(1);

  String reporte = "\n--- REPORTE ESP32 ---\n";
  reporte += "S1: " + (t1 == DEVICE_DISCONNECTED_C ? "ERR" : String(t1, 1) + "C") + " | ";
  reporte += "S2: " + (t2 == DEVICE_DISCONNECTED_C ? "ERR" : String(t2, 1) + "C") + "\n";
  reporte += "Reles: K1=" + String(estadoRele ? "ON" : "OFF") + " K2=" + String(!estadoRele ? "ON" : "OFF") + "\n";
  reporte += "Tactil: " + String(sistemaEstado ? "ENCENDIDO" : "APAGADO") + "\n";
  reporte += "Pantalla: " + String(pantallaEncendida ? "ON" : "SLEEP") + "\n";
  reporte += "---------------------\n";

  Serial.print(reporte);
  if (SerialBT.hasClient())
    SerialBT.print(reporte);
}

void toggleBluetooth()
{
  btActivo = !btActivo;
  if (btActivo)
  {
    setCpuFrequencyMhz(240);
    SerialBT.begin("ESP32_TFT_TEST");
    Serial.println("BT: Visible");
  }
  else
  {
    SerialBT.end();
    Serial.println("BT: Apagado");
  }

  if (pantallaEncendida)
  {
    tft.fillRect(190, 5, 45, 30, btActivo ? COL_ACCENT : COL_CARD);
    tft.setTextColor(COL_TEXTO);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(btActivo ? "BT ON" : "BT OFF", 212, 20, 1);
  }
}

void gestionarModoEnergia(bool despertar)
{
  if (despertar)
  {
    setCpuFrequencyMhz(240);
    tft.writecommand(0x11); // Wake up display
    delay(120);
    digitalWrite(PIN_BL, LOW); // 1 = ON
    pantallaEncendida = true;

    dibujarInterfazBase();
    dibujarBotonSistema(sistemaEstado);
    dibujarBotonBL();
    actualizarVisualReles();
    actualizarTemperaturas();
  }
  else
  {
    digitalWrite(PIN_BL, LOW); // 0 = OFF
    tft.writecommand(0x10);    // Sleep display
    if (btActivo)
      setCpuFrequencyMhz(160);
    else
      setCpuFrequencyMhz(80);
    pantallaEncendida = false;
  }
}

void actualizarTemperaturas()
{
  float t1 = sensors.getTempCByIndex(0);
  float t2 = sensors.getTempCByIndex(1);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(COL_TEXTO, COL_CARD);
  tft.drawString(t1 == DEVICE_DISCONNECTED_C ? "--.- C" : String(t1, 1) + " C", 62, 105, 4);
  tft.drawString(t2 == DEVICE_DISCONNECTED_C ? "--.- C" : String(t2, 1) + " C", 177, 105, 4);
}

void actualizarVisualReles()
{
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(estadoRele ? COL_BTN_ON : COL_SUBTEXTO, COL_CARD);
  tft.drawString(estadoRele ? " ESTADO: ON " : " ESTADO: OFF", 62, 195, 2);
  tft.setTextColor(!estadoRele ? COL_BTN_ON : COL_SUBTEXTO, COL_CARD);
  tft.drawString(!estadoRele ? " ESTADO: ON " : " ESTADO: OFF", 177, 195, 2);
}

void dibujarBotonSistema(bool estado)
{
  uint16_t color = estado ? COL_BTN_ON : COL_BTN_OFF;
  tft.fillRoundRect(btn1X, btnY, btnW, btnH, 8, color);
  tft.drawRoundRect(btn1X, btnY, btnW, btnH, 8, COL_ACCENT);
  tft.setTextColor(COL_TEXTO);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(estado ? "TACTIL ON" : "TACTIL OFF", btn1X + (btnW / 2), btnY + (btnH / 2), 2);
}

void dibujarBotonBL()
{
  tft.fillRoundRect(btn2X, btnY, btnW, btnH, 8, COL_CARD);
  tft.drawRoundRect(btn2X, btnY, btnW, btnH, 8, COL_ACCENT);
  tft.setTextColor(COL_TEXTO);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("SLEEP", btn2X + (btnW / 2), btnY + (btnH / 2), 2);
}

void dibujarInterfazBase()
{
  tft.fillScreen(COL_FONDO);
  tft.fillRect(0, 0, 240, 40, COL_CARD);
  tft.drawFastHLine(0, 40, 240, COL_ACCENT);
  tft.setTextColor(COL_TEXTO);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("PANEL DE CONTROL", 100, 20, 2);
  tft.fillRect(190, 5, 45, 30, btActivo ? COL_ACCENT : COL_CARD);
  tft.drawString(btActivo ? "BT ON" : "BT OFF", 212, 20, 1);
  tft.drawRoundRect(10, 55, 105, cardH, 8, TFT_WHITE);
  tft.drawRoundRect(125, 55, 105, cardH, 8, TFT_WHITE);
  tft.drawRoundRect(10, 150, 105, cardH, 8, TFT_WHITE);
  tft.drawRoundRect(125, 150, 105, cardH, 8, TFT_WHITE);
  tft.setTextColor(COL_SUBTEXTO);
  tft.drawString("Sensor 1", 62, 70, 2);
  tft.drawString("Sensor 2", 177, 70, 2);
  tft.drawString("Rele 1", 62, 165, 2);
  tft.drawString("Rele 2", 177, 165, 2);
}

void touch_calibrate()
{
  uint16_t calData[5];
  uint8_t check = 0; // Inicializamos check en 0

  // 1. Verificar si existe el archivo de calibración usando el namespace fs::
  if (SPIFFS.exists("/TouchCalData"))
  {
    fs::File f = SPIFFS.open("/TouchCalData", "r"); // Cambiado a fs::File
    if (f)
    {
      if (f.readBytes((char *)calData, 14) == 14)
        check = 1;
      f.close();
    }
  }

  if (check && !Serial.available())
  {
    // 2. Si el archivo existe, cargar los datos
    tft.setTouch(calData);
    Serial.println("Datos de calibracion cargados desde SPIFFS");
  }
  else
  {
    // 3. Si no existe, ejecutar calibracion interactiva
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(20, 0);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    tft.println("Toca las esquinas indicadas para calibrar");

    tft.setTextFont(1);
    tft.println("(Mantente presionado hasta que desaparezca)");

    // Ejecuta la rutina de la libreria
    tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);

    // 4. Guardar los nuevos datos en SPIFFS usando fs::File
    fs::File f = SPIFFS.open("/TouchCalData", "w"); // Cambiado a fs::File
    if (f)
    {
      f.write((const unsigned char *)calData, 14);
      f.close();
      Serial.println("Calibracion completa y guardada.");
    }
    tft.fillScreen(TFT_BLACK);
  }
}