#include <WiFi.h>
#include <WiFiClientSecure.h>

// ===== PER-UNIT CONFIG — CHANGE THIS ON EACH BOARD =====
const char* UNIT_ID = "YourName";   // unique name per board
const char* ssid = "YourWiFiName";
const char* password = "YourWiFiPassword";
// =======================================================

const char* NTFY_HOST  = "ntfy.sh";
const char* NTFY_TOPIC = "your-topic-name"; // State your topic

const int BUTTON_PIN = 18;
const int LIGHT_PIN  = 22;

bool lastButtonState = HIGH;
unsigned long lastButtonTime = 0;
const unsigned long debounceDelay = 75;

bool lightActive = false;
unsigned long lightStartTime = 0;
const unsigned long LIGHT_DURATION = 10000;

WiFiClientSecure streamClient;
bool streamConnected = false;
unsigned long lastStreamAttempt = 0;
const unsigned long reconnectInterval = 3000;

unsigned long lastHeapLog = 0;
const unsigned long heapLogInterval = 60000;

unsigned long bootTime = 0;
const unsigned long REBOOT_INTERVAL = 24UL * 60 * 60 * 1000;

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LIGHT_PIN, OUTPUT);
  digitalWrite(LIGHT_PIN, LOW);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Ready as ");
  Serial.println(UNIT_ID);

  bootTime = millis();
  connectStream();
}

void loop() {
  checkButton();
  checkLightTimeout();
  handleStream();
  logHeap();
  checkScheduledReboot();
}

void checkButton() {
  bool currentState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH && currentState == LOW) {
    if (millis() - lastButtonTime > debounceDelay && !lightActive) {
      lastButtonTime = millis();
      Serial.println("== BUTTON PRESSED (local) ==");
      triggerLight();
      publishAlert();
    }
  }
  lastButtonState = currentState;
}

void triggerLight() {
  Serial.println("Siren ON");
  digitalWrite(LIGHT_PIN, HIGH);
  lightActive = true;
  lightStartTime = millis();
}

void checkLightTimeout() {
  if (lightActive && (millis() - lightStartTime > LIGHT_DURATION)) {
    digitalWrite(LIGHT_PIN, LOW);
    lightActive = false;
    Serial.println("Siren OFF");
  }
}

void publishAlert() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping publish");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(3000);

  Serial.println("Attempting publish connect...");
  if (!client.connect(NTFY_HOST, 443)) {
    Serial.println("Publish connect failed");
    return;
  }

  String body = String("ALERT from ") + UNIT_ID;

  client.print(String("POST /") + NTFY_TOPIC + " HTTP/1.1\r\n");
  client.print(String("Host: ") + NTFY_HOST + "\r\n");
  client.print("Title: NUKE BUTTON\r\n");
  client.print("Priority: urgent\r\n");
  client.print("Tags: rotating_light\r\n");
  client.print("Content-Length: " + String(body.length()) + "\r\n");
  client.print("Connection: close\r\n\r\n");
  client.print(body);

  Serial.println("Alert published (request sent)");
  unsigned long t = millis();
  while (client.connected() && millis() - t < 500) {
    while (client.available()) client.read();
  }
  client.stop();
}

void connectStream() {
  streamClient.setInsecure();
  streamClient.setTimeout(3000);   // don't let a bad connect hang the loop
  Serial.println("Opening stream to ntfy...");

  if (streamClient.connect(NTFY_HOST, 443)) {
    streamClient.print(String("GET /") + NTFY_TOPIC + "/json HTTP/1.1\r\n");
    streamClient.print(String("Host: ") + NTFY_HOST + "\r\n");
    streamClient.print("Connection: keep-alive\r\n\r\n");
    streamConnected = true;
    Serial.println("Stream connected — listening for instant alerts");
  } else {
    streamConnected = false;
    Serial.println("Stream connect failed, will retry");
  }
}

void handleStream() {
  if (!streamClient.connected()) {
    streamConnected = false;
    if (millis() - lastStreamAttempt > reconnectInterval) {
      lastStreamAttempt = millis();
      connectStream();
    }
    return;
  }

  while (streamClient.available()) {
    String line = streamClient.readStringUntil('\n');
    if (line.length() == 0) continue;

    if (line.indexOf("\"event\":\"message\"") != -1) {
      String msg = extractField(line, "\"message\":\"");
      Serial.print("Incoming (instant): ");
      Serial.println(msg);

      if (msg.indexOf(UNIT_ID) != -1) {
        Serial.println("(our own press, ignoring)");
      } else if (!lightActive) {
        Serial.println("== REMOTE PRESS — firing siren ==");
        triggerLight();
      }
    }
  }
}

String extractField(String body, String key) {
  int start = body.indexOf(key);
  if (start == -1) return "";
  start += key.length();
  int end = body.indexOf("\"", start);
  if (end == -1) return "";
  return body.substring(start, end);
}

void logHeap() {
  if (millis() - lastHeapLog > heapLogInterval) {
    lastHeapLog = millis();
    Serial.print("[heap] Free: ");
    Serial.println(ESP.getFreeHeap());
  }
}

void checkScheduledReboot() {
  if (millis() - bootTime > REBOOT_INTERVAL) {
    Serial.println("Scheduled reboot for long-term stability");
    delay(100);
    ESP.restart();
  }
}
