"""Build-only facade for real firmware BLE notification, queue and ingestion code.

The link carries characteristic bytes, not BLE radio/ATT frames. Connection setup
is synthetic; subscription, notification handling, queueing, disconnect handling,
measurement application and sensor parsing are extracted from the selected source.
"""
from pathlib import Path
import subprocess
import shutil
import re

ROOT = Path(__file__).resolve().parents[2]


def _function(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def prepare(folder, source_root=None):
    root = Path(source_root or ROOT)
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    client = (root/'src/BLE_Client.cpp').read_text()
    common = (root/'src/BLE_Common.cpp').read_text()
    collector = (root/'src/SensorCollector.cpp').read_text()
    declarations = Path(__file__).with_name('virtual_ble_facade.h').read_text()
    signatures = (
        'static void notifyCB(', 'static bool subscribeToCharacteristic(',
        'bool subscribeToAllNotifications(', 'void MyClientCallback::onDisconnect(',
        'void SpinBLEAdvertisedDevice::set(', 'void SpinBLEAdvertisedDevice::clearState(',
        'void SpinBLEAdvertisedDevice::reset(', 'bool SpinBLEAdvertisedDevice::enqueueData(',
        'NotifyData SpinBLEAdvertisedDevice::dequeueData(',
    )
    impl = '\n'.join(_function(client, s) for s in signatures)
    service_start = common.index('namespace BLEServices {')
    service_end = common.index('}  // namespace BLEServices', service_start) + len('}  // namespace BLEServices')
    impl = common[service_start:service_end] + '\n' + impl
    impl += '\nSensorDataFactory sensorDataFactory;\n' + _function(collector, 'void collectAndSet(')
    impl += '\n' + _function(common, 'void BLECommunications(')
    impl += Path(__file__).with_name('virtual_ble_runtime.inc').read_text()
    # Compile the production parsers as separate translation units. Their clock
    # reads are the sole generated substitution, never packet or parser logic.
    sources = []
    for source in sorted((root/'lib/SS2K/src/sensors').glob('*.cpp')):
        text = source.read_text()
        text = re.sub(r'std::chrono::duration_cast<std::chrono::milliseconds>\(\s*std::chrono::steady_clock::now\(\).time_since_epoch\(\)\s*\).count\(\)', 'clockMs', text)
        if 'steady_clock::now' in text:
            raise RuntimeError(f'Unadapted real-time parser clock: {source}')
        # Include a declaration even in units not using time, keeping source generation uniform.
        generated = folder/('virtual_ble_'+source.name)
        generated.write_text('#include <cstdint>\nextern uint32_t clockMs;\n'+text)
        sources.append(str(generated))
    endian = folder/'virtual_ble_endian.o'
    subprocess.run([shutil.which('gcc') or 'cc', '-I'+str(root/'lib/ArduinoCompat/include'), '-c', str(root/'lib/ArduinoCompat/src/os/endian.c'), '-o', str(endian)], check=True)
    sources += [str(root/'lib/ArduinoCompat/src/NimBLEUUID.cpp'), str(endian)]
    return {"declarations": declarations, "implementation": impl, "sources": sources, "flags": []}
