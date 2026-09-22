#include <Servo.h>

Servo myservo;
int servoAnalogInPin = A0;
int posIs;
int sendValue;
char unityCommand;

void setup() {
 Serial.begin(19200);
 myservo.attach(3);
 delay(1000);
}

void loop() {
    if(Serial.available() > 0){
      posIs = analogRead(servoAnalogInPin);
      sendValue = map(posIs,80,350,0,255);
      Serial.write(sendValue);
      Serial.flush();
      delay(10);
      
      unityCommand = Serial.read();
      switch(unityCommand)
       {
         case 'O':
           {
              if(myservo.attached())  myservo.detach();
           }
           break;
         case 'L':
           {
            if(!myservo.attached())  myservo.attach(3);
            myservo.write(0);    
           }
           break;
         case 'R':
           {
            if(!myservo.attached())  myservo.attach(3);
            myservo.write(80);  
           }
           break;
       }
    while(Serial.available() > 0) {Serial.read();} 
    } 
  }
