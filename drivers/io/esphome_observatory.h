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

#pragma once

#include "defaultdevice.h"
#include "indiesphomeinterface.h"
#include "indiinputinterface.h"
#include "indioutputinterface.h"
#include "indiweatherinterface.h"

#include <array>
#include <string>
#include <vector>

namespace Connection
{
class TCP;
}

class ESPHomeObservatory : public INDI::DefaultDevice,
    public INDI::ESPHomeInterface,
    public INDI::InputInterface,
    public INDI::OutputInterface,
    public INDI::WeatherInterface
{
    public:
        ESPHomeObservatory();
        virtual ~ESPHomeObservatory() override = default;

        virtual bool initProperties() override;
        virtual bool updateProperties() override;
        virtual void ISGetProperties(const char *dev) override;
        virtual bool ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n) override;
        virtual bool ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n) override;
        virtual bool ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n) override;

    protected:
        virtual const char *getDefaultName() override;
        virtual bool Disconnect() override;
        virtual void TimerHit() override;
        virtual bool saveConfigItems(FILE *fp) override;

        virtual bool UpdateDigitalInputs() override;
        virtual bool UpdateAnalogInputs() override;
        virtual bool UpdateDigitalOutputs() override;
        virtual bool CommandOutput(uint32_t index, OutputState command) override;
        virtual IPState updateWeather() override;

        virtual void ESPHomeDeviceInfoAvailable(const INDI::ESPHome::DeviceInfo &info) override;
        virtual void ESPHomeEntityDiscovered(const INDI::ESPHome::EntityInfo &entity) override;
        virtual void ESPHomeStateChanged(const INDI::ESPHome::State &state) override;

    private:
        struct WeatherBinding
        {
            uint32_t key {0};
            std::string parameter;
        };

        bool Handshake();
        void resetBindings();
        void updateDeviceInfoProperty();
        bool hasPendingESPHomeData() const;
        static std::string entityLabel(const INDI::ESPHome::EntityInfo &entity);
        static std::string weatherParameterForEntity(const INDI::ESPHome::EntityInfo &entity);
        void bindOutputEntity(const INDI::ESPHome::EntityInfo &entity);
        void bindInputEntity(const INDI::ESPHome::EntityInfo &entity);
        void bindWeatherEntity(const INDI::ESPHome::EntityInfo &entity);
        int outputIndexForKey(uint32_t key) const;
        int inputIndexForKey(uint32_t key) const;
        const WeatherBinding *weatherBindingForKey(uint32_t key) const;

        Connection::TCP *tcpConnection {nullptr};
        INDI::PropertyText LegacyPasswordTP {1};
        INDI::PropertyText DeviceInfoTP {6};

        static constexpr uint8_t MAX_DIGITAL_INPUTS {16};
        static constexpr uint8_t MAX_DIGITAL_OUTPUTS {16};
        static constexpr uint16_t DEFAULT_ESPHOME_PORT {6053};
        static constexpr uint8_t MAX_FRAMES_PER_TIMER {32};

        int PortFD {-1};
        std::array<uint32_t, MAX_DIGITAL_INPUTS> m_InputKeys {};
        std::array<uint32_t, MAX_DIGITAL_OUTPUTS> m_OutputKeys {};
        std::vector<WeatherBinding> m_WeatherBindings;
};
