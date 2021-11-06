#include <Arduino.h>
#include <U8g2lib.h>
#include <EasyButton.h>
#include <FreqCount/FreqCount.h>

static const byte MODE_BUTTON_PIN = 3;
static const byte CAL_BUTTON_PIN = 2;
static const byte MODE_RELAY_PIN = 12;
static const byte CAL_RELAY_PIN = 13;

U8G2_SSD1306_128X64_NONAME_F_4W_SW_SPI display(U8G2_R0, /* clock=*/4, /* data=*/6, /* cs=*/9, /* dc=*/8, /* reset=*/7);

EasyButton modeButton(MODE_BUTTON_PIN);
EasyButton calButton(CAL_BUTTON_PIN);

void setup()
{
    Serial.begin(9600);
    while (!Serial)
        delay(10);

    Serial.println("Setup");
    pinMode(MODE_BUTTON_PIN, INPUT_PULLUP);
    pinMode(CAL_BUTTON_PIN, INPUT_PULLUP);
    pinMode(MODE_RELAY_PIN, OUTPUT);
    pinMode(CAL_RELAY_PIN, OUTPUT);
    digitalWrite(MODE_RELAY_PIN, LOW);
    digitalWrite(CAL_RELAY_PIN, LOW);
    display.begin();
    FreqCount.begin(1000);
}

void loop()
{
    modeButton.read();
    calButton.read();

    if (FreqCount.available())
    {

        display.clearBuffer();                 // clear the internal memory
        display.setFont(u8g2_font_ncenB08_tr); // choose a suitable font
        unsigned long count = FreqCount.read();
        display.drawStr(0, 10, "Frequency (Hz)");
        char buffer[10];
        sprintf(buffer, "%lu", count);
        display.drawStr(0, 30, buffer);


        sprintf(buffer, "%lu", (long)(0.99920254955 * count));

        display.drawStr(0, 60, buffer);
        Serial.println(count);
    }

    display.sendBuffer(); // transfer internal memory to the display
    delay(1000);
    //Serial.println("Loop");
}
