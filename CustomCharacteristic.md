Please use and refer to the following notes for use of the custom characteristic:

Custom Characteristic for userConfig Variable manipulation via BLE

SMARTSPIN2K_SERVICE_UUID        "77776277-7877-7774-4466-896665500000"
SMARTSPIN2K_CHARACTERISTIC_UUID "77776277-7877-7774-4466-896665500001"

An example follows to read/write 26.3kph to simulatedSpeed:

simulatedSpeed is a float and first needs to be converted to int by *10 for transmission, so convert 26.3kph to 263 (multiply by 10)
Decimal 263 == hexadecimal 0107 but the data needs to be converted to LSO, MSO to match the rest of the BLE spec so 263 == 0x07, 0x01 (LSO,MSO)

So,

If client wants to write (0x02) int value 263 (0x07 0x01) to simulatedSpeed(0x06):

Client Writes:
0x02, 0x06, 0x07, 0x01
(operator, variable, LSO, MSO)

Server will then indicate:
0x80, 0x06, 0x07, 0x01 
(success),(simulatedSpeed),(LSO),(MSO)

Example to read (0x01) from simulatedSpeed (0x06)

Client Writes:
0x01, 0x06
Server will then indicate:
0x80, 0x06, 0x07, 0x01 
(success),(simulatedSpeed),(0x07),(0x01)

Pay special attention to the float values. Since they have to be transmitted as an int, some are converted *100, others are converted *10.
Refer to BLE_Server.cpp for which conversions to use.

True values are >00. False are 00.

Values in first byte:

Written:
  uint8_t read        = 0x01;  // value to request read operation
  uint8_t write       = 0x02;  // Value to request write operation
  
Indicated:
  uint8_t error       = 0xff;  // value server error/unable
  uint8_t success     = 0x80;  // value for success

From BLE_common.h
//custom characteristic codes
| Variable                 |uint8_t| type| Notes                                             |
|--------------------------|:-----:|-----|---------------------------------------------------|
|BLE_firmwareUpdateURL     |0x01   |     |Not Implemented                                    |
|BLE_incline               |0x02   |float|incline from app                                   |
|BLE_simulatedWatts        |0x03   |int  |simulated or read watts                            |
|BLE_simulatedHr           |0x04   |int  |simulated or read HR                               |
|BLE_simulatedCad          |0x05   |float|simulated or read cadence                          |
|BLE_simulatedSpeed        |0x06   |float|Calculated speed                                   |
|BLE_deviceName            |0x07   |     |Not Implemented                                    |
|BLE_shiftStep             |0x08   |int  |Stepper steps per shifter button press             |
|BLE_stepperPower          |0x09   |int  |Stepper power in ma                                |
|BLE_stealthchop           |0x0A   |bool |Stepper stealthchop on/off                         |
|BLE_inclineMultiplier     |0x0B   |float|- multiplied by incline to get steps per % gradient|
|BLE_powerCorrectionFactor |0x0C   |float|.5 - 2.0 to calibrate power output                 |
|BLE_simulateHr            |0x0D   |bool |                                                   |
|BLE_simulateWatts         |0x0E   |bool |                                                   |    
|BLE_simulateCad           |0x0F   |bool |                                                   |
|BLE_ERGMode               |0x10   |bool |                                                   |
|BLE_autoUpdate            |0x11   |bool |updates on (01) or off (00)                        |
|BLE_ssid                  |0x12   |     |Not Implemented                                    |
|BLE_password              |0x13   |     |Not Implemented                                    |
|BLE_foundDevices          |0x14   |     |Not Implemented                                    |
|BLE_connectedPowerMeter   |0x15   |     |Not Implemented                                    |
|BLE_connectedHeartMonitor |0x16   |     |Not Implemented                                    |
|BLE_shifterPosition       |0x17   |int  |That changes when a shift is preformed.            |
|BLE_saveToLittlefs          |0x18   |bool |01 written will save to spiffs.                    |
|BLE_targetPosition        |0x19   |int36|Position (in steps) the motor is maintaining.      |
|BLE_externalControl       |0x1A   |bool |01 disables internal calculation of targetPosition.|
|BLE_syncMode              |0x1B   |bool |01 stops motor movement for external calibration   |

*syncMode will disable the movement of the stepper motor by forcing stepperPosition = targetPosition prior to the motor control. While this mode is enabled, it allows the client to set parameters like incline and shifterPosition without moving the motor from it's current position. Once the parameters are set, this mode should be turned back off and SS2K will resume normal operation.


This characteristic also notifies when a shift is preformed or the button is pressed. 

See code for more references/info in BLE_Server.cpp starting on line 534