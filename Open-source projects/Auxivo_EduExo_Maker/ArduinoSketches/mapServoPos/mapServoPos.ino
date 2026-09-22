#include <Servo.h>

Servo myservo;

int servoAnalogInPin = A0;
int posServo1 = 0;
int posServo2 = 100;
int posSensor1;
int posSensor2;

void setup(){
    Serial.begin(9600);
    myservo.attach(3);
    delay(1000);
}

void loop(){
    myservo.write(posServo1);
    delay(2000);
    posSensor1 = analogRead(servoAnalogInPin);
    myservo.write(posServo2);
    delay(2000);
    posSensor2 = analogRead(servoAnalogInPin);
    Serial.println(posServo1); 
    Serial.println(posServo2); 
    Serial.println(posSensor1); 
    Serial.println(posSensor2);
}
