#include <Arduino.h>

const int RELAYOFF = HIGH;
const int RELAYON = LOW;

const int TRANSISTOROFF = LOW;
const int TRANSISTORON = HIGH;

const long TOGGLETIME = 500;

class Relay {
protected:
  int on, off;

public:
  int pin;
  long toggleTime;

  Relay(int p){
    pin = p;
    on = RELAYON;
    off = RELAYOFF;
    toggleTime = TOGGLETIME;
  }

  void Switch(bool state) {
    int s = off;
    if (state) {
      s = on;
    }
    digitalWrite(pin, s);
  }

  void Toggle() {
    digitalWrite(pin, on);
    delay(toggleTime);
    digitalWrite(pin, off);
  }

  void setup() {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, off);
  }
};

class TransistorRelay : public Relay {
public:
  TransistorRelay(int p) : Relay(p) {
    on = TRANSISTORON;
    off = TRANSISTOROFF;
  }
};
