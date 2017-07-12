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

protected:
    struct Session
    {
        boost::asio::streambuf input_buffer;
        boost::asio::streambuf output_buffer;
        boost::asio::ip::tcp::socket &socket;
        Session(boost::asio::ip::tcp::socket &a_socket): socket(a_socket) {}
    };

protected:
    int run_session(Session&);
    void do_accept();

protected:
    struct _flags
    {
        bool connected;
        bool guiding;
        bool calibrating;
    }
    flags;

protected:
    boost::system::error_code write(Session&, boost::property_tree::ptree const &message);
    boost::system::error_code read(Session&, boost::property_tree::ptree &message);

private:
    std::thread *server_thread;
    boost::asio::io_service io_service;
    boost::asio::ip::tcp::endpoint endpoint;
    boost::asio::ip::tcp::acceptor acceptor;
    boost::asio::ip::tcp::socket socket;
};

#endif /* _3RDPARTY_INDI_MGEN_MGEN_PHD2SERVER_H_ */
