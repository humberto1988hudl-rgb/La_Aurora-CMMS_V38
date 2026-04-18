#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <Preferences.h>

// Configura el sensor DHT11
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// Pines de los selectores
#define SEL_M1 12
#define SEL_M2 13
#define SEL_M3 14

#define RESET_PIN 27  // Botón de reset de horómetros

// WiFi y MQTT
const char* ssid = "WINDTELECOM-7DF0";
const char* password = "vWixYJbU";
const char* mqtt_server = "192.168.1.11";

WiFiClient espClient;
PubSubClient client(espClient);

Preferences preferences;

// Variables de tiempo
unsigned long startM1 = 0, elapsedM1 = 0, lastSaveM1 = 0;
unsigned long startM2 = 0, elapsedM2 = 0, lastSaveM2 = 0;
unsigned long startM3 = 0, elapsedM3 = 0, lastSaveM3 = 0;

bool runningM1 = false, runningM2 = false, runningM3 = false;

const unsigned long SAVE_INTERVAL = 300000; // 5 minutos = 300,000 ms

void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(SEL_M1, INPUT_PULLUP);
  pinMode(SEL_M2, INPUT_PULLUP);
  pinMode(SEL_M3, INPUT_PULLUP);
  pinMode(RESET_PIN, INPUT_PULLUP);

  // Leer tiempos guardados
  preferences.begin("horometros", false);
  elapsedM1 = preferences.getULong("m1", 0);
  elapsedM2 = preferences.getULong("m2", 0);
  elapsedM3 = preferences.getULong("m3", 0);
  preferences.end();

  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado");
  Serial.println(WiFi.localIP());

  client.setServer(mqtt_server, 1884);
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando al broker MQTT...");
    if (client.connect("ESP32-Horometro")) {
      Serial.println("Conectado");
    } else {
      Serial.print("Fallo, rc=");
      Serial.print(client.state());
      delay(2000);
    }
  }
}

void loop() {
  if (!client.connected()) reconnect();
  client.loop();

  handleMachine(SEL_M1, startM1, elapsedM1, runningM1, "cuarto1/maquina1", "m1", lastSaveM1);
  handleMachine(SEL_M2, startM2, elapsedM2, runningM2, "cuarto1/maquina2", "m2", lastSaveM2);
  handleMachine(SEL_M3, startM3, elapsedM3, runningM3, "cuarto1/maquina3", "m3", lastSaveM3);

  handleSensor();

  handleReset();
}

void handleMachine(uint8_t pin, unsigned long &start, unsigned long &elapsed, bool &running,
                   const char* topic, const char* key, unsigned long &lastSave) {
  if (digitalRead(pin) == LOW) {
    if (!running) {
      running = true;
      start = millis();
    }
  } else {
    if (running) {
      elapsed += millis() - start;
      running = false;

      // Guardar al detenerse
      preferences.begin("horometros", false);
      preferences.putULong(key, elapsed);
      preferences.end();
    }
  }

  // Guardar cada X tiempo si sigue encendido
  if (running && (millis() - lastSave > SAVE_INTERVAL)) {
    unsigned long total = elapsed + (millis() - start);
    preferences.begin("horometros", false);
    preferences.putULong(key, total);
    preferences.end();
    lastSave = millis();
  }

  // Publicar por MQTT
  unsigned long total = elapsed;
  if (running) total += millis() - start;

  unsigned long segundos = total / 1000;
  unsigned long minutos = segundos / 60;
  unsigned long horas = minutos / 60;
  segundos %= 60;
  minutos %= 60;

  String payload = "{\"h\":" + String(horas) +
                   ",\"m\":" + String(minutos) +
                   ",\"s\":" + String(segundos) +
                   ",\"activo\":" + String(running ? "true" : "false") +
                   "}";
  client.publish(topic, payload.c_str());
}

void handleSensor() {
  static unsigned long lastSensor = 0;
  if (millis() - lastSensor > 5000) {
    lastSensor = millis();
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (!isnan(t) && !isnan(h)) {
      String payload = "{\"temp\":" + String(t, 1) + ",\"hum\":" + String(h, 1) + "}";
      client.publish("cuarto1/tempHum", payload.c_str());
    }
  }
}

void handleReset() {
  static bool lastState = HIGH;
  bool currentState = digitalRead(RESET_PIN);

  if (lastState == HIGH && currentState == LOW) {
    Serial.println("¡Reset de horómetros!");

    elapsedM1 = elapsedM2 = elapsedM3 = 0;
    preferences.begin("horometros", false);
    preferences.putULong("m1", 0);
    preferences.putULong("m2", 0);
    preferences.putULong("m3", 0);
    preferences.end();
  }

  lastState = currentState;
}
