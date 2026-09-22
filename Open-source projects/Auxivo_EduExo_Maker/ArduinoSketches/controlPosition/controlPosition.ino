#include <Servo.h>

Servo myservo;  

int positionDesired = 0;

void setup() {
  Serial.begin(9600);
  myservo.attach(3);
}

void loop() {
  myservo.write(positionDesired);
}
