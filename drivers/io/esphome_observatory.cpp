/*******************************************************************************
  ESPHome Observatory Controller
  Copyright(c) 2026. All rights reserved.

  This program is free software; you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by the Free
  Software Foundation; either version 2 of the License, or (at your option)
  any later version.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
  FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
*******************************************************************************/

#include "esphome_observatory.h"

#include "connectionplugins/connectiontcp.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>
#include <sys/select.h>
#include <unistd.h>

static std::unique_ptr<ESPHomeObservatory> esphomeObservatory(new ESPHomeObservatory());

namespace
{
std::string lowerASCII(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char oneChar)
    {
        return static_cast<char>(std::tolower(oneChar));
    });
    return value;
}

bool contains(const std::string &haystack, const std::string &needle)
{
    return haystack.find(needle) != std::string::npos;
}
}

ESPHomeObservatory::ESPHomeObservatory()
    : INDI::ESPHomeInterface(this),
      INDI::InputInterface(this),
      INDI::OutputInterface(this),
      INDI::WeatherInterface(this)
{
    setVersion(1, 0);
}

const char *ESPHomeObservatory::getDefaultName()
{
    return "ESPHome Observatory";
}

bool ESPHomeObservatory::initProperties()
{
    INDI::DefaultDevice::initProperties();

    setDriverInterface(AUX_INTERFACE | INPUT_INTERFACE | OUTPUT_INTERFACE | WEATHER_INTERFACE);

    INDI::InputInterface::initProperties(MAIN_CONTROL_TAB, MAX_DIGITAL_INPUTS, 0, "Binary Sensor");
    INDI::OutputInterface::initProperties(MAIN_CONTROL_TAB, MAX_DIGITAL_OUTPUTS, "Switch");

    INDI::WeatherInterface::initProperties("Environment", "Environment");
    addParameter("WEATHER_TEMPERATURE", "Temperature (C)", -15, 35, 15);
    addParameter("WEATHER_HUMIDITY", "Humidity %", 0, 100, 15);
    addParameter("WEATHER_DEWPOINT", "Dew Point (C)", -25, 35, 15);
    addParameter("WEATHER_PRESSURE", "Pressure (hPa)", 800, 1100, 10);
    addParameter("WEATHER_ILLUMINANCE", "Illuminance (lx)", 0, 200000, 10);
    addParameter("WEATHER_WIND_SPEED", "Wind Speed (m/s)", 0, 20, 20);
    addParameter("WEATHER_RAIN", "Rain", 0, 0.5, 20);
    setCriticalParameter("WEATHER_HUMIDITY");
    setCriticalParameter("WEATHER_WIND_SPEED");
    setCriticalParameter("WEATHER_RAIN");

    LegacyPasswordTP[0].fill("PASSWORD", "Password", "");
    LegacyPasswordTP.fill(getDeviceName(), "ESPHOME_API_PASSWORD", "Legacy API Password", CONNECTION_TAB, IP_RW, 60, IPS_IDLE);
    LegacyPasswordTP.load();

    DeviceInfoTP[0].fill("NAME", "Name", "");
    DeviceInfoTP[1].fill("FRIENDLY_NAME", "Friendly Name", "");
    DeviceInfoTP[2].fill("VERSION", "Version", "");
    DeviceInfoTP[3].fill("MODEL", "Model", "");
    DeviceInfoTP[4].fill("MAC_ADDRESS", "MAC", "");
    DeviceInfoTP[5].fill("ENTITIES", "Entities", "0");
    DeviceInfoTP.fill(getDeviceName(), "ESPHOME_DEVICE_INFO", "ESPHome", INFO_TAB, IP_RO, 60, IPS_IDLE);

    tcpConnection = new Connection::TCP(this);
    tcpConnection->setDefaultHost("esphome.local");
    tcpConnection->setDefaultPort(DEFAULT_ESPHOME_PORT);
    tcpConnection->setConnectionType(Connection::TCP::TYPE_TCP);
    tcpConnection->registerHandshake([this]()
    {
        return Handshake();
    });
    registerConnection(tcpConnection);

    addAuxControls();
    setDefaultPollingPeriod(500);

    return true;
}

void ESPHomeObservatory::ISGetProperties(const char *dev)
{
    INDI::DefaultDevice::ISGetProperties(dev);
    defineProperty(LegacyPasswordTP);
}

bool ESPHomeObservatory::updateProperties()
{
    INDI::DefaultDevice::updateProperties();
    INDI::InputInterface::updateProperties();
    INDI::OutputInterface::updateProperties();
    INDI::WeatherInterface::updateProperties();

    if (isConnected())
    {
        updateDeviceInfoProperty();
        defineProperty(DeviceInfoTP);
        SetTimer(getCurrentPollingPeriod());
    }
    else
    {
        deleteProperty(DeviceInfoTP);
    }

    return true;
}

bool ESPHomeObservatory::Disconnect()
{
    disconnectESPHome();
    PortFD = -1;
    return INDI::DefaultDevice::Disconnect();
}

bool ESPHomeObservatory::Handshake()
{
    resetBindings();
    PortFD = tcpConnection->getPortFD();

    if (!connectESPHome(PortFD, "INDI ESPHome Observatory", LegacyPasswordTP[0].getText()))
        return false;

    if (!subscribeESPHomeStates())
        return false;

    updateDeviceInfoProperty();
    return true;
}

void ESPHomeObservatory::TimerHit()
{
    if (!isConnected())
        return;

    for (uint8_t i = 0; i < MAX_FRAMES_PER_TIMER && hasPendingESPHomeData(); ++i)
    {
        if (!processESPHomeState())
            break;
    }

    SetTimer(getCurrentPollingPeriod());
}

bool ESPHomeObservatory::ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    if (dev != nullptr && !strcmp(dev, getDeviceName()))
    {
        if (LegacyPasswordTP.isNameMatch(name))
        {
            LegacyPasswordTP.update(texts, names, n);
            LegacyPasswordTP.setState(IPS_OK);
            LegacyPasswordTP.apply();
            saveConfig(LegacyPasswordTP);
            return true;
        }
    }

    if (INDI::InputInterface::processText(dev, name, texts, names, n))
        return true;

    if (INDI::OutputInterface::processText(dev, name, texts, names, n))
        return true;

    return INDI::DefaultDevice::ISNewText(dev, name, texts, names, n);
}

bool ESPHomeObservatory::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (INDI::OutputInterface::processSwitch(dev, name, states, names, n))
        return true;

    if (INDI::WeatherInterface::processSwitch(dev, name, states, names, n))
        return true;

    return INDI::DefaultDevice::ISNewSwitch(dev, name, states, names, n);
}

bool ESPHomeObservatory::ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    if (INDI::OutputInterface::processNumber(dev, name, values, names, n))
        return true;

    if (INDI::WeatherInterface::processNumber(dev, name, values, names, n))
        return true;

    return INDI::DefaultDevice::ISNewNumber(dev, name, values, names, n);
}

bool ESPHomeObservatory::saveConfigItems(FILE *fp)
{
    INDI::DefaultDevice::saveConfigItems(fp);
    INDI::InputInterface::saveConfigItems(fp);
    INDI::OutputInterface::saveConfigItems(fp);
    INDI::WeatherInterface::saveConfigItems(fp);
    LegacyPasswordTP.save(fp);
    return true;
}

bool ESPHomeObservatory::UpdateDigitalInputs()
{
    return true;
}

bool ESPHomeObservatory::UpdateAnalogInputs()
{
    return true;
}

bool ESPHomeObservatory::UpdateDigitalOutputs()
{
    return true;
}

bool ESPHomeObservatory::CommandOutput(uint32_t index, OutputState command)
{
    if (index >= m_OutputKeys.size() || m_OutputKeys[index] == 0)
    {
        LOGF_ERROR("No ESPHome switch is bound to output %u.", index + 1);
        return false;
    }

    return commandESPHomeSwitch(m_OutputKeys[index], command == OutputState::On);
}

IPState ESPHomeObservatory::updateWeather()
{
    return IPS_OK;
}

void ESPHomeObservatory::ESPHomeDeviceInfoAvailable(const INDI::ESPHome::DeviceInfo &info)
{
    INDI_UNUSED(info);
    updateDeviceInfoProperty();
}

void ESPHomeObservatory::ESPHomeEntityDiscovered(const INDI::ESPHome::EntityInfo &entity)
{
    switch (entity.type)
    {
        case INDI::ESPHome::EntityType::Switch:
            bindOutputEntity(entity);
            break;

        case INDI::ESPHome::EntityType::BinarySensor:
            bindInputEntity(entity);
            break;

        case INDI::ESPHome::EntityType::Sensor:
            bindWeatherEntity(entity);
            break;

        default:
            break;
    }
}

void ESPHomeObservatory::ESPHomeStateChanged(const INDI::ESPHome::State &state)
{
    if (state.type == INDI::ESPHome::EntityType::Switch)
    {
        const int index = outputIndexForKey(state.key);
        if (index < 0)
            return;

        auto &output = DigitalOutputsSP[static_cast<size_t>(index)];
        const auto oldState = output.findOnSwitchIndex();
        const auto newState = state.boolValue ? OutputInterface::On : OutputInterface::Off;
        if (oldState != newState)
        {
            output.reset();
            output[OutputInterface::Off].setState(state.boolValue ? ISS_OFF : ISS_ON);
            output[OutputInterface::On].setState(state.boolValue ? ISS_ON : ISS_OFF);
            output.setState(IPS_OK);
            output.apply();
        }
        return;
    }

    if (state.type == INDI::ESPHome::EntityType::BinarySensor)
    {
        const int index = inputIndexForKey(state.key);
        if (index < 0)
            return;

        auto &input = DigitalInputsSP[static_cast<size_t>(index)];
        const auto oldState = input.findOnSwitchIndex();
        const auto newState = state.boolValue ? InputInterface::On : InputInterface::Off;
        if (oldState != newState)
        {
            input.reset();
            input[InputInterface::Off].setState(state.boolValue ? ISS_OFF : ISS_ON);
            input[InputInterface::On].setState(state.boolValue ? ISS_ON : ISS_OFF);
            input.setState(IPS_OK);
            input.apply();
        }
        return;
    }

    if (state.type == INDI::ESPHome::EntityType::Sensor && !state.missingState)
    {
        const auto binding = weatherBindingForKey(state.key);
        if (binding == nullptr)
            return;

        setParameterValue(binding->parameter, state.floatValue);
        if (syncCriticalParameters())
            critialParametersLP.apply();

        ParametersNP.setState(IPS_OK);
        ParametersNP.apply();
    }
}

void ESPHomeObservatory::resetBindings()
{
    m_InputKeys.fill(0);
    m_OutputKeys.fill(0);
    m_WeatherBindings.clear();
}

void ESPHomeObservatory::updateDeviceInfoProperty()
{
    const auto &info = getESPHomeDeviceInfo();
    DeviceInfoTP[0].setText(info.name);
    DeviceInfoTP[1].setText(info.friendlyName);
    DeviceInfoTP[2].setText(info.esphomeVersion);
    DeviceInfoTP[3].setText(info.model);
    DeviceInfoTP[4].setText(info.macAddress);
    DeviceInfoTP[5].setText(std::to_string(getESPHomeEntities().size()));
    DeviceInfoTP.setState(isESPHomeConnected() ? IPS_OK : IPS_IDLE);
}

bool ESPHomeObservatory::hasPendingESPHomeData() const
{
    if (PortFD < 0)
        return false;

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(PortFD, &readSet);

    timeval timeout {};
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    const auto result = select(PortFD + 1, &readSet, nullptr, nullptr, &timeout);
    return result > 0 && FD_ISSET(PortFD, &readSet);
}

std::string ESPHomeObservatory::entityLabel(const INDI::ESPHome::EntityInfo &entity) const
{
    if (!entity.name.empty())
        return entity.name;

    if (!entity.objectId.empty())
        return entity.objectId;

    return INDI::ESPHome::NativeAPIClient::entityTypeName(entity.type);
}

std::string ESPHomeObservatory::weatherParameterForEntity(const INDI::ESPHome::EntityInfo &entity) const
{
    const auto deviceClass = lowerASCII(entity.deviceClass);
    const auto objectId = lowerASCII(entity.objectId);
    const auto name = lowerASCII(entity.name);
    const auto unit = lowerASCII(entity.unitOfMeasurement);
    const auto combined = deviceClass + " " + objectId + " " + name + " " + unit;

    if (contains(combined, "dew"))
        return "WEATHER_DEWPOINT";
    if (contains(combined, "humid"))
        return "WEATHER_HUMIDITY";
    if (contains(combined, "temp") || unit == "c" || unit == "f")
        return "WEATHER_TEMPERATURE";
    if (contains(combined, "pressure") || contains(unit, "hpa") || contains(unit, "mbar"))
        return "WEATHER_PRESSURE";
    if (contains(combined, "illuminance") || contains(combined, "lux") || unit == "lx")
        return "WEATHER_ILLUMINANCE";
    if (contains(combined, "wind") && (contains(combined, "speed") || contains(unit, "m/s") || contains(unit, "km/h")))
        return "WEATHER_WIND_SPEED";
    if (contains(combined, "rain") || contains(combined, "precipitation"))
        return "WEATHER_RAIN";

    return {};
}

void ESPHomeObservatory::bindOutputEntity(const INDI::ESPHome::EntityInfo &entity)
{
    const auto slot = std::find(m_OutputKeys.begin(), m_OutputKeys.end(), 0);
    if (slot == m_OutputKeys.end())
        return;

    const auto index = static_cast<size_t>(std::distance(m_OutputKeys.begin(), slot));
    m_OutputKeys[index] = entity.key;

    const auto label = entityLabel(entity);
    DigitalOutputsSP[index].setLabel(label);
    DigitalOutputLabelsTP[index].setText(label);
    PulseDurationNP[index].setLabel(label);
}

void ESPHomeObservatory::bindInputEntity(const INDI::ESPHome::EntityInfo &entity)
{
    const auto slot = std::find(m_InputKeys.begin(), m_InputKeys.end(), 0);
    if (slot == m_InputKeys.end())
        return;

    const auto index = static_cast<size_t>(std::distance(m_InputKeys.begin(), slot));
    m_InputKeys[index] = entity.key;

    const auto label = entityLabel(entity);
    DigitalInputsSP[index].setLabel(label);
    DigitalInputLabelsTP[index].setText(label);
}

void ESPHomeObservatory::bindWeatherEntity(const INDI::ESPHome::EntityInfo &entity)
{
    const auto parameter = weatherParameterForEntity(entity);
    if (parameter.empty())
        return;

    const auto duplicate = std::find_if(m_WeatherBindings.begin(), m_WeatherBindings.end(), [&parameter](const auto &binding)
    {
        return binding.parameter == parameter;
    });

    if (duplicate == m_WeatherBindings.end())
        m_WeatherBindings.push_back({entity.key, parameter});
}

int ESPHomeObservatory::outputIndexForKey(uint32_t key) const
{
    const auto it = std::find(m_OutputKeys.begin(), m_OutputKeys.end(), key);
    return it == m_OutputKeys.end() ? -1 : static_cast<int>(std::distance(m_OutputKeys.begin(), it));
}

int ESPHomeObservatory::inputIndexForKey(uint32_t key) const
{
    const auto it = std::find(m_InputKeys.begin(), m_InputKeys.end(), key);
    return it == m_InputKeys.end() ? -1 : static_cast<int>(std::distance(m_InputKeys.begin(), it));
}

const ESPHomeObservatory::WeatherBinding *ESPHomeObservatory::weatherBindingForKey(uint32_t key) const
{
    const auto it = std::find_if(m_WeatherBindings.begin(), m_WeatherBindings.end(), [key](const auto &binding)
    {
        return binding.key == key;
    });

    return it == m_WeatherBindings.end() ? nullptr : &(*it);
}
