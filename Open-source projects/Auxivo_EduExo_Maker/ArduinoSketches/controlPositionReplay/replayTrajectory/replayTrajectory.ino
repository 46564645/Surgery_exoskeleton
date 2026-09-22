#include <Servo.h>

Servo myservo;  // create servo object to control a servo

int positionDesired = 0;   // value output to the PWM

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

void playServo(uint8_t servoPin, uint8_t buttonPin) {
  uint16_t addr = 0;
  Serial.println("Playing");


  myServo.attach(servoPin);
  while (digitalRead(buttonPin)) {    
    uint8_t x = EEPROM.read(addr);
    Serial.print("Read EE: "); Serial.print(x);
    if (x == 255) break;
    // map to 0-180 degrees
    x = map(x, 0, 254, 0, 180);
    Serial.print(" -> "); Serial.println(x);
    myServo.write(x);
    delay(SAMPLE_DELAY);
    addr++;
    if (addr == 512) break;
  }
  Serial.println("Done");
  myServo.detach();
  delay(250);  
}
