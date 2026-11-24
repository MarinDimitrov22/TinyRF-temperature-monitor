#include <TinyRF_13_RX.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

const uint8_t TRANSMITTER_COUNT = 8;
const uint8_t TFT_CS = 10;
const uint8_t TFT_DC = 9;
const uint8_t TFT_RST = 8;

Adafruit_ST7735 display(TFT_CS, TFT_DC, TFT_RST);

bool hasTemperature[TRANSMITTER_COUNT] = {};
bool hasPrinted[TRANSMITTER_COUNT] = {};
int16_t temperatureTenths[TRANSMITTER_COUNT] = {};
uint32_t temperatureReceivedAt[TRANSMITTER_COUNT] = {};
uint32_t lastPrintedAt[TRANSMITTER_COUNT] = {};

void showWaitingScreen()
{
    display.fillScreen(ST77XX_BLACK);
    display.setTextColor(ST77XX_CYAN);
    display.setTextSize(1);
    display.setCursor(8, 12);
    display.print("TinyRF Receiver");
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(8, 42);
    display.print("Waiting for data");
}

void showReadings(uint8_t transmitterId, int16_t temperature, uint16_t batteryCentiVolts)
{
    uint16_t temperatureMagnitude = temperature < 0
        ? (uint16_t)(-(int32_t)temperature)
        : (uint16_t)temperature;

    display.fillScreen(ST77XX_BLACK);
    display.setTextColor(ST77XX_CYAN);
    display.setTextSize(1);
    display.setCursor(8, 8);
    display.print("TinyRF  |  ID ");
    display.print(transmitterId);
    display.drawFastHLine(0, 28, 128, ST77XX_BLUE);

    display.setTextColor(ST77XX_WHITE);
    display.setTextSize(2);
    display.setCursor(8, 42);
    display.print("Temp");
    display.setCursor(8, 64);
    if (temperature < 0)
        display.print('-');
    display.print(temperatureMagnitude / 10);
    display.print('.');
    display.print(temperatureMagnitude % 10);
    display.print(" C");

    display.setCursor(8, 100);
    display.print("Battery");
    display.setCursor(8, 122);
    display.print(batteryCentiVolts / 100);
    display.print('.');
    if (batteryCentiVolts % 100 < 10)
        display.print('0');
    display.print(batteryCentiVolts % 100);
    display.print(" V");
}

void setup()
{
    Serial.begin(9600);
    display.initR(INITR_BLACKTAB);
    display.setRotation(2);
    showWaitingScreen();
    setupReceiver();
}

void loop()
{
    RXMsg msg = getReceivedData();
    if (!msg.dataValid)
        return;

    uint8_t transmitterId = (msg.data >> 12) & 0x07;
    if (msg.data & 0x8000)
    {
        uint8_t batteryAndId = msg.data & 0x00FF;
        uint8_t payloadId = batteryAndId >> 5;
        uint8_t batteryCode = batteryAndId & 0x1F;

        if (payloadId != transmitterId || !hasTemperature[transmitterId])
            return;

        uint32_t now = millis();
        if (now - temperatureReceivedAt[transmitterId] >= 1000UL
            || (hasPrinted[transmitterId]
                && now - lastPrintedAt[transmitterId] < 1000UL))
            return;

        uint16_t batteryCentiVolts = 175 + batteryCode * 5;
        uint16_t temperatureMagnitude = temperatureTenths[transmitterId] < 0
            ? (uint16_t)(-(int32_t)temperatureTenths[transmitterId])
            : (uint16_t)temperatureTenths[transmitterId];

        Serial.print("Transmitter ");
        Serial.print(transmitterId);
        Serial.print(" | Temperature: ");
        if (temperatureTenths[transmitterId] < 0)
            Serial.print('-');
        Serial.print(temperatureMagnitude / 10);
        Serial.print('.');
        Serial.print(temperatureMagnitude % 10);
        Serial.print(" C, Battery: ");
        Serial.print(batteryCentiVolts / 100);
        Serial.print('.');
        if (batteryCentiVolts % 100 < 10)
            Serial.print('0');
        Serial.print(batteryCentiVolts % 100);
        Serial.println(" V");
        showReadings(transmitterId, temperatureTenths[transmitterId], batteryCentiVolts);

        hasPrinted[transmitterId] = true;
        lastPrintedAt[transmitterId] = now;
    }
    else
    {
        uint16_t value = msg.data & 0x0FFF;
        temperatureTenths[transmitterId] = (value & 0x0800)
            ? (int16_t)(value | 0xF000)
            : (int16_t)value;
        temperatureReceivedAt[transmitterId] = millis();
        hasTemperature[transmitterId] = true;
    }
}
