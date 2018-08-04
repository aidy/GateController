#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <WiFiClientSecure.h>
#include <TelegramBotClient.h>
#include <ESP8266HTTPClient.h>
#include <relay.h>
#include <config.h>

const int BellPin = 16;
const int ImpulsePin = 5;
const int PedestrianPin = 4;
const int CutOutPin = 2; //13;

const int Control = 14;
const int Position = 12;
const int Photocell = 3;
const int ClosingSignal = 1;

int lastControlState = LOW;
long debounceDelay = 10;
long lastDebounceTime = 0;

long lastCheck = 0;
long closeDelay = 10 * 60 * 1000;

long lastClosed = 0;
long closeThreshold = 4.2 * closeDelay;
long closeDebounce = 200;

long cycle = 0;
long cycleTime = 100 * 1000; // 40 second opening + 60 second full open

long buttonOn = 0;
long longPress = 1500;

const int SWITCHOFF = HIGH;
const int SWITCHON = LOW;

bool GateClosed = false;

bool startup = true;

long lastClosingSignal = 0;
long closeSignalThreshold = 1500;

long reverseTime = 2000;
long safetyGrace = 60 * 1000;

WiFiClientSecure net_ssl;
TelegramBotClient telegram (BotToken, net_ssl);

ESP8266WebServer server(80);

TransistorRelay Bell(BellPin);
TransistorRelay Impulse(ImpulsePin);
TransistorRelay Pedestrian(PedestrianPin);
Relay CutOut(CutOutPin);

void setup() {
  Bell.setup();
  Impulse.setup();
  Pedestrian.setup();
  CutOut.setup();

  pinMode(Control, INPUT);
  pinMode(Position, INPUT);
  pinMode(Photocell, INPUT);
  pinMode(ClosingSignal, INPUT);

  /*
  pinMode(0, OUTPUT);
  digitalWrite(0, ON);

  pinMode(15, OUTPUT);
  digitalWrite(15, ON);
  */
  WiFi.config(ip, gateway, subnet);
  WiFi.begin(ssid, password);
  WiFi.mode(WIFI_STA);

  server.on("/", [](){
    String state = "Closed";
    long nextClose = 0;
    if (!GateClosed) {
      state = "Open";
      nextClose = (closeDelay - (millis() - lastCheck))/1000;
      if (cycle > 0) {
        long cycleClose = (cycleTime - (millis() - cycle))/1000;
        if (cycleClose < nextClose) {
          nextClose = cycleClose;
        }
      }
      if (millis() - lastClosingSignal <= closeSignalThreshold) {
        state = "Closing";
          if (digitalRead(Photocell) == SWITCHOFF) {
            state = state + ": photocell broken";
          } else {
            state = state + ": photocell intact";
          }
      }
    }
    String autoclose = "Active";
    if (millis() - lastClosed > closeThreshold) {
      autoclose = "Inactive";
      nextClose = 0;
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

  server.on("/disableautoclose", [](){
    lastClosed = millis() - closeThreshold - 1000;
    server.sendHeader("Location", "/");
    server.send(303);
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
  Impulse.Switch(true);
  if (GateClosed) {
    Bell.Switch(true);
  }
  delay(Impulse.toggleTime);
  Impulse.Switch(false);
  Bell.Switch(false);
  if (GateClosed) {
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
  if (!GateClosed) {
    Impulse.Toggle();
  }
  lastCheck = millis();
}

void Open() {
  if (GateClosed) {
    Impulse.Toggle();
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
    // If already closed, don't reopen
    if (!GateClosed) {
      ControlPress();
    }
}

// Do a open-close cycle, if already open, fall back to default behaviour
void StartCycle(bool closed) {
  // If the gate is already open, don't schedule a close
  if (closed) {
    cycle = millis();
  }
}

void StopGate() {
  CutOut.Toggle();
}

/*
  CutOut behaviour is that the next impulses reverses direction
  So we should impulse to start opening, and then cutout again.
  We're then in a position to resume closing as soon as the photocell circuit
  is complete.
*/
void SafetyStop() {
  StopGate();
  delay(100);
  Impulse.Toggle();
  delay(reverseTime);
  StopGate();
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
  int closing = digitalRead(ClosingSignal);
  if (closing == SWITCHON) {
    lastClosingSignal = millis();
  }
  if (millis() - lastClosingSignal <= closeSignalThreshold) {
    // Gate is closing
    if (digitalRead(Photocell) == SWITCHOFF) {
       // Photocell has been broken
       SafetyStop();
    }
  }
  int positionState = digitalRead(Position);
  if (positionState == SWITCHON) {
    lastClosed = millis();
    lastCheck = millis();
  }
  if (millis() - lastClosed < closeDebounce) {
    GateClosed = true;
  } else {
    GateClosed = false;
  }
  if (millis() - lastClosed <= closeThreshold) {
    if (digitalRead(Photocell) == SWITCHON) {
      // Only attempt to autoclose if the photocell is intact
      AutoClose();
    } else {
      // Otherwise push it out into the future.
      lastCheck = millis() - closeDelay + safetyGrace;
      // Reset lastClosed, in case there's something there for a while.
      lastClosed = millis();
    }
  }

  if (cycle > 0 && (millis() - cycle >= cycleTime)) {
    EndCycle();
  }

  int reading = digitalRead(Control);

  if (reading != lastControlState && (millis() - lastDebounceTime > debounceDelay)) {
    reading = digitalRead(Control);
    if (reading == LOW) {
      buttonOn = millis();
      ControlPress();
    } else if (millis() - buttonOn >= longPress) {
        StartCycle(GateClosed);
    }
    lastControlState = reading;
    lastDebounceTime = millis();
  }
}
