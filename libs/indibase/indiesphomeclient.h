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

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace INDI
{
namespace ESPHome
{

enum class EntityType
{
    Unknown,
    BinarySensor,
    Sensor,
    Switch,
    TextSensor
};

struct Frame
{
    uint32_t type {0};
    std::vector<uint8_t> payload;
};

struct HelloResponse
{
    uint32_t apiVersionMajor {0};
    uint32_t apiVersionMinor {0};
    std::string serverInfo;
    std::string name;
};

struct ConnectResponse
{
    bool invalidPassword {false};
};

struct DeviceInfo
{
    bool usesPassword {false};
    bool hasDeepSleep {false};
    uint32_t webserverPort {0};
    std::string name;
    std::string friendlyName;
    std::string macAddress;
    std::string esphomeVersion;
    std::string compilationTime;
    std::string model;
    std::string manufacturer;
    std::string suggestedArea;
};

struct EntityInfo
{
    EntityType type {EntityType::Unknown};
    uint32_t key {0};
    int32_t accuracyDecimals {0};
    bool forceUpdate {false};
    bool disabledByDefault {false};
    std::string objectId;
    std::string name;
    std::string uniqueId;
    std::string icon;
    std::string unitOfMeasurement;
    std::string deviceClass;
};

struct State
{
    EntityType type {EntityType::Unknown};
    uint32_t key {0};
    bool boolValue {false};
    float floatValue {0};
    bool missingState {false};
    std::string textValue;
};

class NativeAPIClient
{
    public:
        NativeAPIClient() = default;
        explicit NativeAPIClient(int socketFD);

        void setSocket(int socketFD);
        int socket() const;

        bool sendHello(const std::string &clientInfo, HelloResponse &response);
        bool connect(const std::string &password, ConnectResponse &response);
        bool requestDeviceInfo(DeviceInfo &info);
        bool listEntities(std::vector<EntityInfo> &entities);
        bool subscribeStates();
        bool ping();
        bool commandSwitch(uint32_t key, bool state);

        bool readFrame(Frame &frame);
        bool readState(State &state, Frame *rawFrame = nullptr);
        bool writeFrame(uint32_t type, const std::vector<uint8_t> &payload);

        static const char *messageTypeName(uint32_t type);
        static const char *entityTypeName(EntityType type);

        static bool parseEntity(uint32_t messageType, const std::vector<uint8_t> &payload, EntityInfo &entity);
        static bool parseState(uint32_t messageType, const std::vector<uint8_t> &payload, State &state);

    private:
        bool readByte(uint8_t &value);
        bool readExact(std::vector<uint8_t> &buffer, size_t size);
        bool writeExact(const std::vector<uint8_t> &buffer);
        bool readVarUInt(uint32_t &value);

        int m_SocketFD {-1};
};

} // namespace ESPHome
} // namespace INDI
