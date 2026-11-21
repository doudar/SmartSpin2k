# SmartSpin2k firmware streaming

Firmware uses the paired custom characteristic on BLE and DirCon. First establish an authenticated session and subscribe to the custom characteristic. The app selects the application image for the target board and computes its SHA-256 digest.

| Inner request | Fields following the three command bytes |
| --- | --- |
| `02 38 00` begin | Image size as LE32, then the 32-byte SHA-256 digest |
| `02 38 01` chunk | Offset as LE32, then file bytes |
| `02 38 02` finish | Empty |
| `02 38 03` abort | Empty |

Wait for each encrypted acknowledgement before sending the next command. A successful reply contains `80 38`, the operation byte and the next expected LE32 offset. An update error has status `ff`. Write sequentially from offset zero. Chunk payloads can contain up to 505 image bytes. Fitting the whole protected record into one transport message typically yields better throughput.

The firmware writes to the inactive application partition. It checks the ESP image header and chip ID, incrementally hashes the image, checks the declared size and digest, and calls ESP-IDF image verification before selecting the boot partition. Only the session that began the update can send its chunks or finish it. Owner disconnect, revocation, invalid image, abort, or 30 seconds without progress discards the transfer. Start again with a new begin command. Successful finish schedules a reboot and retains configuration and companion keys.

Legacy firmware had a dedicated BLE update service. Companion apps select that path only for compatible legacy devices.
