int forceAnalogInPin = A3;
int forceIs;

void setup(){
  Serial.begin(9600);
  delay(1000);
}

void loop(){ 
	Serial.print("Force: ");
	forceIs = analogRead(forceAnalogInPin);
	Serial.println(forceIs);
	delay(10);
}
