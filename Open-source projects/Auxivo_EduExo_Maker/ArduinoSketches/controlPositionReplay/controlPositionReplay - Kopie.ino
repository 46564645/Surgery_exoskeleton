#include <Servo.h>
#include <EEPROM.h>

#define CALIB_MAX 512
#define CALIB_MIN 100
#define SAMPLE_DELAY 25 // in ms, 50ms seems good
#define SAMPLES 300 // number of recorded trajectory points

Servo myservo;  // create servo object to control a servo
int servoPin = 3;
int positionDesired = 0;   // value output to the PWM
int servoAnalogInPin = A0;
int posIs;  

  
void setup() {
    Serial.begin(9600);
}

void loop() {
    Serial.println("Servo RecordPlay starts in 3 seconds");
    delay(3000);
    recordTrajectory();
    delay(3000);
    replayTrajectory();
 }
 
 void recordTrajectory() {
    uint16_t addr = 0;
    Serial.println("Recording");
    for (int i=0; i<SAMPLES; i++){
       posIs = analogRead(servoAnalogInPin);
       uint16_t a = map(posIs, CALIB_MIN, CALIB_MAX, 0, 254);
       EEPROM.write(addr, a);
       addr++;
       if (addr == 512) break;
       delay(SAMPLE_DELAY);
    }
    if (addr != 512) EEPROM.write(addr, 255);
    Serial.println("Done recording");
    delay(250);
}

void replayTrajectory() {
  uint16_t addr = 0;
  myservo.attach(servoPin);
  Serial.println("Playing");
  for (int i=0; i<SAMPLES; i++){
      uint8_t x = EEPROM.read(addr);
      if (x == 255) break;
      positionDesired = map(x, 0, 254, 0, 180);
      myservo.write(positionDesired);
      addr++;
      if (addr == 512) break;
      delay(SAMPLE_DELAY);
  }
  Serial.println("Done replaying");
  myservo.detach();
  delay(250);  
}


