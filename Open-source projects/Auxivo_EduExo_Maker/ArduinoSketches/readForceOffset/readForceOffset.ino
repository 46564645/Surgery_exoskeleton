int forceAnalogInPin = A3;
int forceIs;
const int forceOffset = 439;

void setup(){
  Serial.begin(9600);
  delay(1000);
}

void loop(){ 
	Serial.print("Force: ");
	forceIs = analogRead(forceAnalogInPin);
        forceIs -= forceOffset;
	Serial.println(forceIs);
	delay(10);
}
