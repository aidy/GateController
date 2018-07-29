#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <WiFiClientSecure.h>
#include <TelegramBotClient.h>
#include <ESP8266HTTPClient.h>
#include <config.h>

const int Control = 0;
const int Impulse = 1;
const int Position = 2;
const int Bell = 3;

int lastControlState = LOW;
long debounceDelay = 10;
long lastDebounceTime = 0;

int toggleTime = 500;
long lastCheck = 0;
long closeDelay = 10 * 60 * 1000;

long lastClosed = 0;
long closeThreshold = 4.2 * closeDelay;

long cycle = 0;
long cycleTime = 100 * 1000; // 40 second opening + 60 second full open

long buttonOn = 0;
long longPress = 1500;

const int OFF = HIGH;
const int ON = LOW;

const int GateOPEN = HIGH;
const int GateCLOSED = LOW;

int initialPosition = GateCLOSED;

bool startup = true;

WiFiClientSecure net_ssl;
TelegramBotClient telegram (BotToken, net_ssl);

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

  server.on("/", [](){
    int positionState = digitalRead(Position);
    String state = "Closed";
    long nextClose = 0;
    if (positionState == GateOPEN) {
      state = "Open";
      nextClose = (closeDelay - (millis() - lastCheck))/1000;
      if (cycle > 0) {
        long cycleClose = (cycleTime - (millis() - cycle))/1000;
        if (cycleClose < nextClose) {
          nextClose = cycleClose;
        }
      }
    }
    String autoclose = "Inactive";
    if (millis() - lastClosed <= closeThreshold) {
      autoclose = "Active";
    }

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
  if (positionState == GateCLOSED) {
    if (NotifyURL != "") {
      HTTPClient http;
      http.begin(NotifyURL);
      http.GET();
      http.end();
    }
    telegram.postMessage(TelegramId, "Gate opening");
  }
  lastCheck = millis();
  // reset any cycle operations that are pending.
  cycle = 0;
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
    int positionState = digitalRead(Position);
    // If already closed, don't reopen
    if (positionState == GateOPEN) {
      ControlPress();
    }
}

// Do a open-close cycle, if already open, fall back to default behaviour
void StartCycle(int initialPosition) {
  // If the gate is already open, don't schedule a close
  if (initialPosition == GateCLOSED) {
    cycle = millis();
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  ArduinoOTA.handle();
  server.handleClient();
  if (startup == true) {
    lastClosed = millis();
    lastControlState = digitalRead(Control);
    lastDebounceTime = millis();
    startup = false;
  }
  int positionState = digitalRead(Position);
  if (positionState == GateCLOSED) {
    lastClosed = millis();
  }
  if (millis() - lastClosed <= closeThreshold) {
    AutoClose();
  }

  if (cycle > 0 && (millis() - cycle >= cycleTime)) {
    EndCycle();
  }

  int reading = digitalRead(Control);

  if (reading != lastControlState && (millis() - lastDebounceTime > debounceDelay)) {
    reading = digitalRead(Control);
    if (reading == LOW) {
      buttonOn = millis();
      initialPosition = digitalRead(Position);
      ControlPress();
    } else if (millis() - buttonOn >= longPress) {
        StartCycle(initialPosition);
    }
    lastControlState = reading;
    lastDebounceTime = millis();
  }
}
