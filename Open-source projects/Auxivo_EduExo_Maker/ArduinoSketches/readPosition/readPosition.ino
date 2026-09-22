int servoAnalogInPin = A0;
int posIs;

void setup(){
    Serial.begin(9600);
    delay(1000);
}

void loop(){
    posIs = analogRead(servoAnalogInPin);
    Serial.print("Position:");
    Serial.println(posIs);
    delay(10);
}
