#include <Wire.h>

/*
 * STM32 I2C slave address
 */
const uint8_t STM32_I2C_ADDRESS = 0x42;

/*
 * Arduino GIGA pins
 *
 * These generate the four test signals.
 */
const uint8_t CH1_PIN = 2;
const uint8_t CH2_PIN = 3;
const uint8_t CH3_PIN = 4;
const uint8_t CH4_PIN = 5;

/*
 * STM32 READY signal
 *
 * Arduino D6 <- STM32 PA8
 */
const uint8_t DATA_READY_PIN = 6;

/*
 * Test delays relative to CH1.
 */
const uint32_t CH1_DELAY_US = 0;
const uint32_t CH2_DELAY_US = 1000;
const uint32_t CH3_DELAY_US = 2000;
const uint32_t CH4_DELAY_US = 3000;

/*
 * Width of the generated pulse.
 */
const uint32_t PULSE_WIDTH_US = 50;


void setup()
{
    /*
     * Test signal outputs
     */
    pinMode(CH1_PIN, OUTPUT);
    pinMode(CH2_PIN, OUTPUT);
    pinMode(CH3_PIN, OUTPUT);
    pinMode(CH4_PIN, OUTPUT);

    digitalWrite(CH1_PIN, LOW);
    digitalWrite(CH2_PIN, LOW);
    digitalWrite(CH3_PIN, LOW);
    digitalWrite(CH4_PIN, LOW);

    /*
     * STM32 READY input
     */
    pinMode(DATA_READY_PIN, INPUT);

    /*
     * Start Arduino as I2C master.
     *
     * GIGA uses D20 = SDA and D21 = SCL.
     */
    Wire.begin();

    /*
     * Start conservatively at 100 kHz.
     */
    Wire.setClock(100000);

    Serial.begin(115200);

    delay(500);

    Serial.println();
    Serial.println("Arduino GIGA TIM2/I2C test");
    Serial.println("--------------------------");
    Serial.println("Press 's' to run one test.");
}


void loop()
{
    if (Serial.available() > 0)
    {
        char command = Serial.read();

        if (command == 's' || command == 'S')
        {
            runTest();
        }
    }
}


void runTest()
{
    Serial.println();
    Serial.println("Starting capture...");

    /*
     * Make sure outputs are LOW.
     */
    digitalWrite(CH1_PIN, LOW);
    digitalWrite(CH2_PIN, LOW);
    digitalWrite(CH3_PIN, LOW);
    digitalWrite(CH4_PIN, LOW);

    delay(5);

    /*
     * Tell STM32 to reset TIM2 and arm
     * the next four input captures.
     */
    Wire.beginTransmission(STM32_I2C_ADDRESS);

    Wire.write(0x01);

    uint8_t error = Wire.endTransmission();

    if (error != 0)
    {
        Serial.print("I2C START command failed. Error = ");
        Serial.println(error);
        return;
    }

    /*
     * Give STM32 time to enter capture mode.
     */
    delayMicroseconds(100);

    /*
     * Generate the four rising edges.
     */
    uint32_t startTime = micros();

    // CH1
    digitalWrite(CH1_PIN, HIGH);

    // CH2
    while ((micros() - startTime) < CH2_DELAY_US) {}
    digitalWrite(CH2_PIN, HIGH);

    // CH3
    while ((micros() - startTime) < CH3_DELAY_US) {}
    digitalWrite(CH3_PIN, HIGH);

    // CH4
    while ((micros() - startTime) < CH4_DELAY_US) {}
    digitalWrite(CH4_PIN, HIGH);

    /*
     * Hold outputs high briefly.
     */
    delayMicroseconds(PULSE_WIDTH_US);

    /*
     * Return all inputs LOW.
     */
    digitalWrite(CH1_PIN, LOW);
    digitalWrite(CH2_PIN, LOW);
    digitalWrite(CH3_PIN, LOW);
    digitalWrite(CH4_PIN, LOW);

    /*
     * Wait for STM32 READY.
     */
    uint32_t readyStart = millis();

    while (digitalRead(DATA_READY_PIN) == LOW)
    {
        if ((millis() - readyStart) > 100)
        {
            Serial.println("ERROR: STM32 did not assert READY.");
            return;
        }
    }

    Serial.println("STM32 reports data ready.");

    /*
     * Request four uint32_t timestamps = 16 bytes.
     */
    const uint8_t NUM_BYTES = 16;

    uint8_t received = Wire.requestFrom(
        (int)STM32_I2C_ADDRESS,
        (int)NUM_BYTES
    );

    if (received != NUM_BYTES)
    {
        Serial.print("ERROR: Expected 16 bytes, received ");
        Serial.println(received);

        /*
         * Clear any bytes still in the Wire buffer.
         */
        while (Wire.available())
        {
            Wire.read();
        }

        return;
    }

    /*
     * Read the four timestamps.
     */
    uint32_t capture[4];

    for (int i = 0; i < 4; i++)
    {
        uint8_t b0 = Wire.read();
        uint8_t b1 = Wire.read();
        uint8_t b2 = Wire.read();
        uint8_t b3 = Wire.read();

        capture[i] =
            ((uint32_t)b0) |
            ((uint32_t)b1 << 8) |
            ((uint32_t)b2 << 16) |
            ((uint32_t)b3 << 24);
    }

    /*
     * Calculate timing differences using CH1 as
     * the reference.
     */
    uint32_t dt12 = capture[1] - capture[0];
    uint32_t dt13 = capture[2] - capture[0];
    uint32_t dt14 = capture[3] - capture[0];

    /*
     * Print raw timestamps.
     */
    Serial.println();
    Serial.println("Captured timestamps:");

    Serial.print("CH1 = ");
    Serial.println(capture[0]);

    Serial.print("CH2 = ");
    Serial.println(capture[1]);

    Serial.print("CH3 = ");
    Serial.println(capture[2]);

    Serial.print("CH4 = ");
    Serial.println(capture[3]);

    /*
     * Print timer differences.
     */
    Serial.println();
    Serial.println("Timer differences:");

    Serial.print("CH2 - CH1 = ");
    Serial.print(dt12);
    Serial.println(" counts");

    Serial.print("CH3 - CH1 = ");
    Serial.print(dt13);
    Serial.println(" counts");

    Serial.print("CH4 - CH1 = ");
    Serial.print(dt14);
    Serial.println(" counts");

    /*
     * Convert timer counts to microseconds.
     *
     * TIM2 = 80 MHz -> 12.5 ns/count
     */
    Serial.println();
    Serial.println("Time differences:");

    Serial.print("CH2 - CH1 = ");
    Serial.print(dt12 * 0.0125, 3);
    Serial.println(" us");

    Serial.print("CH3 - CH1 = ");
    Serial.print(dt13 * 0.0125, 3);
    Serial.println(" us");

    Serial.print("CH4 - CH1 = ");
    Serial.print(dt14 * 0.0125, 3);
    Serial.println(" us");

    Serial.println();
    Serial.println("Test complete.");
    Serial.println("Press 's' to run again.");
}