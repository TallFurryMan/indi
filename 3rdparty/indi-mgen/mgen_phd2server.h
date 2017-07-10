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

class MGenPHD2Server
{
public:
    MGenPHD2Server();
    virtual ~MGenPHD2Server();

protected:
    int run_session(boost::asio::ip::tcp::socket&);
    void do_accept();

private:
    std::thread *server_thread;
    boost::asio::io_service io_service;
    boost::asio::ip::tcp::endpoint endpoint;
    boost::asio::ip::tcp::acceptor acceptor;
    boost::asio::ip::tcp::socket socket;
    char data[4096];
};

#endif /* _3RDPARTY_INDI_MGEN_MGEN_PHD2SERVER_H_ */
