#pragma GCC optimize("-O3")
#pragma GCC optimize("-j4")

/**
 * Rpm faker tool for any car with the 722.6 Transmission
 *
 * Displays really quick gear changes on tachometer like in newer mercs,
 * by faking the engine RPM based on the target gear the Transmission
 * is trying to shift into.
 * 
 * Author Ashcon Mohseninia <ashcon50@gmail.com>
 */

#include <mcp2515.h>

MCP2515 mcp(4);

void setup() {
    Serial.begin(115200);
    SPI.begin();
    // Setup can module on Arduino
    mcp.reset();
    mcp.setBitrate(CAN_500KBPS);
    mcp.setNormalMode();
    Serial.println("Ready");
}

#define WHEEL_RPM_ID 0x208 // BS_208h (This is ESP Module)
#define TRANS_ID 0x418 // GS_418h (This is Transmission controller)
#define RPM_ID 0x308 // MS_308h (This is engine control ECU)
#define DIFF_RATIO 2.94 // C200CDI 203.007 has a differential ratio of 2.94 (Part A2023506214)

// Gear ratios for 722.699 (W5A 580)
// Engine must rotate x times for every spin of output shaft
#define GEAR_RAT_1 3.5951
#define GEAR_RAT_2 2.1862
#define GEAR_RAT_3 1.4054
#define GEAR_RAT_4 1.0
#define GEAR_RAT_5 0.8309

float lastRpm = 0.0;
bool shouldFake = false;
float fake_ratio = 0; // Set when we need it

can_frame read;

void loop() {
    if (mcp.readMessage(&read) == MCP2515::ERROR_OK) {
        if (read.can_id == WHEEL_RPM_ID) {
            // Read the real Wheel RPM from sensor
            lastRpm = (read.data[4] & 0b0011111) << 8 | (read.data[5]);
            lastRpm /= 2; // Divide by 2 - Seems to give the real value
            Serial.print("Wheel rpm: ");
            Serial.println(lastRpm);
            lastRpm *= DIFF_RATIO; // Multiply by the differential ratio to get speed of driveshaft
            Serial.print("Propshaft rpm: ");
            Serial.println(lastRpm);
        }
        // Gear is changing up/down, and we can fake the RPM
        if (read.can_id == RPM_ID && shouldFake && fake_ratio != 0 && lastRpm != 0) {
            // Do RPM magic
            unsigned int faked_rpm = lastRpm * fake_ratio; // Do simple maths to calculate what the engine RPM should be
            Serial.print("Setting fake to ");
            Serial.print(faked_rpm);
            Serial.println(" rpm");
            // Now set the fake RPM to the message sent by MS:
            read.data[1] = (uint8_t)(faked_rpm >> 8);
            read.data[2] = (uint8_t) faked_rpm;
            // Now send the modified frame ASAP So IC reads this and not the original one!
            mcp.sendMessage(&read);
        }
        if (read.can_id == TRANS_ID) {
            // 5th byte from TCU:              xxxx_xxxx
            // This is target gear during shift |     |
            // This is the actual gear the car is in  |
            uint8_t tar_gear = (read.data[4] & 0b11110000) >> 4;
            uint8_t act_gear = (read.data[4] & 0b00001111);
            Serial.print("Gears: ");
            Serial.print(tar_gear);
            Serial.print("/");
            Serial.println(act_gear);
            // Ensure we only fake when we need to (test against the actual gear)
            switch (act_gear)
            {
            // Cannot fake RPM in these gears
            case 0xFF: // Unknown gear
            case 0x00: // Neutral
            case 0x11: // Reverse
            case 0x12: // Reverse 2nd
            case 0x13: // Park
                shouldFake = false;
                break;
            // Other number - Its a valid gear (1-5)
            default:
                shouldFake = (act_gear != tar_gear);
                break;
            }
            // If we can fake it, now check what ratio we should use for the fake
            // by checking the gear ratio of the TARGET gear
            if (shouldFake) {
                switch (tar_gear)
                {
                case 1:
                    fake_ratio = GEAR_RAT_1;
                    break;
                case 2:
                    fake_ratio = GEAR_RAT_2;
                    break;
                case 3:
                    fake_ratio = GEAR_RAT_3;
                    break;
                case 4:
                    fake_ratio = GEAR_RAT_4;
                    break;
                case 5:
                    fake_ratio = GEAR_RAT_5;
                    break;
                default:
                    // Target gear is not valid, so prevent faking
                    // This can happen if car is shifting from D -> R
                    shouldFake = false;
                    break;
                }
            }
        }
    }
}