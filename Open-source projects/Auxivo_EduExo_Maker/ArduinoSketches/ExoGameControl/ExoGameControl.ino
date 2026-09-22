int servoAnalogInPin = A0;
int posIs;
byte sendValue;

void setup() {
 Serial.begin(9600);
 delay(1000);
}

void loop() {
  posIs = analogRead(servoAnalogInPin);
  //sendValue = map(posIs,80,350,0,255);
  // motorPosition = map(angleSignal, 560, 310, 0, 90);
  sendValue = map(posIs,560,310,0,255);
  Serial.write(sendValue);
  Serial.flush();
  delay(20);
}
