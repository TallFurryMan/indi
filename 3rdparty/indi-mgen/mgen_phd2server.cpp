/*
 * mgen_phd2server.cpp
 *
 *  Created on: 21 juin 2017
 *      Author: TallFurryMan
 */

#include "indidevapi.h"
#include "indilogger.h"

#include "mgen.h"
#include "mgenautoguider.h"

#include "mgen_phd2server.h"
#include <boost/property_tree/json_parser.hpp>
#include "mgphd2_message.h"

using boost::asio::ip::tcp;

unsigned short PHD2_PORT = 4400;

MGenPHD2Server::MGenPHD2Server():
    io_service(),
    endpoint(tcp::v4(), PHD2_PORT),
    acceptor(io_service, endpoint),
    socket(io_service),
    do_stop(false)
{
    //_S("Starting io service", "");
    //io_service.run();

    _S("Starting server thread", "");
    server_thread = new std::thread(&MGenPHD2Server::do_accept, this);
}

MGenPHD2Server::~MGenPHD2Server()
{
    //_S("Stopping io service","");
    //io_service.stop();
    do_stop = true;

    _S("Joining server thread", "");
    //socket.shutdown();
    socket.close();
    server_thread->join();
    delete server_thread;
}

boost::system::error_code MGenPHD2Server::Session::write(boost::property_tree::iptree const &message)
{
    boost::system::error_code e;
    std::ostream out(&output_buffer);

    boost::property_tree::write_json(out, message, false);
    output_buffer.consume(boost::asio::write(socket, output_buffer, e));

    return e;
}

boost::system::error_code MGenPHD2Server::Session::read(boost::property_tree::iptree &message)
{
    boost::system::error_code e;
    std::string const sep("\n");
    std::size_t const bytes = boost::asio::read_until(socket, input_buffer, sep, e);
    if(!e)
    {
        std::string a_line {
            boost::asio::buffers_begin(input_buffer.data()),
            boost::asio::buffers_begin(input_buffer.data())+bytes-sep.size()
        };
        input_buffer.consume(bytes);
        _S("SERVER: Received: %s",a_line.c_str());
        std::stringstream ss(a_line, std::ios_base::in);
        boost::property_tree::json_parser::read_json(ss, message);
    }
    return e;
}

int MGenPHD2Server::run_session(MGenPHD2Server::Session &session)
{
    /* Notify the version to the client */
    session.write(MGPHD2_EvVersion());

    /* Notify the current state of the driver - stopped for now */
    /* FIXME: current state could be something else if we support persistence between client sessions */
    session.write(MGPHD2_EvAppState("Stopped"));

    while(!session.do_exit)
    {
        try
        {
            boost::property_tree::iptree request;
            boost::system::error_code const e = session.read(request);

            if(e)
            {
                std::cerr << "SERVER: Closing session on error " << e << std::endl;
                break;
            }

            std::string const method = request.get<std::string>("method");

            if(!method.compare("set_connected"))
            {
                /* This is a connect-equipment request, do a quick check with device callbacks */
                flags.connected = 1 == request.get<int>("params.",0);
                session.write(MGPHD2_MethodResult((deviceSearchStar && deviceStartGuiding && deviceStopGuiding) ? 0 : 1));
            }
            else if(!method.compare("guide"))
            {
                /* Check if it is a start- or stop-guiding request */
                bool guiding_request = (1 == request.get<int>("params.",0));

                /* Check our current state */
                if(guiding_request && !flags.guiding)
                {
                    /* This is a start-guiding and we are not currently guiding, check if we're able to search for a star */
                    if (deviceSearchStar)
                        /* We are, so search and select a guiding star */
                        if ((*deviceSearchStar)())
                            /* Search is successfuli and various notifications were emitted, so check if we're able to start guiding */
                            if (deviceStartGuiding())
                                /* We are, so try to start guiding on the current star */
                                if ((*deviceStartGuiding)())
                                {
                                    /* We're now guiding, tell the client */
                                    session.write(MGPHD2_EvSettleDone(0));
                                    flags.guiding = guiding_request;
                                }
                                else session.write(MGPHD2_MethodResult(1,"Failed starting to guide"));
                            else session.write(MGPHD2_MethodResult(1,"Internal error - start guiding method"));
                        else session.write(MGPHD2_MethodResult(1,"No star found"));
                    else session.write(MGPHD2_MethodResult(1,"Internal error - search method"));
                }
                else if(!guiding_request && flags.guiding)
                {
                    /* This is a stop-guiding and we are currently guiding, check if we're able to stop guiding */
                    if (deviceStopGuiding)
                        /* We are, so stop guiding on the current star */
                        if ((*deviceStopGuiding)())
                        {
                            /* We're now not guiding, tell the client */
                            session.write(MGPHD2_EvSettleDone(0));
                            flags.guiding = guiding_request;
                        }
                        else session.write(MGPHD2_MethodResult(1,"Failed stopping guiding"));
                    else session.write(MGPHD2_MethodResult(1,"Internal error - stop guiding method"));
                }
                else session.write(MGPHD2_MethodResult(1,"Invalid guiding request"));
            }
            else session.write(MGPHD2_MethodResult(1,"Not supported"));
        }
        catch (boost::property_tree::json_parser_error &e)
        {
            std::cerr << e.what();
            session.write(MGPHD2_MethodResult(2,"Invalid"));
        }
        catch (boost::property_tree::ptree_bad_path &e)
        {
            std::cerr << e.what();
            session.write(MGPHD2_MethodResult(2,"Invalid"));
        }
    }

    std::cerr << "Closing session" << std::endl;
    //session.socket.shutdown();
    session.socket.close();
    return 0;
}

void MGenPHD2Server::Session::notifyStarSelected(int x, int y)
{
    write(MGPHD2_EvStarSelected(x,y));
}

void MGenPHD2Server::Session::notifyCalibrationStart()
{
    write(MGPHD2_EvStartCalibration());
}

void MGenPHD2Server::Session::notifyCalibrationComplete()
{
    write(MGPHD2_EvCalibrationComplete());
}

void MGenPHD2Server::do_accept()
{
    std::cerr << "Starting to accept" << std::endl;
    while(!do_stop)
    {
        tcp::socket s(io_service);
        acceptor.accept(s);
        std::cerr << "Starting session" << std::endl;
        Session session(s);
        run_session(session);
    }
    std::cerr << "Acceptor done" << std::endl;
}
