#pragma once
// buddy_ble.h — BLE Nordic UART Service (NUS) bridge for T-Keyboard-S3-Pro Buddy.
// Identical wire protocol to claude-desktop-buddy's ble_bridge.h/cpp, adapted
// to compile as a single header for an Arduino sketch (no separate .cpp).
//
// Service UUID  6e400001-b5a3-f393-e0a9-e50e24dcca9e
// RX char       6e400002-...  (desktop → device, WRITE)
// TX char       6e400003-...  (device → desktop, NOTIFY)
//
// After bleInit(), call bleLoopTick() every loop() to pump the BLE stack.
// Incoming bytes are buffered in a ring; read with bleAvailable()/bleRead().
// Send with bleWrite().

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define BLE_NUS_SVC_UUID  "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_NUS_RX_UUID   "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define BLE_NUS_TX_UUID   "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// Ring buffer — 1 KB, power-of-two for cheap masking.
static constexpr size_t BLE_RX_BUF = 1024;
static uint8_t  _bleRxBuf[BLE_RX_BUF];
static uint16_t _bleRxHead = 0;
static uint16_t _bleRxTail = 0;

static BLEServer*          _bleSrv     = nullptr;
static BLECharacteristic*  _bleTxChar  = nullptr;
static bool                _bleConnected = false;

// ---- BLE callbacks --------------------------------------------------------
class _BuddySrvCb : public BLEServerCallbacks {
    void onConnect(BLEServer*) override    { _bleConnected = true;  }
    void onDisconnect(BLEServer* s) override {
        _bleConnected = false;
        s->startAdvertising();
    }
};

class _BuddyRxCb : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* c) override {
        String v = c->getValue();
        for (int i = 0; i < (int)v.length(); i++) {
            char ch = v[i];
            uint16_t next = (_bleRxHead + 1) & (BLE_RX_BUF - 1);
            if (next != _bleRxTail) {         // drop on overflow
                _bleRxBuf[_bleRxHead] = (uint8_t)ch;
                _bleRxHead = next;
            }
        }
    }
};

// ---- Public API -----------------------------------------------------------
inline void bleInit(const char* deviceName) {
    BLEDevice::init(deviceName);
    _bleSrv = BLEDevice::createServer();
    _bleSrv->setCallbacks(new _BuddySrvCb());

    BLEService* svc = _bleSrv->createService(BLE_NUS_SVC_UUID);

    BLECharacteristic* rxChar = svc->createCharacteristic(
        BLE_NUS_RX_UUID, BLECharacteristic::PROPERTY_WRITE);
    rxChar->setCallbacks(new _BuddyRxCb());

    _bleTxChar = svc->createCharacteristic(
        BLE_NUS_TX_UUID,
        BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
    // BLE2902 (CCCD) is added automatically by the NimBLE stack when NOTIFY
    // is set; adding it manually triggers a deprecation warning on core 3.x.

    svc->start();

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(BLE_NUS_SVC_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();
}

inline bool bleConnected() { return _bleConnected; }

inline size_t bleAvailable() {
    return (_bleRxHead - _bleRxTail) & (BLE_RX_BUF - 1);
}

inline int bleRead() {
    if (_bleRxHead == _bleRxTail) return -1;
    uint8_t c = _bleRxBuf[_bleRxTail];
    _bleRxTail = (_bleRxTail + 1) & (BLE_RX_BUF - 1);
    return c;
}

inline size_t bleWrite(const uint8_t* data, size_t len) {
    if (!_bleConnected || !_bleTxChar) return 0;
    // NUS MTU is typically 20 bytes; chunk accordingly.
    size_t sent = 0;
    while (sent < len) {
        size_t chunk = min((size_t)20, len - sent);
        _bleTxChar->setValue(const_cast<uint8_t*>(data + sent), chunk);
        _bleTxChar->notify();
        sent += chunk;
        delay(10);  // give the stack time to flush
    }
    return sent;
}

inline size_t bleWriteStr(const char* s) {
    return bleWrite((const uint8_t*)s, strlen(s));
}
