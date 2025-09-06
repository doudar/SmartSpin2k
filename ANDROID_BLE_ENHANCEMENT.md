# Android BLE Unique Name Generation Enhancement

## Problem
The original `adevName2UniqueName()` function used the last two characters of the BLE MAC address to create unique device names. This worked well for devices with stable MAC addresses, but caused issues with Android devices that regularly change their BLE addresses due to privacy features (MAC address randomization).

## Solution
Enhanced the function to detect devices using randomized MAC addresses and handle them differently:

### For Traditional Devices (Stable MAC):
- **Before**: `"PowerMeter 56"` (device name + last 2 chars of MAC)
- **After**: `"PowerMeter 56"` (unchanged - maintains backward compatibility)

### For Android Devices (Random MAC):
- **Before**: `"John's Phone 3A"` (changes every session as MAC changes)
- **After**: `"John's Phone"` (stable - no MAC suffix)

## Implementation Details

### Detection Logic
Randomized MAC addresses are detected by checking the Local Administration bit (bit 1) in the first byte of the MAC address:
- `00:xx:xx:xx:xx:xx` → Traditional (bit 1 = 0)
- `02:xx:xx:xx:xx:xx` → Randomized (bit 1 = 1)
- `03:xx:xx:xx:xx:xx` → Randomized (bit 1 = 1)

### Function Changes
1. Added `isRandomizedAddress()` helper function
2. Modified `adevName2UniqueName()` to use different logic based on address type
3. Maintained full backward compatibility for existing devices

### Benefits
- Android devices no longer need re-pairing every session
- Existing device configurations remain unchanged
- No breaking changes for current users
- Stable unique names for both device types

## Examples

```cpp
// Traditional device (e.g., dedicated power meter)
// MAC: A4:C1:38:12:34:56 → "PowerMeter 56"

// Android device (e.g., phone/tablet as FTMS)
// MAC: 02:XX:XX:XX:XX:XX → "John's Phone" (stable across sessions)
```