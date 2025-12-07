#include "BLE_Manager.h"
#include "Json_Handler.h"
#include "Display_Manager.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

extern String myJson;  // ← 追加: turnie_device.inoのmyJsonを参照

#define SERVICE_UUID "12345678-1234-1234-1234-1234567890ab"
#define RX_UUID "abcd1234-5678-90ab-cdef-1234567890ab"
#define TX_UUID "abcd1234-5678-90ab-cdef-1234567890ac"

BLECharacteristic *pTxCharacteristic;
// pending queue for file writes processed on main loop
static volatile bool pendingJsonReady = false;
static String pendingJson;

class WriteCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    auto tmp = c->getValue();
    String rxValue(tmp.c_str());
    if (rxValue.isEmpty()) return;

    static String buffer = "";
    // Append incoming bytes (Arduino String)
    buffer += rxValue;

    // When a JSON ends with '}', save it to LittleFS and update display state
    if (buffer.endsWith("}")) {
      // Queue pending write — do actual FS write from main loop via BLE_Tick
      pendingJson = buffer;
      pendingJsonReady = true;
      // Optionally notify client
      if (pTxCharacteristic) {
        pTxCharacteristic->setValue("saved");
        pTxCharacteristic->notify();
      }
      buffer = "";
    }
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    Serial.println("[BLE] Device connected");
  }

  void onDisconnect(BLEServer* pServer) override {
    Serial.println("[BLE] Device disconnected");
    // Restart advertising so others can connect
    pServer->startAdvertising(); 
    Serial.println("[BLE] Advertising restarted");
  }
};

void BLE_Init() {
  BLEDevice::init("turnie 4");
  Serial.printf("[BLE] init: device name=turnie 2\n");

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks()); // Register callbacks

  BLEService *service = server->createService(SERVICE_UUID);

  auto rx = service->createCharacteristic(
      RX_UUID, BLECharacteristic::PROPERTY_WRITE);
  rx->setCallbacks(new WriteCallback());

  pTxCharacteristic = service->createCharacteristic(
      TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());

  service->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->start();
  Serial.println("[BLE] advertising started");
}

// (no duplicate declarations below)

void BLE_Tick() {
  if (!pendingJsonReady) return;
  
  String js = pendingJson;
  pendingJsonReady = false;

  if (!saveJsonToPath("/data.json", js)) {
    Serial.println("[BLE] failed to write /data.json");
    return;
  }
  Serial.println("[BLE] saved /data.json");

  myJson = js;  // ← 追加: グローバル変数を更新
  Serial.println("[BLE] updated myJson");

  loadDisplayFromLittleFS();
  if (!performDisplay()) {
    Serial.println("[BLE] performDisplay: nothing to display");
  }
}
