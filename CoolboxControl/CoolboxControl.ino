#include "DHT.h" //Library for the DHT Temperature Sensor
#include "CommandLine.h"
#include <EEPROM.h> //Library needed to read and write from the EEPROM

//#define DEBUG
//#define NOSAFETY
#define SIGNATURE 0xAE //Used to check if the EEPROM has been initialised
#define MODE_LOCATION 4
#define UNDERVOLTAGEFLAG_LOCATION 8
#define UNDERVOLTAGELEVEL_LOCATION 12
#define TARGETAUTO_LOCATION 16
#define OVERVOLTAGEFLAG_LOCATION 20
#define OVERVOLTAGELEVEL_LOCATION 24
#define ALWAYSONVOLTAGE_LOCATION 28


#define DHTPIN 2    
#define DHTTYPE DHT11   // DHT 11
#define RELAYCONTROL 7
#define ONVALUE HIGH
#define OFFVALUE LOW
#define FIRSTCYCLONMINS 1
#define ONCYCLEIMINS 4
#define OFFCYCLEMINS 1
#define LOWVOLTAGETHRESHOLD 685
#define HIGHVOLTAGETHRESHOLD 770
#define ALLWAYSONVOLTAGE 710
#define TARGETAUTO 15


/* 
 *  Introduction of the Zener protection diode for 5V leads to non-linearity of readings
 *  Lower voltages are fine based on the 10/5.1 divider in circuit. The 4V3 Zener across R2 
 *  means readings above about 9V go non linear. Observed data follows
 *  
Volts  Reading
1.84  131
2.9 208
4.6 333
7.8 540
9.34  610
10.7  656
11.71 664
12.0  690
12.5  701
13  711
13.3  716
13.61 722
14.5  736
14.81 741

Warm weather changes this reading. 773 was recoreded for 14.81


 */


int coolCount=0;
int warmCount=0;
int linevolts=0;
int underVoltageLevel=1023;
int overVoltageLevel=0;
int alwaysOnLevel=1023;
int targetTemp=0;


float GlobalTemp;
float initialTemp=0;
float lastTemp=0;
float startWarmCycleTemp=0;
float startCoolingCycleTemp=0;

bool undervoltagecut=true;
bool overvoltagecut=true;
bool coolingCycle=true;
bool firstRun=true;
bool keepWait=true;
bool verbage=false;
bool changeMode=false;
int  modeSelect=1;
String Mode;

DHT dht(DHTPIN, DHTTYPE);  //INITIALISE the DHT Library instance

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  Serial.println();
  Serial.println();
  Serial.println("Cooler Improver - (c) Gareth Davies, Brighton, 2019");

#ifdef DEBUG
  verbage=true;
#endif

  readDefaults();
  
  Serial.println();
  Serial.println();
  
  dht.begin();
  pinMode(RELAYCONTROL,OUTPUT);
  pinMode(6,OUTPUT);
  pinMode(5,OUTPUT);
  pinMode(A7,INPUT);

  overVoltage();
  underVoltage(); //Check for undervoltage on switch on
  initialTemp=dht.readTemperature();
  Serial.println("Init ON");
  cool();
#ifndef NOSAFETY
  delay(1000);
  underVoltage(); //checks for undervoltage under load
  delay(1000);
#endif
 
  Serial.println("Init OFF");
  warm(); 
#ifndef NOSAFETY
  delay(2000);
#endif
  Serial.print("Ready > ");

}


void loop() {


  //if this is the first loop, turn on cooling now
  //Turn on the coolbox 
  if (firstRun==true)
  {
      startCoolingCycleTemp=dht.readTemperature();
      GlobalTemp=startCoolingCycleTemp;
      lastTemp=startCoolingCycleTemp;
      startWarmCycleTemp=startCoolingCycleTemp;
      if (verbage){
              Serial.print("Starting Temp is ");
              Serial.println(startCoolingCycleTemp);
      }
  selectmode(modeSelect);
   }
}




void selectmode(int modenumber)
{
  modeSelect=modenumber;
  updateMode(modenumber); //writes to EEPROM for restart
  changeMode=false;
  switch (modenumber)
  {
    case 1: Mode="Auto";
            automode();
            break;
    case 2: Mode="Target Auto";
            targetauto();
            break;
    case 3: Mode="Cycle";
            cyclemode();
            break;
    case 4: Mode="Manual Cool";
            longCool();
            break;
    case 5: Mode="Manual Warm";
            longWarm();
            break;
    default:
          modeSelect=1;
          Serial.println("mode select error");
          automode();
  }
}


void targetauto()
{
 
    Mode="Target";
    GlobalTemp=dht.readTemperature();
    while (isnan(GlobalTemp) && (changeMode==false) ) {
          #ifndef NOSAFETY
           Serial.println(F("Fail DHT read!"));
           #endif
        GlobalTemp=dht.readTemperature();
    }

    while ( (GlobalTemp > targetTemp) && (changeMode==false) )
    {
          GlobalTemp=dht.readTemperature();
          while (isnan(GlobalTemp)) {
          #ifndef NOSAFETY
                   Serial.println(F("Fail DHT read!"));
          #endif
                GlobalTemp=dht.readTemperature();
          }

      switchtoCool(ONCYCLEIMINS);
    }
    selectmode(1);
}


void cyclemode(void)
{
  
  while (changeMode==false)
  {
    Mode="Cy Cool";
    switchtoCool(ONCYCLEIMINS);
    Mode="Cy Warm";
    switchtoWarm(ONCYCLEIMINS);
  }
}

void longCool(void)
{
       switchtoCool(60);
}

void longWarm(void)
{
      switchtoWarm(60);
}

void automode(void)
{
  
  if (coolingCycle==true)
  {
    //Did the temp go down?
    if ( (GlobalTemp<startCoolingCycleTemp-0.1) || (GlobalTemp>=startCoolingCycleTemp+0.1) || (GlobalTemp>=startWarmCycleTemp) || (linevolts> alwaysOnLevel))
    { 
      Mode="Auto Cool - Reason ";
      if  (GlobalTemp<startCoolingCycleTemp-0.1)  { Mode.concat(" ; temp -- "); }
      if  (GlobalTemp>startCoolingCycleTemp+0.1)  { Mode.concat(" ; temp ++ "); }
      if  (GlobalTemp>=startWarmCycleTemp)  { Mode.concat(" ; temp >= last warm "); }
      if  (linevolts>alwaysOnLevel) { Mode.concat(" , Sol V++ Pwr-Dump"); }
      
      switchtoCool(ONCYCLEIMINS);
      startCoolingCycleTemp=GlobalTemp;
      waitMins(ONCYCLEIMINS);
    }
     else 
    { 
     switchtoWarm(OFFCYCLEMINS);
     Mode="Auto W->C";
    }
  }

 else
  {
    /// Cooling is off, decide if it needs to go on again

    if (GlobalTemp>=startWarmCycleTemp+0.2)
    {
      Mode="Auto SQ C->W";
      switchtoCool(ONCYCLEIMINS);
    }
    else
    {
      Mode="Auto warm";
      switchtoWarm(OFFCYCLEMINS);
      waitMins(OFFCYCLEMINS);
    }
    if(GlobalTemp<startWarmCycleTemp){startWarmCycleTemp=GlobalTemp;} //keeps it rolling down.
  }
  lastTemp=GlobalTemp;

}


void cool()
{
       digitalWrite(RELAYCONTROL,ONVALUE);
       coolCount++;
       if (verbage){
            Serial.print("COOL Temp now ");
            Serial.print(GlobalTemp);
            Serial.print(" at start of this cycle it was ");
            Serial.print(startWarmCycleTemp,3);
            Serial.println(" -  cool cycle");
      }
}

void warm()
{
       digitalWrite(RELAYCONTROL,OFFVALUE);
       warmCount++;     
       if (verbage) {
            Serial.print("WARM Temp now ");
            Serial.print(GlobalTemp);
            Serial.print(" at starting temp  ");
            Serial.print(startCoolingCycleTemp,3);
            Serial.println(" -  w-cycle");
      }

}


void switchtoCool(int ontime)
{
      coolingCycle=true;
      cool(); 
      waitMins(ontime);
}

void switchtoWarm(int offtime)
{
      coolingCycle=false;
      startWarmCycleTemp=GlobalTemp;
      warm();
      waitMins(offtime);
}



/*
 * WAIT CYCLE WITH ENOUGH KEEP ALIVE / BREAK OUT TO ENABLE A COMMAND LINE INPUT TO CONTINUE TO WORK
 */

void waitMins(int mins)
{
  if (getCommandLineFromSerialPort(CommandLine) )
          {
            DoCommand(CommandLine);
  }
  keepWait=true;
  mins=mins*4;

    for (int lp=0;lp<mins;lp++)
    {
        if ( keepWait && !changeMode ) {
        for (int innerlp=0;innerlp<150;innerlp++)
        {
          delay(100);
          if(!keepWait || changeMode ) {break;}
          underVoltage();
          overVoltage();
          if (getCommandLineFromSerialPort(CommandLine) )
          {
            DoCommand(CommandLine);
          }
        }//for
     } //if keepWait
   }//for
}
 



/* VOLTAGE SENSOR AND UNDERVOLTAGE DETECTION ETC
 *  
 */


bool highLineVoltage(void)
{
  linevolts=analogRead(A7);
  if (linevolts<overVoltageLevel)
  {
    return false;
  }

#ifdef DEBUG
if (verbage) {
  Serial.print("+V ");
  Serial.print(overVoltageLevel);
  Serial.print("  < ");
  Serial.println(linevolts);
}
#endif

  if (!overvoltagecut)
  {
      return false;
  }

  return true;
  
}


bool lowLineVoltage(void)
{
  linevolts=analogRead(A7);
  if (linevolts>underVoltageLevel)
  {
    return false;
  }

#ifdef DEBUG
  Serial.print("DEBUG MODE NOACTION TAKEN - UNDERVOLTS Threhold set at ");
  Serial.print(underVoltageLevel);
  Serial.print(" Measurement = ");
  Serial.println(linevolts);
#endif

    if (!undervoltagecut)
    {
          return false;
    }

  return true;
}

void overVoltage(void)
{

#ifndef NOSAFETY
  String holder=Mode;
  while (highLineVoltage () ==true)
  {
    delay (1000); //wait for the voltage to come back
    //check one more time for transients
    if (highLineVoltage()==true)
    {
      Mode="V++ cut-out";
      digitalWrite(RELAYCONTROL,OFFVALUE);
      Serial.println("++V Cut");
      waitMins(1);
    }
    else
    {
      Serial.println("++V Transient");
    }
  }
#endif
  Mode=holder;
}

void underVoltage(void)
{

#ifndef NOSAFETY
  String holder = Mode;
  while (lowLineVoltage () == true)
  {
    delay (10000); //wait for the voltage to come back
    //check one more time for transients
    if (lowLineVoltage()==true)
    {
      Mode="Under Voltage";
      digitalWrite(RELAYCONTROL,OFFVALUE);
      Serial.println("--V");
      waitMins(1);
    }
    else
    {
      Serial.println("--V Transient");
    }
  }
  Mode=holder;
#endif
}


/*
 * REPORTING ON CURRENT STATUS
 */


void report()
{
  float tt;
  tt=dht.readTemperature();
  while (isnan(tt)) {
#ifndef NOSAFETY
         Serial.println(F("Failed read DHT!"));
#endif
      tt=dht.readTemperature();
      underVoltage();
      waitMins(0);
   }

  Serial.print("Mode = ");
  Serial.println (Mode);

  
  Serial.print("Temperature Now = ");
  Serial.println(tt,3);
  
  Serial.print("Global Temp =");
  Serial.println(GlobalTemp,3);
  
  Serial.print("Temp start of loop =");
  Serial.println(lastTemp,3);
  
  Serial.print("Temp warm loop start =");
  Serial.println(startWarmCycleTemp,3);
  
  
  Serial.print("Temp initialised =");
  Serial.println(initialTemp,3);
  

  Serial.print("Preset Target = ");
  Serial.print(targetTemp);
  Serial.print(" Default = ");
  Serial.println(TARGETAUTO);
  
  Serial.print("On Cycle (mins) = ");
  int a=ONCYCLEIMINS;
  Serial.println(a);
  Serial.print("Off cycle (mins) = ");
  a=OFFCYCLEMINS;
  Serial.println(a);
  
  Serial.print("On Cycles = ");
  Serial.println(coolCount);
  Serial.print("Off Cycles = ");
  Serial.println(warmCount);
  
  float saving=(float)coolCount/(float)(warmCount+coolCount);
  saving=saving*100;

  Serial.print("onPercentage = ");
  Serial.print(saving,2);
  Serial.println("%");

  Serial.print("Voltage Measurement ");
  Serial.println(linevolts);
  

  Serial.print("Undervolts");
  if (undervoltagecut)
  {
    Serial.println(" Y");
  } else
  {
    Serial.println(" N");
  }
  
  Serial.print("Low V cut ");
  Serial.print(underVoltageLevel);
  Serial.print(" Default = ");
  Serial.println(LOWVOLTAGETHRESHOLD);

  Serial.print("Overvolts");
  if (overvoltagecut)
  {
    Serial.println(" Y");
  } else
  {
    Serial.println(" N");
  }  
  Serial.print("High V cut ");
  Serial.print(overVoltageLevel);
  Serial.print(" Default = ");
  Serial.println(HIGHVOLTAGETHRESHOLD);
  
  Serial.print("Always On Voltage ");
  Serial.print(alwaysOnLevel);
  Serial.print(" Default = ");
  Serial.println(ALLWAYSONVOLTAGE);
  
}



/*
 * ALL COMMAND LINE EXECUTION STUFF GOES HERE
 */

/////////////?COMMAND LINE INTERFACE EXECUTION

void printHelp()
{
   Serial.println("");
   Serial.println("COOLER HELP say ");
   Serial.println("break | b");
   Serial.println("cool | c");
   Serial.println("warm | w");
   Serial.println("report | r");
   Serial.println("verbose | v");
   Serial.println("auto | a");
   Serial.println("cycle | cy");
   Serial.println("target | ta");
   Serial.println("default | d --- returns to factory setting");
   //Serial.println("undervoltage");
   //Serial.println("setundervoltage");
   //Serial.println("wakeup");
   Serial.println("Help | ?");
}



/*************************************************************************************************************
     your Command Names Here
*/
const char *coolCommandToken  = "cool";  
const char *coolCommandToken2  = "c";
const char *warmCommandToken  = "warm";
const char *warmCommandToken2  = "w";
const char *reportCommandToken = "report";
const char *reportCommandToken2 = "r";
const char *breakCommandToken = "break";
const char *breakCommandToken2 = "b";
const char *helpCommandToken = "?";
const char *helpCommandToken2 = "help";
const char *toggleVerbageToken = "verbose";
const char *toggleVerbageToken2 = "v";
const char *automodeToken = "auto";
const char *automodeToken2 = "a";
const char *cyclemodeToken = "cycle";
const char *cyclemodeToken2 = "cy";
const char *targetautoToken = "target";
const char *targetautoToken2 = "ta";
const char *returndefaultToken = "default";
const char *returndefaultToken2 = "d";


/****************************************************
   DoMyCommand
*/

bool DoCommand(char * commandLine) {

  bool commandExecuted=false;
  char * ptrToCommandName = strtok(commandLine, delimiters);

   //HELP COMMAND /////////
   if ((strcmp(ptrToCommandName, helpCommandToken) == 0) | strcmp(ptrToCommandName, helpCommandToken2)==0)  { 
     printHelp();
     commandExecuted=true;
     Serial.print("\nReady > ");
   }
   
   if ((strcmp(ptrToCommandName, reportCommandToken) == 0) | strcmp(ptrToCommandName, reportCommandToken2)==0)  { 
     report();
     commandExecuted=true;
     Serial.print("\nReady > ");
   }

   
  if ((strcmp(ptrToCommandName, breakCommandToken) == 0) | strcmp(ptrToCommandName, breakCommandToken2)==0)  { 
     keepWait=false; //break out of the wait loop
     commandExecuted=true;
     Serial.print("\nReady > ");
   }

  if ((strcmp(ptrToCommandName, coolCommandToken) == 0) | strcmp(ptrToCommandName, coolCommandToken2)==0)  { 
     Serial.print("\nReady > ");
     Serial.println("Starting 1 hour COOL cycle - break command to end early");
     modeSelect=4;
     changeMode=true;
     commandExecuted=true;
   }

   if ((strcmp(ptrToCommandName, warmCommandToken) == 0) | strcmp(ptrToCommandName, warmCommandToken2)==0)  { 
     Serial.println("Starting 1 hour WARM cycle - break command to end early");
     Serial.print("\nReady > ");
     modeSelect=5;
     changeMode=true;
     commandExecuted=true;
   }

    if ((strcmp(ptrToCommandName, toggleVerbageToken) == 0) | strcmp(ptrToCommandName, toggleVerbageToken2)==0)  { 
     Serial.println("Toggle Verbose Mode");
     verbage=!verbage;
     Serial.print("Verbose mode = ");
     Serial.println(verbage);
     Serial.print("\nReady > ");
     commandExecuted=true;
   }


  if ((strcmp(ptrToCommandName, returndefaultToken) == 0) | strcmp(ptrToCommandName, returndefaultToken2)==0)  { 
     Serial.println("Returning to factory settings");
     returnToDefault();
     Serial.print("\nReady > ");
     commandExecuted=true;
   }

if ((strcmp(ptrToCommandName, cyclemodeToken) == 0) | strcmp(ptrToCommandName, cyclemodeToken2)==0)  { 
     Serial.println("Cycle Mode");
     Serial.print("Cycle mins = ");
     Serial.println(ONCYCLEIMINS);;
     Serial.print("\nReady > ");
     modeSelect=3;
     changeMode=true;
     commandExecuted=true;
   }

if ((strcmp(ptrToCommandName, automodeToken) == 0) | strcmp(ptrToCommandName, automodeToken2)==0)  { 
     Serial.println("Automode");
     Serial.print("\nReady > ");
     modeSelect=1;
     changeMode=true;
     commandExecuted=true;
   }

   if ((strcmp(ptrToCommandName, targetautoToken) == 0) | strcmp(ptrToCommandName, targetautoToken2)==0)  { 
     Serial.print("Target Auto - Target ");
     Serial.println(TARGETAUTO);
     Serial.print("\nReady > ");
     modeSelect=2;
     changeMode=true;
     commandExecuted=true;
   }
   
   if (!commandExecuted)
   {
      Serial.println("Not Recognised");
      Serial.println("");
      printHelp();
      Serial.print("\nReady > ");
   }

 
}


/***************
 * EEPROM DEFAULT HANDLING AND SETTING ETC
 */


void writeEPROM(int addr, int inp)
{
  byte LSB=inp;
  byte MSB=inp>>8;
  EEPROM.update(addr,LSB);
  EEPROM.update(addr+1,MSB);
#ifdef DEBUG
  Serial.print("EEPROM LOC:");
  Serial.print(addr);
  Serial.print(" Write = ");
  Serial.println(inp);
#endif
}


int readEPROM(int addr)
{
  byte LSB=EEPROM.read(addr);
  byte MSB=EEPROM.read(addr+1);
  int OP=MSB;
  OP = (OP<<8);
  OP = OP|LSB;
#ifdef DEBUG
  Serial.print("EEPROM LOC:");
  Serial.print(addr);
  Serial.print(" = ");
  Serial.println(OP);
#endif
  return OP;
}


void readDefaults()
{
  
  if (EEPROM.read(0) != SIGNATURE)
    {
#ifdef DEBUG
      Serial.println("Initialising defaults");
#endif
      EEPROM.write(0,SIGNATURE);
      returnToDefault();
    }
    else
    {
      readEPROMVals();
    }
}



void readEPROMVals()
{
      undervoltagecut=readEPROM(UNDERVOLTAGEFLAG_LOCATION);
      overvoltagecut=readEPROM(OVERVOLTAGEFLAG_LOCATION);
      modeSelect=readEPROM(MODE_LOCATION);
      underVoltageLevel=readEPROM(UNDERVOLTAGELEVEL_LOCATION);
      overVoltageLevel=readEPROM(OVERVOLTAGELEVEL_LOCATION);
      targetTemp=readEPROM(TARGETAUTO_LOCATION);
      alwaysOnLevel=readEPROM(ALWAYSONVOLTAGE_LOCATION); 
}

void reportEPROMVals()
{
     readEPROM(UNDERVOLTAGEFLAG_LOCATION);
     readEPROM(OVERVOLTAGEFLAG_LOCATION);
     readEPROM(MODE_LOCATION);
     readEPROM(UNDERVOLTAGELEVEL_LOCATION);
     readEPROM(OVERVOLTAGELEVEL_LOCATION);
     readEPROM(TARGETAUTO_LOCATION);
     readEPROM(ALWAYSONVOLTAGE_LOCATION); 
}


void returnToDefault()
{
      updateMode(1);
      updateAlwaysOnLevel(ALLWAYSONVOLTAGE);
      updateLowVoltageThreshold(LOWVOLTAGETHRESHOLD);
      updateHighVoltageThreshold(HIGHVOLTAGETHRESHOLD);
      updateTargetAutoTemp(TARGETAUTO);
      updateLowVoltageFlag(true);
      updateHighVoltageFlag(true);
}

void updateMode(int mode)
{
      writeEPROM(MODE_LOCATION,mode);
      modeSelect=mode;
      changeMode=true;
}

void updateAlwaysOnLevel(int allvolts)
{
      writeEPROM(ALWAYSONVOLTAGE_LOCATION,allvolts);
      alwaysOnLevel=allvolts;
}


void updateLowVoltageThreshold(int LowVolts)
{
      writeEPROM(UNDERVOLTAGELEVEL_LOCATION,LowVolts);
      underVoltageLevel=LowVolts;
}

void updateHighVoltageThreshold(int HighVolts)
{
      writeEPROM(OVERVOLTAGELEVEL_LOCATION,HighVolts);
      overVoltageLevel=HighVolts;

}

void updateLowVoltageFlag(bool onoff)
{
        writeEPROM(UNDERVOLTAGEFLAG_LOCATION,onoff);
        undervoltagecut=onoff;

}

void updateHighVoltageFlag(bool onoff)
{
        writeEPROM(OVERVOLTAGEFLAG_LOCATION,onoff);
        overvoltagecut=onoff;

}

void updateTargetAutoTemp(int Temp)
{
        writeEPROM(TARGETAUTO_LOCATION,Temp);
        targetTemp=Temp;
}

