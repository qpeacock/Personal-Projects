#include <ArduinoBLE.h>
#include <FastLED.h>

#define NUM_LEDS        21
#define DATA_PIN        7
#define LED_TYPE        WS2812B
#define COLOR_ORDER     GRB
#define NUM_SLICES      33
#define TARGET_RPM      240
#define CENTER_LED      10
#define FRAME_TIMEOUT_MS 1000

CRGB leds[NUM_LEDS];

BLEService fanService("12345678-1234-5678-1234-56789abcdef0");
BLECharacteristic rxChar("12345678-1234-5678-1234-56789abcdef1", BLEWrite, 3);
BLECharacteristic txChar("12345678-1234-5678-1234-56789abcdef2", BLERead | BLENotify, 20);

uint8_t frameData[NUM_SLICES][3];
bool frameReady = false;
unsigned long lastReceiveTime = 0;
unsigned long lastSliceTime = 0;
unsigned long sliceDurationUs;
int currentSlice = 0;

void buildFrame(uint8_t bassLit, uint8_t highLit) {
    for (int s = 0; s < NUM_SLICES; s++) {
        frameData[s][0] = 0;
        frameData[s][1] = 0;
        frameData[s][2] = 0;
        for (int i = 0; i < NUM_LEDS; i++) {
            bool on = false;
            if (i == CENTER_LED) {
                on = true;
            } else if (i < CENTER_LED) {
                on = (CENTER_LED - i) <= bassLit;
            } else {
                on = (i - CENTER_LED) <= highLit;
            }
            if (on) {
                frameData[s][i / 8] |= (1 << (i % 8));
            }
        }
    }
}

void displaySlice(int sliceIdx) {
    uint8_t b0 = frameData[sliceIdx][0];
    uint8_t b1 = frameData[sliceIdx][1];
    uint8_t b2 = frameData[sliceIdx][2];
    for (int i = 0; i < NUM_LEDS; i++) {
        bool on;
        if (i < 8)       on = (b0 >> i) & 0x01;
        else if (i < 16) on = (b1 >> (i - 8)) & 0x01;
        else             on = (b2 >> (i - 16)) & 0x01;

        if (on) {
            if (i < CENTER_LED)       leds[i] = CRGB::Red;    // bass side
            else if (i == CENTER_LED) leds[i] = CRGB::Violet;  // center
            else                      leds[i] = CRGB::Blue;  // highs side
        } else {
            leds[i] = CRGB::Black;
        }
    }
    FastLED.show();
}

void setup() {
    Serial.begin(115200);
    while (!Serial) { ; }

    FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(50);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();

    // Clear frame data
    for (int s = 0; s < NUM_SLICES; s++) {
        frameData[s][0] = 0;
        frameData[s][1] = 0;
        frameData[s][2] = 0;
    }

    float periodUs = (60.0f / TARGET_RPM) * 1000000.0f;
    sliceDurationUs = (unsigned long)(periodUs / NUM_SLICES);
    Serial.print("Slice duration (us): ");
    Serial.println(sliceDurationUs);

    if (!BLE.begin()) {
        Serial.println("Starting BLE failed!");
        while (1);
    }

    BLE.setLocalName("UNO_R4_BLE");
    BLE.setAdvertisedService(fanService);
    fanService.addCharacteristic(rxChar);
    fanService.addCharacteristic(txChar);
    BLE.addService(fanService);
    txChar.writeValue("READY");
    BLE.advertise();
    Serial.println("BLE device active, waiting for connections...");
}

void loop() {
    BLEDevice central = BLE.central();

    if (central) {
        Serial.print("Connected: ");
        Serial.println(central.address());
        lastSliceTime = micros();

        while (central.connected()) {

            if (rxChar.written()) {
                int len = rxChar.valueLength();
                if (len == 3) {
                    uint8_t bassLit = rxChar.value()[0];
                    uint8_t highLit = rxChar.value()[1];

                    // Clamp to valid range
                    bassLit = min(bassLit, (uint8_t)10);
                    highLit = min(highLit, (uint8_t)10);

                    Serial.print("Bass: "); Serial.print(bassLit);
                    Serial.print("  Highs: "); Serial.println(highLit);

                    buildFrame(bassLit, highLit);
                    frameReady = true;
                    lastReceiveTime = millis();
                    currentSlice = 0;
                }
            }

            if (frameReady && (millis() - lastReceiveTime > FRAME_TIMEOUT_MS)) {
                frameReady = false;
                fill_solid(leds, NUM_LEDS, CRGB::Black);
                FastLED.show();
                Serial.println("Timed out - LEDs off.");
            }

            unsigned long now = micros();
            if (frameReady && (now - lastSliceTime >= sliceDurationUs)) {
                displaySlice(currentSlice);
                currentSlice = (currentSlice + 1) % NUM_SLICES;
                lastSliceTime = now;
            }

            BLE.poll();
        }

        frameReady = false;
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        Serial.println("Disconnected.");
    }
}