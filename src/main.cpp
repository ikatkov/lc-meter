#include <Arduino.h>
#include <U8g2lib.h>
#include <EasyButton.h>
#include <FreqCount/FreqCount.h>

//internal states
static const byte C_MEASURING_STATE = 1;
static const byte L_MEASURING_STATE = 2;

byte internalState;

//this correction is specific to the arduino board instance, use a lab tool to measure
static const float XTAL_FREQ_COMPENSATION = 0.9992275218;

const unsigned long MEASURE_INTERVAL = 1000;
const float FOUR_PI_SQRD = 39.4784176044; //(2Pi)^2
static const float C2_VALUE = 1180E-12;

static const byte MODE_BUTTON_PIN = 2;
static const byte CAL_BUTTON_PIN = 3;
static const byte MODE_RELAY_PIN = 12;
static const byte CAL_RELAY_PIN = 11;

U8G2_SSD1306_128X64_NONAME_1_4W_SW_SPI display(U8G2_R0, /* clock=*/4, /* data=*/6, /* cs=*/9, /* dc=*/8, /* reset=*/7);

EasyButton modeButton(MODE_BUTTON_PIN);
EasyButton calButton(CAL_BUTTON_PIN);

unsigned long lastMeasureTime;
byte measurmentCounter;
byte progressBar;
byte state;
float C1;
float C3;
float C3_prev;
float L1;
float L3;
float L3_prev;
float F1;
float F2;
float F3;

void onModeButtonPressed();
void onCalButtonPressed();

void displayCalibrationScreen()
{
    display.firstPage();
    do
    {
        display.setDrawColor(1);
        display.drawRBox(34, 2, 60, 60, 3);

        display.setDrawColor(0);
        display.drawRBox(36, 4, 56, 46, 3);
        display.setFont(u8g2_font_7x13_mr);
        display.drawStr(37, 59, "CALIBRATION");

        display.setDrawColor(1);
        display.drawDisc(64, 26, 16, U8G2_DRAW_ALL);
        display.setDrawColor(0);
        display.drawDisc(64, 26, 14, U8G2_DRAW_ALL);
        display.setDrawColor(1);
        display.drawDisc(64, 26, 11, U8G2_DRAW_ALL);

        display.setDrawColor(1);
        display.drawRBox(63, 7, 3, 39, 1);
        display.drawRBox(44, 26, 40, 3, 1);

        display.setDrawColor(0);
        display.drawRBox(63, 17, 2, 19, 1);
        display.drawRBox(54, 26, 20, 2, 1);
    } while (display.nextPage());
}

void formatCapacitorValue(float value, char *buffer)
{
    if (value < 1E-12)
    {
        sprintf(buffer, "........");
    }
    else if (value < 1E-9)
    {
        //pico
        sprintf(buffer, "%.2fpF", value * 1E12);
    }
    else if (value < 1E-6)
    {
        //nano
        sprintf(buffer, "%.2fnF", value * 1E9);
    }
    else if (value < 1E-3)
    {
        //micro
        sprintf(buffer, "%.2fuF", value * 1E6);
    }
    else
    {
        sprintf(buffer, "XXX.XX");
    }
}

void formatInductorValue(float value, char *buffer)
{
    if (value < 1E-9)
    {
        //pico
        sprintf(buffer, "........");
    }
    else if (value < 1E-6)
    {
        //nano
        sprintf(buffer, "%02dnH", (int)round(value * 1E9));
    }
    else if (value < 1E-3)
    {
        //micro
        sprintf(buffer, "%02duH", (int)round(value * 1E6));
    }
    else if (value < 1)
    {
        //milli
        sprintf(buffer, "%02dmH", (int)round(value * 1E3));
    }
    else
    {
        sprintf(buffer, "XXX.XX");
    }
}

void formatFrequency(float value, char *buffer)
{
    sprintf(buffer, "%.3fkHz", (double)value / 1000);
}

void drawInternalValuesDisplay()
{
    display.firstPage();
    do
    {
        display.setDrawColor(1);
        display.setFont(u8g2_font_7x13_mr);
        char buffer[10];
        formatFrequency(F1, buffer);
        display.drawStr(30, 16, buffer);

        // small capacitor
        display.drawBox(17, 35, 13, 2); // --
        display.drawBox(39, 35, 13, 2); // --
        display.drawBox(30, 27, 2, 18); // |
        display.drawBox(37, 27, 2, 18); // |

        // small inductor
        display.drawBox(65, 35, 12, 2); // --
        display.drawCircle(80, 36, 4, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
        display.drawCircle(88, 36, 4, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
        display.drawCircle(96, 36, 4, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
        display.drawBox(100, 35, 12, 2); // --

        //internal values
        display.setFont(u8g2_font_7x13_mr);
        formatCapacitorValue(C1, buffer);
        display.drawStr(14, 60, buffer);

        formatInductorValue(L1, buffer);
        display.drawStr(72, 60, buffer);
    } while (display.nextPage());
}

void drawScreen()
{
    display.clearDisplay();
    char buffer[10];
    if (state == C_MEASURING_STATE)
    {
        display.firstPage();
        do
        {
            display.setDrawColor(1);
            //capacitor symbol
            display.drawBox(27, 45, 13, 2); // --
            display.drawBox(49, 45, 13, 2); // --
            display.drawBox(40, 37, 2, 18); // |
            display.drawBox(47, 37, 2, 18); // |

            //top value
            display.setFont(u8g2_font_profont29_mr);
            formatCapacitorValue(C3, buffer);
            display.drawStr(0, 25, buffer);

            //formatFrequency(F3, buffer);
            if (C3_prev != 0)
            {
                display.setFont(u8g2_font_7x13_mr);
                sprintf(buffer, "%+.3f%%", (double)((C3 - C3_prev) / C3_prev * 100));
                display.drawStr(75, 60, buffer);
            }
            //draw progress dots
            display.setDrawColor(1);
            for (int x = 0; x < measurmentCounter; x++)
            {
                display.drawDisc(10 + x * 10, 60, 3, U8G2_DRAW_ALL);
            }

        } while (display.nextPage());
    }
    else if (state == L_MEASURING_STATE)
    {
        display.firstPage();
        do
        {
            display.setDrawColor(1);
            //inductor symbol
            display.drawBox(7, 45, 12, 2); // --
            display.drawCircle(22, 46, 4, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
            display.drawCircle(30, 46, 4, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
            display.drawCircle(38, 46, 4, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_UPPER_RIGHT);
            display.drawBox(43, 45, 12, 2); // --

            //top value
            display.setFont(u8g2_font_profont29_mr);
            formatInductorValue(L3, buffer);
            display.drawStr(0, 25, buffer);

            if (L3_prev != 0)
            {
                display.setFont(u8g2_font_7x13_mr);
                sprintf(buffer, "%+.3f%%", (double)((L3 - L3_prev) / L3_prev * 100));
                display.drawStr(75, 60, buffer);
            }

            //draw progress dots
            display.setDrawColor(1);
            for (int x = 0; x < measurmentCounter; x++)
            {
                display.drawDisc(10 + x * 10, 60, 3, U8G2_DRAW_ALL);
            }

        } while (display.nextPage());
    }
}

unsigned long getFrequency()
{
    FreqCount.begin(250);
    while (!FreqCount.available())
    {
    }
    FreqCount.read(); //throw away
    while (!FreqCount.available())
    {
    }
    unsigned long freq = 4 * FreqCount.read() * XTAL_FREQ_COMPENSATION;
    FreqCount.end();
    return freq;
}

void calibrate()
{
    Serial.println(F("calibrate"));
    displayCalibrationScreen();
    digitalWrite(CAL_RELAY_PIN, LOW);
    delay(100);
    F1 = getFrequency();
    Serial.print(F("Freq1 (Hz): "));
    Serial.println(F1);

    digitalWrite(CAL_RELAY_PIN, HIGH);
    delay(100);
    F2 = getFrequency();
    Serial.print(F("Freq2 (Hz): "));
    Serial.println(F2);
    digitalWrite(CAL_RELAY_PIN, LOW);
    //calculate C and L

    C1 = (C2_VALUE * F2 * F2) / (F1 * F1 - F2 * F2); //calculate C1 in F

    Serial.print(F("Internal C1: "));
    Serial.println(C1 * 1E12);

    L1 = 1 / (F1 * F1 * C1 * FOUR_PI_SQRD);
    Serial.print(F("Internal L1: "));
    Serial.println(L1 * 1E6);
}

void measureC()
{
    Serial.println(F("measureC"));
    F3 = getFrequency();
    Serial.print(F("Freq3 (Hz): "));
    Serial.println(F3);

    //calculate C3
    float oldC3 = C3;
    C3 = C1 * ((F1 * F1) / (F3 * F3) - 1); //calculate C3 in F
    if (abs(oldC3 - C3) / oldC3 < 0.05)
    {
        //stable value, less than 5% delta
        C3_prev = oldC3;
    }
    Serial.print(F("C3: "));
    char buffer[10];
    formatCapacitorValue(C3, buffer);
    Serial.println(buffer);
}

void measureL()
{
    Serial.println(F("measureL"));
    F3 = getFrequency();
    Serial.print(F("Freq3 (Hz): "));
    Serial.println(F3);

    //calculate L3
    float oldL3 = L3;
    L3 = L1 * ((F1 * F1) / (F3 * F3) - 1); //calculate L3 in H
    if (abs(oldL3 - L3) / oldL3 < 0.05)
    {
        //stable value, less than 5% delta
        L3_prev = oldL3;
    }
    Serial.print(F("L3: "));
    char buffer[10];
    formatInductorValue(L3, buffer);
    Serial.println(buffer);
}

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

    modeButton.onPressed(onModeButtonPressed);
    calButton.onPressed(onCalButtonPressed);

    calibrate();
    drawInternalValuesDisplay();
    delay(1000);
    state = C_MEASURING_STATE;
}

void loop()
{
    modeButton.read();
    calButton.read();

    if (millis() - lastMeasureTime > MEASURE_INTERVAL)
    {
        measurmentCounter = measurmentCounter + 1 % 10;
        if (state == C_MEASURING_STATE)
        {
            measureC();
        }
        else if (state == L_MEASURING_STATE)
        {
            measureL();
        }
        drawScreen();
        lastMeasureTime = millis();
    }
}

void onModeButtonPressed()
{
    Serial.println(F("onModeButtonPressed"));
    if (state == C_MEASURING_STATE)
    {
        digitalWrite(MODE_RELAY_PIN, LOW);
        delay(100);
        state = L_MEASURING_STATE;
        calibrate();
        drawInternalValuesDisplay();
        drawScreen();
    }
    else if (state == L_MEASURING_STATE)
    {
        digitalWrite(MODE_RELAY_PIN, HIGH);
        delay(100);
        state = C_MEASURING_STATE;
        calibrate();
        drawInternalValuesDisplay();
        drawScreen();
    }
}

//also works as menu-up
void onCalButtonPressed()
{
    Serial.println(F("onCalButtonPressed"));
    calibrate();
}