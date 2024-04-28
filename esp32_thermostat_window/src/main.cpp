#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEClient.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define ROOM_TEMP_CHARACTERISTIC_UUID    "11111111-1111-1111-1111-111111111111"
#define HUMIDITY_CHARACTERISTIC_UUID     "22222222-2222-2222-2222-222222222222"
#define THERMOSTAT_STATE_CHARACTERISTIC_UUID "33333333-3333-3333-3333-333333333333"
#define DESIRED_TEMP_CHARACTERISTIC_UUID "66666666-6666-6666-6666-666666666666"
#define WINDOW_STATUS_CHARACTERISTIC_UUID "55555555-5555-5555-5555-555555555555"
#define SERVICE_UUID "12785634-1278-5634-12cd-abef1234abcd"

bool deviceFound = false;
BLEScan* pBLEScan = nullptr;
BLEClient* pClient = nullptr;
BLEAdvertisedDevice* myDevice = nullptr;
bool deviceConnected = false;
bool isCurrentlyScanning = false;
// Declaration for an SH1106 display connected to I2C (SDA, SCL pins)
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

float roomTemp = 0.0;
float humidity = 0.0;
bool thermostatState = false;  // 0 for OFF, 1 for ON 
bool windowOpen = false;  // 0 for Closed, 1 for Open
float desiredTemp = 0.0; //dummy value for user determined temp, which comes from the backend
float heatingTemp = 25.7; //dummy value for the radiator temp

unsigned long lastUpdateTime = 0; // Store the last update time
const long updateInterval = 2000; // Set the desired update interval (20 seconds)

bool connectToServer(BLEAddress serverAddress);
void updateDisplay(float roomTemp, float humidity, bool thermostatState, float outsideTemp, bool windowOpen);
void startBLEScan();

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) override {
        if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(BLEUUID(SERVICE_UUID))) {
            Serial.println("Found device with matching service UUID!");
            myDevice = new BLEAdvertisedDevice(advertisedDevice);
            deviceFound = true;
            isCurrentlyScanning = false;  // Scanning is no longer in progress since the device is found
            BLEDevice::getScan()->stop();
        }
    }
};
class MyClientSecurity : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override {
        Serial.println("Client Passkey Request");
        return 123456;  // Same passkey as server
    }

    void onPassKeyNotify(uint32_t passkey) override {
        Serial.print("Client Passkey Notify: ");
        Serial.println(passkey);
    }

    bool onConfirmPIN(uint32_t passkey) override {
        Serial.print("Confirm PIN: ");
        Serial.println(passkey);
        return true; // Always confirm the passkey
    }

    bool onSecurityRequest() override {
        Serial.println("Security Request from Client");
        return true; // Always accept the security request
    }

    void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
        Serial.println("Client Authentication Complete");
        if (cmpl.success) {
            Serial.println("Client Pairing Success");
        } else {
            Serial.print("Client Pairing Failed, Reason: ");
            Serial.println(cmpl.fail_reason);
        }
    }
};

void setup() {
    Serial.begin(115200);
    Serial.print("starting system");
    // Initialize the display
    if (!display.begin()) {
        Serial.println(F("SH1106 allocation failed"));
        for (;;); // Don't proceed, loop forever
    }
    
    display.clearDisplay();
    delay(50);
    display.setRotation(0); 
    BLEDevice::init("ESP32_Thermostat_client");
    Serial.println("started the client");
    BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
    BLEDevice::setSecurityCallbacks(new MyClientSecurity());
    BLESecurity *pSecurity = new BLESecurity();
    pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
    pSecurity->setCapability(ESP_IO_CAP_IO);
    pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    pBLEScan = BLEDevice::getScan();  // Get the BLE scanner
    Serial.println("scanning");
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);  // Active scan uses more power, but gets results faster
    pBLEScan->start(30, false);  // Start scanning for 5 seconds
    Serial.println("done scanning");
}

void updateDisplay(float roomTemp, float humidity, bool thermostatState, float desiredTemp, bool windowOpen) {
  display.clearDisplay();  // Clear the display for fresh updating
  display.setTextSize(1);  // Normal text size
  display.setTextColor(1); // White
  // Room Temperature
  display.setCursor(0, 0); // Top-left corner
  display.print("Temp: ");
  display.print(roomTemp, 1);
  display.println("C");
  // Humidity
  display.setCursor(0, 10); // Slightly below the room temperature
  display.print("Humidity: ");
  display.print(humidity, 1);
  display.println("%");
  // Thermostat Status - Larger font for emphasis
  display.setTextSize(2);
  display.setCursor(0, 20);
  // Ensure temperature is printed before the thermostat state
  if (thermostatState) {
      display.print("ON ");
      display.print(heatingTemp, 1); // Display the temperature it's heating to
      display.println("C");
  } else {
      display.print("OFF"); // Just display "OFF" if the thermostat is not active
  }
  // Restore text size for additional info
  display.setTextSize(1);
  // user set Temperature
  display.setCursor(0, 40); // Below the thermostat status
  display.print("Set Temp: ");
  display.print(desiredTemp, 1);
  display.println("C");

  // Window Status
  display.setCursor(0, 50); // At the bottom of the display
  display.print("Window: ");
  display.println(windowOpen ? "Open" : "Closed");

  // Display everything on the screen
  display.display();
}

bool connectToServer(BLEAddress serverAddress) {
    pClient = BLEDevice::createClient();
    Serial.println(" - Created client");

    // Connect to the BLE Server
    if (pClient->connect(serverAddress)) {
        Serial.println(" - Connected to server");

        // Discover the remote service
        BLERemoteService* pRemoteService = pClient->getService(BLEUUID(SERVICE_UUID));
        if (pRemoteService == nullptr) {
            Serial.println("Failed to find our service UUID on the connected device.");
            return false;
        }

        // Attempt to discover descriptors after finding the service
        if (!pRemoteService->getCharacteristics()) {
            Serial.println("Failed to find characteristics for the service.");
            return false;
        }

        return true;
    } else {
        Serial.println(" - Failed to connect");
        return false;
    }
}
void fetchAndUpdateCharacteristicValues(BLEClient* pClient) {
    if (pClient->isConnected()) {
        BLERemoteService* pRemoteService = pClient->getService(BLEUUID(SERVICE_UUID));
        if (pRemoteService == nullptr) {
            Serial.println("Failed to find our service UUID");
            return;
        }
        // Room Temperature
        auto pRoomTempCharacteristic = pRemoteService->getCharacteristic(BLEUUID(ROOM_TEMP_CHARACTERISTIC_UUID));
        if (pRoomTempCharacteristic && pRoomTempCharacteristic->canRead()) {
            roomTemp = atof(pRoomTempCharacteristic->readValue().c_str());
            Serial.print("Room Temp: "); Serial.println(roomTemp, 1);
        }

        // Humidity
        auto pHumidityCharacteristic = pRemoteService->getCharacteristic(BLEUUID(HUMIDITY_CHARACTERISTIC_UUID));
        if (pHumidityCharacteristic && pHumidityCharacteristic->canRead()) {
            humidity = atof(pHumidityCharacteristic->readValue().c_str());
            Serial.print("Humidity: "); Serial.println(humidity, 1);
        }
       // Fetch and handle thermostat state
        auto pThermostatStateCharacteristic = pRemoteService->getCharacteristic(BLEUUID(THERMOSTAT_STATE_CHARACTERISTIC_UUID));
        if (pThermostatStateCharacteristic && pThermostatStateCharacteristic->canRead()) {
            String thermostatValue = pThermostatStateCharacteristic->readValue().c_str();
            thermostatState = (thermostatValue.equalsIgnoreCase("On"));  // Using equalsIgnoreCase for robust comparison
            Serial.print("Thermostat State: "); Serial.println(thermostatState ? "On" : "Off");
        }

        // Desired Temperature
        auto pDesiredTempCharacteristic = pRemoteService->getCharacteristic(BLEUUID(DESIRED_TEMP_CHARACTERISTIC_UUID));
        if (pDesiredTempCharacteristic && pDesiredTempCharacteristic->canRead()) {
            desiredTemp = atof(pDesiredTempCharacteristic->readValue().c_str());
            Serial.print("Desired Temp: "); Serial.println(desiredTemp, 1);
        }

        // Window Status
        auto pWindowStatusCharacteristic = pRemoteService->getCharacteristic(BLEUUID(WINDOW_STATUS_CHARACTERISTIC_UUID));
        if (pWindowStatusCharacteristic && pWindowStatusCharacteristic->canRead()) {
            String windowValue = pWindowStatusCharacteristic->readValue().c_str();
            // Correctly interpret "Open" and "Closed" values
            windowOpen = windowValue.equalsIgnoreCase("Open");  // Properly handle "Open" as true
            Serial.print("Window Status: "); Serial.println(windowOpen ? "Open" : "Closed");
        }

        // Logic for controlling the thermostat based on window status
        // if (windowOpen || roomTemp >= desiredTemp) {
        //     thermostatState = false;  // Turn off the thermostat if the window is open or the room is warm enough
        // } else {
        //     thermostatState = true;  // Turn on the thermostat if the room is colder than the desired temperature
        // }

        // Update the display with the latest values
        updateDisplay(roomTemp, humidity, thermostatState, desiredTemp, windowOpen);
    } else {
        Serial.println("Not connected to a server.");
    }
}



void startBLEScan() {
    if (!deviceFound && !isCurrentlyScanning) {  // Only start scanning if not already scanning and device not found
        isCurrentlyScanning = true;  // Set the flag to indicate scanning is in progress
        pBLEScan->start(5, false);  // Scan for 5 seconds
    }
}

void loop() {


    
    unsigned long currentTime = millis();

    // Check if the device has been found and we are connected
    if (deviceConnected) {
        // Fetch and update characteristic values every 20 seconds
        if (currentTime - lastUpdateTime >= updateInterval) {
            fetchAndUpdateCharacteristicValues(pClient);
            lastUpdateTime = currentTime; // Update the last update time
        }
    } else {
        // If not connected, try to find the device and connect
        if (deviceFound) {
            if (connectToServer(myDevice->getAddress())) {
                deviceConnected = true; // Set flag to true after successful connection
                deviceFound = false; // Reset flag after attempting to connect
                isCurrentlyScanning = false; // Indicate that scanning is no longer neededj
                lastUpdateTime = currentTime; // Reset the timer after connecting
            }
        } else {
            // Restart scanning if not currently scanning
            if (!isCurrentlyScanning) {
                startBLEScan();
                isCurrentlyScanning = true; // Set the flag to indicate scanning has started
            }
        }
    }

    // Use non-blocking delay logic to allow other operations
    delay(100); // Short delay to keep the loop responsive
}