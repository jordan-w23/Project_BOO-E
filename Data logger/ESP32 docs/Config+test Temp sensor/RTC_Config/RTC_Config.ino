#include <RTClib.h>
RTC_DS3231 rtc;

#define EEPROM_SIZE 1           
#include <EEPROM.h>

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  EEPROM.begin(1);
  if (!rtc.begin()) {
    Serial.println("RTC module is NOT found");   //For debugging
    return;
  }
  Serial.println("RTC module is found");
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));   //Adjust RTC time to PC's time
  EEPROM.write(0, 0);
  EEPROM.commit();
  Serial.println(EEPROM.read(0));
  }


void loop() {
  // put your main code here, to run repeatedly:
  DateTime now = rtc.now();
  char timestamp[20];                                     //create time stamp string YYYY-MM-DD HH:MM:SS
  sprintf(timestamp, "%04d-%02d-%02d %02d:%02d:%02d", 
          now.year(), now.month(), now.day(), 
          now.hour(), now.minute(), now.second());
  Serial.println(timestamp);
  delay(5000);        

}
