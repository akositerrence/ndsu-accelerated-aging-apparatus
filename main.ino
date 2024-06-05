#define ARDUINO_MAIN
#include "max6675.h"
#include "wiring_private.h"
#include "pins_arduino.h"
#include <SPI.h>
#include <SD.h>

// ---------------------------------------- initialization ---------------------------------------- //

// user defined variables
short highPressureCycles = 10; // number of cycles before depressurization
short lowPressureCycles = 10;  // number of cycles before repressurization
long printInterval = 1000;     // milliseconds between each reading - allow > 250 between reads

// define state switches
const short switchPin = 49;
volatile bool paused = false;

// define general performance pins
short heatingPad = 22;
short entranceCoolingValve = 24;
short exitCoolingValve = 26;
short entranceGasSolenoid = 28;
short exitGasSolenoid = 30;
short transducer = 32;

// define thermocouple pins
short SCK_1 = 44, CS_1 = 46, SO_1 = 48;
MAX6675 thermocouple_1(SCK_1, CS_1, SO_1); // reads temperature exiting pressure vessel
short SCK_2 = 38, CS_2 = 40, SO_2 = 42;
MAX6675 thermocouple_2(SCK_2, CS_2, SO_2); // reads temperature entering pressure vessel

// define execution interval timers
long lastWriteTime = 0; // variable to store the last write time
long lastPrintTime = 0; // initialize last print time to 0

// ---------------------------------------- performance - functions ---------------------------------------- //

void pauseResume()
{
  bool lastState = HIGH;               // initialize last switch state to high
  bool state = digitalRead(switchPin); // initialize current state with pin 49 input
  if (state == LOW && lastState == HIGH)
  {
    paused = !paused;
  }
  lastState = state;
}

void readSensors()
{
  float transducerVoltage = analogRead(A8);                                       // read voltage from transducer
  float flowSensorVoltage = analogRead(A9);                                       // read voltage from flow sensor
  float pressure = ((25 * (transducerVoltage - 0.5)) / 1000);                     // define pressure ( psi )
  float flowRate = (((flowSensorVoltage - 0.70138888888) / 0.0659722222) / 1000); // define flow rate ( liters/min )
}

void printData()
{
  if (millis() - lastPrintTime >= printInterval)
  {
    lastPrintTime = millis(); // update last print time
    // print temperature exiting pressure vessel
    Serial.println("temperature exit = ");
    Serial.print(thermocouple_1.readFahrenheit());
    Serial.print(" F");
    // print temperature entering pressure vessel
    Serial.println("temperature exit = ");
    Serial.print(thermocouple_2.readFahrenheit());
    Serial.print(" F");
    // print pressure
    Serial.println("pressure = ");
    Serial.print(pressure);
    Serial.print(" psi");
    // print flow rate
    Serial.println("flow rate = ");
    Serial.print(flowRate);
    Serial.print(" liters/min");
  }
}

void temperatureCycle()
{
  readSensors();
  while (thermocouple_1.readFahrenheit() >= 30)
  { // open gycol flow to cool until 30 F
    readSensors();
    delay(500);
    digitalWrite(exitCoolingValve, HIGH);
    delay(500);
    digitalWrite(entranceCoolingValve, HIGH);
    delay(500);
    printData();
    delay(500);
  }
  if (i <= highPressureCycles)
  { // jumps to cycle end process
    digitalWrite(entranceCoolingValve, LOW);
    delay(500);
    digitalWrite(exitCoolingValve, LOW);
    delay(5000);

    while (thermocouple_1.readFahrenheit() <= 110)
    {
      readSensors();
      digitalWrite(heatingPad, HIGH);
      printData();
      delay(500);
    }
    digitalWrite(heatingPad, LOW);
    delay(500);
  }
}

// ---------------------------------------- pin - configuration ---------------------------------------- //

void setup()
{
  Serial.begin(9600);                                                      // define serial communciation baud rate ( bits / second )
  pinMode(switchPin, INPUT_PULLUP);                                        // define master switch ( open switch = high, closed switch = low )
  attachInterrupt(digitalPinToInterrupt(switchPin), pauseResume, FALLING); // calls pause function when switch state transitions high to low

  delay(1000);

  // configure pin outputs
  pinMode(heatingPad, OUTPUT);           // heating pad
  pinMode(entranceCoolingValve, OUTPUT); // cooling valve entering heat exchanger
  pinMode(exitCoolingValve, OUTPUT);     // cooling valve exiting heat exchanger
  pinMode(entranceGasSolenoid, OUTPUT);  // gas solenoid valve entering system
  pinMode(exitGasSolenoid, OUTPUT);      // gas solenoid valve exiting system
  pinMode(transducer, OUTPUT);           // transducer
  delay(5000);

  // purge system of air
  digitalWrite(entranceGasSolenoid, HIGH);
  digitalWrite(exitGasSolenoid, HIGH); // open gas solenoids to purge the air with nitrogen
  delay(10000);

  digitalWrite(entranceGasSolenoid, LOW);
  digitalWrite(exitGasSolenoid, LOW); // close gas solenoids once system is purged
  delay(1000);

  Serial.println("reached: system purged");
  digitalWrite(transducer, HIGH); // power on transducer
}

// ---------------------------------------- master - logic ---------------------------------------- //

void loop()
{
  if (paused == false)
  {
    readSensors();
    printData();
    delay(500);

    // start of process
    digitalWrite(entranceCoolingValve, LOW); // close cooling valve entering system
    delay(500);

    digitalWrite(exitCoolingValve, LOW); // close cooling valve exiting system
    delay(5000);

    // power on heating pad until 110 F ~ 43.3 C
    while (thermocouple_1.readFahrenheit() <= 110)
    {
      digitalWrite(heatingPad, HIGH);
      readSensors();
      printData();
      delay(500);
    }
    digitalWrite(heatingPad, LOW);
    delay(5000);

    // pressurize system until 50 psi
    while (pressure < 50)
    {
      readSensors();
      digitalWrite(entranceGasSolenoid, HIGH);
      printData();
      delay(500);
    }
    digitalWrite(entranceGasSolenoid, LOW);
    delay(5000);

    // ---------------------------------------- process - cycling ---------------------------------------- //

    // run process at high pressure ( 50 psi ) for defined cycles with temperature cycling
    for (int i = 0; i <= highPressureCycles + 1; i++)
    {
      temperatureCycle();
      digitalWrite(heatingPad, LOW); // backup heating pad shutdown
    }

    digitalWrite(entranceCoolingValve, HIGH); // close pressurized side of cooling valve - unpressurized exit remains open
    delay(5000);

    // depressure system to 8 psi at 30 F - divider for high - low cycles
    while (pressure > 8)
    {
      readSensors();
      digitalWrite(exitGasSolenoid, HIGH);
      printData();
      delay(500);
    }
    digitalWrite(exitGasSolenoid, LOW);
    delay(5000);

    // run process at low pressure ( 8 psi ) for defined cycles with temperature cycling
    for (int i = 0; i <= lowPressureCycles + 1; i++)
    {
      temperatureCycle()
          digitalWrite(heatingPad, LOW); // backup heating pad shutdown
    }
  }
}