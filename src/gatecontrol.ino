#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <WiFiClientSecure.h>
#include <TelegramBotClient.h>
#include <ESP8266HTTPClient.h>
#include <RCSwitch.h>
#include <relay.h>
#include <config.h>

const int BellPin = 16;
const int ImpulsePin = 5;
const int PedestrianPin = 4;
const int CutOutPin = 2; //13;
const int ClosePin = 14;
const int OpenPin = 13;

const int Control = 0;
const int Position = 12;
const int Photocell = 3;
const int ClosingSignal = 1;

const int RFPin = 15;

int lastControlState = LOW;
long debounceDelay = 10;
long lastDebounceTime = 0;

long lastCheck = 0;
long closeDelay = 10 * 60 * 1000;

long lastClosed = 0;
long closeThreshold = 4.2 * closeDelay;
long closeDebounce = 500;

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

long reverseTime = 4000;
long safetyGrace = 60 * 1000;

long lastPress = 0;
long pressGrace = 5000;

bool forceClose = false;

RCSwitch RFReceiver = RCSwitch();
ulong lastRF = 0;
ulong RFDebounce = 2500; // Flakey RF, so longer to allow for multiple presses.

WiFiClientSecure net_ssl;
TelegramBotClient telegram (BotToken, net_ssl);

ESP8266WebServer server(80);

TransistorRelay Bell(BellPin);
TransistorRelay Impulse(ImpulsePin);
TransistorRelay Pedestrian(PedestrianPin);
TransistorRelay CloseButton(ClosePin);
TransistorRelay OpenButton(OpenPin);
Relay CutOut(CutOutPin);

void setup() {
  Bell.setup();
  Impulse.setup();
  Pedestrian.setup();
  CloseButton.setup();
  OpenButton.setup();
  CutOut.setup();

  pinMode(Control, INPUT_PULLUP);
  pinMode(Position, INPUT_PULLUP);
  pinMode(Photocell, INPUT_PULLUP);
  pinMode(ClosingSignal, INPUT_PULLUP);

  RFReceiver.enableReceive(RFPin);

  /*
  pinMode(0, OUTPUT);
  digitalWrite(0, ON);

  pinMode(15, OUTPUT);
  digitalWrite(15, ON);
  */
  IPAddress ip;
  IPAddress gw;
  IPAddress subnet;
  ip.fromString(IP_ADDRESS);
  gw.fromString(IP_GATEWAY);
  subnet.fromString(IP_SUBNET);

  WiFi.config(ip, gw, subnet);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.mode(WIFI_STA);

  server.on("/", [](){
    String state = "Closed";
    long nextClose = 0;
    long now = millis();
    long lastClosedAgo = now - lastClosed;
    long lastCheckAgo = now - lastCheck;
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

    server.send(200, "text/html", "<p>Gate is: "+state+"</p><p>Last Closed: "+lastClosedAgo+" Last Check: "+lastCheckAgo+"</p><p>Autoclose is: "+autoclose+"</p><p>Closing in: "+nextClose+"</p><form action=\"/config\" method=\"POST\"><p>AutoClose Delay: <input type=\"text\" name=\"close_delay\" value=\""+(closeDelay/1000)+"\"></p><p>AutoClose Threshold: <input type=\"text\" name\"close_threshold\" value=\""+(closeThreshold/1000)+"\"><p>Press Grace: <input type=\"text\" name=\"press_grace\" value=\""+(pressGrace/1000)+"\"><input type=\"submit\" value=\"Submit\"></form></p>");
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

  server.on("/forceclose", []() {
    ForceClose();
    server.sendHeader("Location", "/");
    server.send(303);
    //server.send(200, "text/html", "");
  });

  server.on("/impulse", []() {
    SendImpulse();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/bell", []() {
    Bell.Toggle();
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/status", []() {
    String status = "closed";
    if (!GateClosed) {
      status = "open";
    }

    if (millis() - lastClosingSignal <= closeSignalThreshold) {
      status = "closing";
    }
    server.send(200, "text/plain", status);
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
  if (server.hasArg("press_grace") && server.arg("press_grace") != NULL) {
    long newThreshold;
    newThreshold = server.arg("press_grace").toInt();//strtol(value, &eptr, 10);
    if (newThreshold != 0) {
      pressGrace = newThreshold * 1000;
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void ControlPress() {
  lastPress = millis();
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

void SendImpulse() {
  Impulse.Toggle();
}

void SendClose() {
  CloseButton.Toggle();
}

void Close() {
  CloseButton.Toggle();
  lastCheck = millis();
}

void ForceClose() {
  forceClose = true;
  Close();
}

void Open() {
  OpenButton.Toggle();
  lastCheck = millis();
}

void AutoClose() {
  if (millis() - lastCheck > closeDelay) {
    // If we're trying to autoclose, we must respect the safety devices
    forceClose = false;
    Close();
  }
}

void EndCycle() {
    cycle = 0;
    // If already closed, don't reopen
    if (!GateClosed) {
      SendClose();
    }
}

// Do a open-close cycle, if already open, fall back to default behaviour
void LongPress(bool closed) {
  // If the gate is already open, don't schedule a close
  if (closed) {
    cycle = millis();
  } else {
    forceClose = true;
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
    if ((digitalRead(Photocell) == SWITCHOFF) && (millis() - lastPress > pressGrace) && (forceClose == false)) {
       // Photocell has been broken
       SafetyStop();
    }
  } else if (GateClosed) { // If we're not currently closing, and the gate is closed, reset forceClose
    forceClose = false;
  }
  int positionState = digitalRead(Position);
  long now = millis();
  if (positionState == SWITCHON) {
    lastCheck = now;
    lastClosed = now;
  }
  if (now - lastClosed < closeDebounce) {
    GateClosed = true;
  } else {
    GateClosed = false;
  }
  if (!GateClosed && (millis() - lastClosed <= closeThreshold)) {
    if (digitalRead(Photocell) == SWITCHON) {
      // Only attempt to autoclose if the photocell is intact
      AutoClose();
    } else {
      // Otherwise push it out into the future.
      long lc = millis() - closeDelay + safetyGrace; // lc is 9 minutes ago: Close in 1 minute
      if (lastCheck < lc) { // If we're due to close in > 1 minute, leave it alone
        lastCheck = lc;
      }
      // Reset lastClosed, in case there's something there for a while.
      lc = millis() - (2 * closeDebounce);
      if (lastClosed < lc) {
        lastClosed = lc;
      }
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
      LongPress(GateClosed);
    }
    lastControlState = reading;
    lastDebounceTime = millis();
  }
  server.handleClient();
  if (RFReceiver.available()) {
    ulong received =  RFReceiver.getReceivedValue();
    uint bl = RFReceiver.getReceivedBitlength();
    uint proto = RFReceiver.getReceivedProtocol();
    if (proto == 1 && bl == 24) {
        // TODO: Make less awful.
        if (received == 5795288 || received == 12789976 || received == 14979544 || received == 3769304 || received == 9903576) {
          if (millis() - lastRF > RFDebounce) {
            SendImpulse();
            lastRF = millis();
          }
        }
        if (received == 9903572) {
          ESP.restart();
        }
        if (received == 5795284 || received == 12789972 || received == 14979540 || received == 3769300) {
            if (millis() - lastRF > RFDebounce) {
              ForceClose();
              lastRF = millis();
            }
        }
    }
    RFReceiver.resetAvailable();
  }
}
