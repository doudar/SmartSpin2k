Please use and refer to the following notes for use of the custom characteristic:

Custom Characteristic for userConfig Variable manipulation via BLE

SMARTSPIN2K_SERVICE_UUID        "77776277-7877-7774-4466-896665500000"
SMARTSPIN2K_CHARACTERISTIC_UUID "77776277-7877-7774-4466-896665500001"

The same service and characteristic are published in the DirCon mDNS service.
A DirCon client sends the protocol bytes in a characteristic-write request and receives the custom-characteristic response bytes in that write response.
Subscribed DirCon clients also receive changed-value notifications.

The primary BLE advertisement includes the current Wi-Fi IPv4 address in manufacturer-specific data.
The device name and SmartSpin2k service UUID remain in the scan response. The payload is:

| Offset | Size | Meaning |
|--------|------|---------|
| 0 | 2 | Reserved development company identifier `0xFFFF`, little-endian |
| 2 | 2 | ASCII payload marker `SS` |
| 4 | 1 | Payload format version (`0x01`) |
| 5 | 4 | IPv4 address octets in network/display order |

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
|BLE_stealthChop           |0x0A   |bool |Stepper stealthChop on/off                         |
|BLE_inclineMultiplier     |0x0B   |float|- multiplied by incline to get steps per % gradient|
|BLE_powerCorrectionFactor |0x0C   |float|.5 - 2.0 to calibrate power output                 |
|BLE_simulateHr            |0x0D   |bool |                                                   |
|BLE_simulateWatts         |0x0E   |bool |                                                   |    
|BLE_simulateCad           |0x0F   |bool |                                                   |
|BLE_FTMSMode               |0x10   |bool |                                                   |
|BLE_autoUpdate            |0x11   |bool |Deprecated compatibility field; always off         |
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
|BLE_powerTableData        |0x27   |row  |Read/write a power-table row; first upload row queues homing|
|BLE_UDPLogging            |0x2E   |bool |Enable/disable UDP log streaming                   |
|BLE_hardwareVersion       |0x2F   |str  |Read-only detected hardware revision                |
|BLE_BLELogging            |0x30   |bool/str|Write: enable/disable BLE log streaming. Read: returns last log message|
|BLE_allSettings           |0x31   |JSON |Read-only chunked snapshot of all user settings         |
|BLE_gearTeeth             |0x34   |array|Packed front/rear tooth pairs; metadata/indexed reads |
|BLE_gearPreset            |0x35   |uint16|0 = tooth profile/Unlimited, 1 = Mixed Terrain 1x24 |

*syncMode will disable the movement of the stepper motor by forcing stepperPosition = targetPosition prior to the motor control. While this mode is enabled, it allows the client to set parameters like incline and shifterPosition without moving the motor from it's current position. Once the parameters are set, this mode should be turned back off and SS2K will resume normal operation.

The first power-table row write (`02 27 <row> <little-endian int16 positions...>`) immediately queues low-stop homing, or FTMS reference homing for a connected bike reporting real resistance. The usual cadence check still applies. Startup homing preserves the active table and pending save, so BLE can continue receiving rows during homing. Full homing retains its existing table-reset behavior. Saving keeps the ten-second transfer delay and retries on failure. Further rows while the save is pending do not restart homing.


This characteristic also notifies when a shift is preformed or the button is pressed. 

See code for more references/info in BLE_Server.cpp starting on line 534

Hardware-version example:

- Client writes: `0x01, 0x2F`
- An ESP32-S3 board indicates: `0x80, 0x2F`, followed by the ASCII bytes for `Revision Three (ESP32-S3)`.
- Writes to `0x2F` return `cc_error` because the detected hardware revision is read-only.

All-settings snapshot (BLE or DirCon):

- Client writes `0x01, 0x31`. BLE clients subscribe to indications on the custom characteristic.
  A DirCon client receives the first chunk in the characteristic-write response and is automatically subscribed for the remaining chunks.
- The server serializes `userConfig->returnJSON()` once.
  Over BLE, it sends MTU-sized indications sequentially and waits for each acknowledgement before sending the next.
- Over DirCon, chunks use the same framing and arrive as characteristic notifications after the first write-response chunk.
- Every snapshot chunk begins with this seven-byte header:

| Offset | Size | Meaning |
|--------|------|---------|
| 0 | 1 | `cc_success` (`0x80`) |
| 1 | 1 | `BLE_allSettings` (`0x31`) |
| 2 | 1 | Snapshot framing version (`0x01`) |
| 3 | 2 | Zero-based chunk number, little-endian |
| 5 | 2 | Total chunk count, little-endian |
| 7 | remainder | UTF-8 JSON bytes |

The client validates that it received chunks `0` through `chunk count - 1`, concatenates the bytes after each header, and parses the result as JSON. If the connection closes or a chunk is missing, discard the partial snapshot and issue the read command again. Unknown JSON properties should be ignored so newly added settings remain backward compatible. The snapshot includes sensitive settings such as the Wi-Fi password, consistent with the existing individual password read command.

BLE requests and snapshot acknowledgments are queued for the maintenance task;
an ATT write completion does not mean the settings operation has completed. Wait
for the custom-characteristic response before sending the next request. A bounded
queue logs overflow and drops excess events. Disconnects invalidate queued work,
and a snapshot accepts acknowledgments only from its connection/session. Snapshot
transfers expire after five seconds without progress. The wire format is unchanged.

## Virtual gearing settings (firmware API)

Gear profiles are persisted on both targets and used for ratio-based stepper
shifting. Shift Amount sets the motor distance for the median positive ratio gap.
See [VirtualGearing.md](VirtualGearing.md). The experimental rider-weight ID 0x33
is retired and returns an unsupported/error response; it must not be reused.

### Gear tooth pairs (`0x34`)

Store each pair as `front * 100 + rear` in an unsigned LE16 (`5332` = 53x32).
Supply zero pairs for Unlimited (default), or 2–26 unique pairs. Both tooth
counts must be 1–99 and their rounded ratio must be 500–6000 in thousandths.
Firmware sorts by ratio, breaking ties by packed pair. Distinct pairs with equal
ratios remain separate gears with the same motor offset. The complete profile
is validated before replacement; invalid or truncated writes leave it unchanged.
Writing a tooth array also sets `gearPreset` to 0. While preset 1 is selected,
both metadata and indexed tooth reads return `FF 34` because it has no tooth pairs.

- Unlimited: write `02 34 00`; success/metadata response `80 34 00`. Indexed reads
  return `FF 34`. JSON stores `gearTeeth: []`; fixed Shift Amount spacing applies.
- Write: `02 34 <count u8> <pair0 LE16> ... <pairN LE16>`.
- Success: `80 34 <count>`. Failure: `FF 34`.
- Metadata read: `01 34`; response: `80 34 <count>`.
- Indexed read: `01 34 <zero-based index>`; response:
  `80 34 <count> <index> <pair LE16>`. Out-of-range indexes return `FF 34`.
- Changed-value notification: `80 34 <count>`, including edits that keep the
  count or ratios unchanged. Refresh indexed reads or the all-settings snapshot.
- A 26-gear write is 55 bytes, requiring ATT MTU **58 or larger** for a single
  normal BLE write. Reads, notifications and all-settings snapshots work at
  MTU 23. DirCon has no ATT limit.
- Example: `02 34 02 C7 14 D4 14` submits 53x19 and 53x32; success `80 34 02`.
  After sorting, reading index 0 with `01 34 00` returns `80 34 02 00 D4 14`.

After updating over BLE/DirCon, send the existing save command `02 18` to persist
it. Firmware-hosted web settings save automatically. Persistence and config
snapshots include `gearPreset` and `gearTeeth`; arbitrary ratio-only test profiles
are not supported.
In local simulation/inclination modes, `BLE_shifterPosition` uses **1-based**
gear numbers for bounded profiles and an unbounded signed shift count for
Unlimited (startup and successful homing select gear 8). Bounded profiles start
at one-third of their gear count, rounded down with a minimum of 1. Indexed pair
reads use **0-based** indexes. Bounded gear 1 is the zero shift offset; travel
limits still apply. No companion-app changes are included here.

### Named gear preset (`0x35`)

- Read: `01 35`; response `80 35 <preset LE16>`.
- Select Mixed Terrain 1x24: `02 35 01 00`; response `80 35 01 00`.
- Select Unlimited: `02 35 00 00`; response `80 35 00 00`. To select a bounded
  tooth profile instead, write its pairs through 0x34.
- Unknown preset IDs or invalid lengths return `FF 35` without changing the profile.
- Every profile change notifies `80 35 <preset LE16>`; tooth profiles also notify
  0x34 metadata. Reading value 0 alone does not distinguish Unlimited from a
  bounded tooth profile: read 0x34 as well.

Preset 1 persists as `gearPreset: 1` and `gearTeeth: []`. Its exact 24 ratios are
built into firmware; it reports one rear gear axis, starts at gear 8, and uses
the same median-gap Shift Amount mapping as tooth profiles. Save with `02 18`.
