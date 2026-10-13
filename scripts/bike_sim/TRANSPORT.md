# BLE sensor transport

This module is a real Linux BlueZ GATT peripheral. It advertises Cycling Power
(0x1818) and Cycling Speed and Cadence (0x1816), exposes their measurement,
feature, and sensor-location characteristics, and emits notifications only when
BlueZ has invoked StartNotify. BlueZ owns the CCC descriptors and ATT connection.
There is no native firmware controller, simulated BLE peer, or runtime-value
injection in this transport.

## Requirements

- Linux with running BlueZ and a peripheral-capable LE adapter
- Permissions to register a GATT application and LE advertisement on the system bus
- Adapter already powered on; this program does not change system settings
- `python -m pip install -r scripts/bike_sim/requirements-ble.txt`
- Actual SmartSpin2k firmware configured to use the advertised sensor
- Prefer a dedicated sensor adapter if the same host also operates the FTMS client

Registration failure is an error, never a fallback to an in-process fake. Hardware
advertising, connection, subscription and firmware sensor consumption must be
validated on the bench. Unit-test success is not over-air validation.

## Python interface

```python
from scripts.bike_sim.protocol import SensorPublisher
from scripts.bike_sim.transport_bluez import BlueZBikePeripheral

async with BlueZBikePeripheral(adapter="hci0") as peripheral:
    publisher = SensorPublisher(peripheral, power_interval_s=1, cadence_interval_s=.5)
    # On each plant tick, with elapsed monotonic seconds and actual plant outputs:
    publisher.update(elapsed_s, watts, cadence_rpm)
```

The caller runs a real-time asyncio loop. `CrankState.advance(now_s, rpm)` holds
rpm over the preceding tick interval. Fractional revolutions survive cadence
changes and pauses. Last-event time is the physical revolution crossing, not the
notification time, in 1/1024-second units. Both crank fields wrap at 16 bits.

Power-only CPS packets and crank-only CSC packets permit independent update
schedules. `publish_power(watts, crank)` optionally sends combined pedal-style
CPS. If using combined CPS, do not also send independently delayed cadence in CSC
unless intentionally testing conflicting sensor sources.

For sensor acquisition delay/dropouts, encode a sample at acquisition time using
`encode_power(watts, crank=None)` or `encode_csc(crank)`, retain the immutable
bytes, and later call `publish_power_bytes(payload)` or
`publish_cadence_bytes(payload)`. Do not encode a historical sample using the
current crank state at delivery time.

`SensorPublisher` skips missed schedule slots rather than flooding overdue frames.
Its return tuple and the transport's publish methods report notification emission
while subscribed. This means D-Bus submission to BlueZ, not confirmation that
firmware received or consumed a measurement. `counters` exposes subscription
state, subscription starts and notifications emitted for each characteristic.
`connected_peer_addresses()` reads all connected peers on the selected adapter;
it cannot identify which central subscribed to a characteristic. Combine a
dedicated adapter, expected peer address, both required subscriptions, and actual
firmware telemetry when validating the complete path.

## Validation

```sh
python -B -m unittest discover -s test -p test_bike_ble_transport.py -v
```

Protocol and scheduler tests need only Python. Actual production parser tests
compile `CyclePowerData.cpp` and `CscSensorData.cpp` with GCC/G++, and send generated
wire bytes into those unchanged parsers. GATT interface tests run when dbus-next
is installed; otherwise those four tests explicitly skip.

Two expected failures document existing CSC firmware defects rather than changing
sensor payloads to hide them:

- Crank-counter rollover is not interpreted modulo 65536.
- Event-time rollover subtracts 65535 rather than 65536. Near the 200 RPM parser
  acceptance limit this can reject a valid sample and preserve stale cadence.

The normal generated CPS/CSC stream and simultaneous CPS event/count rollover
pass. Expected-failure cases must become ordinary passing tests if the firmware
parser is later fixed.

The cloud workspace has no demonstrated BLE radio or system-bus registration.
An attempted private D-Bus IPC validation was blocked by its sandbox's socket
restrictions, so even interprocess D-Bus registration remains bench validation.

## Primary references

- [BlueZ GATT API](https://bluez.readthedocs.io/en/latest/gatt-api/)
- [BlueZ advertising API](https://bluez.readthedocs.io/en/latest/advertising-api/)
- [dbus-next service API](https://python-dbus-next.readthedocs.io/en/latest/high-level-service/)
- [Bluetooth SIG CSC service specification](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/CSCS_v1.0/out/en/index-en.html)

The implementation relies on dbus-next's built-in ObjectManager for the GATT
application subtree. The advertisement lives outside that subtree.
