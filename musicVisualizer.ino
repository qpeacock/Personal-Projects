#include <ArduinoBLE.h>
#include <FastLED.h>

#define NUM_LEDS         21
#define CENTER_LED       10
#define DATA_PIN         7
#define LED_TYPE         WS2812B
#define COLOR_ORDER      GRB
#define NUM_SLICES       32
#define TARGET_RPM       200
#define FRAME_TIMEOUT_MS 1000

CRGB leds[NUM_LEDS];

BLEService fanService("12345678-1234-5678-1234-56789abcdef0");
BLECharacteristic rxChar("12345678-1234-5678-1234-56789abcdef1", BLEWrite, 5); // 5-byte payload: bass, mid, color, rpmLow, rpmHigh
BLECharacteristic txChar("12345678-1234-5678-1234-56789abcdef2", BLERead | BLENotify, 20);

uint8_t curBass = 0;
uint8_t curMid  = 0;
uint8_t curColor = 0;
bool frameReady = false;

// Color palettes: [bass color, mid color]
#define NUM_PALETTES 5
const CRGB palettes[NUM_PALETTES][2] = {
    { CRGB::Red,       CRGB::Blue     },
    { CRGB::Green,     CRGB::Purple   },
    { CRGB::Orange,    CRGB::Cyan     },
    { CRGB::HotPink,   CRGB::Lime     },
    { CRGB::Gold,      CRGB(0,191,255) }
};

unsigned long lastReceiveTime = 0;
unsigned long lastSliceTime = 0;
unsigned long sliceDurationUs;
int currentSlice = 0;
unsigned int targetRPM = TARGET_RPM; // default

void displaySlice(int sliceIdx) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    leds[CENTER_LED] = CRGB::Violet;

    bool firstHalf = (sliceIdx < NUM_SLICES / 2);

    uint8_t pidx = min(curColor, (uint8_t)(NUM_PALETTES - 1));
    uint8_t leftArmCount  = firstHalf ? curBass : curMid;
    uint8_t rightArmCount = firstHalf ? curMid  : curBass;
    CRGB leftArmColor     = firstHalf ? palettes[pidx][0] : palettes[pidx][1];
    CRGB rightArmColor    = firstHalf ? palettes[pidx][1] : palettes[pidx][0];

    for (int i = 0; i < leftArmCount && i < CENTER_LED; i++) {
        leds[CENTER_LED - 1 - i] = leftArmColor;
    }
    for (int i = 0; i < rightArmCount && (CENTER_LED + 1 + i) < NUM_LEDS; i++) {
        leds[CENTER_LED + 1 + i] = rightArmColor;
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

    // Initial slice duration
    float periodUs = (60.0f / targetRPM) * 1000000.0f;
    sliceDurationUs = (unsigned long)(periodUs / NUM_SLICES);

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
    Serial.println("BLE active, waiting for connection...");
}

void loop() {
    BLEDevice central = BLE.central();

    if (central) {
        Serial.print("Connected: ");
        Serial.println(central.address());
        lastSliceTime = micros();

        while (central.connected()) {
            // --- Handle BLE writes ---
            if (rxChar.written()) {
                int len = rxChar.valueLength();
                if (len >= 5) { // payload: bass, mid, color, rpmLow, rpmHigh
                    curBass  = min(rxChar.value()[0], (uint8_t)10);
                    curMid   = min(rxChar.value()[1], (uint8_t)10);
                    curColor = min(rxChar.value()[2], (uint8_t)(NUM_PALETTES - 1));

                    // Read RPM from last 2 bytes
                    targetRPM = rxChar.value()[3] | (rxChar.value()[4] << 8);
                    if (targetRPM < 1) targetRPM = TARGET_RPM;

                    // Recalculate slice duration
                    float periodUs = (60.0f / targetRPM) * 1000000.0f;
                    sliceDurationUs = (unsigned long)(periodUs / NUM_SLICES);

                    frameReady = true;
                    lastReceiveTime = millis();
                }
            }

            // Timeout: turn off if no data for a while
            if (frameReady && (millis() - lastReceiveTime > FRAME_TIMEOUT_MS)) {
                frameReady = false;
                fill_solid(leds, NUM_LEDS, CRGB::Black);
                FastLED.show();
            }

            // Advance through slices at current RPM
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