/*
 * mgen_phd2server.cpp
 *
 *  Created on: 21 juin 2017
 *      Author: TallFurryMan
 */

#include "boost/property_tree/ptree.hpp"
#include "boost/property_tree/json_parser.hpp"
#include "mgen_phd2server.h"
#include "mgphd2_message.h"
using boost::asio::ip::tcp;

unsigned short PHD2_PORT = 4400;

MGenPHD2Server::MGenPHD2Server():
    io_service(),
    endpoint(tcp::v4(), PHD2_PORT),
    acceptor(io_service, endpoint),
    socket(io_service)
{
    std::cerr << "Starting io service" << std::endl;
    io_service.run();
    std::cerr << "Starting server thread" << std::endl;
    server_thread = new std::thread(&MGenPHD2Server::do_accept,this);
}

MGenPHD2Server::~MGenPHD2Server()
{
    std::cerr << "Joining server thread" << std::endl;
    server_thread->join();
    delete server_thread;
}

int MGenPHD2Server::run_session(tcp::socket &a_socket)
{
    /*socket.async_read_some(
        boost::asio::buffer(data,sizeof(data)),
        [this](boost::system::error_code e, std::size_t length)
        {
            if(!e)
                std::cout << std::string(data, length) << std::endl;
            else
                std::cout << "Error " << e << std::endl;
            do_read();
        });*/

    /* {"Event":"Version","Timestamp":1499292803.443,"Host":"ARHIMAN","Inst":1,"PHDVersion":"2.6.3","PHDSubver":"","MsgVersion":1 */
    MGPHD2_EvVersion version;

    /*{"Event":"AppState","Timestamp":1499292803.443,"Host":"ARHIMAN","Inst":1,"State":"Stopped"*/
    MGPHD2_EvAppState appstate("Stopped");

    boost::asio::streambuf stream;
    std::ostream out(&stream);

    boost::asio::streambuf buf;
    boost::system::error_code e;
    std::string welcome;

    while(!e)
    {
        std::cerr << "Writing version '";
        boost::property_tree::write_json(std::cerr, version, false);
        std::cerr << "' to socket" << std::endl;
        boost::property_tree::write_json(out, version, false); out << "\r\n";
        boost::asio::write(a_socket, stream);

        std::cerr << "Writing appstate '";
        boost::property_tree::write_json(std::cerr, appstate, false);
        std::cerr << "' to socket" << std::endl;
        boost::property_tree::write_json(out, appstate, false); out << "\r\n";
        boost::asio::write(a_socket, stream);

        std::cerr << "Reading smth from socket" << std::endl;
        boost::asio::read_until(a_socket, buf, '\n', e);
        std::getline(std::istream(&buf),welcome);
        std::cerr << "Echoing '" << welcome << "' to socket" << std::endl;
        boost::asio::write(a_socket, boost::asio::buffer(welcome));
    }

    std::cerr << "Closing socket" << std::endl;
    socket.close();
    return 0;
}

void MGenPHD2Server::do_accept()
{
    /*
    acceptor.async_accept(socket,
        [this](boost::system::error_code e)
        {
            if(!e)
            {
                std::string welcome("Hello");
                boost::asio::write(socket, boost::asio::buffer(welcome, welcome.length()));
                do_read();
            }
        });
        */
    std::cerr << "Starting to accept" << std::endl;
    //while(true)
    {
        tcp::socket s(io_service);
        acceptor.accept(s);
        std::cerr << "Starting session" << std::endl;
        run_session(s);
    }
}
