#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEClient.h>
#include <BLE2902.h>
#include <WiFi.h>
#include <time.h>

#define SERVICE_UUID        "12785634-1278-5634-12cd-abef1234abcd"
#define CHARACTERISTIC_UUID "12345678-1234-abcd-ef34-34567890abcd"

const char* ssid = "Wifi2B40";
const char* password = "gbdce2962";

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;

BLEClient* pClient = nullptr;
bool shouldReconnect = false;  
BLEAdvertisedDevice* myDevice = nullptr;  

unsigned long lastTime = 0;
const long interval = 20000;

void connectToServer();
void printLocalTime();

class MySecurity : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() {
        Serial.println("Passkey Request");
        return 123456; // Return the passkey to be used for pairing (for demonstration purposes)
    }

    void onPassKeyNotify(uint32_t passkey) {
        Serial.print("Passkey Notify Number: ");
        Serial.println(passkey);
    }

    bool onConfirmPIN(uint32_t passkey) {
        Serial.print("Confirm PIN: ");
        Serial.println(passkey);
        return true; // Confirm the passkey (for demonstration purposes)
    }

    bool onSecurityRequest() {
        Serial.println("Security Request");
        return true; // Accept the security request
    }

    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
        Serial.println("Authentication Complete");
        if (cmpl.success) {
            Serial.println("Pairing Success");
        } else {
            Serial.println("Pairing Failed");
        }
    }
};


MySecurity *pSecurity = new MySecurity();


class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        Serial.print("BLE Advertised Device found: Name: ");
        Serial.print(advertisedDevice.getName().c_str());
        Serial.print(", Address: ");
        Serial.println(advertisedDevice.getAddress().toString().c_str());

        if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(BLEUUID(SERVICE_UUID))) {
            Serial.println("Found nRF52840 device with matching service UUID, stopping scan to connect.");
            BLEDevice::getScan()->stop();
            myDevice = new BLEAdvertisedDevice(advertisedDevice);
            shouldReconnect = true;  
        }
    }
};

void setup() {
    Serial.begin(115200);

    Serial.println("Connecting to WiFi...");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
     }
     Serial.println("Connected to WiFi");

    // Initialize NTP
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    Serial.println("Starting BLE client application...");
    BLEDevice::init("ESP32_Client");
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_MITM);
    BLEDevice::setSecurityCallbacks(new MySecurity());

    pClient = BLEDevice::createClient();
    Serial.println(" - Created client!");

    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->start(30, false);  
}
void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}
void connectToServer() {
    if (myDevice != nullptr) {
        if (pClient->connect(myDevice)) {
            Serial.println("Connected to the BLE Server.");

            Serial.print("Service UUID: ");
            Serial.println(SERVICE_UUID);

            BLERemoteService* pRemoteService = pClient->getService(BLEUUID(SERVICE_UUID));
            if (pRemoteService == nullptr) {
                Serial.print("Failed to find our service UUID: ");
                Serial.println(SERVICE_UUID);
                return;
            }

            Serial.print("Characteristic UUID: ");
            Serial.println(CHARACTERISTIC_UUID);

            BLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(BLEUUID(CHARACTERISTIC_UUID));
            if (pRemoteCharacteristic == nullptr) {
                Serial.print("Failed to find our characteristic UUID: ");
                Serial.println(CHARACTERISTIC_UUID);
                return;
            }

            if (pRemoteCharacteristic->canRead()) {
                std::string value = pRemoteCharacteristic->readValue();
                time_t now = time(nullptr); // Get current time
                char timestamp[30];
                strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now)); // Format timestamp
                Serial.print("The characteristic value at ");
                Serial.print(timestamp);
                Serial.print(" was: ");
                Serial.println(value.c_str());
            }
        } else {
            Serial.println("Failed to connect, retrying...");
            shouldReconnect = true;
        }
        delete myDevice;  
        myDevice = nullptr;
    }
}
void loop() {
  unsigned long currentTime = millis();
  
  // Check if it's time to perform the action
  if (currentTime - lastTime >= interval) {
    // Ensure we are connected before trying to read
    if (pClient != nullptr && pClient->isConnected()) {
      BLERemoteService* pRemoteService = pClient->getService(BLEUUID(SERVICE_UUID));
      if (pRemoteService != nullptr) {
        BLERemoteCharacteristic* pRemoteCharacteristic = pRemoteService->getCharacteristic(BLEUUID(CHARACTERISTIC_UUID));
        if (pRemoteCharacteristic != nullptr) {
          if (pRemoteCharacteristic->canRead()) {
            std::string value = pRemoteCharacteristic->readValue();
            time_t now = time(nullptr); // Get current time
            char timestamp[30];
            strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now)); // Format timestamp
            Serial.print("The characteristic value at ");
            Serial.print(timestamp);
            Serial.print(" was: ");
            Serial.println(value.c_str());
          }
        }
      }
    } else {
      Serial.println("Device is not connected. Attempting to reconnect...");
      shouldReconnect = true;
    }

    lastTime = currentTime;  // Update the lastTime to the current time after the action is done
  }

  // Attempt to reconnect if needed
  if (shouldReconnect) {
    connectToServer();
    shouldReconnect = false;
  }
}
