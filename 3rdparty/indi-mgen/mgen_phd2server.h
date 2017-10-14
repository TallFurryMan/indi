/*
 * mgen_phd2server.h
 *
 *  Created on: 21 juin 2017
 *      Author: TallFurryMan
 */

#ifndef _3RDPARTY_INDI_MGEN_MGEN_PHD2SERVER_H_
#define _3RDPARTY_INDI_MGEN_MGEN_PHD2SERVER_H_

#include <cstdlib>
#include <iostream>
#include <string>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <thread>
#include <boost/property_tree/ptree.hpp>

class MGenPHD2Server
{
public:
    MGenPHD2Server();
    virtual ~MGenPHD2Server();

public:
    /** Request the device to search for a guiding star and select it */
    bool (*deviceSearchStar)();
    /** Request the device to start guiding on the current star */
    bool (*deviceStartGuiding)();
    /** Request the device to stop guiding on the current star */
    bool (*deviceStopGuiding)();

protected:
    /** A session with a phd2 client */
    class Session
    {
    public:
        bool do_exit;
        boost::asio::streambuf input_buffer;
        boost::asio::streambuf output_buffer;
        boost::asio::ip::tcp::socket &socket;
    public:
        /** Notify the client a star was selected, with coordinates */
        void notifyStarSelected(int x, int y);
        /** Notify the client star calibration started */
        void notifyCalibrationStart();
        /** Notify the client star calibration completed successfully */
        void notifyCalibrationComplete();
    public:
        /** Write a JSON message to the client */
        boost::system::error_code write(boost::property_tree::iptree const &message);
        /** Read a JSON message from the client */
        boost::system::error_code read(boost::property_tree::iptree &message);
    public:
        Session(boost::asio::ip::tcp::socket &a_socket): do_exit(false), socket(a_socket)  {}
    };

protected:
    bool do_stop;
    /** Process a session with a client */
    int run_session(Session&);
    /** Wait for a client */
    void do_accept();

protected:
    struct _flags
    {
        bool connected;
        bool guiding;
        bool calibrating;
    }
    flags;

private:
    std::thread *server_thread;
    boost::asio::io_service io_service;
    boost::asio::ip::tcp::endpoint endpoint;
    boost::asio::ip::tcp::acceptor acceptor;
    boost::asio::ip::tcp::socket socket;
};

#endif /* _3RDPARTY_INDI_MGEN_MGEN_PHD2SERVER_H_ */
