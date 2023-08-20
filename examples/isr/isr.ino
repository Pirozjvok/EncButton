// энкодер и прерывания
#include <Arduino.h>
#include <EncButton.h>
EncButton eb(2, 3, 4);

void isr() {
    eb.tickISR();
}

void setup() {
    Serial.begin(115200);
    attachInterrupt(0, isr, CHANGE);
    attachInterrupt(1, isr, CHANGE);
}

void loop() {
    eb.tick();

    if (eb.turn()) {
        Serial.print("turn: dir ");
        Serial.print(eb.dir());
        Serial.print(", fast ");
        Serial.print(eb.fast());
        Serial.print(", hold ");
        Serial.print(eb.encHolding());
        Serial.print(", counter ");
        Serial.println(eb.counter);
    }

    delay(100);  // имитация загруженной программы
}