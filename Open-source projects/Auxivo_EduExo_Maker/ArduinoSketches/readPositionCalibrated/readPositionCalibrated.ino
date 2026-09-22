int servoAnalogInPin = A0;
int posIs;
float posIsDeg;

void setup(){
   Serial.begin(9600);
   delay(1000);
}

void loop(){
   posIs = analogRead(servoAnalogInPin);
   posIsDeg = (90.0/(255.0-80.0))*(posIs-80);
   Serial.print("Position (in degree):");
   Serial.println(posIsDeg);
   delay(10);
}
