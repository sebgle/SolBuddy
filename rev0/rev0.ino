/*
  Authors: Sebastian Le, Emir Durakovic
  Revision #0
  Light Project (to be renamed)
  Purpose: read all channels from AS7343 to the Feather and print readings to the serial monitor
*/

#include <Adafruit_AS7343.h>

Adafruit_AS7343 as7343;

void setup() {

  /* Configure serial communication */
  Serial.begin(115200); // init baud rate 
  while (!Serial) { 
    delay(500);
  }

  /* Find Sensor */
  while (!as7343.begin()) {
    Serial.println("Could not find AS7343 sensor!");
    delay(10);
  }
  Serial.println("AS7343 found!");

  /* Configure Sensor */
  as7343.setGain(AS7343_GAIN_256X); // set to default gain
  as7343.setATIME(29);  // Integration cycles
  as7343.setASTEP(599); // Step size   -> integration time = (29 + 1) * (599 + 1) * 2.78us

  /* Print info */
  Serial.print("Gain: ");
  Serial.println(as7343.getGain());
  Serial.print("Integration time: ");
  Serial.print(as7343.getIntegrationTime());
  Serial.println("ms");
}



void loop() {
  uint16_t readings[18];

  // Read all channels (starts measurement, waits, reads internally)
  if (!as7343.readAllChannels(readings)) {
    Serial.println("Read failed!");
    delay(500);
    return;
  }

  // Print spectral channels (wavelength order)
  Serial.println("\n--- Spectral Readings ---");

  Serial.print("F1  (405nm violet):     ");
  Serial.println(readings[AS7343_CHANNEL_F1]);

  Serial.print("F2  (425nm violet-blue):");
  Serial.println(readings[AS7343_CHANNEL_F2]);

  Serial.print("FZ  (450nm blue):       ");
  Serial.println(readings[AS7343_CHANNEL_FZ]);

  Serial.print("F3  (475nm blue-cyan):  ");
  Serial.println(readings[AS7343_CHANNEL_F3]);

  Serial.print("F4  (515nm green):      ");
  Serial.println(readings[AS7343_CHANNEL_F4]);

  Serial.print("F5  (550nm green-yel):  ");
  Serial.println(readings[AS7343_CHANNEL_F5]);

  Serial.print("FY  (555nm yellow-grn): ");
  Serial.println(readings[AS7343_CHANNEL_FY]);

  Serial.print("FXL (600nm orange):     ");
  Serial.println(readings[AS7343_CHANNEL_FXL]);

  Serial.print("F6  (640nm red):        ");
  Serial.println(readings[AS7343_CHANNEL_F6]);

  Serial.print("F7  (690nm deep red):   ");
  Serial.println(readings[AS7343_CHANNEL_F7]);

  Serial.print("F8  (745nm near-IR):    ");
  Serial.println(readings[AS7343_CHANNEL_F8]);

  Serial.print("NIR (855nm near-IR):    ");
  Serial.println(readings[AS7343_CHANNEL_NIR]);

  // Print clear/VIS channels (one from each cycle)
  Serial.print("VIS (clear):            ");
  Serial.println(readings[AS7343_CHANNEL_VIS_TL_0]);

  delay(500);
}
