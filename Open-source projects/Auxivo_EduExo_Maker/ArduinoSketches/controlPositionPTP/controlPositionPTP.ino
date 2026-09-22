#include <Servo.h>

Servo myservo;  

int positionDesired = 0;
int counter = 0;
int reps = 5;

void setup() {
  Serial.begin(9600);
  myservo.attach(3);  
}

void loop() {
  positionDesired = 20;
  myservo.write(positionDesired);
  delay(1000);
  positionDesired = 115;
  myservo.write(positionDesired);
  delay(1000);
  counter++;
  if(counter == reps) 
  { 
    exit(0);
  }
}
