// DESCRIPTION
// This script allows control over the steppermotor in order to perform force-deflection measurements.
// Data is being logged by the Raspberry Pi via the complementary python script which communicates via serial bus with this Arduino.
// User input:
//  - Any number, both positive and negative, for movement relative to the current position. Unit is mm. positive is moving down, negative is moving up.
//  - "c" in order to move the stepper forward until contact is made automatically. Useful to find the proper position from where to start measuring. ********** Needs change for compliant joint.
//  - "s" to start the measurement. You will be prompted to input a measurement distance, then resolution, both in mm.
//  - "f" to get the current force reading of the load cell.
//  - "r" to zero the load cell value
 
 
// Define stepper motor parameters
#define dirPin 2                  // Pin that determines rotation direction
#define stepPin 3                 // Pin that determines rotation input
#define stepsPerRevolution 6400   // Amount of steps per revolution (Set in the stepper motor driver manually)
 
#define screwPitch 1              // Lead screw pitchin mm/rev
 
// Define load cell parameters
#include "HX711.h"                // Load cell library
HX711 loadCellFD;                // Pins used by load cel
float calibration_factor = 439;   // This calibration factor is adjusted to the load cell
 
// Force thresholds
float force_max = 100;                   // Max force in N
float stepperSpeed = 2; //linear speed in mm/s using the 1mm pitch screw
int stepper_delay = 1000000/(2*stepsPerRevolution*stepperSpeed); //microseconds
int samplesAvg = 1;      // Set to 0 for single sample.
String filename = "";
 
void setup() {
  Serial.begin(115200);
  delay(1000);
 
  // Load cell setup
  loadCellFD.begin(5, 6);
  loadCellFD.power_down();
  delay(200);
  loadCellFD.power_up();
  delay(200);
  loadCellFD.set_scale(calibration_factor); //what is this calibration factor?
  // loadCellFD.tare();                   // Optional, resets the scale to 0 at startup
 
  // Stepper motor setup
  pinMode(stepPin, OUTPUT);
  pinMode(dirPin, OUTPUT);
}
 
void MoveStepper(float input, int stepDelay){
  if(input >= 0)  {
    // Set the spinning direction clockwise to move the actuator down with a positive number:
    digitalWrite(dirPin, HIGH);
  }
  else if(input < 0)
  {
    // Set the spinning direction counterclockwise to move the actuator up with a negative number:
    digitalWrite(dirPin, LOW);
  }
 
  for (long i = 0; i <= abs(input)*(stepsPerRevolution/screwPitch); i++)
  {
    digitalWrite(stepPin, HIGH);
    delayMicroseconds(stepDelay);
    digitalWrite(stepPin, LOW);
    delayMicroseconds(stepDelay);
  }
}
 
float ReadLoadCell(int print, int samples){
  float units;
  units = loadCellFD.get_units(samples);            // In gram, averaged over 10 measurements // what is meant with this?
  float force = units/1000.00*9.81;            // Conversion to force. F = m*g
  if (print == 1)
  {
    Serial.print(force,5);
    Serial.println(" N");  
  }
  return force;
}
 
void StartMeasurement(float distance, float resolution, int i){
   
  float deflection = 0;
  float force;
  bool abort = false;
  float correctedDistance = distance-(resolution/10);
 
  while (deflection <= correctedDistance && abort == false){  //Extension / down movement of the actuator.
    force = ReadLoadCell(0,samplesAvg);
 
    Serial.print(force,5);
    Serial.print(" | ");
    Serial.print(deflection,3);
    Serial.print(" | ");
    Serial.println(i+1);
 
    if (abs(force) >= force_max)
    {
      Serial.println();
      Serial.println("Max force reached, aborting measurement");
      abort = true;
      break;
    }
 
    if(deflection<correctedDistance){
      MoveStepper(resolution,stepper_delay);
      deflection += resolution;
    }
  }


  while (deflection >= resolution && abort == false){ //Retraction
    force = ReadLoadCell(0,samplesAvg);
    Serial.print(force,5);
    Serial.print(" | ");
    Serial.print(deflection,3);
    Serial.print(" | ");
    Serial.println(i+1);

    if(deflection > 0){
      MoveStepper(-resolution,stepper_delay);
      deflection -= resolution;
    }
  }

    force = ReadLoadCell(0,samplesAvg); // Force read at 0 deflection after doing the retraction and before new loop
    Serial.print(force,5);
    Serial.print(" | ");
    Serial.print(deflection,3);
    Serial.print(" | ");
    Serial.println(i+1);

}
  
void loop() {
  Serial.println(F("******** MEMSYS FD Tester ********"));
  Serial.println(F("\tMAIN MENU"));
  Serial.println(F("Welcome to the FD testing setup. Please select one of the following options:"));
  Serial.println(F("r - Tare the Load cell back to 0. Make sure nothing is touching the actuator or the coupling."));
  Serial.println(F("f - Show current force measurement. Shows the current single measurement of the load cell."));
  Serial.println(F("a - Adjust Stepper motor speed. Modify the linear speed of the FD in mm/s."));
  Serial.println(F("s - Start a full measurement. Begins a full extension-retraction cycle of force measurements with the desired displacement and distance resolution"));
  Serial.println(F("[-inf,+inf] - Move the actuator any desired distance, positive numbers being extension or DOWN, and negative numbers retraction or UP. Careful with the physical limits of the setup and flexure!"));
  Serial.println(F("**********************************\n\n"));
 
  while(Serial.available() == 0){
    delay(200);
  }
  // Stepper motor loop based on the selected input
  char inByte = Serial.peek();
  if (inByte == 's'){
    Serial.println("Starting measurement cycle...");
    Serial.readString();                     // Clears serial buffer
    Serial.println("Enter specimen name/ID, e.g. EH-00XX-X-Stage-XX");
    while (Serial.available() == 0) {
      delay(200);
    }  // Wait for user input
    filename = Serial.readStringUntil('\n');  // Read filename
    filename.trim();  // Remove whitespace
    Serial.println("Please define measurement distance (mm)");
    while (Serial.available() == 0)
    {
      delay(200);
    }
    float distance = Serial.parseFloat();    // In mm
    Serial.readString();                     // Clears serial buffer
 
    Serial.println("Please define measurement resolution (mm)");
    while (Serial.available() == 0)
    {
      delay(200);
    }
    float resolution = Serial.parseFloat();    // In mm
    Serial.readString();                       // Clears serial buffer

    Serial.println("Set the number of loops to measure");
    while (Serial.available() == 0)
    {
      delay(200);
    }
    int loops = Serial.parseInt();    // In mm
    Serial.readString();                     // Clears serial buffer

    //Organize filename for the test
    String filenameFinal=filename+"_"+distance+"mm_"+resolution+"mm_"+loops+"Loops";
  
    //Start of measurement, header, do not change
    Serial.println("Starting measurement");
    Serial.print("FILENAME|");
    Serial.println(filenameFinal);  // Send to Python
    Serial.println();
    Serial.print("Force[N]");
    Serial.print(" | ");
    Serial.print("Deflection[mm]");
    Serial.print(" | ");
    Serial.println("Loop count");
    Serial.println();
    Serial.println("----------");  // DO NOT CHANGE THIS. This string signals the python script to start recording the data that comes after.

 
    for(int i=0;i<loops;i++){
      StartMeasurement(distance,resolution,i);
    }
    //ENd of measurement indicator, do not change
    Serial.println();
    Serial.println("Measurement completed");
    Serial.println();

  }
  else if (inByte == 'r')
  {
    Serial.println("Starting cell calibration, please don't move or touch the set up");
    Serial.readString();
    loadCellFD.tare();
    Serial.println("Loadcells are calibrated to zero\n\n");
  }
  else if (inByte == 'f')
  {
    Serial.print("Current force value: ");
    Serial.readString();
    ReadLoadCell(1,samplesAvg);
    Serial.println();
  }
  else if (inByte == 'a')
  {
    Serial.readString();                     // Clears serial buffer
    Serial.println("Enter desired stepper motor speed in mm/s. Suggested bewteen 1 and 3mm/s. Default 2mm/s. Maximum 25mm/s");
    while (Serial.available() == 0) {
      delay(200);
    }  // Wait for user input
    stepperSpeed = Serial.parseFloat();    // In mm
    Serial.readString();                     // Clears serial buffer
    stepper_delay = 1000000/(2*stepsPerRevolution*stepperSpeed);
  }
  else
  {
    float input = Serial.parseFloat();         // In mm
    Serial.readString();                       // Clears serial buffer
   
    Serial.print("Moving ");
    Serial.print(input);
    Serial.println("mm");
   
    MoveStepper(input,stepper_delay);
 
    Serial.println("Moved!\n\n");
    ReadLoadCell(1,samplesAvg);
  }
 
 
  delay(200);
}
 
