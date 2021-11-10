#include <Arduino.h>
#include <U8g2lib.h>
#include <EasyButton.h>
#include <FreqCount/FreqCount.h>

//internal states
static const byte C_MEASURING_STATE = 1;
static const byte L_MEASURING_STATE = 2;
static const byte C_SHORT_CIRCUIT_STATE = 3;
static const byte L_OPEN_CIRCUIT_STATE = 4;

byte internalState;

//this correction is specific to the arduino board instance, use a lab tool to measure
//static const float XTAL_FREQ_COMPENSATION = 0.9992275218;
static const float XTAL_FREQ_COMPENSATION = 0.99920589292;

const unsigned long MEASURE_INTERVAL = 1000;
static const float FOUR_PI_SQRD = 39.4784176044; //(2Pi)^2
static const float C2_VALUE = 1210.7E-12;
static const long MAX_F1 = 600000;
static const long MIN_F1 = 10;

// measured C1 value = 839.1pF
// measured L1 value = 95.86uH

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
        display.drawStr(25, 60, "CALIBRATION");

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

void drawCalibraionErrorDisplay()
{
    display.clearDisplay();

    display.firstPage();
    do
    {
        display.setDrawColor(1);

        display.setFont(u8g2_font_7x13_mr);
        display.drawStr(25, 20, "CALIBRATION");
        display.setFont(u8g2_font_profont29_mr);
        display.drawStr(25, 60, "ERROR");

    } while (display.nextPage());
}

void formatCapacitorValue(float value, char *buffer, byte len)
{
    if (value < 1E-12)
    {
        strncpy(buffer, "0.0", len);
    }
    else if (value < 1E-9)
    {
        //pico
        snprintf(buffer, 10, "%.2fpF", value * 1E12);
    }
    else if (value < 1E-6)
    {
        //nano
        snprintf(buffer, 10, "%.2fnF", value * 1E9);
    }
    else if (value < 1E-3)
    {
        //micro
        sprintf(buffer, "%.2fuF", value * 1E6);
    }
    else
    {
        strncpy(buffer, "MAX", len);
    }
}

void formatInductorValue(float value, char *buffer, byte len)
{
    if (value < 1E-9)
    {
        //pico
        strncpy(buffer, "0.0", len);
    }
    else if (value < 1E-6)
    {
        //nano
        snprintf(buffer, len, "%.2fnH", value * 1E9);
    }
    else if (value < 1E-3)
    {
        //micro
        snprintf(buffer, len, "%.2fuH", value * 1E6);
    }
    else if (value < 1)
    {
        //milli
        snprintf(buffer, len, "%.2fmH", value * 1E3);
    }
    else
    {
        strncpy(buffer, "MAX", len);
    }
    //Serial.println(buffer);
}

void formatFrequency(float value, char *buffer, byte len)
{
    snprintf(buffer, len, "%.3f kHz", (double)value / 1000);
}

void drawInternalValuesDisplay()
{
    Serial.println(F("drawInternalValuesDisplay"));
    char bufferF[12];
    char bufferC[10];
    char bufferL[10];
    formatFrequency(F1, bufferF, 12);
    formatCapacitorValue(C1, bufferC, 10);
    formatInductorValue(L1, bufferL, 10);
    display.firstPage();
    do
    {
        display.setDrawColor(1);
        display.setFont(u8g2_font_7x13_mr);
        display.drawStr(30, 16, bufferF);

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
        display.drawStr(5, 64, bufferC);
        display.drawStr(80, 64, bufferL);
    } while (display.nextPage());
}

void drawScreen()
{
    Serial.println(F("drawScreen"));
    char measurementBuffer[10];
    char freqBuffer[12];
    formatFrequency(F3, freqBuffer, 12);

    display.firstPage();
    do
    {
        display.setDrawColor(1);
        //top Freq
        display.setFont(u8g2_font_7x13_mr);
        display.drawStr(0, 13, freqBuffer);

        if (state == C_MEASURING_STATE)
        {
            if (C3 < 1E-12 || abs(C3_prev - C3) / C3_prev < 0.05)
            {
                //stable value, less than 5% delta
                formatCapacitorValue(C3, measurementBuffer, 10);
            }
            else
            {
                //unstable
                strncpy(measurementBuffer, "  ----  ", 10);
            }

            display.drawStr(100, 20, "Cs");
        }
        else if (state == L_MEASURING_STATE)
        {
            if (L3 < 1E-09 || abs(L3_prev - L3) / L3_prev < 0.05)
            {
                //stable value, less than 5% delta
                formatInductorValue(L3, measurementBuffer, 10);
            }
            else
            {
                //unstable
                strncpy(measurementBuffer, "  ----  ", 10);
            }

            display.drawStr(100, 20, "Ls");
        }
        else if (state == C_SHORT_CIRCUIT_STATE)
        {
            display.setFont(u8g2_font_7x13_mr);
            display.drawStr(90, 20, "Cs");
            display.setFont(u8g2_font_profont29_mr);
            display.drawStr(15, 45, "SHORT");
        }
        else if (state == L_OPEN_CIRCUIT_STATE)
        {
            display.setFont(u8g2_font_7x13_mr);
            display.drawStr(90, 20, "Ls");
            display.setFont(u8g2_font_profont29_mr);
            display.drawStr(15, 45, "OPEN");
        }

        if (state == C_MEASURING_STATE || state == L_MEASURING_STATE)
        {
            //main value
            display.setDrawColor(0);
            display.drawBox(0, 32, 128, 20);
            display.setDrawColor(1);
            display.setFont(u8g2_font_profont29_mr);
            display.drawStr(0, 51, measurementBuffer);
        }

        //draw progress dots
        display.setDrawColor(0);
        display.drawBox(0, 60, 128, 64);
        display.setDrawColor(1);
        for (int i = 0; i < measurmentCounter; i++)
        {
            display.drawLine(10 + i * 10, 60, 10 + i * 10, 64);
        }
    } while (display.nextPage());
}

unsigned long getFrequency()
{
    Serial.println(F("getFrequency"));
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

bool calibrateC()
{
    Serial.println(F("calibrateC"));
    measurmentCounter = 0;
    displayCalibrationScreen();
    digitalWrite(MODE_RELAY_PIN, LOW);
    digitalWrite(CAL_RELAY_PIN, LOW);
    delay(100);

    //test for wrong state
    if (getFrequency() > MAX_F1)
    {
        //shorted leads
        state = C_SHORT_CIRCUIT_STATE;
        return false;
    }
    else
    {
        //good state
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

        C3 = 0;
        C3_prev = 0;
        L3 = 0;
        L3_prev = 0;
    }
    return true;
}

bool calibrateL()
{
    Serial.println(F("calibrateL"));
    measurmentCounter = 0;
    displayCalibrationScreen();
    digitalWrite(MODE_RELAY_PIN, HIGH);
    digitalWrite(CAL_RELAY_PIN, LOW);
    delay(100);

    //test for wrong state
    if (getFrequency() < MIN_F1)
    {
        //shorted leads
        state = L_OPEN_CIRCUIT_STATE;
        return false;
    }
    else
    {
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

        C3 = 0;
        C3_prev = 0;
        L3 = 0;
        L3_prev = 0;
    }
    return true;
}

void measureC()
{
    Serial.println(F("measureC"));
    F3 = getFrequency();
    Serial.print(F("Freq3 (Hz): "));
    Serial.println(F3);
    if (F3 > F1 * 1.2) // arbitrary value, F3 oscilates around F1 when nothing is connected
    {
        Serial.println(F("leads are probably shorted or the cap is shorted"));
        if (state != C_SHORT_CIRCUIT_STATE)
        {
            display.clearDisplay();
        }
        state = C_SHORT_CIRCUIT_STATE;
    }
    else
    {
        if (state != C_MEASURING_STATE)
        {
            display.clearDisplay();
        }
        state = C_MEASURING_STATE;
    }

    //calculate C3
    C3_prev = C3;
    C3 = C1 * ((F1 * F1) / (F3 * F3) - 1); //calculate C3 in F

    Serial.print(F("F1: "));
    Serial.println(F1);
    Serial.print(F("F3: "));
    Serial.println(F3);

    char buffer[10];
    Serial.print(F("C1: "));
    formatCapacitorValue(C1, buffer, 10);
    Serial.println(buffer);

    Serial.print(F("C3: "));
    formatCapacitorValue(C3, buffer, 10);
    Serial.println(buffer);
}

void measureL()
{
    Serial.println(F("measureL"));
    F3 = getFrequency();
    Serial.print(F("Freq3 (Hz): "));
    Serial.println(F3);
    if (F3 < 10) // arbitrary value, F3 should be zero for open circuit
    {
        Serial.println(F("leads are probably open or the inductor is open circuit"));
        state = L_OPEN_CIRCUIT_STATE;
        if (state != L_OPEN_CIRCUIT_STATE)
        {
            display.clearDisplay();
        }
    }
    else
    {
        if (state != L_MEASURING_STATE)
        {
            display.clearDisplay();
        }
        state = L_MEASURING_STATE;
        //calculate L3
        L3_prev = L3;
        L3 = L1 * ((F1 * F1) / (F3 * F3) - 1); //calculate L3 in H
    }

    Serial.print(F("F1: "));
    Serial.println(F1);
    Serial.print(F("F3: "));
    Serial.println(F3);

    char buffer[10];
    Serial.print(F("L1: "));
    formatInductorValue(L1, buffer, 10);
    Serial.println(buffer);

    Serial.print(F("L3: "));
    formatInductorValue(L3, buffer, 10);
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

    if (calibrateC())
    {
        drawInternalValuesDisplay();
        delay(1000);
        state = C_MEASURING_STATE;
    }
    else
    {
        drawCalibraionErrorDisplay();
        delay(3000);
    }
}

void loop()
{
    modeButton.read();
    calButton.read();

    if (millis() - lastMeasureTime > MEASURE_INTERVAL)
    {
        measurmentCounter = (measurmentCounter + 1) % 12;
        if (state == C_MEASURING_STATE || state == C_SHORT_CIRCUIT_STATE)
        {
            measureC();
        }
        else if (state == L_MEASURING_STATE || state == L_OPEN_CIRCUIT_STATE)
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
    if (state == C_MEASURING_STATE || state == C_SHORT_CIRCUIT_STATE)
    {
        //switching to L
        digitalWrite(MODE_RELAY_PIN, HIGH);
        delay(100);

        if (calibrateL())
        {
            drawInternalValuesDisplay();
            delay(1000);
            state = L_MEASURING_STATE;
        }
        else
        {
            drawCalibraionErrorDisplay();
            delay(3000);
        }
    }
    else if (state == L_MEASURING_STATE || state == L_OPEN_CIRCUIT_STATE)
    {
        //switching to C
        digitalWrite(MODE_RELAY_PIN, LOW);
        delay(100);
        if (calibrateC())
        {
            drawInternalValuesDisplay();
            delay(1000);
            state = C_MEASURING_STATE;
        }
        else
        {
            drawCalibraionErrorDisplay();
            delay(3000);
        }
    }
}

//also works as menu-up
void onCalButtonPressed()
{
    Serial.println(F("onCalButtonPressed"));
    bool result;
    if (state == C_MEASURING_STATE || state == C_SHORT_CIRCUIT_STATE)
    {
        result = calibrateC();
    }
    else if (state == L_MEASURING_STATE || state == L_OPEN_CIRCUIT_STATE)
    {
        result = calibrateL();
    }
    if (result)
    {
        drawInternalValuesDisplay();
        delay(1000);
    }
    else
    {
        drawCalibraionErrorDisplay();
        delay(3000);
    }
}