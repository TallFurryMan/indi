/*******************************************************************************
  ESPHome Native API INDI interface helper
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

#include "indiesphomeclient.h"

#include <cstdint>
#include <string>
#include <vector>

namespace INDI
{
class DefaultDevice;

class ESPHomeInterface
{
    protected:
        explicit ESPHomeInterface(DefaultDevice *defaultDevice);
        virtual ~ESPHomeInterface() = default;

        bool connectESPHome(int socketFD, const std::string &clientInfo, const std::string &password = std::string());
        bool disconnectESPHome();
        bool subscribeESPHomeStates();
        bool processESPHomeState();
        bool commandESPHomeSwitch(uint32_t key, bool enabled);

        const ESPHome::DeviceInfo &getESPHomeDeviceInfo() const;
        const std::vector<ESPHome::EntityInfo> &getESPHomeEntities() const;
        const ESPHome::EntityInfo *findESPHomeEntity(ESPHome::EntityType type, const std::string &objectIdOrName) const;
        const ESPHome::EntityInfo *findESPHomeEntityByKey(uint32_t key) const;

        bool isESPHomeConnected() const;
        static bool isESPHomeEncryptionSupported();
        const std::string &getESPHomeLastError() const;

        virtual void ESPHomeDeviceInfoAvailable(const ESPHome::DeviceInfo &info);
        virtual void ESPHomeEntityDiscovered(const ESPHome::EntityInfo &entity);
        virtual void ESPHomeStateChanged(const ESPHome::State &state);

    private:
        void setLastError(const std::string &message);

        DefaultDevice *m_defaultDevice {nullptr};
        ESPHome::NativeAPIClient m_Client;
        ESPHome::HelloResponse m_Hello;
        ESPHome::AuthenticationResponse m_Authentication;
        ESPHome::DeviceInfo m_DeviceInfo;
        std::vector<ESPHome::EntityInfo> m_Entities;
        std::string m_LastError;
        bool m_IsConnected {false};
};

} // namespace INDI
