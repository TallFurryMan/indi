/*
    INDI 3rd party driver
    Lacerta MGen Autoguider INDI driver, implemented with help from
    Tommy (teleskopaustria@gmail.com) and Zoltan (mgen@freemail.hu).

    Teleskop & Mikroskop Zentrum (www.teleskop.austria.com)
    A-1050 WIEN, Schönbrunner Strasse 96
    +43 699 1197 0808 (Shop in Wien und Rechnungsanschrift)
    A-4020 LINZ, Gärtnerstrasse 16
    +43 699 1901 2165 (Shop in Linz)

    Lacerta GmbH
    UmsatzSt. Id. Nr.: AT U67203126
    Firmenbuch Nr.: FN 379484s

    Copyright (C) 2017 by TallFurryMan (eric.dejouhanet@gmail.com)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

/** \file mgenautoguider.cpp
    \brief INDI device able to connect to a Lacerta MGen Autoguider.
    \author Eric Dejouhanet
*/

#include <stdio.h>
#include <unistd.h>

#include <memory>
#include <vector>
#include <queue>

#include "indidevapi.h"
#include "indilogger.h"
#include "indiccd.h"

#include "mgen.h"
#include "mgenautoguider.h"

#include "mgcp_enter_normal_mode.h"
#include "mgcp_query_device.h"
#include "mgcmd_nop1.h"
#include "mgcmd_get_fw_version.h"
#include "mgcmd_read_adcs.h"
#include "mgcmd_read_guide_frame.h"
#include "mgio_read_display_frame.h"
#include "mgio_insert_button.h"

using namespace std;
std::unique_ptr<MGenAutoguider> mgenAutoguider(new MGenAutoguider());
MGenAutoguider &MGenAutoguider::instance()
{
    return *mgenAutoguider;
}

const MGIO_INSERT_BUTTON::Button MGIO_Buttons[] = { MGIO_INSERT_BUTTON::IOB_ESC,   MGIO_INSERT_BUTTON::IOB_SET,
                                                    MGIO_INSERT_BUTTON::IOB_UP,    MGIO_INSERT_BUTTON::IOB_LEFT,
                                                    MGIO_INSERT_BUTTON::IOB_RIGHT, MGIO_INSERT_BUTTON::IOB_DOWN };

/**************************************************************************************
** Return properties of device->
***************************************************************************************/
void ISGetProperties(const char *dev)
{
    //IDLog("%s: get properties.", __FUNCTION__);
    mgenAutoguider->ISGetProperties(dev);
}

/**************************************************************************************
** Process new switch from client
***************************************************************************************/
void ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    //IDLog("%s: new switch '%s'.", __FUNCTION__, name);
    mgenAutoguider->ISNewSwitch(dev, name, states, names, n);
}

bool MGenAutoguider::ISNewSwitch(const char *dev, const char *name, ISState *states, char *names[], int n)
{
    if (device && device->isConnected())
    {
        if (!strcmp(dev, getDeviceName()))
        {
            if (!strcmp(name, "MGEN_UI_REMOTE"))
            {
                IUUpdateSwitch(&ui.remote.property, states, names, n);
                ISwitch *const key_switch = IUFindOnSwitch(&ui.remote.property);
                if (key_switch)
                {
                    ui.is_enabled = key_switch->aux == nullptr ? false : true;
                    ui.remote.property.s = IPS_OK;
                }
                else ui.remote.property.s = IPS_ALERT;
                IDSetSwitch(&ui.remote.property, NULL);
            }
            if (!strcmp(name, "MGEN_UI_BUTTONS1"))
            {
                IUUpdateSwitch(&ui.buttons.properties[0], states, names, n);
                ISwitch *const key_switch = IUFindOnSwitch(&ui.buttons.properties[0]);
                if (key_switch)
                {
                    MGIO_INSERT_BUTTON::Button button = *(reinterpret_cast<MGIO_INSERT_BUTTON::Button *>(key_switch->aux));
                    MGIO_INSERT_BUTTON(button).ask(*device);
                    key_switch->s              = ISS_OFF;
                    ui.buttons.properties[0].s = IPS_OK;
                }
                else ui.buttons.properties[0].s = IPS_ALERT;
                IDSetSwitch(&ui.buttons.properties[0], NULL);
            }
            if (!strcmp(name, "MGEN_UI_BUTTONS2"))
            {
                IUUpdateSwitch(&ui.buttons.properties[1], states, names, n);
                ISwitch *const key_switch = IUFindOnSwitch(&ui.buttons.properties[1]);
                if (key_switch)
                {
                    MGIO_INSERT_BUTTON::Button button = *(reinterpret_cast<MGIO_INSERT_BUTTON::Button *>(key_switch->aux));
                    MGIO_INSERT_BUTTON(button).ask(*device);
                    key_switch->s              = ISS_OFF;
                    ui.buttons.properties[1].s = IPS_OK;
                }
                else ui.buttons.properties[1].s = IPS_ALERT;
                IDSetSwitch(&ui.buttons.properties[1], NULL);
            }
            if (!strcmp(name, "MGEN_PHD2"))
            {
                IUUpdateSwitch(&guide.phd2.property, states, names, n);
                ISwitch *const key_switch = IUFindOnSwitch(&guide.phd2.property);
                if (key_switch)
                {
                    if (!strcmp("MGEN_PHD2_SEARCH_STAR", key_switch->name))
                    {
                        engageSearchStar();
                    }
                    else if (!strcmp("MGEN_PHD2_CALIBRATE", key_switch->name))
                    {
                        engageCalibrate();
                    }
                    else if (!strcmp("MGEN_PHD2_START_GUIDING", key_switch->name))
                    {
                        engageToggleGuiding();
                    }
                    else if (!strcmp("MGEN_PHD2_STOP_GUIDING", key_switch->name))
                    {
                        engageToggleGuiding();
                    }
                    key_switch->s = ISS_OFF;
                    guide.phd2.property.s = IPS_OK;
                }
                else guide.phd2.property.s = IPS_ALERT;
                IDSetSwitch(&guide.phd2.property, NULL);
            }
        }
    }

    return CCD::ISNewSwitch(dev, name, states, names, n);
}

/**************************************************************************************
** Process new text from client
***************************************************************************************/
void ISNewText(const char *dev, const char *name, char *texts[], char *names[], int n)
{
    //IDLog("%s: get new text '%s'.", __FUNCTION__, name);
    mgenAutoguider->ISNewText(dev, name, texts, names, n);
}

/**************************************************************************************
** Process new number from client
***************************************************************************************/
void ISNewNumber(const char *dev, const char *name, double values[], char *names[], int n)
{
    //IDLog("%s: new number '%s'.", __FUNCTION__, name);
    mgenAutoguider->ISNewNumber(dev, name, values, names, n);
}

bool MGenAutoguider::ISNewNumber(char const *dev, char const *name, double values[], char *names[], int n)
{
    if (device && device->isConnected())
    {
        if (!strcmp(dev, getDeviceName()))
        {
            if (!strcmp(name, "MGEN_UI_OPTIONS"))
            {
                IUUpdateNumber(&ui.framerate.property, values, names, n);
                ui.framerate.property.s = IPS_OK;
                IDSetNumber(&ui.framerate.property, NULL);
                RemoveTimer(ui.timer);
                TimerHit();
                _S("UI refresh rate is now %+02.2f frames per second", ui.framerate.number.value);
                return true;
            }
        }
    }

    return CCD::ISNewNumber(dev, name, values, names, n);
}

/**************************************************************************************
** Process new blob from client
***************************************************************************************/
void ISNewBLOB(const char *dev, const char *name, int sizes[], int blobsizes[], char *blobs[], char *formats[],
               char *names[], int n)
{
    //IDLog("%s: get new blob '%s'.", __FUNCTION__, name);
    mgenAutoguider->ISNewBLOB(dev, name, sizes, blobsizes, blobs, formats, names, n);
}

/**************************************************************************************
** Process snooped property from another driver
***************************************************************************************/
void ISSnoopDevice(XMLEle *root)
{
    INDI_UNUSED(root);
}

MGenAutoguider::MGenAutoguider(): device(NULL)
{
    SetCCDParams(128, 64, 8, 5.0f, 5.0f);
    PrimaryCCD.setFrameBufferSize(PrimaryCCD.getXRes() * PrimaryCCD.getYRes() * PrimaryCCD.getBPP() / 8, true);
    SetCCDCapability(CCD_HAS_STREAMING);
}

MGenAutoguider::~MGenAutoguider()
{
    Disconnect();
    setConnected(false, IPS_ALERT);
    updateProperties();
    if (device)
        delete device;
}

bool MGenAutoguider::initProperties()
{
    _D("initiating properties", "");

    INDI::CCD::initProperties();

    /* Change defaults */
    {
        /* Make primary CCD compressed by default */
        //SetGuiderCCDCompression(true);

        /* Change name of primary CCD main blob so it doesn't get confused with a capture */
        //char const blob_name[] = "REMOTEUI1";
        //strncpy(PrimaryCCD.FitsB.name, blob_name, sizeof(blob_name));
        //strncpy(PrimaryCCD.FitsBP.name, blob_name, sizeof(blob_name));
    }

    addDebugControl();

    {
        char const TAB[] = "Main Control";
        IUFillText(&version.firmware.text, "MGEN_FIRMWARE_VERSION", "Firmware version", "n/a");
        IUFillTextVector(&version.firmware.property, &version.firmware.text, 1, getDeviceName(), "Versions", "Versions",
                         TAB, IP_RO, 60, IPS_IDLE);
        IUFillNumber(&guide.drift.ascension, "MGEN_DRIFT_RA", "RA", "%+06.2f pix", -65536.0f, +65536.0f, 0, 0);
        IUFillNumber(&guide.drift.declination, "MGEN_DRIFT_DEC", "DEC", "%+06.2f pix", -65536.0f, +65536.0f, 0, 0);
        IUFillNumberVector(&guide.drift.property, (INumber *)&guide.drift, 2, getDeviceName(), "MGEN_DRIFTS",
                                   "Drifts", TAB, IP_RO, 60, IPS_IDLE);
        IUFillLight(&guide.state.has_guide_star, "MGEN_HAS_GUIDE_STAR", "Is present?", IPS_IDLE);
        IUFillLightVector(&guide.state.property, &guide.state.has_guide_star, 1, getDeviceName(), "MGEN_GUIDE_STAR",
                          "Guide star", TAB, IPS_IDLE);
        IUFillSwitch(&guide.phd2.search_star, "MGEN_PHD2_SEARCH_STAR", "Search Star", ISS_OFF);
        IUFillSwitch(&guide.phd2.calibrate, "MGEN_PHD2_CALIBRATE", "Calibrate", ISS_OFF);
        IUFillSwitch(&guide.phd2.start_guiding, "MGEN_PHD2_START_GUIDING", "Start guiding", ISS_OFF);
        IUFillSwitch(&guide.phd2.stop_guiding, "MGEN_PHD2_STOP_GUIDING", "Stop guiding", ISS_OFF);
        IUFillSwitchVector(&guide.phd2.property,(ISwitch*) &guide.phd2, 4, getDeviceName(), "MGEN_PHD2",
                          "PHD2", TAB, IP_RW, ISR_ATMOST1, 0, IPS_IDLE);
    }

    {
        char const TAB[] = "Voltages";
        IUFillNumber(&voltage.levels.logic, "MGEN_LOGIC_VOLTAGE", "Logic [4.8V, 5.1V]", "%+02.2f V", 0, 220, 0, 0);
        IUFillNumber(&voltage.levels.input, "MGEN_INPUT_VOLTAGE", "Input [9.0V, 15.0V]", "%+02.2f V", 0, 220, 0, 0);
        IUFillNumber(&voltage.levels.reference, "MGEN_REFERENCE_VOLTAGE", "Reference [1.1V, 1.3V]", "%+02.2f V", 0, 220,
                     0, 0);
        IUFillNumberVector(&voltage.property, (INumber *)&voltage.levels, 3, getDeviceName(), "MGEN_VOLTAGES",
                           "Voltages", TAB, IP_RO, 60, IPS_IDLE);
    }

    {
        char const TAB[] = "Remote UI";
        IUFillSwitch(&ui.remote.switches[0], "MGEN_UI_FRAMERATE_ENABLE", "Enable", ISS_OFF);
        ui.remote.switches[0].aux = (void*)1;
        IUFillSwitch(&ui.remote.switches[1], "MGEN_UI_FRAMERATE_DISABLE", "Disable", ISS_ON);
        ui.remote.switches[1].aux = (void*)0;
        IUFillSwitchVector(&ui.remote.property, &ui.remote.switches[0], 2, getDeviceName(), "MGEN_UI_REMOTE",
                           "Enable Remote UI", TAB, IP_RW, ISR_1OFMANY, 0, IPS_OK);
        /* FIXME frame rate kills connection quickly, make INDI::CCD blob compressed by default at the expense of server cpu power */
        IUFillNumber(&ui.framerate.number, "MGEN_UI_FRAMERATE", "Frame rate", "%+02.2f fps", 0, 4, 0.25f, 0.5f);
        IUFillNumberVector(&ui.framerate.property, &ui.framerate.number, 1, getDeviceName(), "MGEN_UI_OPTIONS", "UI",
                           TAB, IP_RW, 60, IPS_IDLE);

        IUFillSwitch(&ui.buttons.switches[0], "MGEN_UI_BUTTON_ESC", "ESC", ISS_OFF);
        ui.buttons.switches[0].aux = (void *)&MGIO_Buttons[0];
        IUFillSwitch(&ui.buttons.switches[1], "MGEN_UI_BUTTON_SET", "SET", ISS_OFF);
        ui.buttons.switches[1].aux = (void *)&MGIO_Buttons[1];
        IUFillSwitch(&ui.buttons.switches[2], "MGEN_UI_BUTTON_UP", "UP", ISS_OFF);
        ui.buttons.switches[2].aux = (void *)&MGIO_Buttons[2];
        IUFillSwitch(&ui.buttons.switches[3], "MGEN_UI_BUTTON_LEFT", "LEFT", ISS_OFF);
        ui.buttons.switches[3].aux = (void *)&MGIO_Buttons[3];
        IUFillSwitch(&ui.buttons.switches[4], "MGEN_UI_BUTTON_RIGHT", "RIGHT", ISS_OFF);
        ui.buttons.switches[4].aux = (void *)&MGIO_Buttons[4];
        IUFillSwitch(&ui.buttons.switches[5], "MGEN_UI_BUTTON_DOWN", "DOWN", ISS_OFF);
        ui.buttons.switches[5].aux = (void *)&MGIO_Buttons[5];
        /* ESC SET
         * UP LEFT RIGHT DOWN
         */
        IUFillSwitchVector(&ui.buttons.properties[0], &ui.buttons.switches[0], 2, getDeviceName(), "MGEN_UI_BUTTONS1",
                           "UI Buttons", TAB, IP_RW, ISR_ATMOST1, 0, IPS_IDLE);
        IUFillSwitchVector(&ui.buttons.properties[1], &ui.buttons.switches[2], 4, getDeviceName(), "MGEN_UI_BUTTONS2",
                           "UI Buttons", TAB, IP_RW, ISR_ATMOST1, 0, IPS_IDLE);
    }

    return true;
}

bool MGenAutoguider::updateProperties()
{
    INDI::CCD::updateProperties();

    if (isConnected())
    {
        _D("registering properties", "");
        defineText(&version.firmware.property);
        defineNumber(&voltage.property);
        defineNumber(&guide.drift.property);
        defineLight(&guide.state.property);
        defineSwitch(&guide.phd2.property);
        defineSwitch(&ui.remote.property);
        defineNumber(&ui.framerate.property);
        defineSwitch(&ui.buttons.properties[0]);
        defineSwitch(&ui.buttons.properties[1]);
    }
    else
    {
        _D("removing properties", "");
        deleteProperty(version.firmware.property.name);
        deleteProperty(voltage.property.name);
        deleteProperty(guide.drift.property.name);
        deleteProperty(guide.state.property.name);
        deleteProperty(guide.phd2.property.name);
        deleteProperty(ui.remote.property.name);
        deleteProperty(ui.framerate.property.name);
        deleteProperty(ui.buttons.properties[0].name);
        deleteProperty(ui.buttons.properties[1].name);
    }

    return true;
}

/**************************************************************************************
** Client is asking us to establish connection to the device
***************************************************************************************/
bool MGenAutoguider::Connect()
{
    if (device && device->isConnected())
    {
        _S("ignoring connection request received while connecting or already connected", "");
        return true;
    }

    _D("initiating connection.", "");

    if (device)
        delete device;
    device = new MGenDevice();

    if (!device->Connect(0x0403, 0x6001))
        try
        {
            /* Loop until connection is considered stable */
            while (device->isConnected())
            {
                switch (getOpMode())
                {
                    /* Unknown mode, try to connect in COMPATIBLE mode first */
                    case OPM_UNKNOWN:
                        _D("running device identification", "");

                        /* Run an identification - failing this is not a problem, we'll try to communicate in applicative mode next */
                        if (CR_SUCCESS == MGCP_QUERY_DEVICE().ask(*device))
                        {
                            _D("identified boot/compatible mode", "");
                            device->setOpMode(OPM_COMPATIBLE);
                            continue;
                        }

                        _D("identification failed, try to communicate as if in applicative mode", "");
                        if (device->setOpMode(OPM_APPLICATION))
                        {
                            /* TODO: Not good, the device doesn't support our settings - out of spec, bail out */
                            _E("failed reconfiguring device serial line", "");
                            device->disable();
                            continue;
                        }

                        /* Run a basic exchange */
                        if (CR_SUCCESS != MGCMD_NOP1().ask(*device))
                        {
                            if (device->TurnPowerOn())
                            {
                                _E("failed heartbeat after turning device on", "");
                                device->disable();
                                continue;
                            }
                        }

                        break;

                    case OPM_COMPATIBLE:
                        _D("switching from compatible to normal mode", "");

                        /* Switch to applicative mode */
                        MGCP_ENTER_NORMAL_MODE().ask(*device);

                        if (device->setOpMode(OPM_APPLICATION))
                        {
                            /* TODO: Not good, the device doesn't support our settings - out of spec, bail out */
                            _E("failed reconfiguring device serial line", "");
                            device->disable();
                            continue;
                        }

                        _D("device is in now expected to be in applicative mode", "");

                        if (CR_SUCCESS != MGCMD_NOP1().ask(*device))
                        {
                            device->disable();
                            continue;
                        }

                        break;

                    case OPM_APPLICATION:
                        if (getHeartbeat())
                        {
                            _S("considering device connected", "");
                            /* FIXME: currently no way to tell which timer hit, so set one for the UI only */
                            TimerHit();
                            return device->isConnected();
                        }
                        else if (device->isConnected())
                        {
                            _D("waiting for heartbeat", "");
                            sleep(1);
                        }
                        break;
                }
            }
        }
        catch (IOError &e)
        {
            _E("device disconnected (%s)", e.what());
            device->disable();
        }

    /* We expect to have failed connecting at this point */
    return device->isConnected();
}

/**************************************************************************************
** Client is asking us to terminate connection to the device
***************************************************************************************/
bool MGenAutoguider::Disconnect()
{
    if(!device)
        return true;

    if (device->isConnected())
    {
        _D("initiating disconnection.", "");

        if(!device->wasAlreadyOn())
        {
            /* Move up to the main menu, top menu item */
            for(int i = 0; i < 10; i++)
                MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_ESC).ask(*device);

            /* Move down to fourth menu item */
            for(int i = 0; i < 4; i++)
                MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);

            /* Validate twice - there's still a problem with double sets... */
            MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_SET).ask(*device);
            MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_RIGHT).ask(*device);
            MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_RIGHT).ask(*device);
            MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_SET).ask(*device);
        }

        /* Turn device state off */
        device->TurnPowerOff();
        RemoveTimer(ui.timer);
    }

    return !device->isConnected();
}

/**************************************************************************************
** INDI is asking us for our default device name
***************************************************************************************/
const char *MGenAutoguider::getDefaultName()
{
    return "MGen Autoguider";
}

/**************************************************************************************
** Mode management
***************************************************************************************/

IOMode MGenAutoguider::getOpMode() const
{
    return device->getOpMode();
}

/**************************************************************************************
 * Connection thread
 **************************************************************************************/
void MGenAutoguider::TimerHit()
{
    if (device->isConnected())
        try
        {
            struct timespec tm = { .tv_sec = 0, .tv_nsec = 0 };
            if (clock_gettime(CLOCK_MONOTONIC, &tm))
                return;

            /* If we didn't get the firmware version, ask */
            if (0 == version.timestamp.tv_sec)
            {
                MGCMD_GET_FW_VERSION cmd;
                if (CR_SUCCESS == cmd.ask(*device))
                {
                    sprintf(version.firmware.text.text, "%04X", cmd.fw_version());
                    _D("received version %4.4s", version.firmware.text.text);
                    IDSetText(&version.firmware.property, NULL);
                }
                else
                    _E("failed retrieving firmware version", "");

                version.timestamp = tm;
            }

            /* Heartbeat */
            if (heartbeat.timestamp.tv_sec + 5 < tm.tv_sec)
            {
                getHeartbeat();
                heartbeat.timestamp = tm;
            }

            /* Update ADC values */
            if (0 == voltage.timestamp.tv_sec || voltage.timestamp.tv_sec + 20 < tm.tv_sec)
            {
                MGCMD_READ_ADCS adcs;

                if (CR_SUCCESS == adcs.ask(*device))
                {
                    voltage.levels.logic.value = adcs.logic_voltage();
                    _D("received logic voltage %fV (spec is between 4.8V and 5.1V)", voltage.levels.logic.value);
                    voltage.levels.input.value = adcs.input_voltage();
                    _D("received input voltage %fV (spec is between 9V and 15V)", voltage.levels.input.value);
                    voltage.levels.reference.value = adcs.refer_voltage();
                    _D("received reference voltage %fV (spec is around 1.23V)", voltage.levels.reference.value);

                    /* FIXME: my device has input at 15.07... */
                    if (4.8f <= voltage.levels.logic.value && voltage.levels.logic.value <= 5.1f)
                        if (9.0f <= voltage.levels.input.value && voltage.levels.input.value <= 15.0f)
                            if (1.1 <= voltage.levels.reference.value && voltage.levels.reference.value <= 1.3)
                                voltage.property.s = IPS_OK;
                            else
                            {
                                if (voltage.property.s != IPS_ALERT )
                                    _S("Warning: received out-of-spec reference voltage %fV (spec is around 1.23V)", voltage.levels.reference.value);
                                voltage.property.s = IPS_ALERT;
                            }
                        else
                        {
                            if (voltage.property.s != IPS_ALERT )
                                _S("Warning: received out-of-spec input voltage %fV (spec is between 9V and 15V)", voltage.levels.input.value);
                            voltage.property.s = IPS_ALERT;
                        }
                    else
                    {
                        if (voltage.property.s != IPS_ALERT )
                            _S("Warning: received out-of-spec logic voltage %fV (spec is between 4.8V and 5.1V)", voltage.levels.logic.value);
                        voltage.property.s = IPS_ALERT;
                    }


                    IDSetNumber(&voltage.property, NULL);
                }
                else
                    _E("failed retrieving voltages", "");

                voltage.timestamp = tm;
            }

            /* Update UI frame - I'm trading efficiency for code clarity, sorry for the computation with doubles */
            if (ui.is_enabled && (0 == ui.timestamp.tv_sec || 0 < ui.framerate.number.value))
            {
                double const ui_period = 1.0f / ui.framerate.number.value;
                double const ui_next =
                    (double)ui.timestamp.tv_sec + (double)ui.timestamp.tv_nsec / 1000000000.0f + ui_period;
                double const now = tm.tv_sec + tm.tv_nsec / 1000000000.0f;

                if (ui_next < now)
                {
                    MGIO_READ_DISPLAY_FRAME read_frame;

                    if (CR_SUCCESS == read_frame.ask(*device))
                    {
                        MGIO_READ_DISPLAY_FRAME::ByteFrame frame;
                        read_frame.get_frame(frame);
                        memcpy(PrimaryCCD.getFrameBuffer(), frame.data(), frame.size());
                        ExposureComplete(&PrimaryCCD);
                    }
                    else
                        _E("failed reading remote UI frame", "");

                    ui.timestamp = tm;
                }
            }

            /* Retrieve guide frame information */
            if (guide.is_enabled && (0 == guide.timestamp.tv_sec || guide.timestamp.tv_sec < tm.tv_sec))
            {
                MGCMD_READ_GUIDE_FRAME gframe;

                if (CR_SUCCESS == gframe.ask(*device))
                {
                    if (guide.state.frame_index != gframe.frame_index())
                    {
                        guide.drift.ascension.value = gframe.ascension_drift();
                        _D("received right ascension drift %f", guide.drift.ascension.value);
                        guide.drift.declination.value = gframe.declination_drift();
                        _D("received declination drift %f", guide.drift.declination.value);

                        guide.drift.property.s = IPS_OK;
                        IDSetNumber(&guide.drift.property, NULL);

                        guide.state.has_guide_star.s = gframe.has_guide_star() ? IPS_OK : IPS_ALERT;
                        _D("guide start is %spresent", gframe.has_guide_star() ? "" : "NOT ");

                        guide.state.property.s = IPS_OK;
                        IDSetLight(&guide.state.property, NULL);

                        guide.state.frame_index = gframe.frame_index();
                    }
                }
                else
                    _E("failed reading guide frame", "");

                guide.timestamp = tm;
            }

            /* Rearm the timer, use a minimal timer period of 1s, and shorter if frame rate is higher than 1fps */
            ui.timer = SetTimer(1.0f < ui.framerate.number.value ? (long)(1000.0f / ui.framerate.number.value) : 1000);
        }
        catch (IOError &e)
        {
            _S("device disconnected (%s)", e.what());
            device->disable();
            setConnected(false, IPS_ALERT);
            updateProperties();
        }
}

/**************************************************************************************
 * Helpers
 **************************************************************************************/

bool MGenAutoguider::getHeartbeat()
{
    if (CR_SUCCESS != MGCMD_NOP1().ask(*device))
    {
        heartbeat.no_ack_count++;
        _E("%d times no ack to heartbeat (NOP1 command)", heartbeat.no_ack_count);
        if (5 < heartbeat.no_ack_count)
        {
            device->disable();
            setConnected(false, IPS_ALERT);
            updateProperties();
        }
        return false;
    }
    else
        heartbeat.no_ack_count = 0;

    return true;
}

/**************************************************************************************
 * Automated scenarii
 **************************************************************************************/

void MGenAutoguider::engageRootLevel()
{
    if(device && device->isConnected())
    {
        /* Move up to the main menu, top menu item */
        for(int i = 0; i < 10; i++)
            MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_ESC).ask(*device);
    }
}


void MGenAutoguider::engageSearchStar()
{
    /* Back home */
    engageRootLevel();

    if(device && device->isConnected())
    {
        /* Move down to autoguiding */
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_SET).ask(*device);
        
        /* Move down to search */
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_SET).ask(*device);
        
        /* Keep defaults and search */
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_SET).ask(*device);
        
        /* Wait for search to finish */
        sleep(10);
        
        /* Select the first star available, if any */
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_SET).ask(*device);

        /* Now the star flag will tell if a star was found */
    }
}

void MGenAutoguider::engageCalibrate()
{
    /* Back home */
    engageRootLevel();

    if(device && device->isConnected())
    {
        /* Move down to autoguiding */
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_SET).ask(*device);
        
        /* Already on Current Guiding */
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_SET).ask(*device);
        
        /* Go to page number and shift to page 2 */
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_RIGHT).ask(*device);

        /* Move up to calibration */
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_UP).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_UP).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_SET).ask(*device);

        /* Remove existing calibration */
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_SET).ask(*device);

        /* And launch calibration */
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_SET).ask(*device);
        
        /* Wait for calibration to finish */
        sleep(90);
        
        /* Finish calibration */
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_SET).ask(*device);

        /* Exit calibration, move to page number and return back to page 1 */
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_ESC).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_LEFT).ask(*device);

        /* Now the star movement is calibrated, or no star is available */
    }
}

void MGenAutoguider::engageToggleGuiding()
{
    /* Back home */
    engageRootLevel();

    if(device && device->isConnected())
    {
        /* Move down to autoguiding */
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_SET).ask(*device);
        
        /* Already on Current Guiding */
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_SET).ask(*device);

        /* Move down to toggle guiding */
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_DOWN).ask(*device);
        MGIO_INSERT_BUTTON(MGIO_INSERT_BUTTON::IOB_SET).ask(*device);

        /* Now guider is toggled */
    }
}
