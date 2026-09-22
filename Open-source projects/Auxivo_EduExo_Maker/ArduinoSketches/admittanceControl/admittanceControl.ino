#include <Servo.h>

Servo myservo;
int forceAnalogInPin = A3;
const int forceOffset = 439;
int forceIs;
int forceDesired = 0;
int positionDesired = 40;
float gain = 0.2;

void setup(){
  myservo.attach(3);
  delay(1000);
}

void loop(){
  forceIs = analogRead(forceAnalogInPin)-forceOffset;
  delay(10);
  positionDesired -= gain*(forceIs-forceDesired);
  myservo.write(positionDesired);
}
