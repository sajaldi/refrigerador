#include <WiFi.h>
#include <math.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <HTTPClient.h>

// --- Configuración de la Pantalla OLED ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define I2C_SDA 21
#define I2C_SCL 22
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- Configuración de Red WiFi ---
const char* ssid = "Juda7Works25";
const char* password = "Juda72025";
WiFiServer server(80);

// --- Definición de Pines ---
const int COMPRESSOR_PIN = 2;
const int FAN_PIN = 4;
const int DEFROST_PIN = 5;
const int THERMISTOR_PIN = 34;

// --- Parámetros del Termistor ---
const float R_FIJA = 15000.0;
const float T_NOMINAL = 25.0;
const float R_NOMINAL = 35000.0;
const float B_COEFFICIENT = 3950.0;

// --- Variables de Estado ---
float temperaturaActual = 0.0;
float temperaturaDeseada = 4.0; // Se mantiene para visualización
float temperaturaOffset = 0.0;
String estadoCompresor = "APAGADO";
String estadoResistencia = "APAGADO";
String estadoVentilador = "APAGADO";
String ipAddress = "Conectando...";

// --- Variables para el Ciclo Fijo de Tiempo ---
enum SystemState {
  STARTUP,
  COOLING,
  DEFROSTING
};
SystemState currentState = STARTUP;
unsigned long cycleStartTime = 0; // Almacena el tiempo de inicio del ciclo actual (enfriamiento o deshielo)

// --- Tiempos del Ciclo (en milisegundos) ---
unsigned long startupDelay = 5000;           // 5 segundos (configurable remotamente)
unsigned long coolingDuration = 28500000UL;  // 7h55min por defecto (8h - 5min deshielo)
unsigned long defrostDuration = 300000UL;     // 5 minutos por defecto

// --- Objeto Preferences ---
Preferences preferences;

// --- Configuración Remota (GitHub) ---
const char* CONFIG_URL = "https://raw.githubusercontent.com/sajaldi/refrigerador/main/refrigeradora_config.json";
unsigned long lastConfigFetch = 0;
const unsigned long CONFIG_FETCH_INTERVAL = 3600000; // 1 hora

// --- Temporizadores ---
unsigned long tiempoPrevioSensor = 0;
unsigned long tiempoPrevioPantalla = 0;
const long intervaloSensor = 5000;
const long intervaloPantalla = 1000;

// Función para obtener el estado del sistema y el tiempo restante
void getSystemStatus(String &status, String &timeRemaining) {
  unsigned long tiempoActual = millis();
  unsigned long elapsedTime = tiempoActual - cycleStartTime;

  switch (currentState) {
    case STARTUP:
      status = "Sistema Iniciando";
      timeRemaining = "Faltan " + String((startupDelay - tiempoActual) / 1000 + 1) + "s";
      break;
    case COOLING:
      status = "Enfriando";
      {
        unsigned long remaining = coolingDuration - elapsedTime;
        int hours = remaining / 3600000;
        int minutes = (remaining % 3600000) / 60000;
        timeRemaining = "Deshielo en " + String(hours) + "h " + String(minutes) + "m";
      }
      break;
    case DEFROSTING:
      status = "Deshielo Activo";
      {
        unsigned long remaining = defrostDuration - elapsedTime;
        int minutes = remaining / 60000;
        int seconds = (remaining % 60000) / 1000;
        timeRemaining = "Faltan " + String(minutes) + "m " + String(seconds) + "s";
      }
      break;
    default:
      status = "Estado Desconocido";
      timeRemaining = "";
      break;
  }
}

void startCoolingCycle() {
  Serial.println("INICIANDO CICLO DE ENFRIAMIENTO");
  currentState = COOLING;
  cycleStartTime = millis();

  digitalWrite(DEFROST_PIN, HIGH);
  estadoResistencia = "APAGADO";

  digitalWrite(COMPRESSOR_PIN, LOW);
  estadoCompresor = "ENCENDIDO";

  digitalWrite(FAN_PIN, LOW);
  estadoVentilador = "ENCENDIDO";
}

void startDefrostCycle() {
  Serial.println("INICIANDO CICLO DE DESHIELO");
  currentState = DEFROSTING;
  cycleStartTime = millis();

  digitalWrite(COMPRESSOR_PIN, HIGH);
  estadoCompresor = "APAGADO";

  digitalWrite(FAN_PIN, HIGH);
  estadoVentilador = "APAGADO";

  digitalWrite(DEFROST_PIN, LOW);
  estadoResistencia = "ENCENDIDO";
}


void setup() {
  Serial.begin(115200);

  preferences.begin("mi-refri", false);
  temperaturaOffset = preferences.getFloat("tempOffset", 0.0);
  preferences.end();
  
  Wire.begin(I2C_SDA, I2C_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("Fallo al iniciar la pantalla OLED"));
    for(;;);
  }
  
  pinMode(COMPRESSOR_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(DEFROST_PIN, OUTPUT);
  digitalWrite(COMPRESSOR_PIN, HIGH);
  digitalWrite(FAN_PIN, HIGH);
  digitalWrite(DEFROST_PIN, HIGH);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Conectando a WiFi...");
  display.display();
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado!");
  ipAddress = WiFi.localIP().toString();
  server.begin();
  fetchRemoteConfig();
  lastConfigFetch = millis();
}

void leerTemperatura() {
  int adcValue = analogRead(THERMISTOR_PIN);
  float R_termistor = R_FIJA / (4095.0 / adcValue - 1.0);
  float steinhart = log(R_termistor / R_NOMINAL) / B_COEFFICIENT + 1.0 / (T_NOMINAL + 273.15);
  float tempCalculada = 1.0 / steinhart - 273.15;
  temperaturaActual = tempCalculada + temperaturaOffset;
}

// --- Configuración Remota ---
int parseJSONInt(const String& json, const String& key, int defaultValue) {
  String searchKey = "\"" + key + "\"";
  int keyPos = json.indexOf(searchKey);
  if (keyPos < 0) return defaultValue;
  int colonPos = json.indexOf(":", keyPos + searchKey.length());
  if (colonPos < 0) return defaultValue;
  int valStart = colonPos + 1;
  while (valStart < json.length() && json.charAt(valStart) == ' ') valStart++;
  String numStr = "";
  for (int i = valStart; i < json.length(); i++) {
    char c = json.charAt(i);
    if (c >= '0' && c <= '9') numStr += c;
    else if (c == '.' || c == '-') numStr += c;
    else break;
  }
  if (numStr.length() > 0) return numStr.toInt();
  return defaultValue;
}

void fetchRemoteConfig() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi no disponible, se usan valores locales");
    return;
  }
  HTTPClient http;
  http.begin(CONFIG_URL);
  http.setTimeout(5000);
  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    Serial.println("Config remota cargada:");
    int cm = parseJSONInt(payload, "coolingMinutes", 0);
    if (cm > 0) { coolingDuration = (unsigned long)cm * 60000UL; Serial.print("coolingDuration: "); Serial.print(cm); Serial.println(" min"); }
    int dm = parseJSONInt(payload, "defrostMinutes", 0);
    if (dm > 0) { defrostDuration = (unsigned long)dm * 60000UL; Serial.print("defrostDuration: "); Serial.print(dm); Serial.println(" min"); }
    int ss = parseJSONInt(payload, "startupSeconds", 0);
    if (ss > 0) { startupDelay = (unsigned long)ss * 1000UL; Serial.print("startupDelay: "); Serial.print(ss); Serial.println(" s"); }
  } else {
    Serial.print("Error HTTP al obtener config: ");
    Serial.println(httpCode);
  }
  http.end();
}

void actualizarPantalla() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  if (currentState == STARTUP) {
    unsigned long remainingTime = startupDelay - millis();
    int secondsRemaining = (remainingTime / 1000) + 1;
    
    display.setCursor(10, 10);
    display.println("Sistema iniciando...");
    display.setTextSize(3);
    display.setCursor(40, 28);
    display.print(secondsRemaining);
    display.setTextSize(1);
    display.println(" s");
  } else {
    display.setCursor(0, 0);
    display.print("T: ");
    display.setTextSize(2);
    display.print(String(temperaturaActual, 1));
    display.println(" C");
    
    display.setTextSize(1);
    display.setCursor(0, 18);
    String currentStatus, timeRemaining;
    getSystemStatus(currentStatus, timeRemaining);
    display.println(currentStatus);
    display.setCursor(0,28);
    display.println(timeRemaining);

    display.setCursor(0, 48);
    display.print("IP: ");
    display.println(ipAddress);
  }
  display.display();
}

// La función web se mantiene para control manual y visualización,
// pero el ciclo automático en loop() volverá a tomar el control.
void atenderClienteWeb() {
  WiFiClient client = server.accept();
  if (!client) return;

  String header = "";
  String currentLine = "";
  
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      header += c;
      if (c == '\n') {
        if (currentLine.length() == 0) {
          // Procesar la petición (simplificado, ya que el control es por tiempo)
          if (header.indexOf("GET /compresor/on") >= 0) { digitalWrite(COMPRESSOR_PIN, LOW); estadoCompresor = "ENCENDIDO"; }
          else if (header.indexOf("GET /compresor/off") >= 0) { digitalWrite(COMPRESSOR_PIN, HIGH); estadoCompresor = "APAGADO"; }
          else if (header.indexOf("GET /ventilador/on") >= 0) { digitalWrite(FAN_PIN, LOW); estadoVentilador = "ENCENDIDO"; }
          else if (header.indexOf("GET /ventilador/off") >= 0) { digitalWrite(FAN_PIN, HIGH); estadoVentilador = "APAGADO"; }
          else if (header.indexOf("GET /deshielo/on") >= 0) { digitalWrite(DEFROST_PIN, LOW); estadoResistencia = "ENCENDIDO"; }
          else if (header.indexOf("GET /deshielo/off") >= 0) { digitalWrite(DEFROST_PIN, HIGH); estadoResistencia = "APAGADO"; }
          else if (header.indexOf("GET /calibrate?realTemp=") >= 0) { int pos = header.indexOf("realTemp=") + 9; float tempReal = header.substring(pos).toFloat(); float tempRaw = temperaturaActual - temperaturaOffset; temperaturaOffset = tempReal - tempRaw; preferences.begin("mi-refri", false); preferences.putFloat("tempOffset", temperaturaOffset); preferences.end(); leerTemperatura(); }

          String systemStatus, systemTime;
          getSystemStatus(systemStatus, systemTime);

          // Enviar página HTML
          client.println("HTTP/1.1 200 OK");
          client.println("Content-type:text/html");
          client.println("Connection: close");
          client.println();
          client.println("<!DOCTYPE html><html><head><title>Control Refrigerador</title>");
          client.println("<meta name='viewport' content='width=device-width, initial-scale=1'><meta http-equiv='refresh' content='10'>");
          client.println("<style>html { font-family: Helvetica; text-align: center; } body { background-color: #f7f7f7; } .card { background-color: white; max-width: 400px; margin: 30px auto; padding: 25px; border-radius: 15px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); } h1 { color: #005c9b; } h2 { color: #007bff; } .temp { font-size: 4rem; font-weight: bold; color: #333; } .status { font-size: 1.2rem; margin-bottom: 20px; } a, button, input[type='submit'] { text-decoration: none; color: white; border: none; border-radius: 8px; padding: 15px 20px; font-size: 1rem; margin: 5px; cursor: pointer; display: inline-block; } .btn-on { background-color: #28a745; } .btn-off { background-color: #dc3545; } .btn-cal { background-color: #ffc107; color: black; }</style></head><body>");
          
          client.println("<div class='card'><h1>Control Refrigerador</h1>");
          client.print("<p class='temp'>");
          client.print(temperaturaActual, 1);
          client.println("&deg;C</p>");
          client.print("<p class='status'>Compresor: <strong>");
          client.print(estadoCompresor);
          client.println("</strong></p>");
          client.print("<p class='status'>Ventilador: <strong>");
          client.print(estadoVentilador);
          client.println("</strong></p>");
          client.print("<p class='status'>Resistencia: <strong>");
          client.print(estadoResistencia);
          client.println("</strong></p>");
          client.println("<p><a href='/compresor/on' class='btn-on'>Compresor ON</a> <a href='/compresor/off' class='btn-off'>Compresor OFF</a></p>");
          client.println("<p><a href='/ventilador/on' class='btn-on'>Ventilador ON</a> <a href='/ventilador/off' class='btn-off'>Ventilador OFF</a></p>");
          client.println("<p><a href='/deshielo/on' class='btn-on'>Resistencia ON</a> <a href='/deshielo/off' class='btn-off'>Resistencia OFF</a></p></div>");
          
          client.println("<div class='card'><h2>Estado del Sistema</h2>");
          client.print("<p class='status'>Proceso Actual: <strong>");
          client.print(systemStatus);
          client.println("</strong></p>");
          client.print("<p class='status'>Tiempo Restante: <strong>");
          client.print(systemTime);
          client.println("</strong></p></div>");

          client.println("<div class='card'><h2>Calibracion del Sensor</h2>");
          client.print("<p class='status'>Offset actual: <strong>");
          client.print(temperaturaOffset, 2);
          client.println("&deg;C</strong></p>");
          client.println("<form action='/calibrate' method='GET'><label for='realTemp'>Introduce la temp. real:</label><br>");
          client.println("<input type='number' step='0.1' id='realTemp' name='realTemp' placeholder='Ej: 5.2'>");
          client.println("<input type='submit' value='Calibrar' class='btn-cal'></form></div></body></html>");

          break;
        } else { currentLine = ""; }
      } else if (c != '\r') { currentLine += c; }
    }
  }
  client.stop();
}

void loop() {
  unsigned long tiempoActual = millis();

  // --- Lógica Principal de Control por Tiempo Fijo ---
  switch (currentState) {
    case STARTUP:
      if (tiempoActual >= startupDelay) {
        startCoolingCycle();
      }
      break;

    case COOLING:
      if (tiempoActual - cycleStartTime >= coolingDuration) {
        startDefrostCycle();
      }
      break;

    case DEFROSTING:
      if (tiempoActual - cycleStartTime >= defrostDuration) {
        startCoolingCycle();
      }
      break;
  }
  
  // --- Tareas Secundarias ---
  if (tiempoActual - tiempoPrevioSensor >= intervaloSensor) {
    leerTemperatura();
    tiempoPrevioSensor = tiempoActual;
  }
  
  if (tiempoActual - tiempoPrevioPantalla >= intervaloPantalla) {
    actualizarPantalla();
    tiempoPrevioPantalla = tiempoActual;
  }

  if (tiempoActual - lastConfigFetch >= CONFIG_FETCH_INTERVAL) {
    fetchRemoteConfig();
    lastConfigFetch = tiempoActual;
  }

  atenderClienteWeb();
}
