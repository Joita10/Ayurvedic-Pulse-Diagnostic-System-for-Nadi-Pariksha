#include<Wire.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "MAX30105.h"
#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h> // Click here to get the library: http://librarymanager/All#SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library

SFE_MAX1704X lipo; // Defaults to the MAX17043

double voltage = 0; // Variable to keep track of LiPo voltage
double soc = 0; // Variable to keep track of LiPo state-of-charge (SOC)
bool alert; // Variable to keep track of whether alert has been triggered
MAX30105 KVP;//object for measuring kvp

BLEServer *pulse_server = NULL;//server

//notifying characteristics
BLECharacteristic *vata_characteristic = NULL;
BLECharacteristic *pitta_characteristic = NULL;
BLECharacteristic *kapha_characteristic = NULL;
//read characteristic
BLECharacteristic *test_key_characteristic = NULL;
//battery characteristic
BLECharacteristic *battery_characteristic = NULL;


//to check if the server is connected or not
bool device_connected = false;
bool old_device_connected = false;

//to store sensor values
uint32_t vata_pulse,pitta_pulse,kapha_pulse;

//pins used in esp
const int button_pin = 27;
const int led_pin = 2;
const int latch_pin = 14;

//variables to check whether to start sending sensor data 
RTC_DATA_ATTR bool  isDeviceOn = false; 
bool isSendingData = false;

//uuid's
static BLEUUID SERVICE_UUID("1116410e-628b-46c1-8a98-31c8f8d23378");
static BLEUUID Battery_Service_UUID("88eb95b9-b5d4-4b6b-adfe-e428a33ff431");

#define vata_uuid    "9d561e67-5768-479d-98f4-21999733cd44"
#define pitta_uuid   "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define kapha_uuid   "380f055f-ec66-41a6-9bf5-14a99ced3494"
#define test_key_uuid "1908d398-1546-42bb-880b-9cafd45680be"
#define battery_uuid "c373dcc3-3ff0-41f1-856c-c9bf93f4e0cf"

class CharacteristicCallBack : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String rxValue = pCharacteristic->getValue();
    if (rxValue.length() >= 0) {
      if (rxValue[0]== '1') {
        isSendingData = true;
      } else if (rxValue[0] == '0') {
        isSendingData = false;
      }
    }
  } 
  
};

class MyServerCallbacks:
public BLEServerCallbacks{
  void onConnect(BLEServer *pulse_server){
    device_connected = true;
  };
  void onDisconnect(BLEServer *pulse_server){
    device_connected = false;
  }
};

void i2c_multiplexer(uint8_t bus){
  Wire.beginTransmission(0x70);
  Wire.write(1<<bus);
  Wire.endTransmission();
  
}
void Battery_setup(){
  i2c_multiplexer(1);
  lipo.enableDebugging(); // Uncomment this line to enable helpful debug messages on Serial

  // Set up the MAX17043 LiPo fuel gauge:
  if (lipo.begin() == false) // Connect to the MAX17043 using the default wire port
  {
    Serial.println(F("MAX17043 not detected. Please check wiring. Freezing."));
    while (1)
      ;
  }

	// Quick start restarts the MAX17043 in hopes of getting a more accurate
	// guess for the SOC.
	lipo.quickStart();

	// We can set an interrupt to alert when the battery SoC gets too low.
	// We can alert at anywhere between 1% - 32%:
	lipo.setThreshold(20); // Set alert threshold to 20%.
}

void KVP_setup(){
 
  i2c_multiplexer(2);
  if(KVP.begin()==false){
    Serial.println("Check Wiring / Power for kapha");
    while(1);
  }
  KVP.setup();

  i2c_multiplexer(4);
  if(KVP.begin()==false){
    Serial.println("Check Wiring / Power for pitta");
    while(1);
  }
  KVP.setup();

  i2c_multiplexer(6);
  if(KVP.begin()==false){
    Serial.println("Check Wiring / Power for vata");
    while(1);
  }
  KVP.setup();
}

void BLE_setup(){
  // Create the BLE Device
  BLEDevice::init("Tavisa");

  //Create the BLE Server
  pulse_server = BLEDevice :: createServer();
  pulse_server -> setCallbacks(new MyServerCallbacks());

  //Create the BLE Service
  BLEService *pulse_service = pulse_server -> createService(SERVICE_UUID,30,0);
  BLEService *battery_service = pulse_server -> createService(Battery_Service_UUID,30,0);
  //create characteristic
  vata_characteristic = pulse_service->createCharacteristic(vata_uuid,BLECharacteristic::PROPERTY_NOTIFY);
  pitta_characteristic = pulse_service->createCharacteristic(pitta_uuid,BLECharacteristic::PROPERTY_NOTIFY);
  kapha_characteristic = pulse_service->createCharacteristic(kapha_uuid,BLECharacteristic::PROPERTY_NOTIFY);
  test_key_characteristic = pulse_service->createCharacteristic(test_key_uuid,BLECharacteristic::PROPERTY_READ |BLECharacteristic:: PROPERTY_WRITE);
  battery_characteristic = battery_service->createCharacteristic(battery_uuid,BLECharacteristic::PROPERTY_NOTIFY);
  //create descriptor
  vata_characteristic->addDescriptor(new BLE2902());
  pitta_characteristic->addDescriptor(new BLE2902());
  kapha_characteristic->addDescriptor(new BLE2902());
  test_key_characteristic->addDescriptor(new BLE2902());
  battery_characteristic->addDescriptor(new BLE2902());
  test_key_characteristic->setCallbacks(new CharacteristicCallBack());
  
  // Start the service
  pulse_service->start();
  battery_service->start();
  // Start advertising
  pulse_server -> getAdvertising() -> start();
  
  Serial.println("Waiting a client connection to notify...");
  
}

void battery_measurement(){
  if(device_connected){
    i2c_multiplexer(1);
    // lipo.getSOC() returns the estimated state of charge (e.g. 79%)
    soc = lipo.getSOC();
    if (soc<98){
      static char battery_string[8];
      dtostrf(soc,2,2,battery_string);
      battery_characteristic->setValue(battery_string);
      battery_characteristic->notify();
    }
    else{
      battery_characteristic->setValue("100");
      battery_characteristic->notify();
    }
    Serial.print(soc);
    Serial.print(",");
    delay(3);
  }
}

void BLE_Measurement(){
  
     if(device_connected && isSendingData){

      i2c_multiplexer(6);
      vata_pulse = KVP.getIR();
      static char vata_string[8];
      dtostrf(vata_pulse, 2, 2, vata_string);
      vata_characteristic->setValue(vata_string);
      vata_characteristic->notify();

      Serial.print(vata_pulse);
      Serial.print(",");
      delay(3);

      i2c_multiplexer(4);
      pitta_pulse = KVP.getIR();
      static char pitta_string[8];
      dtostrf(pitta_pulse, 2, 2, pitta_string);
      pitta_characteristic->setValue(pitta_string);
      pitta_characteristic->notify();

      Serial.print(pitta_pulse);
      Serial.print(",");
      delay(3);

      i2c_multiplexer(2);
      kapha_pulse = KVP.getIR();
      static char kapha_string[8];
      dtostrf(kapha_pulse, 2, 2, kapha_string);
      kapha_characteristic->setValue(kapha_string);
      kapha_characteristic->notify();

      Serial.println(kapha_pulse);
    
      delay(3);
      
    }
    
      if(!device_connected && old_device_connected){
      delay(500);
      pulse_server -> startAdvertising();
      Serial.println("Waiting for the client connection...new");
      old_device_connected = device_connected;
     }
     if(device_connected && !old_device_connected){
      old_device_connected = device_connected;
     }

}

/*void buttonPressed(){
  int i = 0;
  
  // Power down if held 2 seconds
  while(digitalRead(button_pin) == LOW){
    delay(50);
    i++;
    if(i == 40){
      int j =5;
  while(j){
    digitalWrite(led_pin, HIGH);
    delay(100);
    digitalWrite(led_pin, LOW);;
    delay(100);
    j--;
  }   
      isDeviceOn = false;
      digitalWrite(latch_pin, LOW);
    }
  }
} */

void setup() {
  Wire.begin();
  Serial.begin(115200);
  // Apply soft latch power
  /*pinMode(latch_pin, OUTPUT);

  // Set power switch as input
  pinMode(button_pin, INPUT);*/
  pinMode(led_pin, OUTPUT);
   int j = 5;
  
  // Power down if held 2 seconds
  //while(digitalRead(button_pin) == LOW){
   /* delay(50);
    i++;
    if(i == 40){
      int j =5;
      digitalWrite(latch_pin, HIGH);
      isDeviceOn=true;*/
  while(j){
    digitalWrite(led_pin, HIGH);
    delay(100);
    digitalWrite(led_pin, LOW);;
    delay(100);
    j--;
  }
    digitalWrite(led_pin, HIGH);
    Serial.println("POWER ON!");
      
    
  

  KVP_setup();
  BLE_setup();
  Battery_setup();
}

void loop() {
 /* if (digitalRead(button_pin)==LOW) {
  buttonPressed();
  }*/
  battery_measurement();
  BLE_Measurement();
}
