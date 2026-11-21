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

## Signed release assets

For automatic updates the app downloads `firmware-manifest.json` and its detached `firmware-manifest.sig` from the selected GitHub release. Verify the signature over the exact UTF-8 bytes, including the final newline, with the pinned publisher public key before using any manifest fields. All signatures use ECDSA P-256 with SHA-256 and binary ASN.1 DER encoding.

Schema 1 carries the release `tag`, an `archive` object containing the ZIP's name, byte size and lowercase SHA-256 hash, and an `images` object mapping `firmware.bin` and `S3firmware.bin` to their byte sizes and lowercase SHA-256 hashes. Match the selected tag and expected ZIP name, verify the archive, then verify the selected board's application image before streaming it.

For a manual signed update, extract the application image and its matching `.bin.sig` from the same ZIP. The detached signature covers the exact raw application bytes. Factory images, bootloaders and partition tables are for USB flashing.

The release workflow reads an encrypted, multiline PEM from the GitHub Actions repository secret `SS2K_PRIVATE_KEY` and its passphrase from `SS2K_KEY_PASS`. It checks that the signing key matches `include/ss2k-release-public.pem` and verifies all three signatures before publication. To verify a downloaded bundle locally, place the ZIP, manifest and manifest signature together, install `scripts/requirements-release.txt`, and run:

```sh
python scripts/sign_release.py verify --tag 26.11.21 --output path/to/downloads
```
