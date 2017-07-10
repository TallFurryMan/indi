/*
 * mgphd2_message.h
 *
 *  Created on: 6 juil. 2017
 *      Author: TallFurryMan
 */

#ifndef _3RDPARTY_INDI_MGEN_MGPHD2_MESSAGE_H_
#define _3RDPARTY_INDI_MGEN_MGPHD2_MESSAGE_H_

#include "boost/property_tree/ptree.hpp"

class MGPHD2_Message: public boost::property_tree::ptree
{
protected:
    MGPHD2_Message()
    {
        boost::property_tree::ptree();
        put("Timestamp", time(nullptr));
        put("Host", "ASTRO1"/*getenv("HOSTNAME")*/);
        put("Inst", 1);
    }
};

class MGPHD2_EvVersion: public MGPHD2_Message
{
public:
    MGPHD2_EvVersion(): MGPHD2_Message()
    {
        put("Event", "Version");
        put("PHDVersion", "2.6.3");
        put("PHDSubver", "MGenAutoguider");
        put("MsgVersion", 1);
    }
};

class MGPHD2_EvLockPositionSet: public MGPHD2_Message
{
public:
    MGPHD2_EvLockPositionSet(float x, float y): MGPHD2_Message()
    {
        put("Event", "LockPositionSet");
        put("X", x);
        put("Y", y);
    }
};

class MGPHD2_EvStarSelected: public MGPHD2_Message
{
public:
    MGPHD2_EvStarSelected(float x, float y): MGPHD2_Message()
    {
        put("Event", "StarSelected");
        put("X", x);
        put("Y", y);
    }
};

class MGPHD2_EvCalibrationComplete: public MGPHD2_Message
{
public:
    MGPHD2_EvCalibrationComplete(): MGPHD2_Message()
    {
        put("Event", "CalibrationComplete");
        put("Mount", "MGen ST4");
    }
};

class MGPHD2_EvStartGuiding: public MGPHD2_Message
{
public:
    MGPHD2_EvStartGuiding(): MGPHD2_Message()
    {
        put("Event", "StartGuiding");
    }
};

class MGPHD2_EvAppState: public MGPHD2_Message
{
public:
    MGPHD2_EvAppState(std::string state): MGPHD2_Message()
    {
        put("Event", "AppState");
        put("State", state);
    }
};

#endif /* 3RDPARTY_INDI_MGEN_MGPHD2_MESSAGE_H_ */
