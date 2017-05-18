/*Uhm hi, you start counting at 0, okay, just for the gateway settings*/
/*Programmed for Gizduino IOT-644*/
#include <SoftwareSerial.h>
#include <avr/pgmspace.h>
#include <SPI.h>
#include <SD.h>
#include <avr/interrupt.h>
#include <math.h>
#include <Servo.h>
//boolean derp = true;
#define MAX_COMMAND_CODE 0x05 //note: starts from 0; 
#define MAX_COMMAND 0x03 // 12 ports * 3 modes if sensor/actuator ; placed as 2 temporarily
#define PORT_COUNT 0x0C // 12 ports
#define PACKET_QUEUE_SIZE 0x03 // Queue for packet
#define SERIAL_QUEUE_SIZE 0x05 // Queue for serial
#define BUFFER_SIZE 0x37 // bytes queue will hold 0x37
#define MAX_CONFIG_PART 0x05 // receiving config
#define MAX_ATTEMPT 0x03 // contacting sink

int CS_PIN = 27; // for SPI; 27 is used for Gizduino IOT-644
byte errorFlag = 0x00; // bits correspond to an error
byte logicalAddress = 0x00; //contains address of the network
byte physicalAddress = 0x01; //unique address of the node (frodam 0-255) (source address)
byte sinkAddress = 0x00; // specifically sink node in case in the future the nodes talk each other na
byte destAddress = 0x00; //destination address (source address is set to destination address)
byte sourceAddress = 0x00; //when receiving packet
byte configVersion = 0x00; //node config version
volatile byte attemptCounter = 0x00; //attempts count for contacting sink
volatile byte attemptIsSet = false; //tells that the attempt is just counted (else mag spam siya until mag %0 ulit)
byte configPartNumber = 0x00; // stores the config part last served
byte configSentPartCtr = 0x00; //config part being read
boolean timerReset = false; //set when there is time config

unsigned int timeoutVal = 0x2010; //5 seconds
boolean requestConfig = false; // checks if node is requesting config

byte apiCount = 0x01; //API version
byte commandCount = 0x00; //command being served (at retrieve)
byte commandCounter = 0x00; //commandCounter for commands; for receiving (@ receive)
byte packetCommandCounter = 0x00; //command counter where count will be placed @ packet
byte packetPos = 0x06; //position of packet; default at command na

unsigned int eventRegister = 0x0000; // if set, event happened; 0-15
unsigned int onDemandRegister = 0x0000; // if set, then there is an odm; 0-15
unsigned int timerRegister = 0x0000; //if set, there is timer triggered; 0 - 15
unsigned int configChangeRegister = 0x0000; // if set, port config was changed

byte portConfigSegment[(int) PORT_COUNT]; // contains port type, odm/event/time mode
unsigned int actuatorValueOnDemandSegment[(int) PORT_COUNT]; // stores data to write on actuator if ODM
unsigned int portValue[(int) PORT_COUNT]; //stores port values but in BCD
unsigned int timerSegment[(int) PORT_COUNT]; //timer segment
unsigned int eventSegment[(int) PORT_COUNT * 2]; //event segment - 24 slots (0-23(17h)) 0-B (if threshold mode) C-17 (if range mode)
unsigned int convertedEventSegment[(int) PORT_COUNT * 2]; // decimal values of event segment
unsigned int actuatorDetailSegment[(int) PORT_COUNT]; //actuator details segment (event)
unsigned int actuatorValueTimerSegment[(int) PORT_COUNT]; // stores data to write on actuator if timer
unsigned int portDataChanged; // stores if the data is changed
unsigned int eventRequest = 0x0000; //if event is requested at port
volatile unsigned int timerRequest = 0x0000; // if timer is requested at port
volatile unsigned int timerGrant = 0x0000; // if timer is granted at port

byte segmentCounter = 0x00;  // counter for parsing the parts of the packet
byte tempModeStorage = 0x00; // stores the port config 00-07
byte portNum = 0x00;//parsing var
byte partCounter = 0x00; //data counter for more than 1 byte data when receiving configs
byte commandValue = 0x00; // for api = 3, contains the command value
byte checker = 0x00; // for api = 2 checker for threshold/range mode; api = 3 counter for receive (port num i think)

boolean headerFound = false;
byte serialQueue[SERIAL_QUEUE_SIZE][BUFFER_SIZE];
byte serialBuffer[BUFFER_SIZE];
//for serial queue
byte serialHead = 0x00;
byte serialTail = 0x00;
boolean isEmpty = true;
boolean isService = false; // status to check serialQueue ; set only when no packet is received or queue is full
boolean isFull = false;

//for packet queue
//reuse headerFound
byte packetQueue[PACKET_QUEUE_SIZE][BUFFER_SIZE];
//byte packetBuffer[BUFFER_SIZE];
byte packetQueueHead = 0x00;
byte packetQueueTail = 0x00;
boolean packetQisEmpty = true;
boolean packetQisService = false;
boolean packetQisFull = false;

byte packetTypeFlag = 0x00; //determines the type of packet received

volatile unsigned long timeCtr; // counter for overflows
long portOverflowCount [(int) PORT_COUNT + 0x01]; //stores the overflow counters to be checked by interrupt; last one is for timeout

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
const char infoMessage20[] PROGMEM = "Actuator Timer Segment: ";
const char actuatorDetail0[] PROGMEM = "Actuator Segment: ";

const char* const messages[] PROGMEM = {segmentBranch0, segmentBranch1, segmentBranch2, segmentBranch3, portConfigTypeBranch0, portConfigTypeBranch1, portConfigTypeBranch2, odmDetail0, odmDetail1, odmDetail2, odmDetail3,
                                        errorMessage0, infoMessage0, infoMessage1, infoMessage2, actuatorDetail0, infoMessage3, infoMessage4, infoMessage5, infoMessage6, infoMessage7,
                                        infoMessage8, infoMessage9, infoMessage10, infoMessage11, errorMessage1, errorMessage2, infoMessage12, infoMessage13, infoMessage14, infoMessage15, infoMessage16, infoMessage17, infoMessage18, infoMessage19, infoMessage20
                                       };
char buffer[32]; //update according to longest message

/************* Other Node Modules *************/
void triggerOnDemand() {
  //  //check which port/s is set
  //  for (byte x = 0x00; x <= 0x0C; x++) {
  //    int onDemand = onDemandRegister & (0x01 << x); //shift it then AND it
  //    Serial.print("onDemandValue: ");
  //    Serial.println(onDemand);
  //    Serial.print("Checking port: ");
  //    Serial.println(x);
  //    if (onDemand == (0x01 << x)) { // if is set
  //      Serial.print("Port set: ");
  //      Serial.println(x);
  //      byte temp = portConfigSegment[x]; // get port config segment of that port
  //      temp = temp & 0x08; // check if 0x08 then actuator; if 0x00 sensor
  //      Serial.println("TEMP: ");
  //      Serial.print(temp);
  //      if (temp == 0x08) { // ODM - actuator write
  //        if (x >= 0x06) { //analog
  //          //          temp = analogWrite(port[x]);
  //
  //        }
  //        else { //digital
  //
  //        }
  //
  //      }
  //      else if (temp == 0x00) { // ODM - sensor read
  //
  //      }
  //
  //
  //    }
  //  }
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

void initializePacket(byte pQueue[]) { // one row //adds necessary stuff at init; needs one part row
  //  sinkAddress = 0x00;// testing
  pQueue[0] = 0xFF; // header
  pQueue[1] = physicalAddress; //source
  pQueue[2] = sinkAddress; //destination ; assumes always sink node
  pQueue[3] = 0x03; //api used to reply is currently 3
}

//insert count @ packet
//pQueue[4] = (packetCommandCounter - 0x01);
//if port data pinapadala
//reset count
//packetCommandCounter = 0x00; // reset counter
//packetPos = 0x05; // reset to put command

void closePacket(byte pQueue[]) {
  if (packetCommandCounter != 0x00) { //possibly sending portData
    pQueue[4] = packetCommandCounter-1; //to remove offset
  }
  
  pQueue[packetPos] = 0xFE; //footer
  packetPos = 0x06;
  packetCommandCounter = 0x00; //reset command counter
  packetQueueTail = (packetQueueTail + 0x01) % PACKET_QUEUE_SIZE; // point to next in queue
}

void insertToPacket(byte pQueue[], byte portNumber) { // for port data
  //command, count, port num, port value
  //adds count for each command
  //checks if equal to buffersize - 2 (coz may footer pa)
  pQueue[5] = 0x0F; //command
  if ((packetPos != (BUFFER_SIZE - 2)) && ((packetPos + 0x03) <= (BUFFER_SIZE - 2))) { //if not full and kapag nilagay mo dapat hindi mapupuno
//    packetPos = packetPos + 0x01;//packetPos starts at command's pos
    pQueue[packetPos] = portNumber;
    packetPos = packetPos + 0x01;
    pQueue[packetPos] = highByte(portValue[portNumber]);
    packetPos = packetPos + 0x01;
    pQueue[packetPos] = lowByte(portValue[portNumber]);
    packetCommandCounter = packetCommandCounter + 0x01; //increment count
    packetPos = packetPos + 0x01;
  }
  else { //puno na or hindi na kasya
    
    closePacket(packetQueue[packetQueueTail]);
    if (packetQueueHead != packetQueueTail) { //if queue is not full
      packetPos = 0x06;
      initializePacket(packetQueue[packetQueueTail]); //create new packet
      insertToPacket(packetQueue[packetQueueTail], portNumber);
//      packetPos = packetPos + 0x01;
    }
    else { //queue is full
      sendPacketQueue();
    }
  }
}

void formatReplyPacket(byte pQueue[], byte command) { // format reply with command param only
  pQueue[4] = command;
  packetPos = 0x05;
}

void printQueue(byte temp[][BUFFER_SIZE], byte queueSize) { // you can remove the queue_Size and buffer size; prints serialBuffer
  byte x = 0x00;
  byte halt = false;
  for (byte y = 0x00; y < queueSize; y++) {
    while (!halt) {
      if (temp[y][x] == 0xFE) {
        Serial.print(temp[y][x], HEX);
        halt = true;
      }
      else {
        Serial.print(temp[y][x], HEX);
      }

      if (x != BUFFER_SIZE)
        x = x + 0x01;
      else {
        halt = true;
      }
    }
    halt = false;
    x = 0x00;
    Serial.println();
  }
}

void printBuffer(byte temp[]) {
  byte x = 0x00;
  byte halt = false;
  while (!halt) {
    if (temp[x] == 0xFE) {
      Serial.print(temp[x], HEX);
      halt = true;
    }
    else {
      Serial.print(temp[x], HEX);
    }

    if (x != BUFFER_SIZE)
      x = x + 0x01;
    else {
      halt = true;
    }
  }
  Serial.println();
}

void loadConfig() { //loads config file and applies it to the registers
  byte fileCounter = 0x00;
  byte index = 0x00;
  byte hiByte = 0x00;
  byte loByte = 0x00;
  byte temp = 0x01; //for setting error flag

  if (!SD.begin(CS_PIN)) { //in case sd card is not init
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[25])));
    Serial.println(buffer); //error
    errorFlag |= temp;
  }
  else {
    File configFile = SD.open("con.log");
    if (configFile) { // check if there is saved config
      while (configFile.available()) {
        int fileTemp = configFile.read();
        //reads the file per byte, in the case of the int (2 bytes) it is divided into 2 parts, hiByte and loByte.
        // then it is appended together

        if (fileCounter == 0x00) { //logical address
          logicalAddress = fileTemp;
          fileCounter = 0x01;
        }
        else if (fileCounter == 0x01) { //physical
          physicalAddress = fileTemp;
          fileCounter = 0x02;
        }
        else if (fileCounter == 0x02) { //sink address
          sinkAddress = fileTemp;
          fileCounter = 0x03;
        }
        else if (fileCounter == 0x03) { //config version
          configVersion = fileTemp;
          fileCounter = 0x04;
        }
        else if (fileCounter == 0x04) { //portconfigsegment(c)
          index = fileTemp;
          fileCounter = 0x05;
        }
        else if (fileCounter == 0x05) {
          portConfigSegment[index] = fileTemp;
          if (index != (PORT_COUNT - 1)) {
            fileCounter = 0x04;
          }
          else {
            fileCounter = 0x06;
          }
        }
        else if (fileCounter == 0x06) { //actuatorValueOnDemandSegment(c)
          index = fileTemp;
          fileCounter = 0x07;
        }
        else if (fileCounter == 0x07) {
          hiByte = fileTemp;
          fileCounter = 0x08;
        }
        else if (fileCounter == 0x08) {
          loByte = fileTemp;
          actuatorValueOnDemandSegment[index] = word(hiByte, loByte);
          if (index != (PORT_COUNT - 1)) {
            fileCounter = 0x06;
          }
          else {
            fileCounter = 0x09;
          }
        }
        else if (fileCounter == 0x09) { //portValue(c)
          index = fileTemp;
          fileCounter = 0x0A;
        }
        else if (fileCounter == 0x0A) {
          hiByte = fileTemp;
          fileCounter = 0x0B;
        }
        else if (fileCounter == 0x0B) {
          loByte = fileTemp;
          portValue[index] = word(hiByte, loByte);
          if (index != (PORT_COUNT - 1)) {
            fileCounter = 0x09;
          }
          else {
            fileCounter = 0x0C;
          }
        }
        else if (fileCounter == 0x0C) { //timerSegment(c)
          index = fileTemp;
          fileCounter = 0x0D;
        }
        else if (fileCounter == 0x0D) {
          hiByte = fileTemp;
          fileCounter = 0x0E;
        }
        else if (fileCounter == 0x0E) {
          loByte = fileTemp;
          timerSegment[index] = word(hiByte, loByte);
          if (index != (PORT_COUNT - 1)) {
            fileCounter = 0x0C;
          }
          else {
            fileCounter = 0x0F;
          }
        }
        else if (fileCounter == 0x0F) { //eventSegment(c*2)
          index = fileTemp;
          fileCounter = 0x10;
        }
        else if (fileCounter == 0x10) {
          hiByte = fileTemp;
          fileCounter = 0x11;
        }
        else if (fileCounter == 0x11) {
          loByte = fileTemp;
          eventSegment[index] = word(hiByte, loByte);
          if (index != ((PORT_COUNT * 2) - 1)) {
            fileCounter = 0x0F;
          }
          else {
            fileCounter = 0x12;
          }
        }
        else if (fileCounter == 0x12) { //actuatorDetailSegment(c)
          index = fileTemp;
          fileCounter = 0x13;
        }
        else if (fileCounter == 0x13) {
          hiByte = fileTemp;
          fileCounter = 0x14;
        }
        else if (fileCounter == 0x14) {
          loByte = fileTemp;
          actuatorDetailSegment[index] = word(hiByte, loByte);
          if (index != (PORT_COUNT - 1)) {
            fileCounter = 0x12;
          }
          else {
            fileCounter = 0x15;
          }
        }
        else if (fileCounter == 0x15) { //actuatorValueTimerSegment
          index = fileTemp;
          fileCounter = 0x16;
        }
        else if (fileCounter == 0x16) {
          hiByte = fileTemp;
          fileCounter = 0x17;
        }
        else if (fileCounter == 0x17) {
          loByte = fileTemp;
          actuatorValueTimerSegment[index] = word(hiByte, loByte);
          if (index != (PORT_COUNT - 1)) {
            fileCounter = 0x15;
          }
          else {
            fileCounter = 0x18; //reset
          }
        }
      }
      //      printRegisters();
      // to inform sink node of successful loadup
      initializePacket(packetQueue[packetQueueHead]);
      formatReplyPacket(packetQueue[packetQueueHead], 0x0B);
      closePacket(packetQueue[packetQueueHead]);
      sendPacketQueue();
      //      printBuffer(packetQueue, PACKET_QUEUE_SIZE);

      configFile.close();
      initializeTimer();
    }
    else { //cannot access sd card/file not found

      //request config from sink node
      requestConfig = true;
      calculateOverflow(timeoutVal, PORT_COUNT);
      // last value of portOverflowCount array is for timeout

      initializePacket(packetQueue[packetQueueTail]);
      formatReplyPacket(packetQueue[packetQueueTail], 0x0D);
      closePacket(packetQueue[packetQueueTail]);
      //      printQueue(packetQueue, PACKET_QUEUE_SIZE);
      Serial.println("Request");
      sendPacketQueue();
      //      while(requestConfig){
      checkTimeout();
      //      }
    }
  }
}

void writeConfig() { // writes node configuration to SD card
  //  if(SD.begin(CS_PIN)){
  if (SD.exists("conf.log")) { //otherwise will overwrite
    SD.remove("conf.log");
    Serial.println("Creating new");
  }
  File configFile = SD.open("conf.log", FILE_WRITE);
  Serial.println("writing");
  if (configFile) {
    configFile.write(logicalAddress);
    configFile.write(physicalAddress);
    configFile.write(sinkAddress);
    //      configFile.println(destAddress, HEX);
    configFile.write(configVersion);
    configFile.flush(); // pre save

    for (byte c = 0x00; c < PORT_COUNT; c++) {
      configFile.write(c);
      configFile.write(portConfigSegment[c]);
    }
    configFile.flush();
    for (byte c = 0x00; c < PORT_COUNT; c++) {
      configFile.write(c);
      configFile.write(highByte(actuatorValueOnDemandSegment[c]));
      configFile.write(lowByte(actuatorValueOnDemandSegment[c]));
    }
    configFile.flush();
    for (byte c = 0x00; c < PORT_COUNT; c++) {
      configFile.write(c);
      configFile.write(highByte(portValue[c]));
      configFile.write(lowByte(portValue[c]));
    }
    configFile.flush();
    for (byte c = 0x00; c < PORT_COUNT; c++) {
      configFile.write(c);
      configFile.write(highByte(timerSegment[c]));
      configFile.write(lowByte(timerSegment[c]));
    }
    configFile.flush();
    for (byte c = 0x00; c < PORT_COUNT * 2; c++) {
      configFile.write(c);
      configFile.write(highByte(eventSegment[c]));
      configFile.write(lowByte(eventSegment[c]));
    }
    configFile.flush();
    for (byte c = 0x00; c < PORT_COUNT; c++) {
      configFile.write(c);
      configFile.write(highByte(actuatorDetailSegment[c]));
      configFile.write(lowByte(actuatorDetailSegment[c]));
    }
    configFile.flush();
    for (byte c = 0x00; c < PORT_COUNT; c++) {
      configFile.write(c);
      configFile.write(highByte(actuatorValueTimerSegment[c]));
      configFile.write(lowByte(actuatorValueTimerSegment[c]));
      //      Serial.println(lowByte(actuatorValueTimerSegment[c]),HEX);
    }
    configFile.close();
    Serial.println("Writing finish");
    //      printRegisters();
  }
  else {
    //      strcpy_P(buffer, (char*)pgm_read_word(&(messages[26])));
    //      Serial.println(buffer); //error
    byte temp = 0x08;
    errorFlag |= 0x08; //cannot access sd card or file not found
    Serial.println(errorFlag, HEX);
  }
  //  }
  //  else{
  //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[25])));
  //    Serial.print(buffer);
  //  }

}

void sendPacketQueue() {
  while (packetQueueHead != packetQueueTail) {
    printBuffer(packetQueue[packetQueueHead]);
    packetQueueHead = (packetQueueHead + 0x01) % PACKET_QUEUE_SIZE; // increment head
  }
  if (packetQueueHead == packetQueueTail) {
    packetQisEmpty = true;
  }
  if (packetQisEmpty == true && requestConfig == true && attemptCounter == 0x00 && errorFlag == 0x00) {
    //attempt counter was added since you only need to restart it every time na restart and no error
    initializeTimer();
    Serial.println("initialize timer");
  }
}

void manipulatePortData(byte index, byte configType) { //checks port type, actuates and senses accordingly 
  //configType is to know where to get actuator value (can be hardcoded but you can get it from portconfigsegment din)
  unsigned int actuatorValue = 0x0000;

  if ((portConfigSegment[index] & 0x08) == 0x08) { //actuator
    if (configType == 0x00) { // time
      actuatorValue = actuatorValueTimerSegment[index];
    }
    else if (configType == 0x01) { // event
      actuatorValue = actuatorDetailSegment[index];
    }
    else if (configType == 0x02) { //odm
      actuatorValue = actuatorValueOnDemandSegment[index];
    }

    //Serial.println(actuatorValue, HEX);

    if (index < 0x06) { // digital actuator
      if (actuatorValue == 0) {
        digitalWrite(index + 0x04, LOW); //port 0 is at pin 4
      }
      else if (actuatorValue == 1) {
        digitalWrite(index + 0x04, HIGH);
      }
      portValue[index] = digitalRead(index + 0x04);
//      Serial.print("Port VALUE");
//      Serial.println(portValue[index],HEX);
    }
    else if (index >= 0x06) { //analog actuator

    }
  }
  else { //sensor
    Serial.println("Sensor!");
    if (index < 0x06) { //Digital sensor
      portValue[index] = digitalRead(index + 0x04);
    }
    else if (index >= 0x06) { // analog sensor

    }
    timerRequest = timerRequest | (1 << index); //to be able to send again
  }
  portDataChanged |= (1 << index); //set port data changed
  Serial.print("data change: ");
  Serial.println(portDataChanged,HEX);
}

void convertEventDetailsToDecimal(byte portNum) { // from bcd to decimal
  unsigned int temp = eventSegment[portNum];
//  Serial.print("Port #: ");
//  Serial.println(portNum, HEX);
  Serial.print("Event: ");
//  Serial.println(temp, HEX);
//  printRegisters();
  if (temp & 0x8000 == 0x8000) { //range mode
//    Serial.println((temp & 0x0FFF), HEX);
    temp = (temp & 0x0FFF); // get data only
    convertedEventSegment[portNum] = bcdToDecimal(temp);
    temp = eventSegment[portNum + 0x0C]; //next part
    temp = (temp & 0x0FFF);
    convertedEventSegment[portNum + 0x0C] = bcdToDecimal(temp);
    //Serial.println("@ Ra");
    Serial.println(convertedEventSegment[portNum]);
    Serial.println(convertedEventSegment[portNum + 0x0C]);
  }
  else { //threshold
    temp = (temp & 0xF000);
    convertedEventSegment[portNum] = bcdToDecimal(temp); //converts it to decimal
    //Serial.print("@ Th: ");
    Serial.println(convertedEventSegment[portNum]);
  }

}

boolean checkEventCondition(byte eventCondition, int tempPortValue, int eventValue) {
  byte conditionReached = false;

  if (eventCondition == 0x00) { //less than not equal
    // portData < eventValue
      if (tempPortValue < eventValue) {
        conditionReached = true;
      }
      Serial.println("<");
    }
    else if (eventCondition == 0x01) { // less than equal
      //portData <= eventValue
      if (tempPortValue <= eventValue) {
        conditionReached = true;
      }
      Serial.println("<=");
    }
    else if (eventCondition == 0x02) { // greater than not equal
      // portData > eventValue
      if (tempPortValue > eventValue) {
        conditionReached = true;
      }
      Serial.println(">");
    }
    else if (eventCondition == 0x03) { // greater than equal
      // portData >= eventValue
      if (tempPortValue >= eventValue) {
        conditionReached = true;
      }
      Serial.println(">=");
    }
  
    return conditionReached;
  }

boolean checkPortConfig() {
  unsigned int actuatorValue;
  byte configCheck = 0x00; // stores which bit is being checked
  byte configType;
  boolean applyConfig = false;


  for (byte x = 0x00; x < PORT_COUNT; x++) {
    unsigned int bitMask = (1 << x);
    //    Serial.println(bitMask, HEX);
    //    Serial.println(configChangeRegister, HEX);
    //    Serial.println(configChangeRegister & bitMask, HEX);
    if ((configChangeRegister & bitMask) == bitMask) { // config was changed
      byte temp = portConfigSegment[x];
//      Serial.print("full port config");
//      Serial.println(temp, HEX);
      configType = temp & 0x07; // get all config
      
      while (configCheck != 0x03) { //checks if config is sent per pin
        byte checker = (configType & (1 << configCheck));
        if (checker == 0x01) { // time based
          Serial.println("@ time!");
          //Serial.print("timerSeg: ");
          //Serial.println(timerSegment[portNum], HEX);
          calculateOverflow(timerSegment[x], x);
//          Serial.println(portOverflowCount[x]);
          timerRequest |= (1 << x); // sets timer request
          applyConfig = true;
          //Serial.println(timerRequest, HEX);
          timerReset = true;
          configChangeRegister = configChangeRegister & ~bitMask; //turns off config changed flag
        }
        else if (checker == 0x02) { // event
          //Serial.println("@ event");
          convertEventDetailsToDecimal(x);
          eventRequest |= (1 << x); //set event request
          applyConfig = true;
          configChangeRegister = configChangeRegister & ~bitMask; //turns off config changed flag
          //Serial.println(eventRequest, HEX);
        }
        else if (checker == 0x04) { // odm
          Serial.println("@ odm");
          manipulatePortData(x, 0x02); // odm type
          applyConfig = true;


          //          commented the following block kasi hindi mo need yun
          //          Serial.print("Port Config: ");
          //          Serial.println(portConfigSegment[x], HEX);
          //          portConfigSegment[x] = portConfigSegment[x] & 0xFB; // turn off odm at port config
          //          Serial.print("Updated Port Config: ");
          //          Serial.println(portConfigSegment[x], HEX);
          portDataChanged |= (1 << x); //inform that it has been updated
          Serial.print("PortDataChange: ");
          Serial.println(portDataChanged, HEX);
          configChangeRegister = configChangeRegister & ~bitMask; //turns off config changed flag
          Serial.print("configChangeRegister: ");
          Serial.println(configChangeRegister, HEX);
        }
        configCheck = configCheck + 0x01;
      }
      configCheck = 0x00; // reset again
    }
    else { // config was not changed
//      Serial.println("skipped");
    }
  }
  Serial.print("End of port config flag: ");
  Serial.println(configChangeRegister, HEX); //dapat 0
  return applyConfig;
}
/************* Utilities *************/

void printRegisters() { // prints all variables stored in the sd card
  //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[27])));
  //    Serial.print(buffer);
  //    Serial.println(logicalAddress,HEX);
  //
  //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[28])));
  //    Serial.print(buffer);
  //    Serial.println(physicalAddress,HEX);
  //
  //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[29])));
  //    Serial.print(buffer);
  //    Serial.println(configVersion,HEX);

//  for (byte x = 0x00; x < PORT_COUNT; x++) {
//    strcpy_P(buffer, (char*)pgm_read_word(&(messages[19])));
//    Serial.print(buffer);
//    Serial.println(portConfigSegment[x], HEX);
//  }

//  for (byte x = 0x00; x < PORT_COUNT; x++) {
//    strcpy_P(buffer, (char*)pgm_read_word(&(messages[35])));
//    Serial.print(buffer);
//    Serial.println(actuatorValueTimerSegment[x], HEX);
//  }

  //  for (byte x = 0x00; x < PORT_COUNT; x++) {
  //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[30])));
  //    Serial.print(buffer);
  //    Serial.println(actuatorValueOnDemandSegment[x], HEX);
  //  }
  //
  //    for(byte x = 0x00; x <PORT_COUNT; x++){ //
  //      strcpy_P(buffer, (char*)pgm_read_word(&(messages[31])));
  //      Serial.print(buffer);
  //      Serial.println(portValue[x]);
  //    }
  //
  //  for (byte x = 0x00; x < PORT_COUNT; x++) {
  //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[22])));
  //    Serial.print(buffer);
  //    Serial.println(timerSegment[x], HEX);
  //  }
  //
    for (byte x = 0x00; x < PORT_COUNT * 2; x++) {
      strcpy_P(buffer, (char*)pgm_read_word(&(messages[24])));
      Serial.print(buffer);
      Serial.println(eventSegment[x], HEX);
    }
  //
  //  for (byte x = 0x00; x < PORT_COUNT; x++) {
  //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[15])));
  //    Serial.print(buffer);
  //    Serial.println(actuatorDetailSegment[x], HEX);
  //  }
}

void checkPortModesSent() { // checks the configs of the ports then set segmentCounter accordingly, expecting the next parameters
  if ((tempModeStorage & 0x01) == 0x01) { //if timebased is set
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[4])));
    Serial.println(buffer);
    segmentCounter = 0x07;
  }
  else if ((tempModeStorage & 0x02) == 0x02) { // event
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[5])));
    Serial.println(buffer);
    segmentCounter = 0x08;
  }
  else if ((tempModeStorage & 0x04) == 0x04) { // on demand
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[6])));
    Serial.println(buffer);
    //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[7])));
    //    Serial.print(buffer);
    //    Serial.println(portNum, HEX);

    byte temp = portConfigSegment[portNum];
    //Checking bit 3 if it is actuator or sensor
    if ((temp & 0x08) == 0x08) { //actuator
      segmentCounter = 0X0D; // get actuator segment
      strcpy_P(buffer, (char*)pgm_read_word(&(messages[33])));
      Serial.print(buffer);
    }
    else if ((temp & 0x08) == 0x00) { // sensor
      strcpy_P(buffer, (char*)pgm_read_word(&(messages[32])));
      Serial.print(buffer);
      tempModeStorage = tempModeStorage ^ 0x04; // switch off odm flag;  assumes dapat 0 na value ni tempMode Storage
      checkOtherCommands(); // check if there are still other configurations
    }
  }
  else { // invalid port config
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[11])));
    Serial.println(buffer);
    segmentCounter = 0x00;
    tempModeStorage = 0x00;
  }
}

void checkOtherCommands() { // checks if there is still commands
  if (commandCounter < commandCount) {
    commandCounter = commandCounter + 1;
    segmentCounter = 0x06; //to port config segment
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[17])));
    Serial.println(buffer);
  }
  else {
    segmentCounter = 0xFF; // go to footer
    strcpy_P(buffer, (char*)pgm_read_word(&(messages[18])));
    Serial.println(buffer);
  }
}

void retrieveSerialQueue(byte queue[], byte head) {
  byte x = 0x00;
  byte halt = false;
  segmentCounter = 0x00;
//  printBuffer(serialQueue[serialTail]);
  //  for(byte x = 0x00; x < BUFFER_SIZE; x++){
  while (!halt) {
    //      Serial.print("SGM CTR: ");
    //      Serial.println(segmentCounter, HEX);
    byte data = queue[x];
//     Serial.print("CTR: ");
//      Serial.println(segmentCounter, HEX);
//    Serial.print(data , HEX);
    
    if (data == 0xFF && segmentCounter == 0x00) {
      segmentCounter = 0x01;
    }
    else if (segmentCounter == 0x01) { //SOURCE
     
      sourceAddress = data;
      segmentCounter = 0x02;
    }
    else if (segmentCounter == 0x02) { //DESTINATION
      if (data != physicalAddress) { //in case di pala para sa kanya; kasi broadcast mode si xbee; technically drop ze packet
        segmentCounter = 0x00;
      }
      else {
        segmentCounter = 0x03; //I think.. ilagay nalang yung physical address ni node mismo
        destAddress = data;
      }
    }
    else if (segmentCounter == 0x03) { //API
      apiCount = data;
      if (apiCount == 2) { // if CONFIG mode, else iba na ieexpect niya na mga kasama na segment
        segmentCounter = 0x04; // check config version
      }
      else if (apiCount == 3) { // if Node Discovery Mode, expects  command parameter
        segmentCounter = 0x0B; //command parameter
      }
      else { //pero technically dapat error na ito
        segmentCounter = 0x05; // go straight to count ; for debug purpose lang
      }
    }
    else if (segmentCounter == 0x04) { // CONFIG VERSION IF API = 2
      //sets packet type to node config
      packetTypeFlag |= 0x02;

      if (configVersion <= data) { // if newer yung config version
        configVersion = data; //take it
        segmentCounter = 0x05; // check count
      }
      else { //Current node config is most recent
        segmentCounter = 0x00;
      }
    }
    else if (segmentCounter == 0x05) { // COUNT
      if (data < MAX_COMMAND) { // if not greater than max command count per packet :D
        commandCount = data;
        segmentCounter = 0x06; //get port configuration
      }
      else { // More than maximum commands that node can handle
        segmentCounter = 0x00;
      }
    }
    else if (segmentCounter == 0x06) { // PORT CONFIGURATION SEGMENT
      //        strcpy_P(buffer, (char*)pgm_read_word(&(messages[0])));
      //        Serial.println(buffer);
      portNum = 0xF0 & data; // to get port number; @ upper byte
      portNum = portNum >> 4; // move it to the right
      configChangeRegister |= (1 << portNum); // to inform which ports was changed
      
      setPortConfigSegment(portNum, data); // stored to port config segment
      tempModeStorage = data & 0x07; // stores the modes sent; @ lower byte

      checkPortModesSent(); // checks modes sent serving timer > event > odm
    }
    else if (segmentCounter == 0x07) { // TIME SEGMENT
      //        strcpy_P(buffer, (char*)pgm_read_word(&(messages[1])));
      //        Serial.println(buffer);
      setTimerSegment(portNum, data);
      if (partCounter == 0x01) { //next part of the time is found
        //Checking bit 3 if it is actuator or sensor
        byte temp = portConfigSegment[portNum];
        //Serial.print("Stored time: ");
        //Serial.println(timerSegment[portNum], HEX);
        if ((temp & 0x08) == 0x08) { //actuator
          segmentCounter = 0x17; // get actuator segment
          partCounter = partCounter ^ partCounter;
          strcpy_P(buffer, (char*)pgm_read_word(&(messages[33])));
          Serial.print(buffer);
        }
        else if ((temp & 0x08) == 0x00) { // sensor
          strcpy_P(buffer, (char*)pgm_read_word(&(messages[32])));
          Serial.print(buffer);
          tempModeStorage = tempModeStorage ^ 0x01; // xor to turn off time base flag
          partCounter = 0x00; //reset part counter
          if (tempModeStorage != 0) { //may iba pang modes; hanapin natin
            checkPortModesSent();
          }
          else { //kung time based lang yung port na yun
            checkOtherCommands(); // check if there are still other configurations
          }
        }

        //          tempModeStorage = tempModeStorage ^ 0x01; // xor to turn off time base flag
        //          partCounter = 0x00; //reset part counter
        //          if(tempModeStorage!=0) { //may iba pang modes; hanapin natin
        //            checkPortModesSent();
        //          }
        //          else{ //kung time based lang yung port na yun
        //            checkOtherCommands(); // check if there are still other configurations
        //          }
      }
      else { // find next part of time
        partCounter = partCounter + 0x01;
      }
    }
    else if (segmentCounter == 0x08) { // EVENT SEGMENT
      setEventSegment(portNum, data);
      if (partCounter == 0x00) {
        checker = data & 0x80; // if 0x80 then, range mode else threshold mode
        partCounter = partCounter | 0x01; // increment to get next part
      }
      else if (partCounter == 0x01) { //partCounter == 0x01
        if (checker == 0x80) {
          //            strcpy_P(buffer, (char*)pgm_read_word(&(messages[12])));
          //            Serial.println(buffer);
          segmentCounter = 0x09; // next range value
          partCounter = 0x00;
          checker = 0x00;
        }
        else {
          //            strcpy_P(buffer, (char*)pgm_read_word(&(messages[13])));
          //            Serial.println(buffer);
          segmentCounter = 0x0A; // threshold mode; one value only
          partCounter = 0x00;
        }
      }
    }
    else if (segmentCounter == 0x09) { // RANGE MODE (EVENT MODE) SECOND VALUE
      setRangeSegment(portNum, data);
      if (partCounter == 0x00) {
        partCounter = partCounter | 0x01;
      }
      else if (partCounter == 0x01) {
        segmentCounter = 0x0A; // to actuator details
        partCounter = 0x00; // reset part counter
        checker = 0x00; // coz it was not reset
      }
    }
    else if (segmentCounter == 0x0A) { // ACTUATOR DETAILS
      setActuatorDetailSegment(portNum, data);
      if (partCounter == 0x01) { // next part of actuator detail segment is found
        partCounter = 0x00; //reset part counter
        checker = 0x00;
        tempModeStorage = tempModeStorage ^ 0x02; // xor to turn off event flag
        if (tempModeStorage != 0) { //may iba pang mode, most likely odm
          checkPortModesSent();
        }
        else { //kung event triggered lang yung port na yun
          checkOtherCommands(); // check if there are still other configurations
        }
      }
      else {
        partCounter = 0x01; // increment part counter
      }
    }
    else if (segmentCounter == 0x0B) { //COMMAND PARAMETER FOR API == 3
      if (data == 0x00) { // Request keep alive message
        commandValue = 0x00;
        packetTypeFlag |= 0x04; //set packet type to node discovery
        segmentCounter = 0x16; // node discovery check
      }
      if (data == 0x02) { //Network Config
        commandValue = 0x02;
        packetTypeFlag |= 0x04; //set packet type to node discovery
        segmentCounter = 0x0E; // go to logical address
      }
      if (data == 0x0E) { // receive configuration
        packetTypeFlag |= 0x01; //set packet type to a startup config
        commandValue = 0x0E; //sets 1st bit to indicate a config is coming
        segmentCounter = 0x0C; //check how many parts
      }
    }
    else if (segmentCounter == 0x0C) { //part checker
      configSentPartCtr = data;

      if (data == 0x00) {
        segmentCounter = 0x15; //actuator detail
      }
      else if (data == 0x01) {
        segmentCounter = 0x14;//event segment
      }
      else if (data == 0x02) {
        segmentCounter = 0x13; //timer segment
      }
      else if (data == 0x03) {
        segmentCounter = 0x18; // actuator value timer
      }
      else if (data == 0x04) {
        segmentCounter = 0x0E; //logical to actuator value on demand
      }
      
    }
    else if (segmentCounter == 0x0D) { // ODM - ACTUATOR SEGMENT
      setActuatorValueOnDemandSegment(portNum, data);

      if (partCounter == 0x01) { // if last part
        tempModeStorage = tempModeStorage ^ 0x04; // switch off odm flag;  assumes dapat 0 na value ni tempMode Storage
        partCounter = partCounter ^ partCounter; // reset part counter
        checkOtherCommands(); // check if there are still other configurations
      }
      else {
        partCounter = partCounter | 0x01;
      }
    }
    else if (segmentCounter == 0x0E) { //API == 3 GET CONFIG - logical address
      logicalAddress  = data;
      if (packetTypeFlag && 0x04 == 0x04) { // if node discovery - network config, go to sink node addr
        segmentCounter = 0x16;
      }
      else { // if requesting config
        segmentCounter = 0x10;
      }
    }
    else if (segmentCounter == 0x10) { //GET CONFIG VERSION
      configVersion = data;
      segmentCounter = 0x11;
    }
    else if (segmentCounter == 0x11) { //PORT CONFIG
      setPortConfigSegment(checker, data);
      partCounter = 0x00;
      if (checker != PORT_COUNT - 1) {
        checker = checker + 0x01;
      }
      else {
        segmentCounter = 0x12;
        checker = 0x00;
        partCounter = partCounter ^ partCounter; // reset part counter
      }
    }
    else if (segmentCounter == 0x12) { // PORT CONFIG - ACUATOR VALUE ON DEMAND
      setActuatorValueOnDemandSegment(checker, data);
      if (partCounter == 0x01) { // if last part ng data
        if (checker != PORT_COUNT - 1) {
          checker = checker + 0x01; // next port
          partCounter = partCounter ^ partCounter; // reset data
        }
        else {
          segmentCounter = 0xFF; // Footer
          checker = checker ^ checker; // clear port
          partCounter = partCounter ^ partCounter; //reset
        }
      }
      else {
        partCounter = partCounter | 0x01; // move to next
      }
    }
    else if (segmentCounter == 0x13) { //PORT CONFIG - TIMER SEGMENT
      setTimerSegment(checker, data);
      if (partCounter == 0x01) {
        if (checker != PORT_COUNT - 1) {
          checker = checker + 0x01; // next port
          partCounter = partCounter ^ partCounter; // reset data
        }
        else {
          segmentCounter = 0xFF; // Footer
          checker = checker ^ checker; // clear port
          partCounter = partCounter ^ partCounter; //reset
        }
      }
      else {
        partCounter = partCounter | 0x01; // move to next
      }
    }
    else if (segmentCounter == 0x14) { // PORT CONFIG - EVENT SEGMENT
      //        Serial.print("Serial Data: ");
      //        Serial.println(data,HEX);
      setEventSegment(checker, data);
      if (partCounter == 0x01) {
        if (checker != ((PORT_COUNT * 0x02) - 0x01)) {
          checker = checker + 0x01; // next port
          partCounter = partCounter ^ partCounter; // reset data
        }
        else {
          segmentCounter = 0xFF; // go to footer
          checker = checker ^ checker; // clear port
          partCounter = partCounter ^ partCounter; //reset
        }
      }
      else {
        partCounter = partCounter | 0x01;
      }
    }
    else if (segmentCounter == 0x15) { // PORT CONFIG - ACTUATOR DETAIL
      //        strcpy_P(buffer, (char*)pgm_read_word(&(messages[3])));
      //        Serial.println(buffer);
      setActuatorDetailSegment(checker, data);
      if (partCounter == 0x01) {
        if (checker != PORT_COUNT - 1) {
          checker++; // next port
          partCounter = partCounter ^ partCounter; // reset data
        }
        else {
          segmentCounter = 0xFF; // go to footers
          checker = checker ^ checker; // clear port
          partCounter = partCounter ^ partCounter; //reset
        }
      }
      else {
        partCounter = partCounter | 0x01; // move to next
      }
    }
    else if (segmentCounter == 0x16) { // NODE DISCOVERY - NETWORK CONFIGURATION - SINK ADDR
      if (commandValue == 0x02) { //network config
        sinkAddress = data; //change sink node addr
      }
      else if (commandValue == 0x00) { //keep alive
        //retain
      }
      segmentCounter = 0xFF;
    }
    else if (segmentCounter == 0x17) { // TIMER - ACTUATOR SEGMENT
//      Serial.print("PC: ");
//      Serial.println(partCounter, HEX);
      setActuatorValueTimerSegment(portNum, data);

      if (partCounter == 0x01) { // if last part
        tempModeStorage = tempModeStorage ^ 0x01; // switch off time base flag
        partCounter = partCounter ^ partCounter; // reset part counter
        if (tempModeStorage != 0) { //may iba pang modes; hanapin natin
          checkPortModesSent();
        }
        else { //kung time based lang yung port na yun
          checkOtherCommands(); // check if there are still other configurations
        }
      }
      else {
        partCounter = partCounter | 0x01;
      }
    }
    else if (segmentCounter == 0x18) { // PORT CONFIG - ACTUATOR VALUE TIMER
      setActuatorValueTimerSegment(checker, data);
      if (partCounter == 0x01) {
        if (checker != (PORT_COUNT - 1)) {
          checker = checker + 0x01; // next port
          partCounter = partCounter ^ partCounter; // reset data
        }
        else {
          segmentCounter = 0xFF; // go to footer
          checker = checker ^ checker; // clear port
          partCounter = partCounter ^ partCounter; //reset
        }
      }
      else {
        partCounter = partCounter | 0x01;
      }
    }
    else if (segmentCounter == 0xFF && data == 0xFE) { // FOOTER
      strcpy_P(buffer, (char*)pgm_read_word(&(messages[16])));
      Serial.println(buffer);
      segmentCounter = 0x00; //reset to check next packet
      commandCounter = 0x00;
      tempModeStorage = 0x00;
      checker = 00;
      partCounter = 00;
      portNum = 00;
      //      if (apiCount != 0x03) { //all other apis except if api is 3, then ctr has to be 3
      //        writeConfig(); // saves configuration to SD card; therefore kapag hindi complete yung packet, hindi siya saved ^^v
      //      }
      //        printRegisters(); // prints all to double check
    }

    if ((x == BUFFER_SIZE) || (segmentCounter == 0xFF && data == 0xFE)){
      //max buffer or footer is found
      halt = true;
    }
    else {
      x = x + 0x01;
    }

  }
  //  }
}

/************ Setters  *************/

void setPortConfigSegmentInit(byte portNum, char portDetails) { //when initializing //CHANGED FROM INT
  byte tempPort = 0x00;
  tempPort = portNum << 4; // to move it pin position
  tempPort = tempPort | portDetails; // to set the 4th pin to output
  portConfigSegment[portNum] = tempPort;
}

void setPortConfigSegment(byte portNum, byte portDetails) {
  //  strcpy_P(buffer, (char*)pgm_read_word(&(messages[7])));
  //  Serial.print(buffer);
  //  Serial.println(portNum, HEX);
  //  strcpy_P(buffer, (char*)pgm_read_word(&(messages[19])));
  //  Serial.print(buffer);
  //  Serial.println(portDetails, HEX);
  portConfigSegment[portNum] = portDetails;
}

void setPortValue(int portNum, int val) {
  portValue[portNum] = val;
}

void setTimerSegment(byte portNum, int val) { // BYTE FROM INT; partCounter is a global var
  if (partCounter == 0x00) {
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
  else if (partCounter == 0x01) {
    int temp = val;
    timerSegment[portNum] = timerSegment[portNum] | val;
    //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[22])));
    //    Serial.print(buffer);
    //    Serial.println(timerSegment[portNum], HEX);
  }
}

void setEventSegment(byte portNum, int val) { //BYTE FROM INT
  //  strcpy_P(buffer, (char*)pgm_read_word(&(messages[7])));
  //  Serial.print(buffer);
  //  Serial.println(portNum);
  if (partCounter == 0x00) {
    eventSegment[portNum] = val << 8;
    //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[23])));
    //    Serial.print(buffer);
    //    Serial.println(eventSegment[portNum], HEX);
  }
  else if ((partCounter & 0x01) == 0x01) {
    eventSegment[portNum] = eventSegment[portNum] | val;
    //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[24])));
    //    Serial.print(buffer);
    //    Serial.println(eventSegment[portNum], HEX);
    //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[7])));
    //    Serial.print(buffer);
    //    Serial.println(portNum,HEX);
  }
}

void setRangeSegment(byte portNum, int val) { // for range type of events //BYTE FROM INT
  //  strcpy_P(buffer, (char*)pgm_read_word(&(messages[7])));
  //  Serial.print(buffer);
  //  Serial.println(portNum);
  if (partCounter == 0x00) {
    //    Serial.print("Initial Value: ");
    //    Serial.println(eventSegment[portNum+0x0C], HEX);
    eventSegment[portNum + 0x0C] = val << 8;
    //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[23])));
    //    Serial.print(buffer);
    //    Serial.println(eventSegment[portNum+0x0C], HEX);
  }
  else if ((partCounter & 0x01) == 0x01) {
    eventSegment[portNum + 0x0C] = eventSegment[portNum + 0x0C] | val;
    //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[23])));
    //    Serial.print(buffer);
    //    Serial.println(eventSegment[portNum+0x0C], HEX);
    //    Serial.print("Index in Array: ");
    //    Serial.println(portNum+0x0C,HEX);
  }
}

void setActuatorDetailSegment(byte portNum, int val) {
  if (partCounter == 0x00) { // store first part
    //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[15]))); // Actuator Segment:
    //    Serial.print(buffer);
    //    Serial.println(actuatorDetailSegment[portNum], HEX);
    actuatorDetailSegment[portNum] = val << 8 ;
    //    Serial.print("Updated Upper Value: ");
    //    Serial.println(actuatorDetailSegment[portNum],HEX);
  }
  else if (partCounter == 0x01) {
    //    Serial.println("LOWER ACTUATOR DETAIL");
    //    Serial.print("Serial Data: ");
    //    Serial.println(val, HEX);
    actuatorDetailSegment[portNum] = actuatorDetailSegment[portNum] | val;
    //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[15]))); // Actuator Segment:
    //    Serial.print(buffer);
    //    Serial.println(actuatorDetailSegment[portNum], HEX);
  }

}

void setActuatorValueOnDemandSegment(byte portNum, int val) {
  if (partCounter == 0x00) { // get port number and store first part
    //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[7])));
    //    Serial.print(buffer);
    //    Serial.println(portNum, HEX);
    actuatorValueOnDemandSegment[portNum] = val << 8 ;
    //    Serial.print("Updated Upper Value: ");
    //    Serial.println(actuatorValueOnDemandSegment[portNum],HEX);
  }
  else if (partCounter == 0x01) {
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

void setActuatorValueTimerSegment(byte portNum, int val) {
  if (partCounter == 0x00) { // get port number and store first part
    //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[7])));
    //    Serial.print(buffer);
    //    Serial.println(portNum, HEX);
    actuatorValueTimerSegment[portNum] = val << 8 ;
    //    Serial.print("Updated Upper Value: ");
    //    Serial.println(actuatorValueTimerSegment[portNum], HEX);
  }
  else if (partCounter == 0x01) {
    //    Serial.println("LOWER ACTUATOR VALUE TIMER DETAIL");
    //    Serial.print("Serial Data: ");
    //    Serial.println(val, HEX);
    actuatorValueTimerSegment[portNum] = actuatorValueTimerSegment[portNum] | val;
    //    Serial.print("Full Actuator value Segment : ");
    //    Serial.println(actuatorValueTimerSegment[portNum], HEX);
    //    strcpy_P(buffer, (char*)pgm_read_word(&(messages[7])));
    //    Serial.print(buffer);
    //    Serial.println(portNum,HEX);
  }
}

void calculateOverflow(unsigned int tempTime, byte portOverflowIndex) {
  //convert time to seconds store to realTime
  //Serial.print("Time: ");
  //Serial.println(tempTime, HEX);
  byte timeUnit = tempTime >> 12; // checks which unit
  unsigned int timeKeeper = tempTime & (0x0FFF); // get time only masking it
  //convert bcd to dec
  long timeTemp = bcdToDecimal(timeKeeper);
  //Serial.print("timeUnit: ");
  //Serial.println(timeUnit, HEX);
  //Serial.print("timeKeeper: ");
  //Serial.println(timeKeeper,HEX);
  long realTime = 0x00; //32 bits ; max is 24hrs 86400 seconds
  float timeMS = 0.000;

  //CONVERTS TO SECONDS
  if (timeUnit == 0x01 ) { //ms
    timeMS = timeTemp / 1000.0;
    //    Serial.println(timeMS,3); //0.99
  }
  else if (timeUnit == 0x02) { //sec
    //it is as is
  }
  else if (timeUnit == 0x04) { //min
    timeTemp = timeTemp * 60;
  }
  else if (timeUnit == 0x08) { //hour
    timeTemp = timeTemp * 3600;
  }

  realTime = timeTemp;
  //  Serial.println(realTime);

  //GETTING THE OVERFLOW VALUE IF TIME >=17ms ELSE overflowValue = tickValue
  //9-16ms = 0 overflow - need realTime = totalTicks;
  //well, up to 100ms lang siya sooo I'll just leave it here
  int frequency = 15625; // clock frequency / prescaler
  long totalTicks;
  if (timeUnit == 0x01) { //if ms; because it is float
    totalTicks = timeMS * frequency;
  }
  else {
    totalTicks = realTime * frequency;
  }
  long overflowCount;
  if (timeTemp <= 16 && timeUnit == 0x01) { //if less than 16ms kasi hindi mag overflow
    overflowCount  = totalTicks;
  }
  else {
    overflowCount = totalTicks / pow(2, 8); //8 coz timer 2 has 8 bits
  }
  portOverflowCount[portOverflowIndex] = overflowCount;

  //  Serial.print("OFC: ");
  //  Serial.println(portOverflowCount[portOverflowIndex]);


  //check overflowCount if reached @ INTERRUPT

}

unsigned int bcdToDecimal(unsigned int nTime) { //returns unsigned int to save space
  unsigned int temp = 0;
  //999
  temp = ((nTime >> 8) % 16);
  temp *= 10;
  temp += ((nTime >> 4) % 16);
  temp *= 10;
  temp += (nTime % 16);
  return temp;
}

void checkTimeout() {
  if (requestConfig == true  && attemptIsSet) { //trying to request config and it has not yet come
    if (attemptCounter <= MAX_ATTEMPT) { // requests again
      initializePacket(packetQueue[packetQueueTail]);
      formatReplyPacket(packetQueue[packetQueueTail], 0x0D); // request config
      closePacket(packetQueue[packetQueueTail]);
      //      printQueue(packetQueue, PACKET_QUEUE_SIZE);
      attemptIsSet = false;
      //      Serial.println("Again!!");
    }
    else { // max reached
      attemptCounter = 0x00; // reset
      requestConfig = false;
      errorFlag |= 0x02;
      initializePacket(packetQueue[packetQueueTail]);
      formatReplyPacket(packetQueue[packetQueueTail], 0x09); //max attempts is reached
      closePacket(packetQueue[packetQueueTail]);
      Serial.println("Max reached");
    }
    sendPacketQueue();
  }
}

ISR(TIMER2_OVF_vect) {
  timeCtr++;
  //  Serial.println(timeCtr);
  if ((timeCtr % portOverflowCount[PORT_COUNT]) == 0 && (requestConfig == true) && (portOverflowCount[PORT_COUNT] != 0)) { // when requesting at startup
    //if it reached timeout and requestConfig is set and it is not zero
    attemptCounter = attemptCounter + 0x01; // increment counter
    attemptIsSet = true;
  }
  if (((timeCtr % portOverflowCount[0]) == 0) && ((timerRequest & 0x0001) == 0x0001)) { //if it is time and there is a request
    timerGrant |= (1 << 0); // turn on grant
    //    timerGrant |= 0x01;
//    Serial.println(portOverflowCount[0],HEX);
//    Serial.println(timeCtr);
//    Serial.print("G: ");
//    Serial.println(timerGrant, HEX);
    timerRequest = timerRequest & ~(1 << 0); // turn off request flag
//    Serial.println(timerRequest, HEX);
  }
  if (((timeCtr % portOverflowCount[1]) == 0) && ((timerRequest & 0x0002) == 0x0002)) {
    timerGrant |= (1 << 1);
    timerRequest = timerRequest & ~(1 << 1);
  }
  if (((timeCtr % portOverflowCount[2]) == 0) && ((timerRequest & 0x0004) == 0x0004)) {
    timerGrant |= (1 << 2);
    timerRequest = timerRequest & ~(1 << 2);
  }
  if (((timeCtr % portOverflowCount[3]) == 0) && ((timerRequest & 0x0008) == 0x0008)) {
    timerGrant |= (1 << 3);
    timerRequest = timerRequest & ~(1 << 3);
  }
  if (((timeCtr % portOverflowCount[4]) == 0) && ((timerRequest & 0x0010) == 0x0010)) {
    timerGrant |= (1 << 4);
    timerRequest = timerRequest & ~(1 << 4);
  }
  if (((timeCtr % portOverflowCount[5]) == 0) && ((timerRequest & 0x0020) == 0x0020)) {
    timerGrant |= (1 << 5);
    timerRequest = timerRequest & ~(1 << 5);
  }
  if (((timeCtr % portOverflowCount[6]) == 0) && ((timerRequest & 0x0040) == 0x0040)) {
    timerGrant |= (1 << 6);
    timerRequest = timerRequest & ~(1 << 6);
  }
  if (((timeCtr % portOverflowCount[7]) == 0) && ((timerRequest & 0x0080) == 0x0080)) {
    timerGrant |= (1 << 7);
    timerRequest = timerRequest & ~(1 << 7);
  }
  if (((timeCtr % portOverflowCount[8]) == 0) && ((timerRequest & 0x0100) == 0x0100)) {
    timerGrant |= (1 << 8);
    timerRequest = timerRequest & ~(1 << 8);
  }
  if (((timeCtr % portOverflowCount[9]) == 0) && ((timerRequest & 0x0200) == 0x0200)) {
    timerGrant |= (1 << 9);
    timerRequest = timerRequest & ~(1 << 9);
  }
  if (((timeCtr % portOverflowCount[0x0A]) == 0) && ((timerRequest & 0x0400) == 0x0400)) {
    timerGrant |= (1 << 0x0A);
    timerRequest = timerRequest & ~(1 << 0x0A);
  }
  if (((timeCtr % portOverflowCount[0x0B]) == 0) && ((timerRequest & 0x0800) == 0x0800)) {
    timerGrant |= (1 << 0x0B);
    timerRequest = timerRequest & ~(1 << 0x0B);
  }
}

void initializeTimer() {
  //  Serial.println("S Timer");
  //setting it up to normal mode
  // one OVF = 16ms ( 256 / (16MHZ/1024))
  cli(); //disable global interrupts

  TCCR2A = 0 ;
  TCCR2B = 0 ;
  TCCR2B |= (1 << CS22) | (1 << CS21) | (1 << CS20); // set prescaler to 1024
  TIMSK2 |= (1 << TOIE2); //enable interrupt to overflow
  sei(); //enable global interrupts
}

void setup() {
  //initialize pins
  pinMode(4, OUTPUT);
  pinMode(5, OUTPUT);
  pinMode(6, OUTPUT);
  pinMode(7, OUTPUT);
  pinMode(8, OUTPUT);
  pinMode(9, OUTPUT);

  //setting to low
  digitalWrite(4, LOW);
  digitalWrite(5, LOW);
  digitalWrite(6, LOW);
  digitalWrite(7, LOW);
  digitalWrite(8, LOW);
  digitalWrite(9, LOW);

  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);

  // to initiate serial communication
  Serial.begin(9600);
  timeCtr = 0;

  // NOTE: AVOID PUTTING STUFF ON PIN 0 & 1 coz that is where serial is (programming, debugging)
  for (byte c = 0x00; c < 0x0C; c++) {
    setPortConfigSegmentInit(c, 1); // set the port to OUTPUT
    //    Serial.print("Port Config Segment at Start: ");
    //    Serial.println(getPortConfigSegment(c), HEX);
  }

  //sets values of the arrays to 0, needed else it would not print anything
  memset(actuatorValueOnDemandSegment, 0, sizeof(actuatorValueOnDemandSegment));
  memset(actuatorValueTimerSegment, 0, sizeof(actuatorValueTimerSegment));
  memset(portValue, 0, sizeof(portValue));
  memset(timerSegment, 0, sizeof(timerSegment));
  memset(eventSegment, 0, sizeof(eventSegment));
  memset(actuatorDetailSegment, 0, sizeof(actuatorDetailSegment));
  memset(portOverflowCount, 0, sizeof(portOverflowCount));
  memset(serialBuffer, 0x00, sizeof(serialBuffer));
  memset(serialQueue, 0x00, sizeof(serialQueue));
  memset(convertedEventSegment, 0x00, sizeof(convertedEventSegment));

  //    if(SD.begin(CS_PIN)){ // uncomment entire block to reset node (to all 0)
  //      writeConfig(); //meron itong sd.begin kasi nagrurun ito ideally after config... therefore na sd.begin na ni loadConfig na ito sooo if gusto mo siya irun agad, place sd.begin
  //    }
  //    else{
  //      byte temp = 0x01;
  //      errorFlag |= temp; // cannot access sd card
  //      Serial.println(errorFlag, HEX);
  //    }

  loadConfig(); //basically during the node's lifetime, lagi ito una, so if mag fail ito, may problem sa sd card (either wala or sira) therefore contact sink
//  printRegisters();
}

void loop() {
  boolean wait = 0;
  byte serialData;  //temporary store of data read from Serial
  static byte index = 0; //for queue
  static size_t pos = 0; //for buffer

  //Communication module - Receive
  if (Serial.available() > 0) {
    serialData = Serial.read(); // 1 byte
//    Serial.print(serialData, HEX);

    if (serialData == 0xFF) { // serialhead found start reading
      headerFound = true;
    }

    if (headerFound) {
      serialBuffer[pos++] = serialData;
    }

    if (serialData == 0xFE) {
      serialBuffer[pos] = 0xFE; //adds footer
      //      Serial.print("T: ");
      //      Serial.println(serialTail, HEX);

      if (serialHead != ((serialTail + 0x01) % SERIAL_QUEUE_SIZE)) { // tail is producer
        isEmpty = false;

        for (byte x = 0x00; x < pos; x++) {
          //store data to perma queue
          serialQueue[serialTail][x] = serialBuffer[x];
          //          Serial.print(serialQueue[serialTail][x],HEX);
        }
        //        Serial.println();
        //        printBuffer(serialQueue);

        pos = 0;
        serialTail = (serialTail + 0x01) % SERIAL_QUEUE_SIZE; // increment tail
        headerFound = false;
//        Serial.println("Read");
//        printQueue(serialQueue, SERIAL_QUEUE_SIZE);
//        Serial.print("H: ");
//        Serial.println(serialHead, HEX);
//        Serial.print("T: ");
//        Serial.println(serialTail, HEX);
      }
      else {
        Serial.println("Full Queue");
        isFull = true;
        printQueue(serialQueue, SERIAL_QUEUE_SIZE);
        isService = true;
        pos = 0;
      }
    }
  }
  else {
    isService = true;
    if (requestConfig == true)
      checkTimeout(); //if nothing is received
  }

  if (isService) { // check serial queue
    isService = false;

    if (!isEmpty) { // there are messages
      retrieveSerialQueue(serialQueue[serialHead], serialHead); //get message, store to variables, setting flags
      serialHead = (serialHead + 0x01) % SERIAL_QUEUE_SIZE; // increment head
      if (serialHead == serialTail) { //check if empty
        isEmpty = true;
      }
      //insert checking muna here
      //this is processing na like waiting for the next part number, formatting replies
      if (requestConfig == true) { //if it is waiting for config
        //if the packet is a config packet
        if ((packetTypeFlag & 0x01) == 0x01) { // request startup config
          if (configSentPartCtr == configPartNumber) {
            attemptCounter = 0x00; //reset it coz may dumating na tama
            packetTypeFlag = packetTypeFlag & 0xFE;
            configPartNumber = configPartNumber + 0x01; //expect next packet
            if (configPartNumber-1 == MAX_CONFIG_PART-1) { // if it is max already
              writeConfig(); //save config
              requestConfig = false;  // turn off request
              attemptCounter = 0xFF; //para hindi magreset yung timer
              configPartNumber = 0x00;
              initializePacket(packetQueue[packetQueueTail]);
              formatReplyPacket(packetQueue[packetQueueTail], 0x0C); //acknowledge of full config
              closePacket(packetQueue[packetQueueTail]);
            }
          }
          else {
              Serial.print("SerialHead: ");
              Serial.println(serialHead,HEX);
              Serial.print("SerialTail: ");
              Serial.println(serialTail,HEX);
              Serial.println("broken config");
              initializePacket(packetQueue[packetQueueTail]);
              formatReplyPacket(packetQueue[packetQueueTail], 0x08); //sent configuration is broken
              closePacket(packetQueue[packetQueueTail]);
              errorFlag |= 0x04;
              requestConfig = false;
          }
        }
        else { // unexpected packet (non config type) drop itttt
        }
      }
      else if ((packetTypeFlag & 0x02) == 0x02) { // node configuration
        boolean applyConfig = checkPortConfig();
        if (applyConfig) { // if successfully applied
          initializePacket(packetQueue[packetQueueTail]);
          formatReplyPacket(packetQueue[packetQueueTail], 0x06);
          closePacket(packetQueue[packetQueueTail]);
          writeConfig();
          packetTypeFlag = packetTypeFlag & 0xFD; // turn off node config flag

          if (timerReset) { // if needs to be reset
            Serial.println("timerReset");
            initializeTimer();
            timerReset = false;
          }
          else{
            Serial.println("!timerReset");
          }
        } else {
          Serial.println("!config");
        }
      }
      else if ((packetTypeFlag & 0x04) == 0x04) { // node discovery
        if (commandValue == 0x00) { // Request Keep Alive
          initializePacket(packetQueue[packetQueueTail]);
          formatReplyPacket(packetQueue[packetQueueTail], 0x01);
          closePacket(packetQueue[packetQueueTail]);
        }
        else if (commandValue == 0x02) {// Network Config
          initializePacket(packetQueue[packetQueueTail]);
          formatReplyPacket(packetQueue[packetQueueTail], 0x03);
          closePacket(packetQueue[packetQueueTail]);
        }
        packetTypeFlag = packetTypeFlag & 0xFB; // turn off packet type flag for node discov
      }
      sendPacketQueue();
    }
    else { //main loop if no message (checking flags)
            
      if(timerGrant != 0x00){ //check timer grant
//        Serial.println("Timer Grant");
//        Serial.print("@main");
//        Serial.println(timerGrant,HEX);
        unsigned int timerGrantMask = 0x00;

        for (byte x = 0x00; x < PORT_COUNT; x++){
          timerGrantMask = (1 << x); 

          if((timerGrantMask & timerGrant) == timerGrantMask){ // if set
            manipulatePortData(x,0x00); //timer
            timerGrant = timerGrant & ~(1<<x); // clear timer grant of bit
//            Serial.println(timerGrant, HEX);
          }
        }
//        Serial.print("End loop timerGrant: ");
//        Serial.println(timerGrant, HEX);// kahit hindi zero kasi interrupt ito

      }
      if (eventRequest != 0x00) { // check event request

        unsigned int eventRequestMask = 0x0000;
        int eventValue;
        byte eventCondition;
        boolean conditionReached = false;
        int tempPortValue;

        Serial.println("Event Request");

        for (byte x = 00; x < PORT_COUNT; x++) {
          eventRequestMask = (eventRequestMask << x);

          //check if port is event based
          if ((eventRequestMask & eventRequest) == eventRequestMask) {

            //read port value
            if (x < 0x06) { //digital
              tempPortValue = digitalRead(x + 0x04);
              Serial.print("Read port val: ");
              Serial.println(tempPortValue);
            }
            else if (x >= 0x06) { //analog
              tempPortValue = analogRead((x % 6)); //write at analog pin
              Serial.print("Read port val: ");
              Serial.println(tempPortValue);
            }

            //check condition
            eventValue = convertedEventSegment[x];
            eventCondition = ((eventSegment[x] & 0x3000) >> 12); //retain the condition
            Serial.print("Event Condition:");
            Serial.println(eventCondition, HEX);

            conditionReached = checkEventCondition(eventCondition, tempPortValue, eventValue);

            eventValue = eventSegment[x]; //getting event values

            if ((eventValue & 0x80) == 0x80) { //check if range mode
              Serial.println("Range!");
              eventCondition = ((eventSegment[x + 0x0C] & 0x3000) >> 12); //retain the condition
              eventValue = convertedEventSegment[x + 0x0C]; //get second value

              conditionReached &= checkEventCondition(eventCondition, tempPortValue, eventValue); //check again
              Serial.print("Condition Result:");
              Serial.println(conditionReached);
            }

            if (conditionReached) {
              Serial.println("condition was true");
              portValue[x] = tempPortValue; //save port value
              portDataChanged |= eventRequestMask; // to tell that the port data has changed

              unsigned int actuatorValue = actuatorDetailSegment[x] & 0x0FFF;
              Serial.print("actuator Value: ");
              Serial.println(actuatorValue);
              byte actuatorPort = ((actuatorDetailSegment[x] & 0xF000) >> 12);  //get actuator port
              Serial.print("actuator Port: ");
              Serial.println(actuatorPort, HEX);
              manipulatePortData(actuatorPort, 0x02); // write data to config port and store its port value
              eventRequest |= ~(1 << x); //turn off event request of sensor bit
              Serial.print("Event Request: ");
              Serial.println(eventRequest, HEX);
              portDataChanged |= (1 << actuatorPort); // tells port data of actuator port has changed
            }
          }
        }
        Serial.print("End loop eventRequest: ");
        Serial.println(eventRequest, HEX);// dapat zero
      }
      if (portDataChanged != 0x00) { //to form packet
        unsigned int portDataChangedMask;
        initializePacket(packetQueue[packetQueueTail]);
        
        for (byte x = 0x00; x < PORT_COUNT; x++) { //find which port was changed
          portDataChangedMask = (1 << x);

          if ((portDataChanged & portDataChangedMask) == portDataChangedMask) { //portData was changed
            insertToPacket(packetQueue[packetQueueTail], x);
            portDataChanged = portDataChanged & ~portDataChangedMask; //turn off port data changed of bit
            Serial.print("data change after send: ");
            Serial.println(portDataChanged,HEX);
          }
        }
        closePacket(packetQueue[packetQueueTail]);
        sendPacketQueue();
      }
      

//      //test if sending data is exceeding buffer size 
//      while(derp){
//      initializePacket(packetQueue[packetQueueTail]);
//      insertToPacket(packetQueue[packetQueueTail], 0x00);
//      insertToPacket(packetQueue[packetQueueTail], 0x01);
//      insertToPacket(packetQueue[packetQueueTail], 0x02);
//      insertToPacket(packetQueue[packetQueueTail], 0x03);
//      insertToPacket(packetQueue[packetQueueTail], 0x04);
//      insertToPacket(packetQueue[packetQueueTail], 0x05);
//      insertToPacket(packetQueue[packetQueueTail], 0x06);
//      insertToPacket(packetQueue[packetQueueTail], 0x07);
//      insertToPacket(packetQueue[packetQueueTail], 0x08);
//      insertToPacket(packetQueue[packetQueueTail], 0x09);
//      insertToPacket(packetQueue[packetQueueTail], 0x0A);
//      insertToPacket(packetQueue[packetQueueTail], 0x0B);
//      insertToPacket(packetQueue[packetQueueTail], 0x0C);
//      insertToPacket(packetQueue[packetQueueTail], 0x0D);
//      insertToPacket(packetQueue[packetQueueTail], 0x0E);
//      insertToPacket(packetQueue[packetQueueTail], 0x0F);
//      insertToPacket(packetQueue[packetQueueTail], 0x10);
//      insertToPacket(packetQueue[packetQueueTail], 0x11);
//      insertToPacket(packetQueue[packetQueueTail], 0x12);
//      insertToPacket(packetQueue[packetQueueTail], 0x13);
//      insertToPacket(packetQueue[packetQueueTail], 0x14);
//      insertToPacket(packetQueue[packetQueueTail], 0x15);
//      insertToPacket(packetQueue[packetQueueTail], 0x16);
//      insertToPacket(packetQueue[packetQueueTail], 0x17);
//      insertToPacket(packetQueue[packetQueueTail], 0x18);
//      insertToPacket(packetQueue[packetQueueTail], 0x19);
//      insertToPacket(packetQueue[packetQueueTail], 0x1A);
//      insertToPacket(packetQueue[packetQueueTail], 0x1B);
//      insertToPacket(packetQueue[packetQueueTail], 0x1C);
//      insertToPacket(packetQueue[packetQueueTail], 0x1D);
//      insertToPacket(packetQueue[packetQueueTail], 0x1E);
//      insertToPacket(packetQueue[packetQueueTail], 0x1F);
//      closePacket(packetQueue[packetQueueTail]);
//      sendPacketQueue();
//      derp = false;
//      }
    }
  }
}
