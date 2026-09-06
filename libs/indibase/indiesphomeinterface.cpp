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

#include "indiesphomeinterface.h"

#include "defaultdevice.h"
#include "indilogger.h"

#include <algorithm>

namespace
{
bool nameMatches(const INDI::ESPHome::EntityInfo &entity, const std::string &objectIdOrName)
{
    return entity.objectId == objectIdOrName || entity.name == objectIdOrName || entity.uniqueId == objectIdOrName;
}
}

namespace INDI
{

ESPHomeInterface::ESPHomeInterface(DefaultDevice *defaultDevice) : m_defaultDevice(defaultDevice)
{
}

bool ESPHomeInterface::connectESPHome(int socketFD, const std::string &clientInfo, const std::string &password)
{
    m_IsConnected = false;
    m_Entities.clear();
    m_DeviceInfo = ESPHome::DeviceInfo {};
    m_Client.setSocket(socketFD);

    if (!m_Client.sendHello(clientInfo, m_Hello))
    {
        setLastError("ESPHome hello failed.");
        return false;
    }

    if (!m_Client.connect(password, m_Connect))
    {
        setLastError("ESPHome connect failed.");
        return false;
    }

    if (m_Connect.invalidPassword)
    {
        setLastError("ESPHome rejected the API password.");
        return false;
    }

    if (!m_Client.requestDeviceInfo(m_DeviceInfo))
    {
        setLastError("ESPHome device info request failed.");
        return false;
    }

    ESPHomeDeviceInfoAvailable(m_DeviceInfo);

    if (!m_Client.listEntities(m_Entities))
    {
        setLastError("ESPHome entity discovery failed.");
        return false;
    }

    for (const auto &entity : m_Entities)
        ESPHomeEntityDiscovered(entity);

    m_LastError.clear();
    m_IsConnected = true;

    if (m_defaultDevice != nullptr)
    {
        DEBUGFDEVICE(m_defaultDevice->getDeviceName(), INDI::Logger::DBG_SESSION,
                     "ESPHome API connected to %s with %u discovered entities.",
                     m_DeviceInfo.name.empty() ? m_Hello.name.c_str() : m_DeviceInfo.name.c_str(),
                     static_cast<unsigned int>(m_Entities.size()));
    }
    return true;
}

bool ESPHomeInterface::disconnectESPHome()
{
    m_IsConnected = false;
    m_Client.setSocket(-1);
    return true;
}

bool ESPHomeInterface::subscribeESPHomeStates()
{
    if (!m_IsConnected)
    {
        setLastError("ESPHome is not connected.");
        return false;
    }

    if (!m_Client.subscribeStates())
    {
        setLastError("ESPHome state subscription failed.");
        return false;
    }

    return true;
}

bool ESPHomeInterface::processESPHomeState()
{
    if (!m_IsConnected)
    {
        setLastError("ESPHome is not connected.");
        return false;
    }

    ESPHome::Frame frame;
    if (!m_Client.readFrame(frame))
    {
        setLastError("ESPHome state read failed.");
        return false;
    }

    ESPHome::State state;
    if (ESPHome::NativeAPIClient::parseState(frame.type, frame.payload, state))
        ESPHomeStateChanged(state);

    return true;
}

bool ESPHomeInterface::commandESPHomeSwitch(uint32_t key, bool enabled)
{
    if (!m_IsConnected)
    {
        setLastError("ESPHome is not connected.");
        return false;
    }

    if (!m_Client.commandSwitch(key, enabled))
    {
        setLastError("ESPHome switch command failed.");
        return false;
    }

    return true;
}

const ESPHome::DeviceInfo &ESPHomeInterface::getESPHomeDeviceInfo() const
{
    return m_DeviceInfo;
}

const std::vector<ESPHome::EntityInfo> &ESPHomeInterface::getESPHomeEntities() const
{
    return m_Entities;
}

const ESPHome::EntityInfo *ESPHomeInterface::findESPHomeEntity(ESPHome::EntityType type,
        const std::string &objectIdOrName) const
{
    const auto it = std::find_if(m_Entities.begin(), m_Entities.end(), [type, &objectIdOrName](const auto &entity)
    {
        return entity.type == type && nameMatches(entity, objectIdOrName);
    });

    return it == m_Entities.end() ? nullptr : &(*it);
}

const ESPHome::EntityInfo *ESPHomeInterface::findESPHomeEntityByKey(uint32_t key) const
{
    const auto it = std::find_if(m_Entities.begin(), m_Entities.end(), [key](const auto &entity)
    {
        return entity.key == key;
    });

    return it == m_Entities.end() ? nullptr : &(*it);
}

bool ESPHomeInterface::isESPHomeConnected() const
{
    return m_IsConnected;
}

bool ESPHomeInterface::isESPHomeEncryptionSupported() const
{
    return false;
}

const std::string &ESPHomeInterface::getESPHomeLastError() const
{
    return m_LastError;
}

void ESPHomeInterface::ESPHomeDeviceInfoAvailable(const ESPHome::DeviceInfo &info)
{
    INDI_UNUSED(info);
}

void ESPHomeInterface::ESPHomeEntityDiscovered(const ESPHome::EntityInfo &entity)
{
    INDI_UNUSED(entity);
}

void ESPHomeInterface::ESPHomeStateChanged(const ESPHome::State &state)
{
    INDI_UNUSED(state);
}

void ESPHomeInterface::setLastError(const std::string &message)
{
    m_LastError = message;

    if (m_defaultDevice != nullptr)
        DEBUGFDEVICE(m_defaultDevice->getDeviceName(), INDI::Logger::DBG_ERROR, "%s", message.c_str());
}

} // namespace INDI
