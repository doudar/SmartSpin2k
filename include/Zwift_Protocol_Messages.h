/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
*/

#pragma once

#include <cstdint>
#include <type_traits>

namespace ZwiftProtocol {

template <typename Enum>
constexpr auto toUnderlying(Enum value) noexcept -> std::underlying_type_t<Enum> {
	return static_cast<std::underlying_type_t<Enum>>(value);
}

enum class CommandCode : uint8_t {
	HubRequest = 0x00,
	HubRidingData = 0x03,
	HubCommand = 0x04,
	TrainerConfigStatus = 0x05,  // Device → Zwift: report trainer config (virtual shifting)
	PlayKeyPadStatus = 0x07,
	PlayCommand = 0x12,
	Idle = 0x19,
	RideKeyPadStatus = 0x23,
	ClickKeyPadStatus = 0x37,
	DeviceInformation = 0x3C,
};

enum class WireType : uint8_t {
	Varint = 0,
	Fixed64 = 1,
	LengthDelimited = 2,
	StartGroup = 3,
	EndGroup = 4,
	Fixed32 = 5,
};

template <typename FieldEnum>
constexpr uint8_t makeTag(FieldEnum field, WireType wireType) noexcept {
	return static_cast<uint8_t>((toUnderlying(field) << 3U) | toUnderlying(wireType));
}

namespace HubRequest {
enum class Field : uint8_t {
	DataId = 1,
};

enum class DataId : uint16_t {
	GeneralInfo = 0,
	GearRatio = 520,
};

constexpr uint16_t kReplyDataStart = 512;
constexpr uint16_t kReplyDataEnd = 534;
}  // namespace HubRequest

namespace HubRidingData {
enum class Field : uint8_t {
	Power = 1,
	Cadence = 2,
	SpeedX100 = 3,
	HeartRate = 4,
	Unknown1 = 5,
	Unknown2 = 6,
};
}  // namespace HubRidingData

namespace SimulationParam {
enum class Field : uint8_t {
	Wind = 1,
	InclineX100 = 2,
	CWa = 3,
	Crr = 4,
};
}  // namespace SimulationParam

namespace PhysicalParam {
enum class Field : uint8_t {
	GearRatioX10000 = 2,
	BikeWeightX100 = 4,
	RiderWeightX100 = 5,
};
}  // namespace PhysicalParam

namespace HubCommand {
enum class Field : uint8_t {
	PowerTarget = 3,
	Simulation = 4,
	Physical = 5,
};
}  // namespace HubCommand

namespace TrainerConfigStatus {
enum class Field : uint8_t {
	GearIndex = 1,
	Simulation = 2,
	VirtualShift = 3,
	Tilt = 4,
	Unknown5 = 5,
};
}  // namespace TrainerConfigStatus

namespace TrainerConfigSimulation {
enum class Field : uint8_t {
	Resistance = 1,
	ErgPower = 2,
	AveragingWindow = 3,
	Wind = 4,
	Grade = 5,
	RealGearRatio = 6,
	VirtualGearRatio = 7,
	Cw = 8,
	WheelDiameter = 9,
	BikeMass = 10,
	RiderMass = 11,
	Crr = 12,
	FrontalArea = 13,
	EBrake = 14,
};
}  // namespace TrainerConfigSimulation

namespace TrainerConfigVirtualShift {
enum class Field : uint8_t {
	VirtualShiftingMode = 1,
	Field2 = 2,
	Field3 = 3,
};
}  // namespace TrainerConfigVirtualShift

enum class PlayButtonStatus : uint8_t {
	On = 0,
	Off = 1,
};

namespace PlayKeyPadStatus {
enum class Field : uint8_t {
	RightPad = 1,
	ButtonYUp = 2,
	ButtonZLeft = 3,
	ButtonARight = 4,
	ButtonBDown = 5,
	ButtonOn = 6,
	ButtonShift = 7,
	AnalogLR = 8,
	AnalogUD = 9,
};
}  // namespace PlayKeyPadStatus

namespace PlayCommandParameters {
enum class Field : uint8_t {
	Param1 = 1,
	Param2 = 2,
	HapticPattern = 3,
};
}  // namespace PlayCommandParameters

namespace PlayCommandContents {
enum class Field : uint8_t {
	CommandParameters = 1,
};
}  // namespace PlayCommandContents

namespace PlayCommand {
enum class Field : uint8_t {
	CommandContents = 2,
};
}  // namespace PlayCommand

namespace Idle {
enum class Field : uint8_t {
	Unknown2 = 2,
};
}  // namespace Idle

enum class RideButtonMask : uint32_t {
	Left = 0x00001,
	Up = 0x00002,
	Right = 0x00004,
	Down = 0x00008,
	A = 0x00010,
	B = 0x00020,
	Y = 0x00040,
	Z = 0x00100,
	ShiftUpLeft = 0x00200,
	ShiftDownLeft = 0x00400,
	PowerupLeft = 0x00800,
	OnOffLeft = 0x01000,
	ShiftUpRight = 0x02000,
	ShiftDownRight = 0x04000,
	PowerupRight = 0x10000,
	OnOffRight = 0x20000,
};

constexpr RideButtonMask operator|(RideButtonMask lhs, RideButtonMask rhs) noexcept {
	return static_cast<RideButtonMask>(toUnderlying(lhs) | toUnderlying(rhs));
}

constexpr RideButtonMask operator&(RideButtonMask lhs, RideButtonMask rhs) noexcept {
	return static_cast<RideButtonMask>(toUnderlying(lhs) & toUnderlying(rhs));
}

constexpr RideButtonMask operator~(RideButtonMask value) noexcept {
	return static_cast<RideButtonMask>(~toUnderlying(value));
}

enum class RideAnalogLocation : uint8_t {
	Left = 0,
	Right = 1,
	Up = 2,
	Down = 3,
};

namespace RideAnalogKeyPress {
enum class Field : uint8_t {
	Location = 1,
	AnalogValue = 2,
};
}  // namespace RideAnalogKeyPress

namespace RideAnalogKeyGroup {
enum class Field : uint8_t {
	GroupStatus = 1,
};
}  // namespace RideAnalogKeyGroup

namespace RideKeyPadStatus {
enum class Field : uint8_t {
	ButtonMap = 1,
	AnalogButtons = 2,
};
}  // namespace RideKeyPadStatus

namespace ClickKeyPadStatus {
enum class Field : uint8_t {
	ButtonPlus = 1,
	ButtonMinus = 2,
};
}  // namespace ClickKeyPadStatus

namespace DeviceInformationContent {
enum class Field : uint8_t {
Unknown1 = 1,
SoftwareVersion = 2,
DeviceName = 3,
Unknown4 = 4,
Unknown5 = 5,
SerialNumber = 6,
HardwareVersion = 7,
ReplyData = 8,
Unknown9 = 9,
ProtocolVersion = 10,
Unknown10 = 10,
Unknown13 = 13,
};
}  // namespace DeviceInformationContent

namespace SubContent {
enum class Field : uint8_t {
	Content = 1,
	Unknown2 = 2,
	Unknown4 = 4,
	Unknown5 = 5,
	Unknown6 = 6,
};
}  // namespace SubContent

namespace DeviceInformation {
enum class Field : uint8_t {
	InformationId = 1,
	SubContent = 2,
};
}  // namespace DeviceInformation

}  // namespace ZwiftProtocol
