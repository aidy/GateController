#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <config.h>

const int Control = 0;
const int Impulse = 1;
const int Position = 2;
const int Bell = 3;

int lastControlState = LOW;
long debounceDelay = 50;
long lastDebounceTime = 0;

int toggleTime = 500;
long lastCheck = 0;
long closeDelay = 10 * 60 * 1000;

long lastClosed = 0;
long closeThreshold = 4.2 * closeDelay;

long cycle = 0;
long cycleTime = 100 * 1000; // 40 second opening + 60 second full open

long buttonOn = 0;
long longPress = 800;

const int OFF = HIGH;
const int ON = LOW;

const int GateOPEN = HIGH;
const int GateCLOSED = LOW;

ESP8266WebServer server(80);

void setup() {
  pinMode(Control, INPUT);
  pinMode(Impulse, OUTPUT);
  pinMode(Position, INPUT);
  pinMode(Bell, OUTPUT);
  digitalWrite(Impulse, OFF);
  digitalWrite(Bell, OFF);

  WiFi.config(ip, gateway, subnet);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  server.on("/", [](){
    int positionState = digitalRead(Position);
    String state = "Closed";
    if (positionState == GateOPEN) {
      state = "Open";
    }
    String autoclose = "Inactive";
    if (millis() - lastClosed <= closeThreshold) {
      autoclose = "Active";
    }
    long nextClose = (closeDelay - (millis() - lastCheck))/1000;

    server.send(200, "text/html", "<p>Gate is: "+state+"</p><p>Autoclose is: "+autoclose+"</p><p>Closing in: "+nextClose+"</p><form action=\"/config\" method=\"POST\"><p>AutoClose Delay: <input type=\"text\" name=\"close_delay\" value=\""+(closeDelay/1000)+"\"></p><p>AutoClose Threshold: <input type=\"text\" name\"close_threshold\" value=\""+(closeThreshold/1000)+"\"><input type=\"submit\" value=\"Submit\"></form></p>");
  });

  server.on("/open", [](){
    Open();
    server.sendHeader("Location", "/");
    server.send(303);
    //server.send(200, "text/html", "");
  });

  server.on("/close", []() {
    Close();
    server.sendHeader("Location", "/");
    server.send(303);
    //server.send(200, "text/html", "");
  });

  server.on("/config", HTTP_POST, handleConfig);

  server.begin();

  ArduinoOTA.begin();
}

void handleConfig() {
  if (server.hasArg("close_delay") && server.arg("close_delay") != NULL) {
    long newDelay;
    newDelay = server.arg("close_delay").toInt();//strtol(value, &eptr, 10);
    if (newDelay != 0) {
      closeDelay = newDelay * 1000;
    }
  }
  if (server.hasArg("close_threshold") && server.arg("close_threshold") != NULL) {
    long newThreshold;
    newThreshold = server.arg("close_threshold").toInt();//strtol(value, &eptr, 10);
    if (newThreshold != 0) {
      closeThreshold = newThreshold * 1000;
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void ControlPress() {
  int positionState = digitalRead(Position);
  digitalWrite(Impulse, ON);
  if (positionState == GateCLOSED) {
    digitalWrite(Bell, ON);
  }
  delay(toggleTime);
  digitalWrite(Impulse, OFF);
  digitalWrite(Bell, OFF);
  lastCheck = millis();
}

void Close() {
  int positionState = digitalRead(Position);
  if (positionState == GateOPEN) {
    digitalWrite(Impulse, ON);
    delay(toggleTime);
    digitalWrite(Impulse, OFF);
  }
  lastCheck = millis();
}

void Open() {
  int positionState = digitalRead(Position);
  if (positionState == GateCLOSED) {
    digitalWrite(Impulse, ON);
    delay(toggleTime);
    digitalWrite(Impulse, OFF);
  }
  lastCheck = millis();
}

void AutoClose() {
  if (millis() - lastCheck > closeDelay) {
    Close();
  }
}

void EndCycle() {
    cycle = 0;
    ControlPress();
}

void StartCycle() {
  ControlPress();
  cycle = millis();
}

void loop() {
  // put your main code here, to run repeatedly:
  ArduinoOTA.handle();
  server.handleClient();
  int positionState = digitalRead(Position);
  if (positionState == GateCLOSED) {
    lastClosed = millis();
  }
  if (millis() - lastClosed <= closeThreshold) {
    AutoClose();
  }
  if (cycle > 0 && (millis() > cycle + cycleTime)) {
    EndCycle();
  }
  int reading = digitalRead(Control);
  if (reading != lastControlState) {
    if (millis() - lastDebounceTime > debounceDelay) {
      if (reading == LOW) {
        buttonOn = millis();
        // ControlPress();
      }
      if (reading == HIGH) {
        long pressTime = millis() - buttonOn;
        if (pressTime < longPress) {
          ControlPress();
        } else {
          StartCycle();
        }
      }
    }
    lastDebounceTime = millis();
  }
  lastControlState = reading;
}
