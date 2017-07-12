/*
 * mgen_phd2server.cpp
 *
 *  Created on: 21 juin 2017
 *      Author: TallFurryMan
 */

#include "mgen_phd2server.h"
#include <boost/property_tree/json_parser.hpp>
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

boost::system::error_code MGenPHD2Server::write(MGenPHD2Server::Session &session, boost::property_tree::ptree const &message)
{
    boost::system::error_code e;
    std::ostream out(&session.output_buffer);

    std::cerr << "SERVER: Writing '";
    boost::property_tree::write_json(std::cerr, message, false);
    std::cerr << "' to socket" << std::endl;

    boost::property_tree::write_json(out, message, false);
    session.output_buffer.consume(boost::asio::write(session.socket, session.output_buffer, e));

    return e;
}

boost::system::error_code MGenPHD2Server::read(MGenPHD2Server::Session &session, boost::property_tree::ptree &message)
{
    boost::system::error_code e;
    std::string buffer;

    /* read_until may read further than the marker, but getline will only consume up to the marker */
    boost::asio::read_until(session.socket, session.input_buffer, '\n', e);
    /*std::getline(std::istream(&session.input_buffer), buffer);

    buffer.erase(std::remove_if(buffer.begin(), buffer.end(), [](char const x){return '\r' == x || '\n' == x;}), buffer.end());
    std::cerr << "SERVER: Received '" << buffer << "' from socket" << std::endl;*/

    std::istream in(&session.input_buffer);
    boost::property_tree::read_json(in, message);

    std::cerr << "SERVER: Read '";
    boost::property_tree::write_json(std::cerr, message);
    std::cerr << "' from socket" << std::endl;
    return e;
}

int MGenPHD2Server::run_session(MGenPHD2Server::Session &session)
{
    /* {"Event":"Version","Timestamp":1499292803.443,"Host":"ARHIMAN","Inst":1,"PHDVersion":"2.6.3","PHDSubver":"","MsgVersion":1 */
    write(session, MGPHD2_EvVersion());

    /*{"Event":"AppState","Timestamp":1499292803.443,"Host":"ARHIMAN","Inst":1,"State":"Stopped"*/
    write(session, MGPHD2_EvAppState("Stopped"));

    boost::system::error_code e;

    /*std::cerr << "Writing version '";
    boost::property_tree::write_json(std::cerr, version, false);
    std::cerr << "' to socket" << std::endl;
    boost::property_tree::write_json(out, version, false);
    stream.consume(boost::asio::write(a_socket, stream));*/

    /*std::cerr << "Writing appstate '";
    boost::property_tree::write_json(std::cerr, appstate, false);
    std::cerr << "' to socket" << std::endl;
    boost::property_tree::write_json(out, appstate, false);
    stream.consume(boost::asio::write(a_socket, stream));*/

    while(!e)
    {
        std::string message;

        //std::cerr << "Reading smth from socket" << std::endl;
        try
        {
            boost::property_tree::ptree request;
            read(session, request);

            if(!request.get<std::string>("method").compare("set_connected"))
            {
                flags.connected = 1 == request.get<int>("params.",0);
                write(session, MGPHD2_MethodResult(0));
            }
            else if(!request.get<std::string>("method").compare("guide"))
            {
                flags.guiding = 1 == request.get<int>("params.",0);
                write(session, MGPHD2_EvStarSelected(120,60));
                write(session, MGPHD2_EvStartCalibration());
                sleep(1);
                write(session, MGPHD2_EvCalibrationComplete());
                sleep(1);
                write(session, MGPHD2_EvSettleDone(0));
            }
            else write(session, MGPHD2_MethodResult(1,"Not supported"));
        }
        catch (boost::property_tree::json_parser_error &e)
        {
            std::cerr << e.what();
            write(session, MGPHD2_MethodResult(2,"Invalid"));
            break;
        }

        //stream.consume(boost::asio::write(a_socket, boost::asio::buffer(message)));
    }

    std::cerr << "Closing session" << std::endl;
    session.socket.close();
    return 0;
}

void MGenPHD2Server::do_accept()
{
    std::cerr << "Starting to accept" << std::endl;
    //while(true)
    {
        tcp::socket s(io_service);
        acceptor.accept(s);
        std::cerr << "Starting session" << std::endl;
        Session session(s);
        run_session(session);
    }
    socket.close();
}
