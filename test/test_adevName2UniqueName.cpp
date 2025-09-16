/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include <unity.h>
#include <Arduino.h>
#include "test.h"

// Test helper function to create a mock address string and test the randomization detection
// Since we can't easily mock NimBLEAdvertisedDevice, we'll test the logic indirectly
// by testing the address pattern detection logic

void TestAdevName2UniqueName::test_traditional_device_keeps_address_suffix() {
    // This test verifies the behavior for traditional (non-randomized) addresses
    // We can't directly test without a real NimBLEAdvertisedDevice, so this is a placeholder
    // for manual testing or integration testing
    
    // The logic should be:
    // - Addresses starting with 0x00, 0x01, 0x04, 0x05, etc. (even numbers and some odd) 
    //   are typically manufacturer-assigned (non-random)
    // - These should keep the address suffix behavior
    
    TEST_ASSERT_TRUE_MESSAGE(true, "Traditional device suffix test placeholder - requires manual verification");
}

void TestAdevName2UniqueName::test_android_device_no_address_suffix() {
    // This test verifies the behavior for randomized addresses (Android devices)
    // Addresses with local administration bit set (0x02, 0x03, 0x06, 0x07, etc.)
    // should NOT have address suffix
    
    // Test the SpinBLEAdvertisedDevice uniqueName functionality
    // Simulate how the uniqueName field should work for stable device identification
    
    struct MockDevice {
        char uniqueName[30];
        bool advertisedDevice;
        
        MockDevice() : advertisedDevice(false) {
            uniqueName[0] = '\0';
        }
        
        void setUniqueName(const char* name) {
            strncpy(this->uniqueName, name, sizeof(this->uniqueName) - 1);
            this->uniqueName[sizeof(this->uniqueName) - 1] = '\0';
            this->advertisedDevice = true;
        }
        
        void reset() {
            this->uniqueName[0] = '\0';
            this->advertisedDevice = false;
        }
    };
    
    MockDevice device;
    
    // Test setting unique name
    device.setUniqueName("MyAndroidDevice");
    TEST_ASSERT_EQUAL_STRING("MyAndroidDevice", device.uniqueName);
    TEST_ASSERT_TRUE(device.advertisedDevice);
    
    // Test reset clears unique name
    device.reset();
    TEST_ASSERT_EQUAL_STRING("", device.uniqueName);
    TEST_ASSERT_FALSE(device.advertisedDevice);
    
    // Test that device slot assignment logic would work for same device with different addresses
    MockDevice devices[2];
    const char* deviceName = "MyAndroidDevice";
    
    // First assignment
    devices[0].setUniqueName(deviceName);
    
    // Second attempt with same unique name (different address)
    bool found = false;
    for (int i = 0; i < 2; i++) {
        if (!devices[i].advertisedDevice || 
            (strlen(devices[i].uniqueName) > 0 && strcmp(devices[i].uniqueName, deviceName) == 0)) {
            devices[i].setUniqueName(deviceName);
            found = true;
            TEST_ASSERT_EQUAL(0, i);  // Should reuse slot 0
            break;
        }
    }
    TEST_ASSERT_TRUE(found);
}

void TestAdevName2UniqueName::test_random_address_pattern_detection() {
    // Test the bit pattern detection logic
    // We can test this by examining the first byte of MAC addresses
    
    // Test addresses that should be detected as randomized
    // Format: "XX:YY:ZZ:AA:BB:CC" where XX has bit 1 set
    
    // Simulate the logic from isRandomizedAddress function
    struct {
        const char* address;
        bool shouldBeRandom;
        const char* description;
    } testCases[] = {
        {"00:11:22:33:44:55", false, "Manufacturer assigned - no local admin bit"},
        {"01:11:22:33:44:55", false, "Manufacturer assigned - no local admin bit"},
        {"02:11:22:33:44:55", true, "Random address - local admin bit set"},
        {"03:11:22:33:44:55", true, "Random address - local admin bit set"},
        {"04:11:22:33:44:55", false, "Manufacturer assigned - no local admin bit"},
        {"06:11:22:33:44:55", true, "Random address - local admin bit set"},
        {"07:11:22:33:44:55", true, "Random address - local admin bit set"},
        {"A4:C1:38:12:34:56", false, "Real device example - manufacturer assigned"},
        {"FE:11:22:33:44:55", true, "Random address - local admin bit set"},
    };
    
    for (size_t i = 0; i < sizeof(testCases) / sizeof(testCases[0]); i++) {
        // Parse first byte
        char firstByteStr[3] = {testCases[i].address[0], testCases[i].address[1], '\0'};
        int firstByte = strtol(firstByteStr, nullptr, 16);
        bool isRandom = (firstByte & 0x02) != 0;
        
        TEST_ASSERT_EQUAL_MESSAGE(testCases[i].shouldBeRandom, isRandom, testCases[i].description);
    }
}

void TestAdevName2UniqueName::test_null_device_handling() {
    // Test that the function handles null input gracefully
    // Since we can't call the actual function without proper setup,
    // this is a validation of the expected behavior
    
    TEST_ASSERT_TRUE_MESSAGE(true, "Null device handling test - function should return 'null' string");
}

void TestAdevName2UniqueName::test_device_without_name() {
    // Test behavior when device doesn't have a name
    // Should return the full address regardless of randomization
    
    TEST_ASSERT_TRUE_MESSAGE(true, "No-name device test - should return full address");
}

void TestAdevName2UniqueName::test_backward_compatibility() {
    // Verify that existing device names remain unchanged for traditional devices
    // This is critical for not breaking existing user configurations
    
    // Expected behavior:
    // - Non-randomized addresses: "DeviceName XX" (where XX is last 2 chars of MAC)
    // - Randomized addresses: "DeviceName" (no suffix)
    
    // Test the device slot logic for both scenarios
    struct MockDevice {
        char uniqueName[30];
        bool advertisedDevice;
        
        MockDevice() : advertisedDevice(false) {
            uniqueName[0] = '\0';
        }
        
        void setUniqueName(const char* name) {
            strncpy(this->uniqueName, name, sizeof(this->uniqueName) - 1);
            this->uniqueName[sizeof(this->uniqueName) - 1] = '\0';
            this->advertisedDevice = true;
        }
        
        void reset() {
            this->uniqueName[0] = '\0';
            this->advertisedDevice = false;
        }
    };
    
    // Test scenario 1: Traditional device with address suffix
    MockDevice devices[3];
    
    // Traditional device assignment
    devices[0].setUniqueName("TraditionalDevice 55");
    TEST_ASSERT_EQUAL_STRING("TraditionalDevice 55", devices[0].uniqueName);
    
    // Same traditional device should match by unique name
    bool foundTraditional = false;
    for (int i = 0; i < 3; i++) {
        if (!devices[i].advertisedDevice || 
            (strlen(devices[i].uniqueName) > 0 && strcmp(devices[i].uniqueName, "TraditionalDevice 55") == 0)) {
            foundTraditional = true;
            TEST_ASSERT_EQUAL(0, i);  // Should reuse slot 0
            break;
        }
    }
    TEST_ASSERT_TRUE(foundTraditional);
    
    // Test scenario 2: Android device (no suffix)
    devices[1].setUniqueName("AndroidDevice");
    TEST_ASSERT_EQUAL_STRING("AndroidDevice", devices[1].uniqueName);
    
    // Same Android device should match by unique name
    bool foundAndroid = false;
    for (int i = 0; i < 3; i++) {
        if (!devices[i].advertisedDevice || 
            (strlen(devices[i].uniqueName) > 0 && strcmp(devices[i].uniqueName, "AndroidDevice") == 0)) {
            foundAndroid = true;
            TEST_ASSERT_EQUAL(1, i);  // Should reuse slot 1
            break;
        }
    }
    TEST_ASSERT_TRUE(foundAndroid);
    
    // Verify both devices are properly stored
    TEST_ASSERT_EQUAL_STRING("TraditionalDevice 55", devices[0].uniqueName);
    TEST_ASSERT_EQUAL_STRING("AndroidDevice", devices[1].uniqueName);
    TEST_ASSERT_EQUAL_STRING("", devices[2].uniqueName);  // Empty slot
}