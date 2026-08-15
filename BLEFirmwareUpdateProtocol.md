# BLE Firmware Update Protocol

SmartSpin2k exposes the firmware update service `4fafc201-1fb5-459e-8fcc-c5c9c331914b` with two characteristics:

- Control/status (`62ec0272-3ec5-11eb-b378-0242ac130003`): read, write, notify.
- Firmware data (`62ec0272-3ec5-11eb-b378-0242ac130005`): write with or without response.

All multi-byte integers are unsigned little-endian values. Protocol packets are at most 12 bytes so control and status traffic fits the minimum ATT MTU of 23.

The firmware advertises a local ATT MTU of 515 and makes a best-effort MTU exchange request on connection and again when START is accepted. The negotiated value is still limited by the peer and operating system; transfer remains functional with an MTU of 23.

## Update sequence

1. Subscribe to control/status notifications and read its current value. A 12-byte status packet with protocol version `1` indicates support for this protocol.
2. Calculate the firmware file's standard CRC-32 and write START to the control characteristic.
3. START first reports `Preparing` while sensor connections are released and the inactive partition is erased. Wait for `Updating`, then write the firmware bytes in order to the data characteristic. Any non-empty chunk size is accepted. Use `min(512, negotiated ATT MTU - 3)` bytes; this is 20 bytes when Windows reports MTU 23. Write-without-response is the preferred fast path; writes with response remain available as a conservative fallback.
4. After exactly the declared image length has been written, write FINISH to the control characteristic.
5. Follow status notifications through `Verifying` and `Rebooting`. A disconnect after `Rebooting` is expected.

The firmware buffers only the ESP image header. It calculates CRC-32 incrementally and writes incoming data directly to the inactive OTA partition.

## Control commands

| Command | Value | Packet |
| --- | ---: | --- |
| START | `0x01` | `[command, version, image_size:u32, crc32:u32]` (10 bytes) |
| FINISH | `0x02` | `[command]` |
| ABORT | `0x03` | `[command]` |
| QUERY | `0x04` | `[command]` |

START requires protocol version `1`. `crc32` is the standard reflected CRC-32 used by common ZIP/zlib implementations (polynomial `0xedb88320`; the `123456789` test vector produces `0xcbf43926`).

## Status packet

Every status read or notification is 12 bytes:

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 1 | Protocol version (`1`) |
| 1 | 1 | State |
| 2 | 1 | Error code (`0` when no error) |
| 3 | 1 | Capability flags |
| 4 | 4 | Bytes received |
| 8 | 4 | Declared image size |

Capability flags are `0x01` length-aware EOF, `0x02` CRC-32 verification, `0x04` variable data chunks, and `0x08` write-without-response support. Version 1 reports all four (`0x0f`). Progress notifications are throttled to approximately every 64 KiB; the app can show immediate progress from queued writes and use the firmware's received-byte count as confirmation.

### States

| Value | State | Meaning |
| ---: | --- | --- |
| `0x00` | Waiting | Ready for a START command |
| `0x01` | Preparing | Metadata accepted; sensor links are being released and the inactive OTA partition is being erased |
| `0x02` | Updating | OTA partition is ready; firmware data writes may begin |
| `0x03` | Flashing | Firmware data is being written; byte counters report received progress |
| `0x04` | Verifying | FINISH received; length, CRC, and ESP image validation are running |
| `0x05` | Rebooting | New boot partition selected; disconnect is expected |
| `0xff` | Error | Update stopped; inspect the error code |

### Errors

| Value | Error |
| ---: | --- |
| `0x00` | None |
| `0x01` | Invalid command |
| `0x02` | Unsupported protocol version |
| `0x03` | Invalid START packet |
| `0x04` | Another update is active |
| `0x05` | Invalid image size |
| `0x06` | No OTA partition available |
| `0x07` | Command/data came from the wrong connection |
| `0x08` | Update has not been started |
| `0x09` | Empty data write |
| `0x0a` | More bytes received than declared |
| `0x0b` | Invalid image header or wrong ESP chip |
| `0x0c` | OTA begin failed |
| `0x0d` | Flash write failed |
| `0x0e` | FINISH received before the declared byte count |
| `0x0f` | CRC-32 mismatch |
| `0x10` | ESP image verification failed |
| `0x11` | New boot partition could not be selected |
| `0x12` | No firmware data received for 30 seconds |
| `0x13` | Update aborted by the client |
| `0x14` | Update connection was lost |

Any transfer failure, 30-second data timeout, ABORT, or update-connection loss aborts the inactive OTA write and schedules a reboot. The boot partition is changed only after complete image verification, so these failures continue booting the existing known-good firmware.
