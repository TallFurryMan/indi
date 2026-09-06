/*******************************************************************************
  ESPHome Native API client support
  Copyright(c) 2026. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License version 2 as published by the Free Software Foundation.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public License
  along with this library; see the file COPYING.LIB.  If not, write to
  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
  Boston, MA 02110-1301, USA.
*******************************************************************************/

#include "indiesphomeclient.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <unistd.h>

namespace
{
constexpr uint8_t PLAINTEXT_FRAME_MARKER {0x00};

enum WireType
{
    WIRE_VARINT = 0,
    WIRE_64BIT = 1,
    WIRE_LENGTH_DELIMITED = 2,
    WIRE_32BIT = 5
};

enum MessageType : uint32_t
{
    HELLO_REQUEST = 1,
    HELLO_RESPONSE = 2,
    AUTHENTICATION_REQUEST = 3,
    AUTHENTICATION_RESPONSE = 4,
    DISCONNECT_REQUEST = 5,
    DISCONNECT_RESPONSE = 6,
    PING_REQUEST = 7,
    PING_RESPONSE = 8,
    DEVICE_INFO_REQUEST = 9,
    DEVICE_INFO_RESPONSE = 10,
    LIST_ENTITIES_REQUEST = 11,
    LIST_ENTITIES_BINARY_SENSOR_RESPONSE = 12,
    LIST_ENTITIES_COVER_RESPONSE = 13,
    LIST_ENTITIES_SENSOR_RESPONSE = 16,
    LIST_ENTITIES_SWITCH_RESPONSE = 17,
    LIST_ENTITIES_TEXT_SENSOR_RESPONSE = 18,
    LIST_ENTITIES_DONE_RESPONSE = 19,
    SUBSCRIBE_STATES_REQUEST = 20,
    BINARY_SENSOR_STATE_RESPONSE = 21,
    COVER_STATE_RESPONSE = 22,
    SENSOR_STATE_RESPONSE = 25,
    SWITCH_STATE_RESPONSE = 26,
    TEXT_SENSOR_STATE_RESPONSE = 27,
    COVER_COMMAND_REQUEST = 30,
    SWITCH_COMMAND_REQUEST = 33,
    LIST_ENTITIES_BUTTON_RESPONSE = 61,
    BUTTON_COMMAND_REQUEST = 62
};

enum LegacyCoverCommand : uint32_t
{
    LEGACY_COVER_OPEN = 0,
    LEGACY_COVER_CLOSE = 1,
    LEGACY_COVER_STOP = 2
};

struct ProtoField
{
    uint32_t number {0};
    uint8_t wireType {0};
    uint64_t varintValue {0};
    uint32_t fixed32Value {0};
    std::string bytesValue;
};

void appendVarUInt(std::vector<uint8_t> &buffer, uint64_t value)
{
    while (value > 0x7f)
    {
        buffer.push_back(static_cast<uint8_t>((value & 0x7f) | 0x80));
        value >>= 7;
    }

    buffer.push_back(static_cast<uint8_t>(value));
}

void appendTag(std::vector<uint8_t> &buffer, uint32_t fieldNumber, uint8_t wireType)
{
    appendVarUInt(buffer, (static_cast<uint64_t>(fieldNumber) << 3) | wireType);
}

void appendString(std::vector<uint8_t> &buffer, uint32_t fieldNumber, const std::string &value)
{
    appendTag(buffer, fieldNumber, WIRE_LENGTH_DELIMITED);
    appendVarUInt(buffer, value.size());
    buffer.insert(buffer.end(), value.begin(), value.end());
}

void appendBool(std::vector<uint8_t> &buffer, uint32_t fieldNumber, bool value)
{
    appendTag(buffer, fieldNumber, WIRE_VARINT);
    appendVarUInt(buffer, value ? 1 : 0);
}

void appendUInt32(std::vector<uint8_t> &buffer, uint32_t fieldNumber, uint32_t value)
{
    appendTag(buffer, fieldNumber, WIRE_VARINT);
    appendVarUInt(buffer, value);
}

void appendFixed32(std::vector<uint8_t> &buffer, uint32_t fieldNumber, uint32_t value)
{
    appendTag(buffer, fieldNumber, WIRE_32BIT);
    buffer.push_back(static_cast<uint8_t>(value & 0xff));
    buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

void appendFloat(std::vector<uint8_t> &buffer, uint32_t fieldNumber, float value)
{
    uint32_t fixedValue = 0;
    static_assert(sizeof(value) == sizeof(fixedValue), "float must be 32-bit");
    std::memcpy(&fixedValue, &value, sizeof(fixedValue));
    appendFixed32(buffer, fieldNumber, fixedValue);
}

bool readVarUInt(const std::vector<uint8_t> &buffer, size_t &offset, uint64_t &value)
{
    value = 0;
    uint8_t shift = 0;

    while (offset < buffer.size() && shift < 64)
    {
        const auto byte = buffer[offset++];
        value |= static_cast<uint64_t>(byte & 0x7f) << shift;

        if ((byte & 0x80) == 0)
            return true;

        shift += 7;
    }

    return false;
}

bool skipBytes(const std::vector<uint8_t> &buffer, size_t &offset, size_t count)
{
    if (count > buffer.size() || offset > buffer.size() - count)
        return false;

    offset += count;
    return true;
}

bool parseFields(const std::vector<uint8_t> &payload, std::vector<ProtoField> &fields)
{
    fields.clear();
    size_t offset = 0;

    while (offset < payload.size())
    {
        uint64_t tag = 0;
        if (!readVarUInt(payload, offset, tag))
            return false;

        ProtoField field;
        field.number = static_cast<uint32_t>(tag >> 3);
        field.wireType = static_cast<uint8_t>(tag & 0x07);

        switch (field.wireType)
        {
            case WIRE_VARINT:
                if (!readVarUInt(payload, offset, field.varintValue))
                    return false;
                break;

            case WIRE_64BIT:
                if (!skipBytes(payload, offset, 8))
                    return false;
                break;

            case WIRE_LENGTH_DELIMITED:
            {
                uint64_t size = 0;
                if (!readVarUInt(payload, offset, size))
                    return false;

                if (size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
                    return false;

                const auto byteCount = static_cast<size_t>(size);
                if (byteCount > payload.size() || offset > payload.size() - byteCount)
                    return false;

                field.bytesValue.assign(reinterpret_cast<const char *>(payload.data() + offset), byteCount);
                offset += byteCount;
                break;
            }

            case WIRE_32BIT:
                if (payload.size() < 4 || offset > payload.size() - 4)
                    return false;

                field.fixed32Value = static_cast<uint32_t>(payload[offset]) |
                                     (static_cast<uint32_t>(payload[offset + 1]) << 8) |
                                     (static_cast<uint32_t>(payload[offset + 2]) << 16) |
                                     (static_cast<uint32_t>(payload[offset + 3]) << 24);
                offset += 4;
                break;

            default:
                return false;
        }

        fields.push_back(std::move(field));
    }

    return true;
}

std::string fieldString(const std::vector<ProtoField> &fields, uint32_t number)
{
    const auto field = std::find_if(fields.begin(), fields.end(), [number](const auto &oneField)
    {
        return oneField.number == number && oneField.wireType == WIRE_LENGTH_DELIMITED;
    });

    return field == fields.end() ? std::string() : field->bytesValue;
}

uint32_t fieldUInt32(const std::vector<ProtoField> &fields, uint32_t number, uint32_t defaultValue = 0)
{
    for (const auto &field : fields)
    {
        if (field.number != number)
            continue;

        if (field.wireType == WIRE_VARINT)
            return static_cast<uint32_t>(field.varintValue);

        if (field.wireType == WIRE_32BIT)
            return field.fixed32Value;
    }

    return defaultValue;
}

bool fieldBool(const std::vector<ProtoField> &fields, uint32_t number, bool defaultValue = false)
{
    const auto field = std::find_if(fields.begin(), fields.end(), [number](const auto &oneField)
    {
        return oneField.number == number && oneField.wireType == WIRE_VARINT;
    });

    return field == fields.end() ? defaultValue : field->varintValue != 0;
}

int32_t fieldInt32(const std::vector<ProtoField> &fields, uint32_t number, int32_t defaultValue = 0)
{
    const auto field = std::find_if(fields.begin(), fields.end(), [number](const auto &oneField)
    {
        return oneField.number == number && oneField.wireType == WIRE_VARINT;
    });

    return field == fields.end() ? defaultValue : static_cast<int32_t>(field->varintValue);
}

float fieldFloat(const std::vector<ProtoField> &fields, uint32_t number, float defaultValue = 0)
{
    const auto field = std::find_if(fields.begin(), fields.end(), [number](const auto &oneField)
    {
        return oneField.number == number && oneField.wireType == WIRE_32BIT;
    });

    if (field == fields.end())
        return defaultValue;

    float value = 0;
    static_assert(sizeof(value) == sizeof(field->fixed32Value), "float must be 32-bit");
    std::memcpy(&value, &field->fixed32Value, sizeof(value));
    return value;
}

bool parseHelloResponse(const std::vector<uint8_t> &payload, INDI::ESPHome::HelloResponse &response)
{
    std::vector<ProtoField> fields;
    if (!parseFields(payload, fields))
        return false;

    response.apiVersionMajor = fieldUInt32(fields, 1);
    response.apiVersionMinor = fieldUInt32(fields, 2);
    response.serverInfo = fieldString(fields, 3);
    response.name = fieldString(fields, 4);
    return true;
}

bool parseAuthenticationResponse(const std::vector<uint8_t> &payload, INDI::ESPHome::AuthenticationResponse &response)
{
    std::vector<ProtoField> fields;
    if (!parseFields(payload, fields))
        return false;

    response.invalidPassword = fieldBool(fields, 1);
    return true;
}

bool parseDeviceInfo(const std::vector<uint8_t> &payload, INDI::ESPHome::DeviceInfo &info)
{
    std::vector<ProtoField> fields;
    if (!parseFields(payload, fields))
        return false;

    info.usesPassword = fieldBool(fields, 1);
    info.name = fieldString(fields, 2);
    info.macAddress = fieldString(fields, 3);
    info.esphomeVersion = fieldString(fields, 4);
    info.compilationTime = fieldString(fields, 5);
    info.model = fieldString(fields, 6);
    info.hasDeepSleep = fieldBool(fields, 7);
    info.webserverPort = fieldUInt32(fields, 10);
    info.manufacturer = fieldString(fields, 12);
    info.friendlyName = fieldString(fields, 13);
    info.suggestedArea = fieldString(fields, 16);
    return true;
}

std::vector<uint8_t> buildHelloRequest(const std::string &clientInfo)
{
    std::vector<uint8_t> payload;
    appendString(payload, 1, clientInfo);
    appendUInt32(payload, 2, 1);
    appendUInt32(payload, 3, 10);
    return payload;
}

std::vector<uint8_t> buildAuthenticationRequest(const std::string &password)
{
    std::vector<uint8_t> payload;
    if (!password.empty())
        appendString(payload, 1, password);
    return payload;
}

std::vector<uint8_t> buildSwitchCommandRequest(uint32_t key, bool state)
{
    std::vector<uint8_t> payload;
    appendFixed32(payload, 1, key);
    appendBool(payload, 2, state);
    return payload;
}

std::vector<uint8_t> buildButtonCommandRequest(uint32_t key)
{
    std::vector<uint8_t> payload;
    appendFixed32(payload, 1, key);
    return payload;
}

std::vector<uint8_t> buildCoverCommandRequest(uint32_t key, INDI::ESPHome::CoverCommand command)
{
    std::vector<uint8_t> payload;
    appendFixed32(payload, 1, key);

    switch (command)
    {
        case INDI::ESPHome::CoverCommand::Open:
            appendBool(payload, 2, true);
            appendUInt32(payload, 3, LEGACY_COVER_OPEN);
            appendBool(payload, 4, true);
            appendFloat(payload, 5, 1.0F);
            break;

        case INDI::ESPHome::CoverCommand::Close:
            appendBool(payload, 2, true);
            appendUInt32(payload, 3, LEGACY_COVER_CLOSE);
            appendBool(payload, 4, true);
            appendFloat(payload, 5, 0.0F);
            break;

        case INDI::ESPHome::CoverCommand::Stop:
            appendBool(payload, 2, true);
            appendUInt32(payload, 3, LEGACY_COVER_STOP);
            appendBool(payload, 8, true);
            break;
    }

    return payload;
}

} // namespace

namespace INDI
{
namespace ESPHome
{

NativeAPIClient::NativeAPIClient(int socketFD) : m_SocketFD(socketFD)
{
}

void NativeAPIClient::setSocket(int socketFD)
{
    m_SocketFD = socketFD;
}

int NativeAPIClient::socket() const
{
    return m_SocketFD;
}

bool NativeAPIClient::sendHello(const std::string &clientInfo, HelloResponse &response)
{
    if (!writeFrame(HELLO_REQUEST, buildHelloRequest(clientInfo)))
        return false;

    Frame frame;
    if (!readFrame(frame) || frame.type != HELLO_RESPONSE)
        return false;

    return parseHelloResponse(frame.payload, response);
}

bool NativeAPIClient::authenticate(const std::string &password, AuthenticationResponse &response)
{
    if (!writeFrame(AUTHENTICATION_REQUEST, buildAuthenticationRequest(password)))
        return false;

    Frame frame;
    if (!readFrame(frame) || frame.type != AUTHENTICATION_RESPONSE)
        return false;

    return parseAuthenticationResponse(frame.payload, response);
}

bool NativeAPIClient::requestDeviceInfo(DeviceInfo &info)
{
    if (!writeFrame(DEVICE_INFO_REQUEST, {}))
        return false;

    Frame frame;
    if (!readFrame(frame) || frame.type != DEVICE_INFO_RESPONSE)
        return false;

    return parseDeviceInfo(frame.payload, info);
}

bool NativeAPIClient::listEntities(std::vector<EntityInfo> &entities)
{
    if (!writeFrame(LIST_ENTITIES_REQUEST, {}))
        return false;

    entities.clear();

    while (true)
    {
        Frame frame;
        if (!readFrame(frame))
            return false;

        if (frame.type == LIST_ENTITIES_DONE_RESPONSE)
            return true;

        EntityInfo entity;
        if (parseEntity(frame.type, frame.payload, entity))
            entities.push_back(std::move(entity));
    }
}

bool NativeAPIClient::subscribeStates()
{
    return writeFrame(SUBSCRIBE_STATES_REQUEST, {});
}

bool NativeAPIClient::ping()
{
    return writeFrame(PING_REQUEST, {});
}

bool NativeAPIClient::sendPingResponse()
{
    return writeFrame(PING_RESPONSE, {});
}

bool NativeAPIClient::commandButton(uint32_t key)
{
    return writeFrame(BUTTON_COMMAND_REQUEST, buildButtonCommandRequest(key));
}

bool NativeAPIClient::commandCover(uint32_t key, CoverCommand command)
{
    return writeFrame(COVER_COMMAND_REQUEST, buildCoverCommandRequest(key, command));
}

bool NativeAPIClient::commandSwitch(uint32_t key, bool state)
{
    return writeFrame(SWITCH_COMMAND_REQUEST, buildSwitchCommandRequest(key, state));
}

bool NativeAPIClient::readFrame(Frame &frame)
{
    uint8_t marker = 0;
    if (!readByte(marker) || marker != PLAINTEXT_FRAME_MARKER)
        return false;

    uint32_t payloadSize = 0;
    uint32_t messageType = 0;
    if (!readVarUInt(payloadSize) || !readVarUInt(messageType))
        return false;

    frame.type = messageType;
    frame.payload.clear();
    return readExact(frame.payload, payloadSize);
}

bool NativeAPIClient::readState(State &state, Frame *rawFrame)
{
    Frame frame;

    while (readFrame(frame))
    {
        if (rawFrame != nullptr)
            *rawFrame = frame;

        if (parseState(frame.type, frame.payload, state))
            return true;
    }

    return false;
}

bool NativeAPIClient::writeFrame(uint32_t type, const std::vector<uint8_t> &payload)
{
    if (m_SocketFD < 0)
        return false;

    std::vector<uint8_t> frame;
    frame.push_back(PLAINTEXT_FRAME_MARKER);
    appendVarUInt(frame, payload.size());
    appendVarUInt(frame, type);
    frame.insert(frame.end(), payload.begin(), payload.end());
    return writeExact(frame);
}

const char *NativeAPIClient::messageTypeName(uint32_t type)
{
    switch (type)
    {
        case HELLO_REQUEST:
            return "HelloRequest";
        case HELLO_RESPONSE:
            return "HelloResponse";
        case AUTHENTICATION_REQUEST:
            return "AuthenticationRequest";
        case AUTHENTICATION_RESPONSE:
            return "AuthenticationResponse";
        case DISCONNECT_REQUEST:
            return "DisconnectRequest";
        case DISCONNECT_RESPONSE:
            return "DisconnectResponse";
        case PING_REQUEST:
            return "PingRequest";
        case PING_RESPONSE:
            return "PingResponse";
        case DEVICE_INFO_REQUEST:
            return "DeviceInfoRequest";
        case DEVICE_INFO_RESPONSE:
            return "DeviceInfoResponse";
        case LIST_ENTITIES_REQUEST:
            return "ListEntitiesRequest";
        case LIST_ENTITIES_BINARY_SENSOR_RESPONSE:
            return "ListEntitiesBinarySensorResponse";
        case LIST_ENTITIES_COVER_RESPONSE:
            return "ListEntitiesCoverResponse";
        case LIST_ENTITIES_SENSOR_RESPONSE:
            return "ListEntitiesSensorResponse";
        case LIST_ENTITIES_SWITCH_RESPONSE:
            return "ListEntitiesSwitchResponse";
        case LIST_ENTITIES_TEXT_SENSOR_RESPONSE:
            return "ListEntitiesTextSensorResponse";
        case LIST_ENTITIES_DONE_RESPONSE:
            return "ListEntitiesDoneResponse";
        case SUBSCRIBE_STATES_REQUEST:
            return "SubscribeStatesRequest";
        case BINARY_SENSOR_STATE_RESPONSE:
            return "BinarySensorStateResponse";
        case COVER_STATE_RESPONSE:
            return "CoverStateResponse";
        case SENSOR_STATE_RESPONSE:
            return "SensorStateResponse";
        case SWITCH_STATE_RESPONSE:
            return "SwitchStateResponse";
        case TEXT_SENSOR_STATE_RESPONSE:
            return "TextSensorStateResponse";
        case COVER_COMMAND_REQUEST:
            return "CoverCommandRequest";
        case SWITCH_COMMAND_REQUEST:
            return "SwitchCommandRequest";
        case LIST_ENTITIES_BUTTON_RESPONSE:
            return "ListEntitiesButtonResponse";
        case BUTTON_COMMAND_REQUEST:
            return "ButtonCommandRequest";
        default:
            return "Unknown";
    }
}

const char *NativeAPIClient::entityTypeName(EntityType type)
{
    switch (type)
    {
        case EntityType::BinarySensor:
            return "binary_sensor";
        case EntityType::Button:
            return "button";
        case EntityType::Cover:
            return "cover";
        case EntityType::Sensor:
            return "sensor";
        case EntityType::Switch:
            return "switch";
        case EntityType::TextSensor:
            return "text_sensor";
        default:
            return "unknown";
    }
}

bool NativeAPIClient::isPingRequest(uint32_t type)
{
    return type == PING_REQUEST;
}

bool NativeAPIClient::isDisconnectRequest(uint32_t type)
{
    return type == DISCONNECT_REQUEST;
}

bool NativeAPIClient::parseEntity(uint32_t messageType, const std::vector<uint8_t> &payload, EntityInfo &entity)
{
    std::vector<ProtoField> fields;
    if (!parseFields(payload, fields))
        return false;

    switch (messageType)
    {
        case LIST_ENTITIES_BINARY_SENSOR_RESPONSE:
            entity.type = EntityType::BinarySensor;
            entity.objectId = fieldString(fields, 1);
            entity.key = fieldUInt32(fields, 2);
            entity.name = fieldString(fields, 3);
            entity.uniqueId = fieldString(fields, 4);
            entity.deviceClass = fieldString(fields, 5);
            entity.disabledByDefault = fieldBool(fields, 7);
            entity.icon = fieldString(fields, 8);
            break;

        case LIST_ENTITIES_COVER_RESPONSE:
            entity.type = EntityType::Cover;
            entity.objectId = fieldString(fields, 1);
            entity.key = fieldUInt32(fields, 2);
            entity.name = fieldString(fields, 3);
            entity.uniqueId = fieldString(fields, 4);
            entity.deviceClass = fieldString(fields, 8);
            entity.disabledByDefault = fieldBool(fields, 9);
            entity.icon = fieldString(fields, 10);
            break;

        case LIST_ENTITIES_SENSOR_RESPONSE:
            entity.type = EntityType::Sensor;
            entity.objectId = fieldString(fields, 1);
            entity.key = fieldUInt32(fields, 2);
            entity.name = fieldString(fields, 3);
            entity.uniqueId = fieldString(fields, 4);
            entity.icon = fieldString(fields, 5);
            entity.unitOfMeasurement = fieldString(fields, 6);
            entity.accuracyDecimals = fieldInt32(fields, 7);
            entity.forceUpdate = fieldBool(fields, 8);
            entity.deviceClass = fieldString(fields, 9);
            entity.disabledByDefault = fieldBool(fields, 12);
            break;

        case LIST_ENTITIES_SWITCH_RESPONSE:
            entity.type = EntityType::Switch;
            entity.objectId = fieldString(fields, 1);
            entity.key = fieldUInt32(fields, 2);
            entity.name = fieldString(fields, 3);
            entity.uniqueId = fieldString(fields, 4);
            entity.icon = fieldString(fields, 5);
            entity.disabledByDefault = fieldBool(fields, 7);
            entity.deviceClass = fieldString(fields, 9);
            break;

        case LIST_ENTITIES_TEXT_SENSOR_RESPONSE:
            entity.type = EntityType::TextSensor;
            entity.objectId = fieldString(fields, 1);
            entity.key = fieldUInt32(fields, 2);
            entity.name = fieldString(fields, 3);
            entity.uniqueId = fieldString(fields, 4);
            entity.icon = fieldString(fields, 5);
            entity.disabledByDefault = fieldBool(fields, 6);
            entity.deviceClass = fieldString(fields, 8);
            break;

        case LIST_ENTITIES_BUTTON_RESPONSE:
            entity.type = EntityType::Button;
            entity.objectId = fieldString(fields, 1);
            entity.key = fieldUInt32(fields, 2);
            entity.name = fieldString(fields, 3);
            entity.uniqueId = fieldString(fields, 4);
            entity.icon = fieldString(fields, 5);
            entity.deviceClass = fieldString(fields, 6);
            entity.disabledByDefault = fieldBool(fields, 8);
            break;

        default:
            return false;
    }

    return entity.key != 0;
}

bool NativeAPIClient::parseState(uint32_t messageType, const std::vector<uint8_t> &payload, State &state)
{
    std::vector<ProtoField> fields;
    if (!parseFields(payload, fields))
        return false;

    switch (messageType)
    {
        case BINARY_SENSOR_STATE_RESPONSE:
            state.type = EntityType::BinarySensor;
            state.key = fieldUInt32(fields, 1);
            state.boolValue = fieldBool(fields, 2);
            state.missingState = fieldBool(fields, 3);
            return state.key != 0;

        case COVER_STATE_RESPONSE:
            state.type = EntityType::Cover;
            state.key = fieldUInt32(fields, 1);
            state.position = fieldFloat(fields, 3);
            state.operation = fieldInt32(fields, 5);
            return state.key != 0;

        case SENSOR_STATE_RESPONSE:
            state.type = EntityType::Sensor;
            state.key = fieldUInt32(fields, 1);
            state.floatValue = fieldFloat(fields, 2);
            state.missingState = fieldBool(fields, 3);
            return state.key != 0;

        case SWITCH_STATE_RESPONSE:
            state.type = EntityType::Switch;
            state.key = fieldUInt32(fields, 1);
            state.boolValue = fieldBool(fields, 2);
            return state.key != 0;

        case TEXT_SENSOR_STATE_RESPONSE:
            state.type = EntityType::TextSensor;
            state.key = fieldUInt32(fields, 1);
            state.textValue = fieldString(fields, 2);
            state.missingState = fieldBool(fields, 3);
            return state.key != 0;

        default:
            return false;
    }
}

bool NativeAPIClient::readByte(uint8_t &value)
{
    if (m_SocketFD < 0)
        return false;

    while (true)
    {
        const auto result = ::read(m_SocketFD, &value, 1);
        if (result == 1)
            return true;

        if (result < 0 && errno == EINTR)
            continue;

        return false;
    }
}

bool NativeAPIClient::readExact(std::vector<uint8_t> &buffer, size_t size)
{
    if (m_SocketFD < 0)
        return false;

    buffer.resize(size);
    size_t received = 0;

    while (received < size)
    {
        const auto result = ::read(m_SocketFD, buffer.data() + received, size - received);
        if (result > 0)
        {
            received += static_cast<size_t>(result);
            continue;
        }

        if (result < 0 && errno == EINTR)
            continue;

        return false;
    }

    return true;
}

bool NativeAPIClient::writeExact(const std::vector<uint8_t> &buffer)
{
    if (m_SocketFD < 0)
        return false;

    size_t written = 0;
    while (written < buffer.size())
    {
        const auto result = ::write(m_SocketFD, buffer.data() + written, buffer.size() - written);
        if (result > 0)
        {
            written += static_cast<size_t>(result);
            continue;
        }

        if (result < 0 && errno == EINTR)
            continue;

        return false;
    }

    return true;
}

bool NativeAPIClient::readVarUInt(uint32_t &value)
{
    value = 0;
    uint8_t shift = 0;

    while (shift < 32)
    {
        uint8_t byte = 0;
        if (!readByte(byte))
            return false;

        value |= static_cast<uint32_t>(byte & 0x7f) << shift;

        if ((byte & 0x80) == 0)
            return true;

        shift += 7;
    }

    return false;
}

} // namespace ESPHome
} // namespace INDI
