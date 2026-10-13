"""Linux BlueZ D-Bus GATT peripheral exposing actual CPS and CSC services.

Requires an LE peripheral-capable adapter, running BlueZ, system-bus permissions,
plus optional dbus-next. No mock transport or direct firmware state writes occur.
BlueZ creates the CCC descriptors from the notify flags. A subscription means a
central enabled notifications; it is not an acknowledgment of packet receipt.

References:
https://bluez.readthedocs.io/en/latest/gatt-api/
https://bluez.readthedocs.io/en/latest/advertising-api/
https://python-dbus-next.readthedocs.io/en/latest/high-level-service/
"""
import asyncio
import re
import sys
from .protocol import encode_csc, encode_power

BASE = "/org/smartspin2k/bikesim"
APP = BASE + "/app"
GATT_SERVICE = "org.bluez.GattService1"
GATT_CHAR = "org.bluez.GattCharacteristic1"


def uuid(short):
    return f"0000{short}-0000-1000-8000-00805f9b34fb"


def _interfaces():
    """Lazy optional dependency: protocol tests/imports do not need D-Bus."""
    try:
        from dbus_next import Variant, DBusError
        from dbus_next.constants import PropertyAccess
        from dbus_next.service import ServiceInterface, dbus_property, method
    except ImportError as exc:
        raise RuntimeError("BLE transport requires dbus-next; install scripts/bike_sim/requirements-ble.txt") from exc

    class Service(ServiceInterface):
        def __init__(self, short, path):
            super().__init__(GATT_SERVICE)
            self.path = path
            self.short = short

        @dbus_property(access=PropertyAccess.READ)
        def UUID(self) -> 's':
            return uuid(self.short)

        @dbus_property(access=PropertyAccess.READ)
        def Primary(self) -> 'b':
            return True

        def properties(self):
            return {"UUID": Variant('s', self.UUID), "Primary": Variant('b', True)}

    class Characteristic(ServiceInterface):
        def __init__(self, short, service, index, value=b'', notify=False):
            super().__init__(GATT_CHAR)
            self.short, self.service = short, service
            self.path = f"{service.path}/char{index}"
            self.value, self.notify = value, notify
            self.notifying = False
            self.emitted = 0
            self.subscription_count = 0

        @dbus_property(access=PropertyAccess.READ)
        def UUID(self) -> 's':
            return uuid(self.short)

        @dbus_property(access=PropertyAccess.READ)
        def Service(self) -> 'o':
            return self.service.path

        @dbus_property(access=PropertyAccess.READ)
        def Flags(self) -> 'as':
            return ['notify'] if self.notify else ['read']

        @dbus_property(access=PropertyAccess.READ)
        def Value(self) -> 'ay':
            return self.value

        @dbus_property(access=PropertyAccess.READ)
        def Notifying(self) -> 'b':
            return self.notifying

        @method()
        def ReadValue(self, options: 'a{sv}') -> 'ay':
            if self.notify:
                raise DBusError('org.bluez.Error.NotPermitted', 'Measurement is notify-only')
            offset = options.get('offset')
            offset = offset.value if offset else 0
            if offset > len(self.value):
                raise DBusError('org.bluez.Error.InvalidOffset', 'Offset exceeds value length')
            return self.value[offset:]

        @method()
        def StartNotify(self):
            if not self.notify:
                raise DBusError('org.bluez.Error.NotSupported', 'Read-only characteristic')
            if not self.notifying:
                self.notifying = True
                self.subscription_count += 1
                self.emit_properties_changed({'Notifying': True})

        @method()
        def StopNotify(self):
            self.notifying = False
            self.emit_properties_changed({'Notifying': False})

        def publish(self, payload):
            self.value = bytes(payload)
            if not self.notifying:
                return False
            self.emit_properties_changed({'Value': self.value})
            self.emitted += 1
            return True

        def properties(self):
            return {"UUID": Variant('s', self.UUID), "Service": Variant('o', self.Service),
                    "Flags": Variant('as', self.Flags), "Value": Variant('ay', self.value),
                    "Notifying": Variant('b', self.notifying)}

    class Advertisement(ServiceInterface):
        def __init__(self, name):
            super().__init__('org.bluez.LEAdvertisement1')
            self.local_name = name
            self.released = False

        @method()
        def Release(self):
            self.released = True

        @dbus_property(access=PropertyAccess.READ)
        def Type(self) -> 's':
            return 'peripheral'

        @dbus_property(access=PropertyAccess.READ)
        def ServiceUUIDs(self) -> 'as':
            return [uuid('1818'), uuid('1816')]

        @dbus_property(access=PropertyAccess.READ)
        def LocalName(self) -> 's':
            return self.local_name

    return Service, Characteristic, Advertisement


class BlueZBikePeripheral:
    """Use on the owning asyncio loop; async context manager handles cleanup."""

    def __init__(self, adapter='hci0', name='SS2K Bike Sim'):
        if not re.fullmatch(r'hci\d+', adapter):
            raise ValueError("Adapter must be a BlueZ name such as hci0")
        if not name or len(name.encode('utf-8')) > 20:
            raise ValueError("Advertising name must contain 1–20 UTF-8 bytes")
        self.adapter, self.name = adapter, name
        self._bus = None
        self._power = self._cadence = None
        self._gatt = self._advertising = None
        self._registered = self._advertised = False
        self._exports = []
        self._advertisement = None

    async def start(self):
        if self._bus is not None:
            raise RuntimeError("BLE peripheral already started")
        if sys.platform != 'linux':
            raise RuntimeError("The BLE peripheral requires Linux BlueZ and an LE adapter")
        Service, Characteristic, Advertisement = _interfaces()
        from dbus_next.aio import MessageBus
        from dbus_next.constants import BusType
        try:
            self._bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
            path = f'/org/bluez/{self.adapter}'
            introspection = await self._bus.introspect('org.bluez', path)
            proxy = self._bus.get_proxy_object('org.bluez', path, introspection)
            self._gatt = proxy.get_interface('org.bluez.GattManager1')
            self._advertising = proxy.get_interface('org.bluez.LEAdvertisingManager1')
            props = proxy.get_interface('org.freedesktop.DBus.Properties')
            powered = await props.call_get('org.bluez.Adapter1', 'Powered')
            if not powered.value:
                raise RuntimeError(f'{self.adapter} is off; power the adapter on before running')
            power_service = Service('1818', APP + '/service0')
            cadence_service = Service('1816', APP + '/service1')
            self._power = Characteristic('2a63', power_service, 0, encode_power(0), True)
            self._cadence = Characteristic('2a5b', cadence_service, 0, b'\x02\x00\x00\x00\x00', True)
            objects = [power_service, self._power,
                       Characteristic('2a65', power_service, 1, b'\x08\x00\x00\x00'),
                       Characteristic('2a5d', power_service, 2, b'\x05'),
                       cadence_service, self._cadence,
                       Characteristic('2a5c', cadence_service, 1, b'\x02\x00'),
                       Characteristic('2a5d', cadence_service, 2, b'\x05')]
            # dbus-next supplies ObjectManager for this virtual parent path.
            # Keep the advertisement outside APP so it is not a GATT object.
            for obj in objects:
                self._export(obj.path, obj)
            self._advertisement = Advertisement(self.name)
            self._export(BASE + '/advertisement0', self._advertisement)
            await self._gatt.call_register_application(APP, {})
            self._registered = True
            await self._advertising.call_register_advertisement(BASE + '/advertisement0', {})
            self._advertised = True
        except BaseException:
            await self.stop()
            raise
        return self

    def _export(self, path, obj):
        self._bus.export(path, obj)
        self._exports.append(path)

    async def stop(self):
        """Best-effort unregister; bus disconnect removes registration on failures."""
        if self._bus is None:
            return
        try:
            if self._advertised:
                try:
                    await asyncio.wait_for(self._advertising.call_unregister_advertisement(BASE + '/advertisement0'), 3)
                except Exception:
                    pass
            if self._registered:
                try:
                    await asyncio.wait_for(self._gatt.call_unregister_application(APP), 3)
                except Exception:
                    pass
        finally:
            for path in self._exports:
                self._bus.unexport(path)
            self._bus.disconnect()
            self._bus = None
            self._exports.clear()
            self._registered = self._advertised = False
            if self._power:
                self._power.notifying = False
            if self._cadence:
                self._cadence.notifying = False

    async def __aenter__(self):
        return await self.start()

    async def __aexit__(self, *exc):
        await self.stop()

    @property
    def power_subscribed(self):
        return bool(self._bus and self._power and self._power.notifying)

    @property
    def cadence_subscribed(self):
        return bool(self._bus and self._cadence and self._cadence.notifying)

    @property
    def counters(self):
        return {key: {"subscribed": bool(self._bus and char and char.notifying),
                      "subscription_count": char.subscription_count if char else 0,
                      "notifications_emitted": char.emitted if char else 0}
                for key, char in [('power', self._power), ('cadence', self._cadence)]}

    async def connected_peer_addresses(self):
        """Read connected peers on this adapter, not per-characteristic identity.

        This includes connections initiated by another process on the same adapter;
        combine with a dedicated adapter and subscriber/firmware telemetry evidence.
        """
        if self._bus is None:
            raise RuntimeError("Start the BLE peripheral before querying peers")
        introspection = await self._bus.introspect('org.bluez', '/')
        proxy = self._bus.get_proxy_object('org.bluez', '/', introspection)
        manager = proxy.get_interface('org.freedesktop.DBus.ObjectManager')
        objects = await manager.call_get_managed_objects()
        addresses = []
        prefix = f'/org/bluez/{self.adapter}/'
        for path, interfaces in objects.items():
            device = interfaces.get('org.bluez.Device1', {})
            if path.startswith(prefix) and device.get('Connected') and device['Connected'].value:
                address = device.get('Address')
                if address:
                    addresses.append(address.value.upper())
        return sorted(set(addresses))

    def publish_power_bytes(self, payload):
        if not self._registered or self._power is None:
            raise RuntimeError("Start the BLE peripheral before publishing")
        return self._power.publish(payload)

    def publish_cadence_bytes(self, payload):
        if not self._registered or self._cadence is None:
            raise RuntimeError("Start the BLE peripheral before publishing")
        return self._cadence.publish(payload)

    def publish_power(self, watts, crank=None):
        return self.publish_power_bytes(encode_power(watts, crank))

    def publish_cadence(self, crank):
        return self.publish_cadence_bytes(encode_csc(crank))
