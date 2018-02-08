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
    socket(io_service)
{
    do_stop = false;

    _D("starting server thread", "");
    server_thread = new std::thread(&MGenPHD2Server::do_accept, this);
}

MGenPHD2Server::~MGenPHD2Server()
{
    do_stop = true;

    _D("joining server thread", "");
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
        _D("received: %s", a_line.c_str());
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

    while(true)
    {
        try
        {
            boost::property_tree::iptree request;
            boost::system::error_code const e = session.read(request);

            if(e)
            {
                _D("closing session on error %d", e);
                break;
            }

            std::string const method = request.get<std::string>("method", "");

            if(!method.compare("set_connected"))
            {
                /* This is a connect-equipment request, do a quick check with device callbacks */
                flags.connected = 1 == request.get<int>("params.", 0);
                session.write(MGPHD2_MethodResult(device ? 0 : 1));
            }
            else if(!method.compare("guide"))
            {
                /* Bail out if not connected */
                if(!flags.connected)
                {
                    session.write(MGPHD2_MethodResult(1,"Not connected"));
                }
                else
                {
                    /* Check if it is a start- or stop-guiding request */
                    bool guiding_request = (1 == request.get<int>("params.", 0));

                    /* Check our current state */
                    if(guiding_request && !flags.guiding)
                    {
                        /* This is a start-guiding and we are not currently guiding, search and select a guiding star */
                        if (device->engageSearchStar())
                            /* Search is successful and various notifications were emitted, so try to start guiding on the current star */
                            if (device->engageStartGuiding())
                            {
                                /* We're now guiding, tell the client */
                                session.write(MGPHD2_EvSettleDone(0));
                                flags.guiding = guiding_request;
                            }
                            else session.write(MGPHD2_MethodResult(1,"Failed starting to guide"));
                        else session.write(MGPHD2_MethodResult(1,"No star found"));
                    }
                    else if(!guiding_request && flags.guiding)
                    {
                        /* This is a stop-guiding and we are currently guiding, so stop guiding on the current star */
                        if (device->engageStopGuiding())
                        {
                            /* We're now not guiding, tell the client */
                            session.write(MGPHD2_EvSettleDone(0));
                            flags.guiding = guiding_request;
                        }
                        else session.write(MGPHD2_MethodResult(1,"Failed stopping guiding"));
                    }
                    else session.write(MGPHD2_MethodResult(1,"Invalid guiding request"));
                }
            }
            else session.write(MGPHD2_MethodResult(1,"Not supported"));
        }
        catch (boost::property_tree::json_parser_error &e)
        {
            std::cerr << e.what() <<  std::endl;
            session.write(MGPHD2_MethodResult(2,"Invalid"));
        }
        catch (boost::property_tree::ptree_bad_path &e)
        {
            std::cerr << e.what() <<  std::endl;
            session.write(MGPHD2_MethodResult(2,"Invalid"));
        }
    }

    _S("closing session", "");
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
    _D("Starting to accept", "");
    while(!do_stop)
    {
        tcp::socket s(io_service);
        acceptor.accept(s);
        _S("starting session with %s:%d", s.remote_endpoint().address().to_string().c_str(), s.remote_endpoint().port());
        Session session(s);
        run_session(session);
    }
    _D("Acceptor done", "");
}
