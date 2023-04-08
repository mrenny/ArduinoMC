/*Megan's microcredential project*/

//Import Libraries
  #include <Arduino_HS300x.h>//onboard  temp/humidity sensor
  #include <Arduino_LPS22HB.h>//onboard barometer
  #include <Adafruit_AS7341.h>//10 channel spectrometer
  #include "RTClib.h"//clock
  #include <SPI.h>//Protocol for SPI
  #include <SD.h>//Protocol for file system
  #include <Wire.h>//Protocol for I2C
  #include <ArduinoBLE.h>//Protocol for BLE
//BLE Definitions
  BLEService myService("0000ffe0-0000-1000-8000-00805f9b34fb"); // service characteristic
  BLECharacteristic receive("EFE5", BLEIndicate | BLEWriteWithoutResponse ,0x20);
  BLECharacteristic transfer("FFE1", BLEWrite | BLENotify,0x10); 
  char bleMode; // will be t or r
  int wake;
//Instrument Definitions/Pin Assignments
  Adafruit_AS7341 as7341;
  RTC_PCF8563 rtc;
  int sd = 2;//SD card CS pin
  int pumpPin = 27;//pin pump vcc is connected to
  int heatPin = 21;//pin heating pad is connected to
  int shade0 = 23;//pin for motor enable [can be 3V] as long as Vs and Vss are 5V https://www.arduino.cc/documents/datasheets/H-bridge_motor_driver.PDF
  int shade1 = 14;//pin for motor direction 1
  int shade2 = 13;//pin for motor direction 2
  int moist = 4;//pin for ADC reading of soil sensor
  int reset = 11; //pin attached to reset pin
  int calSoil = 15;//pin for callibrating soil moisture sensor
  int klock = 12;//pin to check
  int R = 22;//onboard RGB LED
  int G = 23;//onboard RGB LED
  int B = 24;//onboard RGB LED
  File myFile;
//Global Variables
  int skip;//number of measurements to skip
  int dt;//measurement interval in ms
  float t;//temperature value
  float rh;//relative humidity value
  float p;//pressure value
  float VPD;//VPD value
  int soil;//soil moisture ADC reading
  float soilP;//soil moisture in percentage
  float setT;//temperature setpoint
  float setW;//soil set point
  int soilDry;//soil dry end callibration value
  int soilWet;//soil wet end callibration value
  int instErr[]={0, 0, 0, 0};//error on T&RH, P, PAR, and SD 
void writeBLE(String message){//Definition for writing a message over BLE, described above any other funcitons that might use it
  byte plain[message.length()]; // message buffer
  message.getBytes(plain, message.length()); // convert to bytes
  receive.writeValue(plain,message.length());
}
void setup() {//Allows for instrument callibrations if certain pins are shorted, decides if BLE sould be in TX or RX, and starts all peripherals including BLE
  //Set Pin Modes
    pinMode(R, OUTPUT);
    pinMode(G, OUTPUT);
    pinMode(B, OUTPUT);//Set RBG Pins
    pinMode(pumpPin, OUTPUT);
    pinMode(heatPin,OUTPUT);
    pinMode(shade0, OUTPUT);
    pinMode(shade1, OUTPUT);
    pinMode(shade2, OUTPUT);
    pinMode(moist,INPUT);
    pinMode(reset,OUTPUT);
    pinMode(klock,INPUT);
    pinMode(calSoil,INPUT);
    digitalWrite(pumpPin,LOW);
    digitalWrite(shade0,LOW);
    digitalWrite(reset,HIGH);//Active High [not being reset]
  //Start and Set Clock time
    //If we're programming the board for the first time, its connected to Serial and will get the current date/time from the computer uploading the firmware
    if (digitalRead(klock)){
      Serial.begin(115200);
      if (!rtc.begin()){
      Serial.println("Error connecting to the RTC. Makesure clock pin is shorted high");
      } else {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      }
    } else {rtc.begin();}
    //Note that because the clock will be set before packaging, there is no error handling for this. It either works or it doesn't
  //Initialize SD Card to decide if device /should/ be receiving [default] or sending data
    if (!SD.begin(sd)){//If SD doesn't begin, raise error flag, turn on LED, keep going in transmit mode so we can see what the error is from a phone
      digitalWrite(R, LOW);//active low
      bleMode = 't';//default to sending information
      instErr[3]=1;//raise error flag
    } else{//Otherwise the SD card began and we can at least /try/ to see what mode we wanted to be in
    myFile = SD.open("BLE.txt");//File on pre-formattted/written SD card to store t or r;
      if (myFile.available()){
        bleMode = myFile.peek();//there should be only be t or r. Peek allows me to check multiple times at the same location in the event a line break or extra character is present
        myFile.close();
      }
    }
  //Callibrate the Soil Moisture sensor
    if (digitalRead(calSoil)){
      Serial.begin(115200);
      Serial.println("Change Serial to 'No Line Ending' then Put Sensor in Dry State");
      delay(10000);
      Serial.println("When Satisfied Send Any Message Over Serial");
      while(Serial.available()){
        soilDry = analogRead(moist);
        Serial.println(String(soil));
        delay(500);
      }
      myFile = SD.open("soilDry.txt",O_WRITE);//File on pre-formattted/written SD card to store t or r;
      myFile.print(String(soilDry));
      myFile.close();
      Serial.println("Now Submerge Sensor in Wet State");
      delay(10000);
      Serial.println("When Satisfied Send Any Message Over Serial");
      while(Serial.available()){
        soilWet = analogRead(moist);
        Serial.println(String(soil));
        delay(500);
      }
      myFile = SD.open("soilWet.txt",O_WRITE);//File on pre-formattted/written SD card to store t or r;
      myFile.print(String(soilWet));
      myFile.close();
    }
  //Initialize instruments; if one doesn't work, Red LED will turn on and instErr value will turn from 0 to 1; Also populate the desired setPoints from files
    //Initialize the temp/humidity sensor. It should just work because this is inside the uC
      if (!HS300x.begin()) {
        digitalWrite(R,LOW);//active low
        bleMode='t';
        instErr[0]=1;
      }
      myFile = SD.open("tsp.txt");//File on pre-formattted/written SD card to store t or r;
      String tempSP="";
      while(myFile.available()){
        tempSP = tempSP + (char)myFile.read();
      }
      setT = tempSP.toFloat();
    //Populate Soil Moisture Set Point
      myFile = SD.open("wsp.txt");//File on pre-formattted/written SD card to store t or r;
      String soilSP="";
      while(myFile.available()){
        soilSP = soilSP + (char)myFile.read();
      }
      setW = soilSP.toFloat();
    //Initialize the barometer
      if (!BARO.begin()) {
        digitalWrite(R,LOW);//active low
        bleMode='t';
        instErr[1]=1;}
    //Initialize the PAR meter
      if (!as7341.begin()){
        digitalWrite(R,LOW);//active low
        bleMode='t';
        instErr[2]=1;
      }
      as7341.setATIME(100);
      as7341.setASTEP(999);
      as7341.setGain(AS7341_GAIN_256X);

    //Initialize DataAquisition Variables
      getDtSkip();
  //Initalize BLE based on requested mode/Service
    //Start up BLE and flash blue LED
      while (!BLE.begin()) { //Blink blue LED while waiting for BLE to start
        digitalWrite(B, LOW);
        delay(500);
        digitalWrite(B, HIGH);
        delay(500);
      }
    //Add approprite characteristic to service
      if (bleMode == 't'){
        myService.addCharacteristic(transfer); 
      } else {
        myService.addCharacteristic(receive);
      }
      BLE.addService(myService); // add service to BLE dev
      BLEAdvertisingData scanData; // Build scan response data packet
      if (bleMode =='r'){
        scanData.setLocalName("PlantSitterRemote"); // set BLE name
        BLE.setDeviceName("PlantSitterRemote"); // set BLE name
      } else {
        scanData.setLocalName("PlantSitterData"); // set BLE name
        BLE.setDeviceName("PlantSitterData"); // set BLE name
      }
      BLE.setScanResponseData(scanData); // set name for scanners
    //Start handler if needed
      if (bleMode == 'r'){//this isn't needed for transmitting data because we don't need an event handler when sending things
        receive.setEventHandler(BLEWritten, WriteCharacteristicHandler); // add written handler
      }
    //Advertise BLE
      wake = 0;//we only just started so the user is likely only just signing on!
      BLE.advertise(); // advertise BLE from MakerBLE board
  //If in transmission mode, send data to phone and restart
    if (bleMode == 't'){
      int stay = 0;
      int timeOUT = dt*10;
    //Declare self a central device
      BLEDevice central = BLE.central();
      if (central) {//if a connection is made
    //check if there are any errors we need to make alerts about
        int errCheck=0;
        for (int i=1; i <=4; i++){
          errCheck=errCheck + instErr[i];
        }
    //If errors, ONLY ADVERTISE which things are in error. Wait for physical restart
        if (errCheck>0){
          while (central.connected()){
            for (int i=1; i <=4; i++){
              switch(i){
                case 1:
                  if (instErr[i] == 1){
                  writeBLE("Temperature and Humidity Sensor Error");}
                  break;
                case 2:
                  if (instErr[i] == 1){
                  writeBLE("Pressure Sensor Error");}
                  break;
                case 3:
                if (instErr[i] == 1){
                writeBLE("Light Sensor Error");}
                  break;
                case 4:
                  if (instErr[i] == 1){
                  writeBLE("SD Card Error");}
                  break;
              }
              delay(500);
            }
          }
    //If no errors, send/save data normally.
        } else {
          while (central.connected() && stay <10) {
    //Get data, save, display, and try to fix values far from set points since we are not able to control them in TX mode[Could honestly be in a function or something idk...]
            String strToPrint = ""; // string to print 
            //Save the time
              digitalWrite(G,LOW);
              myFile = SD.open("DATA.txt",FILE_WRITE);
              DateTime now = rtc.now();
              myFile.print(now.year(), DEC);
              myFile.print(',');
              strToPrint+= String(now.year());strToPrint+=",";
              myFile.print(now.month(), DEC);
              myFile.print(',');
              strToPrint+= String(now.month());strToPrint+=",";
              myFile.print(now.day(), DEC);
              myFile.print(',');
              strToPrint+= String(now.day());strToPrint+=",";
              myFile.print(now.hour(), DEC);
              myFile.print(',');
              strToPrint+= String(now.hour());strToPrint+=",";
              myFile.print(now.minute(), DEC);
              myFile.print(',');
              strToPrint+= String(now.minute());strToPrint+=",";
              myFile.print(now.second(), DEC);
              myFile.print(',');
              strToPrint+= String(now.second());strToPrint+=",";
            //Save Temperature
              float t = HS300x.readTemperature();//get temp in C
              myFile.print(String(t,2));
              myFile.print(',');
              strToPrint+= String(t,2);strToPrint+=",";
              if (t<=setT*0.90){
                heatOn();
              }
            //Save Soil Moisture
              soil = analogRead(moist);
              soilP = soil/(soilWet-soilDry);
              myFile.print(String(soilP,2));
              if (soilP<=setW*0.50){
                waterNow();
              }
            //Save RH
              float rh = HS300x.readHumidity();//get RH as %
              myFile.print(String(rh,2));
              myFile.print(',');
              strToPrint+= String(rh,2);strToPrint+=",";
            //Save Pressure
              float p = BARO.readPressure();//get pressure in kPa
              myFile.print(String(p/1000,2));
              myFile.print(',');
              strToPrint+= String(p,2);strToPrint+=",";
            //Save VPD
              float e = 17.2694*t/(t+237.3);//figure out dew point
              float eu = 2.71828;
              float psat = 610.78*pow(eu , e);//from dewpoint figure out psat
              float pw = psat*(1-(rh/100));//now figure out how much your vpd is
              float mpa = pw/1000;//unit conversion
              myFile.print(String(mpa/1000,2));
              myFile.print(',');
              strToPrint+= String(mpa,2);strToPrint+=",";
            //Save Light Readings
              //Max reading is 65535
              uint16_t readings[12];
              as7341.readAllChannels(readings);
              myFile.print(String(readings[0]));//415nm
              myFile.print(',');
              strToPrint+= String(readings[0]);strToPrint+=",";
              myFile.print(String(readings[1]));//445nm
              myFile.print(',');
              strToPrint+= String(readings[1]);strToPrint+=",";
              myFile.print(String(readings[2]));//480 nm
              myFile.print(',');
              strToPrint+= String(readings[2]);strToPrint+=",";
              myFile.print(String(readings[3]));//515 nm
              myFile.print(',');
              strToPrint+= String(readings[3]);strToPrint+=",";
              myFile.print(String(readings[6]));//555 nm
              myFile.print(',');
              strToPrint+= String(readings[6]);strToPrint+=",";
              myFile.print(String(readings[7]));//590 nm
              myFile.print(',');
              strToPrint+= String(readings[7]);strToPrint+=",";
              myFile.print(String(readings[8]));//630 nm
              myFile.print(',');
              strToPrint+= String(readings[8]);strToPrint+=",";
              myFile.print(String(readings[9]));//680 nm
              myFile.print(',');
              strToPrint+= String(readings[9]);strToPrint+=",";
              myFile.print(String(readings[11]));//IR @ 910 nm
              myFile.print(',');
              strToPrint+= String(readings[11]);strToPrint+=",";
              myFile.print(String(readings[10]));//white
              myFile.print(','); 
              strToPrint+= String(readings[10]);strToPrint+=",";
            //Save Actions
              myFile.print('0');//watered
              myFile.print(','); 
              strToPrint+= String('0');strToPrint+=",";
              myFile.print('0');//shade
              myFile.print(','); 
              strToPrint+= String('0');strToPrint+=",";
              myFile.println('0');//heat
              strToPrint+= String('0');strToPrint+="\n";
            //close file and send data
              myFile.close();
              writeBLE(strToPrint);
              digitalWrite(G,HIGH);
            //Delay and incriment stay before starting over
              delay(dt);
              stay=stay+1;
            }
          myFile = SD.open("BLE.txt",O_WRITE);
          myFile.print("r");
          myFile.close();
          writeBLE("Returning to Remote Mode in 5");
          delay(1000);
          writeBLE("4");
          delay(1000);
          writeBLE("3");
          delay(1000);
          writeBLE("2");
          delay(1000);
          writeBLE("1");
          delay(1000);
          writeBLE("Goodbye");
          delay(500);
          digitalWrite(reset,LOW);
        }
      }
    //RESET THE DEVICE IF CENTRAL DISCONNECTS
      digitalWrite(reset,LOW);
    }
}
void writeMenu(){//Sends remote control options to phone
  writeBLE("Welcome to Plant Sitter! Choose an action:");
  writeBLE("[1] Water the Plant");
  writeBLE("[2] Move the Shade");
  writeBLE("[3] Change a Set Point");
  writeBLE("[4] Change the Schedule");
  writeBLE("[5] View/Export Data");
}
void getData(int a, int b, int c, bool fix){//Saves data and tries to fix issues if collected values are far from setpoint
  //Save the time
    digitalWrite(G,LOW);
    myFile = SD.open("DATA.txt",FILE_WRITE);
    DateTime now = rtc.now();
    myFile.print(now.year(), DEC);
    myFile.print(',');
    myFile.print(now.month(), DEC);
    myFile.print(',');
    myFile.print(now.day(), DEC);
    myFile.print(',');
    myFile.print(now.hour(), DEC);
    myFile.print(',');
    myFile.print(now.minute(), DEC);
    myFile.print(',');
    myFile.print(now.second(), DEC);
    myFile.print(',');
  //Save Temperature
    float t = HS300x.readTemperature();//get temp in C
    myFile.print(String(t,2));
    myFile.print(',');
    if (t<=setT*0.45 && fix){ //if SP 25 and temp drops to 15, [~75F to ~50F]
      heatOn();
    }
  //Save Soil Moisture
    soil = analogRead(moist);
    soilP = soil/(soilWet-soilDry);
    myFile.print(String(soilP,2));
    if (soilP<=setW*0.90 && fix){
      waterNow();
    }
  //Save RH
    float rh = HS300x.readHumidity();//get RH as %
    myFile.print(String(rh,2));
    myFile.print(',');
  //Save Pressure
    float p = BARO.readPressure();//get pressure in kPa
    myFile.print(String(p/1000,2));
    myFile.print(',');
  //Save VPD
    float e = 17.2694*t/(t+237.3);//figure out dew point
    float eu = 2.71828;
    float psat = 610.78*pow(eu , e);//from dewpoint figure out psat
    float pw = psat*(1-(rh/100));//now figure out how much your vpd is
    float mpa = pw/1000;//unit conversion
    myFile.print(String(mpa/1000,2));
    myFile.print(',');
  //Save Light Readings
  //Max reading is 65535
    uint16_t readings[12];
    as7341.readAllChannels(readings);
    myFile.print(String(readings[0]));//415nm
    myFile.print(',');
    myFile.print(String(readings[1]));//445nm
    myFile.print(',');
    myFile.print(String(readings[2]));//480 nm
    myFile.print(',');
    myFile.print(String(readings[3]));//515 nm
    myFile.print(',');
    myFile.print(String(readings[6]));//555 nm
    myFile.print(',');
    myFile.print(String(readings[7]));//590 nm
    myFile.print(',');
    myFile.print(String(readings[8]));//630 nm
    myFile.print(',');
    myFile.print(String(readings[9]));//680 nm
    myFile.print(',');
    myFile.print(String(readings[11]));//IR @ 910 nm
    myFile.print(',');
    myFile.print(String(readings[10]));//white
    myFile.print(','); 
  //Save Actions
    myFile.print(String(a));//watered
    myFile.print(','); 
    myFile.print(String(b));//shade
    myFile.print(','); 
    myFile.println(String(c));//heat
  //close file
    myFile.close();
    digitalWrite(G,HIGH);
}
void WriteCharacteristicHandler(BLEDevice central, BLECharacteristic characteristic) {
  char status;//menu-global variable that we can redeclare each time the handler is called
  switch (wake){
    case 0://Send Menu Options and Wait for Choice
      {
      writeMenu();//Write Menu Options to App
      wake = 1;//you've now sent the menu options at least once
      break;
      }
    case 1://handle menu selection
      {
      String inputText1 = (char*)receive.value(); // read incoming data 
      int choice1 = inputText1.toInt();
      switch (choice1){
        case 1://water the plant
          {
          writeBLE("Applying @300 ml of Water over 5 seconds");//let the user know their command has been received
          digitalWrite(G,LOW);
          getData(1,0,0,false);
          waterNow();
          digitalWrite(G,HIGH);
          wake==0;
          break;//break choice 1.1
          }
        case 2://Change shade position
          {
          myFile = SD.open("shade.txt");//File on pre-formattted/written SD card to store b or d for bright or dark;
          if (myFile.available()){
            status = myFile.peek();//there should be only be t or r. Peek allows me to check multiple times at the same location in the event a line break or extra character is present
            myFile.close();
          }
          getData(0,1,0,false);
          if (status == 'b'){
            digitalWrite(G,LOW);
            closeShade();
            digitalWrite(G,HIGH);
            myFile = SD.open("shade.txt", O_WRITE);//Overwrites the file
            myFile.print("d");
            myFile.close();
          } else {
            digitalWrite(G,LOW);
            openShade();
            digitalWrite(G,HIGH);
            myFile = SD.open("shade.txt", O_WRITE);//Overwrites the file
            myFile.print("b");
            myFile.close();
          }
          wake==0;
          break;//break choice 1.2
          }
        case 3://change a set point
          {
          writeBLE("The current setpoints for Temperature and Water are: ");//let the user know their command has been received
          writeBLE(String(setT,2));
          writeBLE("and");
          writeBLE(String(setW,2));
          writeBLE("Change a Set Point?");
          writeBLE("[1] Change Temperature Set Point:");
          writeBLE("[2] Change Soil Moisture Set Point:");
          writeBLE("[3] Return to Menu");
          wake==2;
          break;//break choice 1.3
          }
        case 4://change schedule
          {
          int dtmin = dt/1000/60;
          String interval = String(dtmin);
          int pausetime = dtmin*skip;
          String pause = String(pausetime);
          writeBLE("The current measurement interval is: "+interval+" minutes.");
          writeBLE("Automation is paused for "+pause+" more minutes.");
          writeBLE("Change Schedule?");
          writeBLE("[1] Yes");
          writeBLE("[2] No");
          wake=5;
          break;//break 1.4
          }
        case 5://export data
          {
          writeBLE("Would you like to disconnect from Remote Control");
          writeBLE("and reconnect to in Data View?");
          writeBLE("[1] Stay in Remote Control");
          writeBLE("[2] Go to Data View");
          wake=8;
          break;//break 1.5
          }
      }//end menu choice switch
      break;//break wake case 1
      }//end wake case 1
    case 2://Change which set point?
      {
      String inputText2 = (char*)receive.value(); // read incoming data
      int choice2 = inputText2.toInt();
      switch(choice2) {
        case 1:
          wake = 3;
          writeBLE("Enter New Set Point as ## C");
          break;//temperature
        case 2:
          wake = 4;
          writeBLE("Enter New Set Point as 0.##");
          break;//water
        case 3:
          wake = 0;
          break;//neither
      }
      break;//break wake 2
      }
    case 3://change temp setpoint
      {
      String inputText3 = (char*)receive.value(); // read incoming data
      float tsp = inputText3.toFloat();
      myFile = SD.open("tsp.txt",O_WRITE);
      if (myFile.available()){
        myFile.print(inputText3);}
      myFile.close();
      writeBLE("New Temperature Set Point: "+inputText3+" in degrees C");
      wake==0;
      break;//break wake 3
      }
    case 4://change water setpoint
      {
      String inputText4 = (char*)receive.value(); // read incoming data
      float wsp = inputText4.toFloat();
      myFile = SD.open("wsp.txt",O_WRITE);
      if (myFile.available()){
        myFile.print(inputText4);}
      myFile.close();
      writeBLE("New Watering Set Point: "+inputText4+" % to Max Soil Moisture");
      wake==0;
      break;//break wake 4
      }
    case 5://handle schedule submenu
      {
      String inputText5 = (char*)receive.value(); // read incoming data
      int choice5 = inputText5.toInt();
      switch(choice5) {
        case 1:// change schedule
          wake = 6;
          writeBLE("Pick the new measurement interval in minutes [ints only]");
          break;
        case 2:// return to menu
          wake = 0;
          break;
      }
      break;//break wake 5
      }
    case 6://change dt
      {
      String inputText6 = (char*)receive.value(); // read incoming data
      int delayMin = inputText6.toInt();
      dt = delayMin*60*1000;
      myFile = SD.open("dt.txt",O_WRITE);
      if (myFile.available()){
        myFile.print(inputText6);}
      myFile.close();
      writeBLE("New dt set to: "+inputText6+" minutes");
      writeBLE("Skip automation for how many minutes?");
      wake = 7;
      break;//break wake 6
      }
    case 7://change skip
      {
      String inputText7 = (char*)receive.value(); // read incoming data
      int skipMin = inputText7.toInt();
      float skipNum = skipMin/dt;
      skip = round(skipNum);
      String skipLog = String(skip);
      myFile = SD.open("skip.txt",O_WRITE);
      if (myFile.available()){
        myFile.print(skipLog);}
      myFile.close();
      writeBLE("Automation will be paused for "+inputText7+" more minutes");
      wake=0;
      break;
      }
    case 8://confirm switching mode
      String inputText8 = (char*)receive.value(); // read incoming data 
      int choice8 = inputText8.toInt();
      switch(choice8){
        case 1:
          wake = 0;
          break;
        case 2:
          writeBLE("Plant Sitter is Getting Ready to Switch to Data View&Export Mode");
          getData(0,0,0,true);
          writeBLE("Plant Sitter is Saving State");
          myFile = SD.open("BLE.txt",O_WRITE);//File on pre-formattted/written SD card to store t or r;
          myFile.write("t");
          myFile.close();
          writeBLE("Plant Sitter will restart in Data View&Export Mode in");
          writeBLE("10");
          delay(1000);
          writeBLE("9");
          delay(1000);
          writeBLE("8");
          delay(1000);
          writeBLE("7");
          delay(1000);
          writeBLE("6");
          delay(1000);
          writeBLE("5");
          delay(1000);
          writeBLE("4");
          delay(1000);
          writeBLE("3");
          delay(1000);
          writeBLE("2");
          delay(1000);
          writeBLE("1");
          delay(1000);
          writeBLE("Goodbye!");
          digitalWrite(reset, LOW);//cycle uC
          break;//breaks case 2
      }//ends subchoice switch
      break;//ends case 8
  }//end wake switch
}
void waterNow(){
  digitalWrite(pumpPin,HIGH);
  delay(5000);//time required to deliver 300 ml of water based on calculations/assumptions below
  digitalWrite(pumpPin,LOW);
  //Time calculation for water
    /*Assumptions
      Pump rated at 0.1 amps
      Assume 3 V applied 
      It takes 10 Pascals to lift water 1 mm
      Assume pump located 0.5 m below delivery point
      ==============================================
      P=IV -> 0.3 Watts == 0.3 N*m/s
      Must exert 5000 Pa == 5000 N/m*m
      Unit wise: (N*m*m*m) / (N*s) - > m^3 / s
      0.3/5000 = 0.00006 cubic meters of water per second
      or 60 ml per second */
}
void closeShade(){
  digitalWrite(shade1,HIGH);//Set up motor direction
  digitalWrite(shade2,LOW);
  digitalWrite(shade0,HIGH);
  delay(5000);//a resistor over motor pins will ensure the motor speed is appropriate to move in 5 seconds
  digitalWrite(shade0,LOW);
  writeBLE("Shade Closed");
}
void openShade(){
  digitalWrite(shade1,LOW);//Set up motor direction
  digitalWrite(shade2,HIGH);
  digitalWrite(shade0,HIGH);
  delay(5000);//a resistor over motor pins will ensure the motor speed is appropriate to move in 5 seconds
  digitalWrite(shade0,LOW);
  writeBLE("Shade Opened");
}
void heatOn(){
  for (int j=0;j<10;j++){
    digitalWrite(heatPin,HIGH);
    delay(500);
    digitalWrite(heatPin,LOW);
    delay(500);
  }
}
void getDtSkip(){
  myFile = SD.open("dt.txt");//File on pre-formattted/written SD card to store t or r;
  String timeInt;
  while (myFile.available()){
      timeInt=timeInt + (char)myFile.read();
  }
  myFile.close();
  int timeInterval = timeInt.toInt();
  dt = 60*1000*timeInterval;
  ///now get skip///
  myFile = SD.open("skip.txt");//File on pre-formattted/written SD card to store t or r;
  String ignore;
  while (myFile.available()){
      ignore=ignore + (char)myFile.read();
  }
  myFile.close();
  skip = ignore.toInt();
}
void loop() {//Only enters if we're in RX mode
  //while we're not activley taking a measurement, see if there are incoming messages over BLE
    BLE.poll(dt);//wait for an event over BLE until we need to use thinking power to grab the new data set
  //When its about time to take a measurement, get the data, and decrease the skip index
    if (skip == 0) {
      getData(0,0,0,true);
    } else {
      getData(0,0,0,false);
      skip = skip - 1;
      myFile = SD.open("skip.txt",O_WRITE);
      String count = String(skip);
      myFile.print(count);
      myFile.close();
    }
}