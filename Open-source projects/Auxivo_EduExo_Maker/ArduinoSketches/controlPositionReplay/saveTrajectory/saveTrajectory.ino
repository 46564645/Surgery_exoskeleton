#include <Servo.h>
#include <EEPROM.h>

Servo myservo;  // create servo object to control a servo

int positionDesired = 0;   // value output to the PWM
int servoAnalogInPin = A0
int posIs;  

void setup() {
  Serial.begin(9600);
  myservo.attach(3);  // attaches the servo on pin 9 to the servo object
}

void loop() {
  positionDesired = 0;
  myservo.write(positionDesired);
  delay(1000);
  positionDesired = 110;
  myservo.write(positionDesired);
  delay(1000);
}


#define CALIB_MAX 512
#define CALIB_MIN 100
#define SAMPLE_DELAY 25 // in ms, 50ms seems good




Servo myServo;  
  
void setup() {
  Serial.begin(9600);

}

void loop() {
 if (! digitalRead(recordButtonPin)) {
   delay(10);
   // wait for released
   while (! digitalRead(recordButtonPin));
   delay(20);
   // OK released!
   recordServo(servoPin, feedbackPin, recordButtonPin);
 }
 
  if (! digitalRead(playButtonPin)) {
   delay(10);
   // wait for released
   while (! digitalRead(playButtonPin));
   delay(20);
   // OK released!
   playServo(servoPin, playButtonPin);
 }
}

void recordServo(uint8_t servoPin, uint8_t analogPin, uint8_t buttonPin) {
  uint16_t addr = 0;
  
  Serial.println("Recording");
  digitalWrite(ledPin, HIGH);
  


  
  pinMode(analogPin, INPUT); 
  while (digitalRead(buttonPin)) {
     uint16_t a = analogRead(analogPin);
     
     Serial.print("Read analog: "); Serial.print(a);
     if (a < CALIB_MIN) a = CALIB_MIN;
     if (a > CALIB_MAX) a = CALIB_MAX;
     a = map(a, CALIB_MIN, CALIB_MAX, 0, 254);
     Serial.print(" -> "); Serial.println(a);
     EEPROM.write(addr, a);
     addr++;
     if (addr == 512) break;
     delay(SAMPLE_DELAY);
  }
  if (addr != 512) EEPROM.write(addr, 255);

  digitalWrite(ledPin, LOW);

  Serial.println("Done");
  delay(250);
}
