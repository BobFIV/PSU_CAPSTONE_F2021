void setup() {
  // put your setup code here, to run once:
   Serial.begin(9600); // open serial port, set the baud rate to 9600 bps
    pinMode(13, OUTPUT); //Set pin 13 as an 'output' pin as we will make it output a voltage.
    digitalWrite(13, LOW); //This turns on pin 13/supplies it with 3.3 Volts.
}

void loop() {
  // put your main code here, to run repeatedly:
    int analogValue = analogRead(A0);  //put Sensor insert into soil
    Serial.println(analogValue);
    if (analogValue > 300) {
      digitalWrite(13, HIGH);
    } else {
      digitalWrite(13, LOW);
    }
    delay(1000);
}
