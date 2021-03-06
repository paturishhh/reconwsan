/*Uhm hi, you start counting at 0, okay, just for the gateway settings*/
/*Programmed for Gizduino IOT-644*/
#include <SoftwareSerial.h>
#include <avr/pgmspace.h>
#include <SPI.h>
#include <SD.h>
#include <avr/interrupt.h>
#include <math.h>

#define MAX_COMMAND_CODE 0x05 //note: starts from 0; 
#define MAX_COMMAND 0x03 // 12 ports * 3 modes if sensor/actuator ; placed as 2 temporarily
#define PORT_COUNT 0x0C // 12 ports
#define QUEUE_SIZE 0x08 // Queue for packet and serial
#define BUFFER_SIZE 0x10 // bytes queue will hold

int CS_PIN = 27; // for SPI; 27 is used for Gizduino IOT-644
byte errorFlag = 0x00; // bits correspond to an error
byte logicalAddress = 0x00; //contains address of the network
byte physicalAddress = 0x01; //unique address of the node (frodam 0-255) (source address)
byte destAddress = 0x00; //destination address (source address is set to destination address)
byte sourceAddress = 0x00;
byte configVersion = 0x00; //node config version
byte maxAttempt = 0x03; //for contacting the sink
boolean requestConfig = false; // checks if node is requesting config

byte apiCount = 0x01; //API version
byte commandCount = 0x00; //command being served
byte commandCounter = 0x00; //commandCounter for commands; for receiving
String packetReply = ""; //packet used to reply to node
unsigned int eventNotif; //event triggered command notification
unsigned int onDemandRegister; // if set, then there is an odm; 0-15

byte portConfigSegment[(int) PORT_COUNT]; // contains port type, odm/event/time mode
unsigned int actuatorValueOnDemandSegment[(int) PORT_COUNT]; // stores data to write on actuator if ODM
unsigned int portValue[(int) PORT_COUNT]; //stores port values but in BCD
unsigned int timerSegment[(int) PORT_COUNT]; //timer segment
unsigned int eventSegment[(int) PORT_COUNT * 2]; //event segment - 24 slots (0-23(17h)) 0-B (if threshold mode) C-17 (if range mode)
unsigned int actuatorDetailSegment[(int) PORT_COUNT]; //actuator details segment
byte segmentCounter = 0x00;  // counter for parsing the parts of the packet
byte tempModeStorage = 0x00; // stores the port config 00-07
byte portNum = 0x00;//parsing var
byte partCounter = 0x00; //part of the data counter when receiving configs
//byte actuatorPort = 0x00; //actuator port for event triggered
byte commandValue = 0x00; // for api = 3, contains the command value
byte checker = 0x00; // for api = 2 checker for threshold/range mode; api = 3 counter for receive

boolean headerFound = false;
byte serialQueue[QUEUE_SIZE][BUFFER_SIZE];
byte serialBuffer[BUFFER_SIZE];
//for queue
byte serialHead = 0x00;
byte serialTail = 0x00;
boolean isEmpty = true;
boolean isService = false; // status to check serialQueue ; set only when no packet is received or queue is full
boolean isFull = false; 

volatile unsigned long timeCtr; // counter for overflows
long portOverflowCounts [(int) PORT_COUNT]; //stores the overflow counters to be checked by interrupt


//Messages used which would be saved to the program space (flash) to save space
const char segmentBranch0[] PROGMEM = "@ PORT CONFIG SEGMENT";
const char segmentBranch1[] PROGMEM = "@ TIME SEGMENT";
const char segmentBranch2[] PROGMEM = "@ EVENT SEGMENT";
const char segmentBranch3[] PROGMEM = "@ ACTUATOR SEGMENT";
const char portConfigTypeBranch0[] PROGMEM = "Time based";
const char portConfigTypeBranch1[] PROGMEM = "Event base";
const char portConfigTypeBranch2[] PROGMEM = "ODM";
const char odmDetail0[] PROGMEM = "Port Num: ";
const char odmDetail1[] PROGMEM = "onDemandRegister: ";
const char odmDetail2[] PROGMEM = "Value of AND: ";
const char odmDetail3[] PROGMEM = "Value of TEMP: ";
const char errorMessage0[] PROGMEM = "Invalid port configuration!";
const char errorMessage1[] PROGMEM = "No SD Card Read";
const char errorMessage2[] PROGMEM = "Cannot access SD Card";
const char infoMessage0[] PROGMEM = "Range Mode";
const char infoMessage1[] PROGMEM = "Threshold Mode";
const char infoMessage2[] PROGMEM = "Keep Alive Message Requested";
const char infoMessage3[] PROGMEM = "Reset Segment Counter";
const char infoMessage4[] PROGMEM = "Getting Next Command";
const char infoMessage5[] PROGMEM = "All Commands Served";
const char infoMessage6[] PROGMEM = "Port Details: ";
const char infoMessage7[] PROGMEM = "Setting Timer Segment: ";
const char infoMessage8[] PROGMEM = "Updated Timer Segment: ";
const char infoMessage9[] PROGMEM = "Full Timer Segment: ";
const char infoMessage10[] PROGMEM = "Updated Event Segment1: ";
const char infoMessage11[] PROGMEM = "Full Event Segment: ";
const char infoMessage12[] PROGMEM = "Logical Address: ";
const char infoMessage13[] PROGMEM = "Physical Address: ";
const char infoMessage14[] PROGMEM = "Config Version: ";
const char infoMessage15[] PROGMEM = "Actuator onDemand Segment: ";
const char infoMessage16[] PROGMEM = "Port Value: ";
const char infoMessage17[] PROGMEM = "Sensor";
const char infoMessage18[] PROGMEM = "Actuator";
const char infoMessage19[] PROGMEM = "Incoming Port Configuration";
const char actuatorDetail0[] PROGMEM = "Actuator Segment: ";

const char* const messages[] PROGMEM = {segmentBranch0, segmentBranch1, segmentBranch2, segmentBranch3, portConfigTypeBranch0, portConfigTypeBranch1, portConfigTypeBranch2, odmDetail0, odmDetail1, odmDetail2, odmDetail3,
                                      errorMessage0,infoMessage0, infoMessage1, infoMessage2, actuatorDetail0, infoMessage3, infoMessage4, infoMessage5, infoMessage6, infoMessage7,
                                      infoMessage8, infoMessage9, infoMessage10, infoMessage11, errorMessage1, errorMessage2, infoMessage12, infoMessage13, infoMessage14, infoMessage15, infoMessage16, infoMessage17, infoMessage18, infoMessage19 };
char buffer[32]; //update according to longest message

/************* Other Node Modules *************/
void triggerOnDemand(){
  //check which port/s is set
  for(byte x = 0x00; x <=0x0C; x++){
    int onDemand = onDemandRegister & (0x01 << x); //shift it then AND it
    Serial.print("onDemandValue: ");
    Serial.println(onDemand);
    Serial.print("Checking port: ");
    Serial.println(x);
    if(onDemand == (0x01 << x)){ // if is set
      Serial.print("Port set: ");
      Serial.println(x);
      byte temp = portConfigSegment[x]; // get port config segment of that port
      temp = temp & 0x08; // check if 0x08 then actuator; if 0x00 sensor
      Serial.println("TEMP: ");
      Serial.print(temp);
      if(temp == 0x08){ // ODM - actuator write
        if(x>=0x06){ //analog
//          temp = analogWrite(port[x]);
          
        }
        else{ //digital
          
        }
        
      }
      else if(temp == 0x00){ // ODM - sensor read
        
      }
      
      
    }
  }
  //for each port set
    // check 3rd bit of port config segment [of that port]
      //if sensor
        // if port num is 0-5 then digital
        // else if port num 6-B then analog
        //read sensor data
        // save in port value
      //if actuator
        // if port num is 0-5 then digital
        // else if port num 6-B then analog
        //switch actuator data
        // store data in port value
      //clear it on onDemandRegister
     
}

void loadConfig(){ //loads config file and applies it to the registers
  byte fileCounter = 0x00;
  byte index = 0x00;
  byte hiByte = 0x00;
  byte loByte = 0x00;
  byte temp = 0x01; //for setting error flag
  
  if(!SD.begin(CS_PIN)){//in case sd card is not read
//    strcpy_P(buffer, (char*)pgm_read_word(&(messages[25])));
//    Serial.println(buffer); //error
    errorFlag |= temp;
    Serial.println(errorFlag, HEX);
    //contact sink node
  }
  else{
    File configFile = SD.open("conf.log");
    if(configFile){ // check if exists, if not then no sd card
      while(configFile.available()){
        int fileTemp = configFile.read();
        //reads the file per byte, in the case of the int (2 bytes) it is divided into 2 parts, hiByte and loByte.
        // then it is appended together
        if(fileCounter == 0x00){ //logical address
          logicalAddress = fileTemp;
          fileCounter = 0x01;
        }
        else if(fileCounter == 0x01){ //physical
          physicalAddress = fileTemp;
          fileCounter = 0x02;
        }
        else if(fileCounter == 0x02){ //config version
          configVersion = fileTemp;
          fileCounter = 0x03;
        }
        else if(fileCounter == 0x03){ //portconfigsegment(c)
          index = fileTemp;
          fileCounter = 0x04;
        }
        else if(fileCounter == 0x04){ 
          portConfigSegment[index] = fileTemp; 
          if(index!=(PORT_COUNT -1)){
            fileCounter = 0x03;
          }
          else{
            fileCounter = 0x05;
          }
         }
         else if(fileCounter == 0x05){ //actuatorValueOnDemandSegment(c)
          index = fileTemp;
          fileCounter = 0x06;
         }
         else if(fileCounter == 0x06){
          hiByte = fileTemp;
          fileCounter = 0x07;
         }
         else if(fileCounter == 0x07){ 
          loByte = fileTemp;
          actuatorValueOnDemandSegment[index] = word(hiByte, loByte);
          if(index !=(PORT_COUNT -1)){
            fileCounter = 0x05;
          }
          else{
            fileCounter = 0x08;
          }
         }
         else if(fileCounter == 0x08){ //portValue(c) 
          index = fileTemp;
          fileCounter = 0x09;
         }
         else if(fileCounter == 0x09){ 
          hiByte = fileTemp;
          fileCounter = 0x0A;
         }
         else if(fileCounter == 0x0A){ 
          loByte = fileTemp;
          portValue[index] = word(hiByte, loByte);
          if(index!= (PORT_COUNT -1)){
            fileCounter = 0x08;
          }
          else{
            fileCounter = 0x0B;
          }
         }
         else if(fileCounter == 0x0B){ //timerSegment(c) 
          index = fileTemp;
          fileCounter = 0x0C;
         }
         else if(fileCounter == 0x0C){
          hiByte = fileTemp;
          fileCounter = 0x0D;
         }
         else if(fileCounter == 0x0D){ 
          loByte = fileTemp;
          timerSegment[index] = word(hiByte, loByte);
          if(index!= (PORT_COUNT -1)){
            fileCounter = 0x0B;
          }
          else{
            fileCounter = 0x0E;
          }
         }
         else if(fileCounter == 0x0E){ //eventSegment(c*2) 
          index = fileTemp;
          fileCounter = 0x0F;
         }
         else if(fileCounter == 0x0F){
          hiByte = fileTemp;
          fileCounter = 0x10;
         }
         else if(fileCounter == 0x10){
          loByte = fileTemp;
          eventSegment[index] = word(hiByte, loByte);
          if(index!= ((PORT_COUNT * 2)-1)){
            fileCounter = 0x0E;
          }
          else{
            fileCounter = 0x11;
          }
         }
         else if(fileCounter == 0x11){ //actuatorDetailSegment(c)
          index = fileTemp;
          fileCounter = 0x12;
         }
         else if(fileCounter == 0x12){
          hiByte = fileTemp;
          fileCounter = 0x13;
         }
         else if(fileCounter == 0x13){
          loByte = fileTemp;
          actuatorDetailSegment[index] = word(hiByte, loByte);
          if(index!= (PORT_COUNT -1)){
            fileCounter = 0x11;
          }
          else{
            fileCounter = 0x14;
          }
         }
    }
      configFile.close();
      initializeTimer();
      calculateOverflow();
    }
    else{
//      strcpy_P(buffer, (char*)pgm_read_word(&(messages[26])));
//      Serial.println(buffer); //error
      byte temp = 0x08; 
      errorFlag |= 0x08; //setting error flag
      Serial.println(errorFlag, HEX);
    }
  }
}

void writeConfig(){  // writes node configuration to SD card
//  if(SD.begin(CS_PIN)){
    if(SD.exists("conf.log")){ //otherwise will overwrite
      SD.remove("conf.log");
      Serial.println("Creating new");
    }
    File configFile = SD.open("conf.log", FILE_WRITE);
    Serial.println("writing");
    if(configFile){
      configFile.write(logicalAddress);
      configFile.write(physicalAddress);
//      configFile.println(destAddress, HEX);
      configFile.write(configVersion);
      configFile.flush(); // pre save
        
      for(byte c=0x00; c<PORT_COUNT;c++){
        configFile.write(c);
        configFile.write(portConfigSegment[c]);
      }
      configFile.flush();
        for(byte c=0x00; c<PORT_COUNT;c++){
          configFile.write(c);
          configFile.write(highByte(actuatorValueOnDemandSegment[c]));
          configFile.write(lowByte(actuatorValueOnDemandSegment[c]));
        }
        configFile.flush();
        for(byte c=0x00; c<PORT_COUNT;c++){
          configFile.write(c);
          configFile.write(highByte(portValue[c]));
          configFile.write(lowByte(portValue[c]));
        }
        configFile.flush();
      for(byte c=0x00; c<PORT_COUNT;c++){
        configFile.write(c);
        configFile.write(highByte(timerSegment[c]));
        configFile.write(lowByte(timerSegment[c]));
      }
      configFile.flush();
        for(byte c=0x00; c<PORT_COUNT * 2; c++){
          configFile.write(c);
          configFile.write(highByte(eventSegment[c]));
          configFile.write(lowByte(eventSegment[c]));
        }
        configFile.flush();
        for(byte c=0x00; c<PORT_COUNT;c++){
          configFile.write(c);
          configFile.write(highByte(actuatorDetailSegment[c]));
          configFile.write(lowByte(actuatorDetailSegment[c]));
        }
      configFile.close();
      Serial.println("Writing finish");
    }
    else{
//      strcpy_P(buffer, (char*)pgm_read_word(&(messages[26])));
//      Serial.println(buffer); //error
      byte temp = 0x08; 
      errorFlag |= 0x08; //setting error flag
      Serial.println(errorFlag, HEX);
    }
//  }
//  else{
//    strcpy_P(buffer, (char*)pgm_read_word(&(messages[25])));
//    Serial.print(buffer);    
//  }
  
}
/************* Utilities *************/
void printBuffer(byte temp[QUEUE_SIZE][BUFFER_SIZE]){ // prints serialBuffer
  byte x = 0x00;
  byte halt = false;
  for(byte y = 0x00; y < QUEUE_SIZE; y++){
    for(byte x = 0x00; x < BUFFER_SIZE; x++){  
      while(!halt){
        Serial.print(temp[y][x],HEX);
        if(temp[y][x] == 0xFE)
          halt = true; 
        x = x + 0x01;
      }
    }
    halt = false;
    Serial.println();
  }
}

void printRegisters(){ // prints all variables stored in the sd card
  strcpy_P(buffer, (char*)pgm_read_word(&(messages[27])));
  Serial.print(buffer); 
  Serial.println(logicalAddress,HEX);

  strcpy_P(buffer, (char*)pgm_read_word(&(messages[28])));
  Serial.print(buffer); 
  Serial.println(physicalAddress,HEX);

  strcpy_P(buffer, (char*)pgm_read_word(&(messages[29])));
  Serial.print(buffer); 
  Serial.println(configVersion,HEX);

  for(byte x = 0x00; x <PORT_COUNT; x++){
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[19])));
    Serial.print(buffer);  
    Serial.println(portConfigSegment[x],HEX);  
  }

  for(byte x = 0x00; x <PORT_COUNT; x++){
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[30])));
    Serial.print(buffer);  
    Serial.println(actuatorValueOnDemandSegment[x],HEX);  
  }

  for(byte x = 0x00; x <PORT_COUNT; x++){ // 
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[31])));
    Serial.print(buffer);  
    Serial.println(portValue[x]);  
  }

  for(byte x = 0x00; x <PORT_COUNT; x++){
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[22])));
    Serial.print(buffer);  
    Serial.println(timerSegment[x],HEX);  
  }

  for(byte x = 0x00; x <PORT_COUNT*2; x++){
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[24])));
    Serial.print(buffer);  
    Serial.println(eventSegment[x],HEX);  
  }

  for(byte x = 0x00; x <PORT_COUNT; x++){
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[15])));
    Serial.print(buffer);  
    Serial.println(actuatorDetailSegment[x],HEX);  
  }  
}

void checkPortModesSent(){ // checks the configs of the ports then set segmentCounter accordingly, expecting the next parameters
  if ((tempModeStorage & 0x01) == 0x01){ //if timebased is set
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[4])));
    Serial.println(buffer);
    segmentCounter = 0x07;
  }
  else if((tempModeStorage&0x02) == 0x02){ // event
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[5])));
    Serial.println(buffer);
    segmentCounter = 0x08;
  }
  else if((tempModeStorage&0x04) == 0x04){ // on demand
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[6])));
    Serial.println(buffer);
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[7])));
    Serial.print(buffer);
    Serial.println(portNum, HEX);
    byte temp = portConfigSegment[portNum];
    onDemandRegister = 0x01 << portNum;
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[8])));
    Serial.print(buffer);
    Serial.println(onDemandRegister, BIN);
    
    //Checking bit 3 if it is actuator or sensor
    if((temp & 0x08) == 0x08){ //actuator
      segmentCounter = 0X0D; // get actuator segment
      strcpy_P(buffer, (char*)pgm_read_word(&(messages[33])));
      Serial.print(buffer);
    }
    else if((temp&0x08) == 0x00){ // sensor
      strcpy_P(buffer, (char*)pgm_read_word(&(messages[32])));
      Serial.print(buffer);
      tempModeStorage = tempModeStorage ^ 0x04; // switch off odm flag;  assumes dapat 0 na value ni tempMode Storage
      checkOtherCommands(); // check if there are still other configurations
    }
    else{
      //error ito
    }
  }
  else{ // invalid port config
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[11])));
    Serial.println(buffer);
    segmentCounter = 0x00;
    tempModeStorage = 0x00;
  }
}
  
void checkOtherCommands(){ // checks if there is still commands 
  if(commandCounter < commandCount){
    commandCounter = commandCounter + 1; 
    segmentCounter = 0x06; //to port config segment
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[17])));
    Serial.println(buffer);
  }
  else{
    segmentCounter = 0xFF; // go to footer
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[18])));
    Serial.println(buffer);
  }
}
/************ Setters  *************/
//USE CAUTION WHEN USING THIS.. I THINK TAMA NA NAMAN GINAGAWA NIYA
void setEventNotif(int val){
  eventNotif = eventNotif | val;
}

void setPortConfigSegmentInit(byte portNum, char portDetails){ //when initializing //CHANGED FROM INT
  byte tempPort = 0x00;
  tempPort = portNum << 4; // to move it pin position
  tempPort = tempPort | portDetails; // to set the 4th pin to output
  portConfigSegment[portNum] = tempPort;
}

void setPortConfigSegment(byte portNum, byte portDetails){
//  strcpy_P(buffer, (char*)pgm_read_word(&(messages[7])));
//  Serial.print(buffer);
//  Serial.println(portNum, HEX);
//  strcpy_P(buffer, (char*)pgm_read_word(&(messages[19])));
//  Serial.print(buffer);
//  Serial.println(portDetails, HEX);
  portConfigSegment[portNum] = portDetails;
}

void setPortValue(int portNum, int val){
  portValue[portNum] = val;
}

void setTimerSegment(byte portNum, int val){  // BYTE FROM INT; partCounter is a global var
  if(partCounter==0x00){
//    strcpy_P(buffer, (char*)pgm_read_word(&(messages[7])));
//    Serial.print(buffer);
//    Serial.println(portNum);
    timerSegment[portNum] = val;
//    strcpy_P(buffer, (char*)pgm_read_word(&(messages[20])));
//    Serial.print(buffer);
//    Serial.println(timerSegment[portNum], HEX);
    timerSegment[portNum] = timerSegment[portNum] << 8;
//    strcpy_P(buffer, (char*)pgm_read_word(&(messages[21])));
//    Serial.print(buffer);
//    Serial.println(timerSegment[portNum], HEX);
  }
  else if(partCounter==0x01){
    int temp = val;
    timerSegment[portNum] = timerSegment[portNum] | val;
//    strcpy_P(buffer, (char*)pgm_read_word(&(messages[22])));
//    Serial.print(buffer);
//    Serial.println(timerSegment[portNum], HEX);
  }
}

void setEventSegment(byte portNum, int val){ //BYTE FROM INT
//  strcpy_P(buffer, (char*)pgm_read_word(&(messages[7])));
//  Serial.print(buffer);
//  Serial.println(portNum);
  if(partCounter == 0x00){
    eventSegment[portNum] = val << 8;
//    strcpy_P(buffer, (char*)pgm_read_word(&(messages[23])));
//    Serial.print(buffer);
//    Serial.println(eventSegment[portNum], HEX);
  }
  else if((partCounter & 0x01) == 0x01){
    eventSegment[portNum] = eventSegment[portNum] | val;
//    strcpy_P(buffer, (char*)pgm_read_word(&(messages[24])));
//    Serial.print(buffer);
//    Serial.println(eventSegment[portNum], HEX);
//    strcpy_P(buffer, (char*)pgm_read_word(&(messages[7])));
//    Serial.print(buffer);
//    Serial.println(portNum,HEX);
  }
}

void setRangeSegment(byte portNum, int val){ // for range type of events //BYTE FROM INT
//  strcpy_P(buffer, (char*)pgm_read_word(&(messages[7])));
//  Serial.print(buffer);
//  Serial.println(portNum);
  if(partCounter == 0x00){
//    Serial.print("Initial Value: ");
//    Serial.println(eventSegment[portNum+0x0C], HEX);
    eventSegment[portNum+0x0C] = val << 8;
//    strcpy_P(buffer, (char*)pgm_read_word(&(messages[23])));
//    Serial.print(buffer);
//    Serial.println(eventSegment[portNum+0x0C], HEX);
  }
  else if((partCounter & 0x01) == 0x01){
    eventSegment[portNum+0x0C] = eventSegment[portNum+0x0C] | val;
//    strcpy_P(buffer, (char*)pgm_read_word(&(messages[23])));
//    Serial.print(buffer);
//    Serial.println(eventSegment[portNum+0x0C], HEX);
//    Serial.print("Index in Array: ");
//    Serial.println(portNum+0x0C,HEX);
  }
}

void setActuatorDetailSegment(byte portNum, int val){ 
  if(partCounter == 0x00){ // store first part
//    strcpy_P(buffer, (char*)pgm_read_word(&(messages[15]))); // Actuator Segment:
//    Serial.print(buffer);
//    Serial.println(actuatorDetailSegment[portNum], HEX);
    actuatorDetailSegment[portNum] = val << 8 ;
//    Serial.print("Updated Upper Value: ");
//    Serial.println(actuatorDetailSegment[portNum],HEX);
  }
  else if(partCounter == 0x01){
//    Serial.println("LOWER ACTUATOR DETAIL");
//    Serial.print("Serial Data: ");
//    Serial.println(val, HEX);
    actuatorDetailSegment[portNum] = actuatorDetailSegment[portNum] | val;
//    strcpy_P(buffer, (char*)pgm_read_word(&(messages[15]))); // Actuator Segment:
//    Serial.print(buffer);
//    Serial.println(actuatorDetailSegment[portNum], HEX);
  }
  
}

void setActuatorValueOnDemandSegment(byte portNum, int val){
  if(partCounter == 0x00){ // get port number and store first part
//    strcpy_P(buffer, (char*)pgm_read_word(&(messages[7])));
//    Serial.print(buffer);
//    Serial.println(portNum, HEX);
    actuatorValueOnDemandSegment[portNum] = val << 8 ;
//    Serial.print("Updated Upper Value: ");
//    Serial.println(actuatorValueOnDemandSegment[portNum],HEX);
  }
  else if(partCounter == 0x01){
//    Serial.println("LOWER ACTUATOR VALUE ON DEMAND DETAIL");
//    Serial.print("Serial Data: ");
//    Serial.println(val, HEX);
    actuatorValueOnDemandSegment[portNum] = actuatorValueOnDemandSegment[portNum] | val;
//    Serial.print("Full Actuator value Segment : ");
//    Serial.println(actuatorValueOnDemandSegment[portNum], HEX);
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[7])));
//    Serial.print(buffer);
//    Serial.println(portNum,HEX);
  }
}

void calculateOverflow(){
   //if set yung timerRegister (set when may timeBased)
  //do this to timeSegment[bit that was set]
  //stop timer ah
  
  //convert time to seconds store to realTime
  unsigned int tempTime = 0x1100; //sample data ; 1.5hrs
  byte timeUnit = tempTime >> 12; // checks which unit 
  unsigned int timeKeeper = tempTime & ((1 << 12)-1); // get time only masking it
  //convert bcd to dec
  long timeTemp = bcd2dec(timeKeeper); //number in seconds
  long realTime = 0x00; //32 bits ; max is 24hrs 86400 seconds
  float timeMS = 0.000;

  //CONVERTS TO SECONDS
  if(timeUnit == 0x01 ){//ms 
    timeMS = timeTemp / 1000.0;
//    Serial.println(timeMS,3); //0.99
  }
  else if(timeUnit == 0x02){//sec
    //it is as is
  }
  else if(timeUnit == 0x04){//min    
    timeTemp = timeTemp * 60;
  }
  else if(timeUnit == 0x08){//hour
    timeTemp = timeTemp * 3600;
  }
  
  realTime = timeTemp;
//  Serial.println(realTime);

  //GETTING THE OVERFLOW VALUE IF TIME >=17ms ELSE overflowValue = tickValue
  //9-16ms = 0 overflow - need realTime = totalTicks;
  //well, up to 100ms lang siya sooo I'll just leave it here
  int frequency = 15625; // clock frequency / prescaler
  long totalTicks;
  if(timeUnit == 0x01){ //if ms; because it is float
    totalTicks = timeMS * frequency;
  }
  else{
    totalTicks = realTime * frequency;
  }
  long overflowCount;
  if(timeTemp <=16 && timeUnit == 0x01){ //if less than 16ms kasi hindi mag overflow
    overflowCount  = totalTicks;
  }
  else{
    overflowCount = totalTicks/pow(2, 8); //8 coz timer 2 has 8 bits
  }
 
//  Serial.print("OFC: ");
//  Serial.println(overflowCount);

  
  //check overflowCount if reached @ INTERRUPT
  
}

long bcd2dec(unsigned int nTime){
//  byte x;
  long temp = 0;
  //999
  temp = ((nTime>>8)%16);
//  nTime = nTime << 8;
//  Serial.println(temp);
  temp *= 10;
//  Serial.println(temp);
  temp += ((nTime>>4)%16);
  temp *= 10;
  temp += (nTime %16);
  return temp;
}

ISR(TIMER2_OVF_vect){
  timeCtr++;
//  Serial.println(timeCtr);
//  Serial.println(timeCtr);
  if((timeCtr%305) == 0){ //5sec
    Serial.println("Five seconds");
    digitalWrite(4, digitalRead(4) ^ 1);
  }
  if((timeCtr%219726) == 0){ //1hr
    Serial.println("1hr");
    digitalWrite(5, digitalRead(5)^1);
  }
  if((timeCtr%61) == 0){ //1 sec
//    Serial.println("1 sec");
    digitalWrite(6, digitalRead(6)^1);
  }
  if((timeCtr%30) == 0){ //0.5s
//    Serial.println("0.5sec");
    digitalWrite(7, digitalRead(7)^1);
  }
}

void initializeTimer(){
  //setting it up to normal mode
  // one OVF = 16ms ( 256 / (16MHZ/1024))
  cli(); //disable global interrupts

  TCCR2A = 0 ; 
  TCCR2B = 0 ; 
  TCCR2B |= (1 << CS22) | (1 << CS21) | (1 << CS20); // set prescaler to 1024
  TIMSK2 |= (1 << TOIE2); //enable interrupt to overflow
  sei(); //enable global interrupts  
}

void setup(){
  //test pins for timer
  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);
  pinMode(6, OUTPUT);
  pinMode(7, OUTPUT);
  pinMode(CS_PIN, OUTPUT); 
  digitalWrite(CS_PIN, HIGH);

   // to initiate serial communication
  Serial.begin(9600);
  timeCtr = 0;

// NOTE: AVOID PUTTING STUFF ON PIN 0 & 1 coz that is where serial is (programming, debugging)
  for (byte c=0x00; c<0x0C; c++){
    setPortConfigSegmentInit(c, 0); // set the port to input
//    Serial.print("Port Config Segment at Start: ");
//    Serial.println(getPortConfigSegment(c), HEX);
  }
  
  //sets values of the arrays to 0, needed else it would not print anything
  memset(actuatorValueOnDemandSegment,0,sizeof(actuatorValueOnDemandSegment)); 
  memset(portValue,0,sizeof(portValue)); 
  memset(timerSegment,0,sizeof(timerSegment)); 
  memset(eventSegment,0,sizeof(eventSegment)); 
  memset(actuatorDetailSegment,0,sizeof(actuatorDetailSegment)); 
  memset(portOverflowCounts, 0, sizeof(portOverflowCounts));
  memset(serialBuffer,0x00, sizeof(serialBuffer));
  memset(serialQueue, 0x00, sizeof(serialQueue));

//  if(SD.begin(CS_PIN)){ // uncomment entire block to reset node (to all 0)
//    writeConfig(); //meron itong sd.begin kasi nagrurun ito ideally after config... therefore na sd.begin na ni loadConfig na ito sooo if gusto mo siya irun agad, place sd.begin
//  }
//  else{
//    byte temp = 0x01;
//    errorFlag |= temp; // cannot access sd card
//    Serial.println(errorFlag, HEX);
//  }
  
  loadConfig(); //basically during the node's lifetime, lagi ito una, so if mag fail ito, may problem sa sd card (either wala or sira) therefore contact sink node
  //you cannot run write and load back to back coz you call sd.begin twice 
//  printRegisters();
 }

void loop(){
  boolean wait = 0;
  byte attemptCounter; //for contacting the sink
  byte serialData;  //temporary store of data read from Serial
  byte configSentPartCtr; 
  static byte index = 0; //for queue
  static size_t pos = 0; //for buffer
  
  //Communication module - Receive
  
//  if(Serial.available()>0){
////    delay(100);
//    while(Serial.available()>0){
//      if(!wait)
//       serialData = Serial.read();
//      else wait = 0;
//      
//      if(serialData == 0xFF && segmentCounter == 0x00){ //HEADER
//        segmentCounter = 0x01;
//      }
//      else if(segmentCounter == 0x01){ //SOURCE 
//        sourceAddress = (byte)serialData; 
//        segmentCounter = 0x02;
//      }
//      else if(segmentCounter == 0x02){ //DESTINATION 
//        if(serialData != physicalAddress){ //in case di pala para sa kanya; kasi broadcast mode si xbee; technically drop ze packet
//          segmentCounter = 0x00;
//          destAddress = (byte)serialData;
//        }
//        else{
//          segmentCounter = 0x03; //I think.. ilagay nalang yung physical address ni node mismo
//        }
//      }
//      else if(segmentCounter == 0x03){ //API
//        apiCount = (byte)serialData;
//        if(apiCount==2){ // if CONFIG mode, else iba na ieexpect niya na mga kasama na segment
//          segmentCounter = 0x04; // check config version
//        }
//        else if(apiCount==3){ // if Node Discovery Mode, expects  command parameter
//          segmentCounter = 0x0B; //command parameter
//        }
//        else{ //pero technically dapat error na ito
//          segmentCounter = 0x05; // go straight to count ; for debug purpose lang
//        }
//        
//      }
//      else if(segmentCounter == 0x04){ // CONFIG VERSION IF API = 2
//         if(configVersion <= serialData){ // if newer yung config version
//          configVersion = serialData; //take it
//          segmentCounter = 0x05; // check count
//         }
//         else{ //Current node config is most recent
//          segmentCounter = 0x00;
//         }
//      }
//      else if(segmentCounter == 0x05){ // COUNT
//        if(serialData < MAX_COMMAND){ // if not greater than max command count per packet :D
//          commandCount = byte(serialData);
//          segmentCounter = 0x06; //get port configuration
//        }
//        else{ // More than maximum commands that node can handle
//          segmentCounter = 0x00;
//        }
//      }
//      else if(segmentCounter == 0x06){ // PORT CONFIGURATION SEGMENT
//        strcpy_P(buffer, (char*)pgm_read_word(&(messages[0])));
//        Serial.println(buffer);
//        portNum = 0xF0 & serialData; // to get port number; @ upper byte
//        portNum = portNum >> 4; // move it to the right
//        setPortConfigSegment(portNum, serialData); // stored to port config segment
//        tempModeStorage = serialData & 0x07; // stores the modes sent; @ lower byte
//
//        checkPortModesSent(); // checks modes sent serving timer > event > odm
//      }
//      else if(segmentCounter == 0x07){ // TIME SEGMENT
//        strcpy_P(buffer, (char*)pgm_read_word(&(messages[1])));
//        Serial.println(buffer);
//        setTimerSegment(portNum, serialData);
//        if(partCounter==0x01) { //next part of the time is found
//          tempModeStorage = tempModeStorage ^ 0x01; // xor to turn off time base flag
//          partCounter = 0x00; //reset part counter
//          if(tempModeStorage!=0) { //may iba pang modes; hanapin natin
//            checkPortModesSent();
//          }
//          else{ //kung time based lang yung port na yun
//            checkOtherCommands(); // check if there are still other configurations
//          }
//        }
//        else{ // find next part of time
//          partCounter = partCounter + 0x01;
//        }
//      }
//      else if(segmentCounter == 0x08){ // EVENT SEGMENT
//        setEventSegment(portNum, serialData);
//        if(partCounter == 0x00){
//          checker = serialData & 0x80; // if 0x80 then, range mode else threshold mode
//          partCounter = partCounter | 0x01; // increment to get next part
//        }
//        else if(partCounter == 0x01){ //partCounter == 0x01
//          if(checker == 0x80){ 
//            strcpy_P(buffer, (char*)pgm_read_word(&(messages[12])));
//            Serial.println(buffer);
//            segmentCounter = 0x09; // next range value
//            partCounter = 0x00;
//            checker = 0x00;
//          }
//          else{
//            strcpy_P(buffer, (char*)pgm_read_word(&(messages[13])));
//            Serial.println(buffer);
//            segmentCounter = 0x0A; // threshold mode; one value only
//            partCounter = 0x00;
//          }
//        }
//      }
//      else if(segmentCounter == 0x09){ // RANGE MODE (EVENT MODE) SECOND VALUE
//        setRangeSegment(portNum, serialData);
//        if(partCounter == 0x00){
//          partCounter = partCounter | 0x01;   
//        }
//        else if(partCounter == 0x01){
//          segmentCounter = 0x0A; // to actuator details
//          partCounter = 0x00; // reset part counter
//          checker = 0x00; // coz it was not reset
//        }
//      }
//      else if(segmentCounter == 0x0A){// ACTUATOR DETAILS
//        strcpy_P(buffer, (char*)pgm_read_word(&(messages[3])));
//        Serial.println(buffer);
//        setActuatorDetailSegment(portNum, serialData); 
//        if(partCounter == 0x01){ // next part of actuator detail segment is found
//          partCounter = 0x00; //reset part counter
//          checker = 0x00;
//          tempModeStorage = tempModeStorage ^ 0x02; // xor to turn off event flag
//          if(tempModeStorage!=0) { //may iba pang mode, most likely odm
//            checkPortModesSent();
//          }
//          else{ //kung event triggered lang yung port na yun
//            checkOtherCommands(); // check if there are still other configurations
//          }          
//        }
//        else{
//          partCounter = 0x01; // increment part counter
//        }
//      }
//      else if(segmentCounter == 0x0B){ //COMMAND PARAMETER FOR API == 3
//        if (serialData == 0x00){ // Request keep alive message
//          commandValue = 0x01; // sets lsb to indicate request keep alive is there
//          strcpy_P(buffer, (char*)pgm_read_word(&(messages[14])));
//          Serial.println(buffer);
//          segmentCounter = 0xFF; // go to footer
//        }
//        if (serialData == 0xFF){ // receive configuration
//          commandValue = 0xFF; //sets 1st bit to indicate a config is coming
//          strcpy_P(buffer, (char*)pgm_read_word(&(messages[34])));
//          Serial.println(buffer);
//          segmentCounter = 0x0C; //check how many parts
//        }
//      }
//      else if(segmentCounter == 0x0C){ //part checker
//        configSentPartCtr = serialData;
//        if(serialData == 0x00){
//          segmentCounter = 0x15; //actuator detail
//        }
//        else if (serialData == 0x01) {
//          segmentCounter = 0x14;//event segment
//        }
//        else if (serialData == 0x02) {
//          segmentCounter = 0x13; //timer segment
//        }
//        else if (serialData == 0x03) {
//          segmentCounter = 0x0E; //logical to actuator value on demand
//        }
//      }
//      else if(segmentCounter == 0x0D){ // ODM - ACTUATOR SEGMENT
//        strcpy_P(buffer, (char*)pgm_read_word(&(messages[30])));
//        Serial.print(buffer);
//        Serial.println(serialData,HEX); 
//        setActuatorValueOnDemandSegment(portNum, serialData);
//
//        if(partCounter == 0x01){ // if last part
//          tempModeStorage = tempModeStorage ^ 0x04; // switch off odm flag;  assumes dapat 0 na value ni tempMode Storage
//          partCounter = partCounter ^ partCounter; // reset part counter
//          checkOtherCommands(); // check if there are still other configurations
//        }
//        else{
//          partCounter = partCounter | 0x01;
//        }
//      }
//      else if(segmentCounter == 0x0E){ //API == 3 GET CONFIG - logical address
//        logicalAddress  = serialData;
//        segmentCounter = 0x0F; 
//      }
//      else if(segmentCounter == 0x0F){ //GET CONFIG - physical
//        physicalAddress = serialData;
//        segmentCounter = 0x10;
//      }
//      else if(segmentCounter == 0x10){ //GET CONFIG VERSION
//        configVersion = serialData;
//        segmentCounter = 0x11;
//      }
//      else if(segmentCounter == 0x11){ //PORT CONFIG
//        setPortConfigSegment(checker, serialData);
//        partCounter = 0x00;
//        if(checker != PORT_COUNT -1){
//          checker = checker + 0x01;
//        }
//        else{
//          segmentCounter = 0x12; 
//          checker = 0x00;
//        }
//      }
//      else if(segmentCounter == 0x12){ // PORT CONFIG - ACUATOR VALUE ON DEMAND
////        strcpy_P(buffer, (char*)pgm_read_word(&(messages[30])));
////        Serial.println(buffer);
//        setActuatorValueOnDemandSegment(checker, serialData);
//        if(partCounter == 0x01){ // if last part ng data
//          if(checker != PORT_COUNT-1){
//            checker = checker + 0x01; // next port
//            partCounter = partCounter ^ partCounter; // reset data
//          }
//          else{
//            segmentCounter = 0xFF; // Footer
//            checker = checker ^ checker; // clear port
//            partCounter = partCounter ^ partCounter; //reset
//          }
//        }
//        else{
//          partCounter = partCounter | 0x01; // move to next
//        }
//      }
//      else if(segmentCounter == 0x13){ //PORT CONFIG - TIMER SEGMENT
////        strcpy_P(buffer, (char*)pgm_read_word(&(messages[1])));
////        Serial.println(buffer);
//        setTimerSegment(checker, serialData);
//        if(partCounter == 0x01){
//          if(checker != PORT_COUNT -1){
//            checker = checker + 0x01; // next port
//            partCounter = partCounter ^ partCounter; // reset data
//          }
//          else{
//            segmentCounter = 0xFF; // Footer
//            checker = checker ^ checker; // clear port
//            partCounter = partCounter ^ partCounter; //reset
//          }
//        }
//        else{
//          partCounter = partCounter | 0x01; // move to next
//        }
//      }
//      else if(segmentCounter == 0x14){ // PORT CONFIG - EVENT SEGMENT
//        Serial.print("Serial Data: ");
//        Serial.println(serialData,HEX);
//        setEventSegment(checker, serialData); 
//        if(partCounter == 0x01){
//          if(checker != ((PORT_COUNT*0x02) - 0x01)){
//            checker = checker + 0x01; // next port
//            partCounter = partCounter ^ partCounter; // reset data
//          }
//          else{
//            segmentCounter = 0xFF; // go to footer
//            checker = checker ^ checker; // clear port
//            partCounter = partCounter ^ partCounter; //reset
//          }
//        }
//        else{
//          partCounter = partCounter | 0x01;
//        }
//      }
//      else if(segmentCounter == 0x15){ // PORT CONFIG - ACTUATOR DETAIL
//        strcpy_P(buffer, (char*)pgm_read_word(&(messages[3])));
//        Serial.println(buffer);
//        setActuatorDetailSegment(checker, serialData);
//        if(partCounter == 0x01){
//          if(checker != PORT_COUNT -1){
//            checker++; // next port
//            partCounter = partCounter ^ partCounter; // reset data
//          }
//          else{
//            segmentCounter = 0xFF; // go to footers
//            checker = checker ^ checker; // clear port
//            partCounter = partCounter ^ partCounter; //reset
//          }
//        }
//        else{
//          partCounter = partCounter | 0x01; // move to next
//        }
//        
//      }
//      else if(segmentCounter == 0xFF && serialData == 0xFE){ // FOOTER
//        strcpy_P(buffer, (char*)pgm_read_word(&(messages[16])));
//        Serial.println(buffer);
//        segmentCounter = 0x00; //reset to check next packet
//        commandCounter = 0x00;
//        tempModeStorage = 0x00;    
//        checker = 00;
//        partCounter = 00;
//        portNum = 00; 
//        if((apiCount == 0x03 && configSentPartCtr==0x03) || apiCount != 0x03 ){ //all other apis except if api is 3, then ctr has to be 3
//          writeConfig(); // saves configuration to SD card; therefore kapag hindi complete yung packet, hindi siya saved ^^v
//        } 
////        printRegisters(); // prints all to double check
//     }
//     //    checkAllPortMode();
//  }
//    
// }
    
    //after loop; do what is said there sa port config segment
    //set ports if output or input
    //event triggered - set actuator port mode in actuator details
    if(Serial.available()>0){
    serialData = Serial.read(); // 1 byte
    
    if(serialData == 0xFF){ // serialhead found start reading
      headerFound = true;
    }
    
    if(headerFound){
      serialBuffer[pos++] = serialData;
    }
    
    if(serialData == 0xFE){
      serialBuffer[pos] = 0xFE; //adds footer
      
      // newly init
      if(serialHead == serialTail && isEmpty){
        isEmpty = false;
        for(byte x = 0x00; x < pos; x++){
          //store data to perma queue
          serialQueue[serialTail][x] = serialBuffer[x]; 
        }
        
        pos = 0;
        serialTail = serialTail + 0x01; // increment tail
        headerFound = false;
      }
      
      // serialHead == serialTail !empty //full queue
      else if(serialHead == serialTail && !isEmpty){
        Serial.println("Full Queue");
        isFull = true;
        printBuffer(serialQueue);
        isService = true;
        pos = 0;
        
      }
      
      else{//store data to perma queue
        for(byte x = 0x00; x < pos; x++){
          serialQueue[serialTail][x] = serialBuffer[x]; 
          Serial.print(serialQueue[serialTail][x],HEX);
        }
        Serial.println();
        pos = 0;
        serialTail = serialTail + 0x01; //increment tail
        headerFound = false;
        if(serialTail == QUEUE_SIZE){
          serialTail = 0x00;
        }
      }
    }
   }
   else{
    isService = true;
   }
  
  if(isService){
    Serial.println("Checking if queue is empty"); // this is a flooding message
    isService = false;
  }
}

