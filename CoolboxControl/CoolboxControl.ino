#include "DHT.h"
#include "CommandLine.h"
//#define DEBUG
//#define NOSAFETY
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
float GlobalTemp;
float initialTemp=0;
float lastTemp=0;
float startWarmCycleTemp=0;
float startCoolingCycleTemp=0;
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
  
#ifndef NOSAFETY
  Serial.println("Undervoltage cut off enabled");
#else
  Serial.println("WARNING - RUNNNING WITH NO SAFETY MEASURES");
#endif

#ifdef DEBUG
  verbage=true;
#endif


  Serial.println();
  Serial.println();
  
  dht.begin();
  pinMode(RELAYCONTROL,OUTPUT);
  pinMode(6,OUTPUT);
  pinMode(5,OUTPUT);
  pinMode(A7,INPUT);

  overVoltage();
  underVoltage(); //Check for undervoltage on switch on

  
  Serial.print("Line Voltage = ");
  Serial.println(linevolts);
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
      coolingCycle=true;
      startCoolingCycleTemp=dht.readTemperature();
      GlobalTemp=startCoolingCycleTemp;
      lastTemp=startCoolingCycleTemp;
      startWarmCycleTemp=startCoolingCycleTemp;
        if (verbage){
              Serial.print("Starting Temp is ");
              Serial.println(startCoolingCycleTemp);
        }

#ifndef DEBUG
      while (isnan(startCoolingCycleTemp)) {
#ifndef NOSAFETY
          Serial.println(F("Failed to read from DHT sensor!"));
#endif
          startCoolingCycleTemp=dht.readTemperature();
          GlobalTemp=startCoolingCycleTemp;
          startWarmCycleTemp=startCoolingCycleTemp;
          underVoltage();
          waitMins(0);
       }
#endif
  selectmode(modeSelect);
   }
}



void selectmode(int modenumber)
{
  changeMode=false;
  modeSelect=modenumber;
  switch (modenumber)
  {
    case 1: automode();
            break;
    case 2: targetauto();
            break;
    case 3: cyclemode();
            break;
    case 4: longCool();
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

    while ( (GlobalTemp > TARGETAUTO) && (changeMode==false) )
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


void automode(void)
{
  
  if (coolingCycle==true)
  {
    //Did the temp go down?
    if ( (GlobalTemp<startCoolingCycleTemp-0.1) || (GlobalTemp>=startCoolingCycleTemp+0.1) || (GlobalTemp>=startWarmCycleTemp) || (linevolts> ALLWAYSONVOLTAGE))
    { 
      Mode="Auto Cool - Reason ";
      if  (GlobalTemp<startCoolingCycleTemp-0.1)  { Mode.concat(" ; temp -- "); }
      if  (GlobalTemp>startCoolingCycleTemp+0.1)  { Mode.concat(" ; temp ++ "); }
      if  (GlobalTemp>=startWarmCycleTemp)  { Mode.concat(" ; temp >= last warm "); }
      if  (linevolts>ALLWAYSONVOLTAGE) { Mode.concat(" , Sol V++ Pwr-Dump"); }
      
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
 



bool highLineVoltage(void)
{
  linevolts=analogRead(A7);
  if (linevolts<HIGHVOLTAGETHRESHOLD)
  {
    return false;
  }

#ifdef DEBUG
if (verbage) {
  Serial.print("+V set at ");
  Serial.print(HIGHVOLTAGETHRESHOLD);
  Serial.print("  now ");
  Serial.println(linevolts);
}
#endif

#ifdef NOSAFETY
      return false;
#endif

  return true;
  
}


bool lowLineVoltage(void)
{
  linevolts=analogRead(A7);
  if (linevolts>LOWVOLTAGETHRESHOLD)
  {
    return false;
  }

#ifdef DEBUG
  Serial.print("DEBUG MODE NOACTION TAKEN - UNDERVOLTS Threhold set at ");
  Serial.print(LOWVOLTAGETHRESHOLD);
  Serial.print(" Measurement = ");
  Serial.println(linevolts);
#endif

#ifdef NOSAFETY
      return false;
#endif

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
      Serial.println("++Volt Cut");
      waitMins(1);
    }
    else
    {
      Serial.println("++Volt Trans");
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
      Serial.println("--Volt");
      waitMins(1);
    }
    else
    {
      Serial.println("Trans");
    }
  }
  Mode=holder;
#endif
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
  Serial.println(Mode);
  
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

  Serial.print("DutyCycle = ");
  Serial.print(saving,2);
  Serial.println("%");


  Serial.print("Voltage Measurement ");
  Serial.println(linevolts);
  Serial.print("Turn Off Low Voltage Threshold ");
  Serial.println(LOWVOLTAGETHRESHOLD);
  Serial.print("High Voltage Protection Threshold ");
  Serial.println(HIGHVOLTAGETHRESHOLD);
  Serial.print("Always On Voltage ");
  Serial.println(ALLWAYSONVOLTAGE);
  
}



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
const char *underVoltageCommandToken = "undervoltage";
const char *setundervoltageCommandToken = "setundervoltage";
const char *toggleVerbageToken = "verbose";
const char *toggleVerbageToken2 = "v";
const char *automodeToken = "auto";
const char *automodeToken2 = "a";
const char *cyclemodeToken = "cycle";
const char *cyclemodeToken2 = "cy";
const char *targetautoToken = "target";
const char *targetautoToken2 = "ta";


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
     Mode="Manual Cool";
     modeSelect=4;
     changeMode=true;
     commandExecuted=true;
   }

   if ((strcmp(ptrToCommandName, warmCommandToken) == 0) | strcmp(ptrToCommandName, warmCommandToken2)==0)  { 
     Serial.println("Starting 1 hour WARM cycle - break command to end early");
     Serial.print("\nReady > ");
     Mode="Manual Warm";
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

