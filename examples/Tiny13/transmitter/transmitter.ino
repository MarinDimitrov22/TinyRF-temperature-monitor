#include <TinyRF_13_TX.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

const uint8_t SENSOR_POWER_PIN = 3;
const uint8_t SENSOR_DATA_PIN = 4;
const uint8_t TRANSMITTER_POWER_PIN = 1;
const uint8_t TRANSMITTER_ID = 1;

ISR(WDT_vect)
{
}

void sleepForTwoSeconds()
{
    cli();
    wdt_reset();
    MCUSR &= ~_BV(WDRF);
    WDTCR = _BV(WDCE) | _BV(WDE) | _BV(WDTIF);
    WDTCR = _BV(WDTIE) | _BV(WDP2) | _BV(WDP1) | _BV(WDP0);

    set_sleep_mode(SLEEP_MODE_PWR_DOWN);
    sleep_enable();
    sei();
    sleep_cpu();
    sleep_disable();

    cli();
    WDTCR = _BV(WDCE) | _BV(WDE);
    WDTCR = 0;
    sei();
}

void oneWireLow()
{
    DDRB |= _BV(DDB4);
    PORTB &= ~_BV(PORTB4);
}

void oneWireRelease()
{
    PORTB &= ~_BV(PORTB4);
    DDRB &= ~_BV(DDB4);
}

bool oneWireReset()
{
    oneWireLow();
    delayMicroseconds(480);
    oneWireRelease();
    delayMicroseconds(70);
    bool present = !(PINB & _BV(PINB4));
    delayMicroseconds(410);
    return present;
}

void oneWireWriteBit(uint8_t bitValue)
{
    oneWireLow();
    if (bitValue)
    {
        delayMicroseconds(6);
        oneWireRelease();
        delayMicroseconds(64);
    }
    else
    {
        delayMicroseconds(60);
        oneWireRelease();
        delayMicroseconds(10);
    }
}

uint8_t oneWireReadBit()
{
    oneWireLow();
    delayMicroseconds(6);
    oneWireRelease();
    delayMicroseconds(9);
    uint8_t bitValue = (PINB & _BV(PINB4)) ? 1 : 0;
    delayMicroseconds(55);
    return bitValue;
}

void oneWireWriteByte(uint8_t value)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        oneWireWriteBit(value & 1);
        value >>= 1;
    }
}

uint8_t oneWireReadByte()
{
    uint8_t value = 0;
    for (uint8_t i = 0; i < 8; i++)
    {
        value |= oneWireReadBit() << i;
    }
    return value;
}

uint8_t scratchpadCrc(const uint8_t *data)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < 8; i++)
    {
        uint8_t value = data[i];
        for (uint8_t bit = 0; bit < 8; bit++)
        {
            uint8_t mix = (crc ^ value) & 1;
            crc >>= 1;
            if (mix)
                crc ^= 0x8C;
            value >>= 1;
        }
    }
    return crc;
}

bool readTemperature(int16_t *temperatureTenths)
{
    digitalWrite(SENSOR_POWER_PIN, HIGH);
    delay(10);

    bool success = oneWireReset();
    if (success)
    {
        oneWireWriteByte(0xCC);
        oneWireWriteByte(0x44);
        delay(750);
        success = oneWireReset();
    }

    if (success)
    {
        oneWireWriteByte(0xCC);
        oneWireWriteByte(0xBE);

        uint8_t scratchpad[9];
        for (uint8_t i = 0; i < 9; i++)
        {
            scratchpad[i] = oneWireReadByte();
        }
        success = scratchpadCrc(scratchpad) == scratchpad[8];
        if (success)
        {
            int16_t raw = (int16_t)(((uint16_t)scratchpad[1] << 8) | scratchpad[0]);
            *temperatureTenths = (raw * 10) / 16;
        }
    }

    digitalWrite(SENSOR_POWER_PIN, LOW);
    return success;
}

uint16_t readBatteryCentiVolts()
{
    ADMUX = _BV(REFS0) | _BV(MUX0);
    ADCSRA = _BV(ADEN) | _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0);
    delay(2);

    ADCSRA |= _BV(ADSC);
    while (ADCSRA & _BV(ADSC))
    {
    }
    uint16_t adc = ADC;

    ADCSRA = 0;
    ADMUX = 0;

    return (uint16_t)((adc * 11U + 16U) >> 5);
}

uint8_t encodeBatteryLevel(uint16_t centiVolts)
{
    if (centiVolts <= 175)
        return 0;
    if (centiVolts >= 330)
        return 31;
    return (centiVolts - 175 + 2) / 5;
}

void setup()
{
    DDRB |= _BV(DDB1);
    DDRB &= ~_BV(DDB2);
    PORTB &= ~(_BV(PORTB1) | _BV(PORTB2));
    DIDR0 |= _BV(ADC1D);
    pinMode(SENSOR_POWER_PIN, OUTPUT);
    digitalWrite(SENSOR_POWER_PIN, LOW);
    pinMode(SENSOR_DATA_PIN, INPUT);
    pinMode(TRANSMITTER_POWER_PIN, OUTPUT);
    digitalWrite(TRANSMITTER_POWER_PIN, LOW);
    setupTransmitter();
}

void loop()
{
    int16_t temperatureTenths;
    if (readTemperature(&temperatureTenths))
    {
        digitalWrite(TRANSMITTER_POWER_PIN, HIGH);
        delay(10);
        uint16_t batteryCentiVolts = readBatteryCentiVolts();
        uint16_t temperaturePacket = ((uint16_t)TRANSMITTER_ID << 12)
            | ((uint16_t)temperatureTenths & 0x0FFF);
        uint8_t batteryAndId = (TRANSMITTER_ID << 5)
            | encodeBatteryLevel(batteryCentiVolts);
        uint16_t batteryPacket = 0x8000
            | ((uint16_t)TRANSMITTER_ID << 12)
            | batteryAndId;

        for (uint8_t i = 0; i < 3; i++)
        {
            send(temperaturePacket);
            delay(5);
            send(batteryPacket);
            delay(5);
        }
        digitalWrite(TRANSMITTER_POWER_PIN, LOW);
    }
    for (uint8_t i = 0; i < 10; i++)
    {
        sleepForTwoSeconds();
    }
}
