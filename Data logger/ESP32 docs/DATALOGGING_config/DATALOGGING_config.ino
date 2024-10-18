/*TODO: COMMENTS
        

MILESTONES: SD card read
            Webserver active
            reed switch interupts
            data log
            CSV format
            Keep RTC time
            LED status indicators


  Basic description: Log and store time stamped data onto an SD card and using an esp to remotely access the data stored on the sd card. 
  Two different modes are made: Logging data, and initiate an asynchronous webserver to upload and dowload files from the SD card to your PC. 
    
 This code was developed by integrating pieces of code from various examples and projects made publically available. 
 The main contributers can be attributed to My Circuits 2022 and David Bird 2018.   
 
 Any redistribution or reproduction of any part or all of the contents in any form is prohibited other than the following:
 1. You may print or download to a local hard disk extracts for your personal and non-commercial use only.
 2. You may copy the content to individual third parties for their personal use, but only if you acknowledge the authors David Bird (2018), My Circuits (2022), Jordan Williams (2024) as the source of the material.
    
USE https://app.datawrapper.de/chart/klh7x/upload TO PLOT DATA IN A GRAPH BY SIMPLY UPLOADING A CSV.

*/
//Libraries for ESP32 WiFi
#include <WiFi.h>            
#include <ESP32WebServer.h> 
#include <ESPmDNS.h>
#include <esp_system.h>

//Includes headers of webserver and CSS style file
#include <CSS.h>  
#include <SPI.h>
#include <SD.h>
//#include <CSV_Parser.h>

//Headers for DS18B20 temp sensors
#include <OneWire.h>
#include <DallasTemperature.h>

//BME280 hum, pres, temp sensor
#include <Adafruit_BME280.h>  
#include <Wire.h>
#include <Adafruit_Sensor.h>

//Real Time Clock lib
#include <RTClib.h>
#include <EEPROM.h>
RTC_DS3231 rtc;

ESP32WebServer server(80);

#define servername "BOOEserver" //Define the name to your server...
#define SD_pin 3               //GPIO 16 in my case
#define w_mode 2                //GPIO 2 - Work Mode: logger or webserver
#define Log_status 32           //LED indicator ***GREEN***
#define Webserver_status 25     //LED indicator ***RED***
#define Idle_indicator 33       //LED indicator ***BLUE***
#define ONE_WIRE_BUS 4         //GPIO 4 data wire for Temp
#define EEPROM_SIZE 1           //Number of bytes to acces from EEPROM
#define RTC_ALARM_PIN 14

//variables to keep track of the timing of recent interrupts
const unsigned long debounceDelay = 900;  
volatile unsigned long lastInterruptTime = 0;
const char *username = "admin";
const char *password = "root";
volatile bool interruptTriggered = false; 
volatile bool waitingForSecondPress = false;
bool SD_present = false;                      //Controls if the SD card is present or not
bool Error_state = false;
bool powerOnReset = false;
int numberOfDevices;                          // Number of temperature devices found
float BME_pres, BME_temp, BME_hum;            //declare BME float variables
const int flagAddress = 0;    
int loggingDelay = 10;                      //******LOGGING DELAY HERE (in seconds)************

OneWire oneWire(ONE_WIRE_BUS);          //Setup a oneWire instance to communicate with any OneWire devices
DallasTemperature sensors(&oneWire);    //Pass our oneWire reference to Dallas Temperature.

DeviceAddress tempDeviceAddress;        // Variable to store a found device address
Adafruit_BME280 bme;

File logFile;
String logFileName;
char filename[30];

/****************************************************************
**************  INTERRUPT SERVICE ROUTINE  **********************
****************************************************************/
void IRAM_ATTR isr(){                   
  unsigned long currentTime = millis();
  if (currentTime - lastInterruptTime > debounceDelay){   //debounce function
    if (!waitingForSecondPress){
        interruptTriggered = true;
        waitingForSecondPress=true;
    } else {
        waitingForSecondPress=false;
    }
    lastInterruptTime = currentTime;
  }
} //isr end

/****************************************************************
***************  Create new ERROR log file  *********************
****************************************************************/
void logError(const String& errorMessage){
  File errorLogFile = SD.open("/error_log.txt", FILE_APPEND);
    if (errorLogFile) {
      DateTime now = rtc.now();  // Get the current timestamp
      char timestamp[20];
      sprintf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());    // Write timestamp and error message to the log file
      errorLogFile.print(timestamp);
      errorLogFile.print(": ");
      errorLogFile.println(errorMessage);
      errorLogFile.close();                                  // Close the log file
      Serial.println("Error logged: " + errorMessage);      // Debug output to the Serial Monitor
    } else {
        Serial.println("Failed to open error log file for writing");
    }
}

/****************************************************************
*****************  Create new file  *****************************
****************************************************************/
String createNewLogFile(){          //csv file name
  DateTime now = rtc.now();
  //char filename[30];
  sprintf(filename, "/%04d%02d%02d_data_test.csv", now.year(), now.month(), now.day(), now.hour(), now.minute());   //***********FILE NAME SET HERE**********
  // Create a new file
  logFile = SD.open(filename, FILE_APPEND);
  if (!logFile) {
    Serial.println("Creating new file");
    logFile = SD.open(filename, FILE_WRITE);
    logFile.println("Timestamp,TempC0,TempC1,BME_Temp,BME_Press,BME_Hum");
    logFile.flush();
  }
  logFile.close();
  return String(filename);
}

/****************************************************************
**************************  SETUP  ******************************
****************************************************************/
void setup(void){
  pinMode(RTC_ALARM_PIN , INPUT_PULLUP);
  pinMode(Log_status, OUTPUT);              //LED pin
  pinMode(Webserver_status, OUTPUT);        //LED pin
  pinMode(Idle_indicator, OUTPUT);        //LED pin
  pinMode(w_mode, INPUT_PULLUP);            //working mode
  attachInterrupt(w_mode, isr, FALLING);    //ISR interrupt GPIO pin

  Serial.begin(9600);
  delay(500);

    //Initialise RTC and set time to PC's time if not correct
  if (!rtc.begin()) {
    Serial.println("RTC module is NOT found");   //For debugging
    logError("RTC module is NOT found");
    Error_state = true;
    for (int i=0; i<5; i++){
      digitalWrite(Log_status, HIGH); //GREEN
      digitalWrite(Webserver_status, HIGH); //RED
      delay(500);
      digitalWrite(Log_status, LOW);
      digitalWrite(Webserver_status, LOW);
    }
    return;
  }
  else{
    Serial.println("RTC module is found");              //For debugging
    digitalWrite(Log_status, HIGH);  //GREEN
    delay(500);
    digitalWrite(Log_status, LOW);
    delay(250);
  }
  
  EEPROM.begin(1);                                    //initialise EEPROM                 
  //Serial.println(EEPROM.read(0));                     //Store flag in EEPROM address 0
  if (EEPROM.read(flagAddress) == 0){
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));   //Adjust RTC time to PC's time
    EEPROM.write(flagAddress, 1);
    EEPROM.commit();
    Serial.println("Time has been updated");
  } else{
    Serial.println("Time is correct");
    digitalWrite(Log_status, HIGH);
    delay(500);
    digitalWrite(Log_status, LOW);
    delay(250);
  }

  rtc.disable32K();
  rtc.clearAlarm(1);
  rtc.clearAlarm(2);
  rtc.disableAlarm(2);
  rtc.writeSqwPinMode(DS3231_OFF);

  sensors.begin();                             // Start up oneWire & Dallas Temperature library
  numberOfDevices = sensors.getDeviceCount();   // locate devices on the bus
  Serial.print("Found ");
  Serial.print(numberOfDevices, DEC);
  Serial.println(" DS18B20 devices.");

  if (sensors.getDeviceCount() >= 2){
    digitalWrite(Log_status, HIGH);
    delay(500);
    digitalWrite(Log_status, LOW);
    delay(250);
  }

  if (sensors.getDeviceCount() < 2){
    Error_state = true;
    logError("Error with DS18B20 devices.");
    for (int i=0; i<5; i++){
      digitalWrite(Idle_indicator, HIGH); //BLUE
      digitalWrite(Webserver_status, HIGH); //RED
      delay(500);
      digitalWrite(Idle_indicator, LOW);
      digitalWrite(Webserver_status, LOW);
      }
  }
   


  //Initialise BME280
  if (!bme.begin(0x76)) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");    //For debugging
    logError("No BME280 found.");
    Error_state = true;
    for (int i=0; i<5; i++){
    digitalWrite(Idle_indicator, HIGH);
    digitalWrite(Webserver_status, HIGH);
    delay(500);
    digitalWrite(Idle_indicator, LOW);
    digitalWrite(Webserver_status, LOW);
    }
    return;
  }
  else{
    Serial.println("Valid BME280 sensor found");      //For debugging
    digitalWrite(Log_status, HIGH);
    delay(500);
    digitalWrite(Log_status, LOW);
    delay(250);
  }

  //see if the card is present and can be initialised.
  Serial.print(F("Initializing SD card..."));               //For debugging
  if (!SD.begin(SD_pin)) {
    Serial.println(F("ERROR: Card failed, or not present"));      //For debugging
    SD_present = false;
    Error_state = true;

    for (int i=0; i<5; i++){
    //digitalWrite(Idle_indicator, HIGH);
    digitalWrite(Webserver_status, HIGH); //RED
    //digitalWrite(Log_status, HIGH);
    delay(500);
    //digitalWrite(Idle_indicator, LOW);
    digitalWrite(Webserver_status, LOW);
    //digitalWrite(Log_status, LOW);
    }

    ESP.restart();
    return;
  }
  else{
    Serial.println("Card initialized.");        //For debugging
    SD_present = true;
    digitalWrite(Log_status, HIGH);
    delay(500);
    digitalWrite(Log_status, LOW);
    delay(250);
  }
  
  WiFi.softAP("BOO-E", "221343687");  //Network and password for the access point genereted by ESP32 http://BOOEserver.local/
  if (!MDNS.begin(servername)) {
    Serial.println("Error setting up MDNS responder!");    //For debugging
    logError("Error setting up MDNS responder!");
    digitalWrite(Webserver_status, HIGH);
    delay(2000);
    digitalWrite(Webserver_status, LOW);
    ESP.restart();
    }

  /*********  Server Commands  **********/
  server.on("/", SD_dir);
  server.on("/upload", File_Upload);
  server.on("/fupload", HTTP_POST, []() 
  { 
      server.send(200); 
    }, 
    handleFileUpload);

  server.begin();
  Serial.println("HTTP server started");        //For debugging

  Serial.println("Initialisation Finish");
  if (Error_state == false){
    logError("Successfull initialisation!");
    Serial.println("Initialisation Finish");
    for (int i =0; i < 2; i++){
      digitalWrite(Log_status, HIGH);
      digitalWrite(Webserver_status, HIGH);
      digitalWrite(Idle_indicator, HIGH);
      delay(500);
      digitalWrite(Log_status, LOW);
      digitalWrite(Webserver_status, LOW);
      digitalWrite(Idle_indicator, LOW);
      delay(250);
    }
  }
  if (Error_state == true){
    for(int i=0; 1< 10; i++){
      digitalWrite(Webserver_status, HIGH);
      delay(500);
      digitalWrite(Webserver_status, LOW);
      delay(500);
      Serial.println("REBOOT ESP32!!!");
    }
    ESP.restart();
  }

}//setup end

/****************************************************************
**************************  LOOP  *******************************
****************************************************************/
void loop(void){
  //Mode change detection  
  digitalWrite(Log_status, LOW);                                       //Set LED to LOW
  digitalWrite(Webserver_status, LOW);                                //Set LED to HIGH
  digitalWrite(Idle_indicator, HIGH);                                //Set LED to HIGH
  //rtc.clearAlarm(1);

  if (interruptTriggered){
    rtc.clearAlarm(1);
    digitalWrite(Log_status, LOW);                                       //Set LED to LOW
    digitalWrite(Webserver_status, HIGH);                                //Set LED to HIGH
    digitalWrite(Idle_indicator, LOW);
    interruptTriggered = false; //resets intertupt flag
    Serial.println("Button pressed, waiting for next press... http://BOOEserver.local/");         //For debugging
    WiFi.softAP("BOO-E", "12345678");
    delay(500);

    while(waitingForSecondPress){  
      server.handleClient();                                             //Listen for client connections
      delay(100);
    }
    Serial.println("Button pressed again, exiting wait loop...");        //For debugging
    logError("Mode cycled.");
    rtc.setAlarm1(rtc.now() + TimeSpan(30),DS3231_A1_Second);
    Serial.println("Logging starts in 30 seconds");
  }//interupt end

  /*esp_reset_reason_t resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_POWERON) {
    // Set the flag for power-on reset
    Serial.println("Power on. Logging starts in 5 seconds");
    delay(5000);
    Data_Log();
  }*/

  if (digitalRead(RTC_ALARM_PIN) == LOW){
    Data_Log();                            //Call Data logging function
  }
}//loop end

/*********  FUNCTIONS  **********/

/****************************************************************
**************************  Data logging  ***********************
****************************************************************/
void Data_Log(){
  rtc.clearAlarm(1);
  DateTime now = rtc.now();
  //DateTime wakeUp;
  Serial.println();
  digitalWrite(Log_status, HIGH);         //Set LED to HIGH
  digitalWrite(Webserver_status, LOW);
  digitalWrite(Idle_indicator, LOW);
  sensors.requestTemperatures();                      //Call temp reading from DS18B20 sensors
  float TempC0 = sensors.getTempCByIndex(1);          
  float TempC1 = sensors.getTempCByIndex(0);
  float BME_Temp = bme.readTemperature();           //Call BME readings
  float BME_Press = bme.readPressure() / 100.0F;
  float BME_Hum = bme.readHumidity();
   
  // Open file for appending data
  logFileName = createNewLogFile();
  logFile = SD.open(filename, FILE_APPEND);
  if (!logFile) {
    logError("Failed to open file for data logging");     //log error
    Serial.println("Failed to open file for appending");
    digitalWrite(Log_status, HIGH);         //Set LED to HIGH
    digitalWrite(Webserver_status, HIGH);
    digitalWrite(Idle_indicator, HIGH);
    return;
  }
  Serial.println("File open");        //For debugging
  if (logFile.size() == 0) {
    logFile.println("Timestamp,TempC0,TempC1,BME_Temp,BME_Press,BME_Hum");   // If file is empty, write headers
  }
  
  char timestamp[20];                                     //create time stamp string YYYY-MM-DD HH:MM:SS
  sprintf(timestamp, "%04d-%02d-%02dT%02d:%02d:%02d", 
          now.year(), now.month(), now.day(), 
          now.hour(), now.minute(), now.second());
  //Create csv string format
  logFile.print(timestamp);
  logFile.print(",");
  logFile.print(TempC0);
  logFile.print(",");
  logFile.print(TempC1);
  logFile.print(",");
  logFile.print(BME_Temp);
  logFile.print(",");
  logFile.print(BME_Press);
  logFile.print(",");
  logFile.println(BME_Hum);
  logFile.close(); // Close the current file
 
  //For Debugging
  Serial.print(timestamp);
  Serial.print(",");
  Serial.print(TempC0);
  Serial.print(",");
  Serial.print(TempC1);
  Serial.print(",");
  Serial.print(BME_Temp);
  Serial.print(",");
  Serial.print(BME_Press);
  Serial.print(",");
  Serial.println(BME_Hum);

  Serial.println("Data written to file");   //For Debugging
  Serial.println();

  File dataF = SD.open("/CONFIG.TXT");
  String L_delay =dataF.readStringUntil('\n');
  L_delay.replace("Sampling rate (sec): ","");
  L_delay.replace("\"","");
  L_delay.replace("\n","");
  loggingDelay = L_delay.toInt();
  dataF.close();
  
  if (loggingDelay <= 60){
    rtc.setAlarm1(rtc.now() + TimeSpan(loggingDelay),DS3231_A1_Second);   //RTC logging alarm SECONDS
  }
  if( loggingDelay > 60 && loggingDelay <= 3600){
    rtc.setAlarm1(rtc.now() + TimeSpan(loggingDelay/60),DS3231_A1_Minute);   //RTC logging alarm MINUTE
  }
  if( loggingDelay > 3600 && loggingDelay <= 86400){
    rtc.setAlarm1(rtc.now() + TimeSpan(loggingDelay/3600),DS3231_A1_Hour);   //RTC logging alarm hour
  }
  
 /* if(L_delay.indexOf("(sec)") >= 0){
    L_delay.replace("Sampling rate (sec): ","");
    L_delay.replace("\"","");
    L_delay.replace("\n","");
    loggingDelay = L_delay.toInt();
    rtc.setAlarm1(rtc.now() + TimeSpan(loggingDelay),DS3231_A1_Second);   //RTC logging alarm
  }
  if(L_delay.indexOf("(min)") >= 0){
    L_delay.replace("Sampling rate (min): ","");
    L_delay.replace("\"","");
    L_delay.replace("\n","");
    loggingDelay = L_delay.toInt();
    rtc.setAlarm1(rtc.now() + TimeSpan(loggingDelay),DS3231_A1_Minute);   //RTC logging alarm
  }*/
  
  delay(100);
  Serial.print(loggingDelay);   //For Debugging
  Serial.println(" second logging delay");
}//Data logging end

/****************************************************************
**********  WEBSERVER ACTION (DO NOT EDIT THIS SECTION)  ********
****************************************************************/
//Init=ial page of the server web, list directory and give you the chance of deleting and uploading
void SD_dir() {
  if(!server.authenticate(username, password)){
    return server.requestAuthentication();
  }
  if (SD_present) 
  {
    //Action acording to post, dowload or delete, by MC 2022
    if (server.args() > 0)  //Arguments were received, ignored if there are not arguments
    {
      Serial.println(server.arg(0));

      String Order = server.arg(0);
      Serial.println(Order);

      if (Order.indexOf("download_") >= 0) 
      {
        Order.remove(0, 9);
        SD_file_download(Order);
        Serial.println(Order);
      }

      if ((server.arg(0)).indexOf("delete_") >= 0) 
      {
        Order.remove(0, 7);
        SD_file_delete(Order);
        Serial.println(Order);
      }
    }

    File root = SD.open("/");
    if (root) 
    {
      root.rewindDirectory();
      SendHTML_Header();
      webpage += F("<table align='center'>");
      webpage += F("<tr><th>Name/Type</th><th style='width:20%'>Type File/Dir</th><th>File Size</th></tr>");
      printDirectory("/", 0);
      webpage += F("</table>");
      SendHTML_Content();
      root.close();
    } 
    else {
      SendHTML_Header();
      webpage += F("<h3>No Files Found</h3>");
    }
    append_page_footer();
    SendHTML_Content();
    SendHTML_Stop();  //Stop is needed because no content length was sent
  } 
  else ReportSDNotPresent();
}

//Upload a file to the SD
void File_Upload() {
  append_page_header();
  webpage += F("<h3>Select File to Upload</h3>");
  webpage += F("<FORM action='/fupload' method='post' enctype='multipart/form-data'>");
  webpage += F("<input class='buttons' style='width:25%' type='file' name='fupload' id = 'fupload' value=''>");
  webpage += F("<button class='buttons' style='width:10%' type='submit'>Upload File</button><br><br>");
  webpage += F("<a href='/'>[Back]</a><br><br>");
  append_page_footer();
  server.send(200, "text/html", webpage);
}

//Prints the directory, it is called in void SD_dir()
void printDirectory(const char* dirname, uint8_t levels) {

  File root = SD.open(dirname);

  if (!root) 
  {
    return;
  }
  if (!root.isDirectory()) 
  {
    return;
  }
  File file = root.openNextFile();

  int i = 0;
  while (file) 
  {
    if (webpage.length() > 1000) 
    {
      SendHTML_Content();
    }
    if (file.isDirectory()) 
    {
      webpage += "<tr><td>" + String(file.isDirectory() ? "Dir" : "File") + "</td><td>" + String(file.name()) + "</td><td></td></tr>";
      printDirectory(file.name(), levels - 1);
    } 
    else 
    {
      webpage += "<tr><td>" + String(file.name()) + "</td>";
      webpage += "<td>" + String(file.isDirectory() ? "Dir" : "File") + "</td>";
      int bytes = file.size();
      String fsize = "";
      if (bytes < 1024) fsize = String(bytes) + " B";
      else if (bytes < (1024 * 1024)) fsize = String(bytes / 1024.0, 3) + " KB";
      else if (bytes < (1024 * 1024 * 1024)) fsize = String(bytes / 1024.0 / 1024.0, 3) + " MB";
      else fsize = String(bytes / 1024.0 / 1024.0 / 1024.0, 3) + " GB";
      webpage += "<td>" + fsize + "</td>";
      webpage += "<td>";
      webpage += F("<FORM action='/' method='post'>");
      webpage += F("<button type='submit' name='download'");
      webpage += F("' value='");
      webpage += "download_" + String(file.name());
      webpage += F("'>Download</button>");
      webpage += "</td>";
      webpage += "<td>";
      webpage += F("<FORM action='/' method='post'>");
      webpage += F("<button type='submit' name='delete'");
      webpage += F("' value='");
      webpage += "delete_" + String(file.name());
      webpage += F("'>Delete</button>");
      webpage += "</td>";
      webpage += "</tr>";
    }
    file = root.openNextFile();
    i++;
  }
  file.close();
}

//Download a file from the SD, it is called in void SD_dir()
void SD_file_download(String filename) {
  if (SD_present) {
    File download = SD.open("/" + filename);
    if (download) 
    {
      server.sendHeader("Content-Type", "text/text");
      server.sendHeader("Content-Disposition", "attachment; filename=" + filename);
      server.sendHeader("Connection", "close");
      server.streamFile(download, "application/octet-stream");
      download.close();
    } 
    else ReportFileNotPresent("download");
  } 
  else ReportSDNotPresent();
}

//Handles the file upload a file to the SD
File UploadFile;
//Upload a new file to the Filing system
void handleFileUpload() {
  HTTPUpload& uploadfile = server.upload();  //See https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer/srcv
                                                    //For further information on 'status' structure, there are other reasons such as a failed transfer that could be used
  if (uploadfile.status == UPLOAD_FILE_START) 
  {
    String filename = uploadfile.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    Serial.print("Upload File Name: ");
    Serial.println(filename);
    SD.remove(filename);                         //Remove a previous version, otherwise data is appended the file again
    UploadFile = SD.open(filename, FILE_WRITE);  //Open the file for writing in SD (create it, if doesn't exist)        *********************************
    if (!UploadFile) {
      logError("Failed to create file for upload: " + uploadfile.filename);
      return;
    }
    filename = String();
  } 
  else if (uploadfile.status == UPLOAD_FILE_WRITE) 
  {
    if (UploadFile) UploadFile.write(uploadfile.buf, uploadfile.currentSize);  // Write the received bytes to the file
  } 
  else if (uploadfile.status == UPLOAD_FILE_END) 
  {
    if (UploadFile)  //If the file was successfully created
    {
      UploadFile.close();  //Close the file again
      Serial.print("Upload Size: ");
      Serial.println(uploadfile.totalSize);
      webpage = "";
      append_page_header();
      webpage += F("<h3>File was successfully uploaded</h3>");
      webpage += F("<h2>Uploaded File Name: ");
      webpage += uploadfile.filename + "</h2>";
      webpage += F("<h2>File Size: ");
      webpage += file_size(uploadfile.totalSize) + "</h2><br><br>";
      webpage += F("<a href='/'>[Back]</a><br><br>");
      append_page_footer();
      server.send(200, "text/html", webpage);
    } 
    else 
    {
      ReportCouldNotCreateFile("upload");
    }
  }
}

//Delete a file from the SD, it is called in void SD_dir()
void SD_file_delete(String filename) {
  if (SD_present) 
  {
    SendHTML_Header();
    File dataFile = SD.open("/" + filename, FILE_READ);  //Now read data from SD Card
    if (dataFile) 
    {
      if (SD.remove("/" + filename)) 
      {
        Serial.println(F("File deleted successfully"));
        webpage += "<h3>File '" + filename + "' has been erased</h3>";
        webpage += F("<a href='/'>[Back]</a><br><br>");
      } 
      else 
      {
        webpage += F("<h3>File was not deleted - error</h3>");
        webpage += F("<a href='/'>[Back]</a><br><br>");
      }
    } 
    else ReportFileNotPresent("delete");
    append_page_footer();
    SendHTML_Content();
    SendHTML_Stop();
  } 
  else ReportSDNotPresent();
}

//SendHTML_Header
void SendHTML_Header() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "-1");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");  //Empty content inhibits Content-length header so we have to close the socket ourselves.
  append_page_header();
  server.sendContent(webpage);
  webpage = "";}

//SendHTML_Content
void SendHTML_Content() {
  server.sendContent(webpage);
  webpage = "";}

//SendHTML_Stop
void SendHTML_Stop() {
  server.sendContent("");
  server.client().stop(); } //Stop is needed because no content length was sent

//ReportSDNotPresent
void ReportSDNotPresent() {
  SendHTML_Header();
  webpage += F("<h3>No SD Card present</h3>");
  webpage += F("<a href='/'>[Back]</a><br><br>");
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();}

//ReportFileNotPresent
void ReportFileNotPresent(String target) {
  SendHTML_Header();
  webpage += F("<h3>File does not exist</h3>");
  webpage += F("<a href='/"); webpage += target + "'>[Back]</a><br><br>";
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();}

//ReportCouldNotCreateFile
void ReportCouldNotCreateFile(String target) {
  SendHTML_Header();
  webpage += F("<h3>Could Not Create Uploaded File (write-protected?)</h3>");
  webpage += F("<a href='/"); webpage += target + "'>[Back]</a><br><br>";
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();}

//File size conversion
String file_size(int bytes) {
  String fsize = "";
  if (bytes < 1024)                       fsize = String(bytes) + " B";
  else if (bytes < (1024 * 1024))         fsize = String(bytes / 1024.0, 3) + " KB";
  else if (bytes < (1024 * 1024 * 1024))  fsize = String(bytes / 1024.0 / 1024.0, 3) + " MB";
  else                                    fsize = String(bytes / 1024.0 / 1024.0 / 1024.0, 3) + " GB";
  return fsize;}
/****************************************************************
****************************  FINISHED  *************************
****************************************************************/