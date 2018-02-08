/*
 * mgen_phd2server_test.cpp
 *
 *  Created on: 21 juin 2017
 *      Author: TallFurryMan
 */
#define BOOST_TEST_MODULE PHD2 MGen Interface
#include <boost/test/included/unit_test.hpp>

#include "mgen_phd2server.h"
using boost::asio::ip::tcp;

#include <boost/property_tree/json_parser.hpp>

struct fixture
{
    MGenPHD2Server *server;
    boost::asio::io_service io_service;
    tcp::socket s;
    tcp::resolver resolver;

    class FakeDriver: public MGenPHD2Server::PHD2Device
    {
    public:
        int _dSearchStar;
        int _dCalibrate;
        int _dStartGuiding;
        int _dStopGuiding;
    public:
        virtual bool engageSearchStar() { _dSearchStar++; return true; }
        virtual bool engageCalibrate() { _dCalibrate++; return true; }
        virtual bool engageStartGuiding() { _dStartGuiding++; return true; }
        virtual bool engageStopGuiding() { _dStopGuiding++; return true; }
    public:
        FakeDriver():
            _dSearchStar(0),
            _dCalibrate(0),
            _dStartGuiding(0),
            _dStopGuiding(0) {};
    };

    fixture():
        io_service(),
        s(io_service),
        resolver(io_service)
    {
        BOOST_TEST_MESSAGE("Starting server");
        server = new MGenPHD2Server();

        BOOST_TEST_MESSAGE("Connecting to server on 127.0.0.1:4400");
        boost::asio::connect(s, resolver.resolve({"127.0.0.1","4400"}));
    }

    ~fixture()
    {
        BOOST_TEST_MESSAGE("Closing client");
        s.close();

        BOOST_TEST_MESSAGE("Terminating server");
        delete server;
    }
};

boost::system::error_code read_message(boost::property_tree::iptree &message, tcp::socket &s, boost::asio::streambuf &buf)
{
    boost::system::error_code e;
    std::string sep("\n");
    std::size_t const bytes = boost::asio::read_until(s, buf, sep, e);
    if(!e)
    {
        std::string a_line {
            boost::asio::buffers_begin(buf.data()),
            boost::asio::buffers_begin(buf.data()) + bytes - sep.size()
        };
        buf.consume(bytes);
        std::cerr << "Received: " << a_line << std::endl;
        std::stringstream ss(a_line, std::ios_base::in);
        boost::property_tree::json_parser::read_json(ss, message);
    }
    return e;
}


BOOST_FIXTURE_TEST_CASE(initial_messages, fixture)
{
    boost::asio::streambuf buf;
    boost::system::error_code e;
    boost::property_tree::iptree answer;

    BOOST_TEST_MESSAGE("Reading version message");

    BOOST_CHECK(!read_message(answer, s, buf));
    BOOST_CHECK(!answer.get<std::string>("Event","").compare("Version"));
    BOOST_CHECK(!answer.get<std::string>("Host","").empty());
    BOOST_CHECK(!answer.get<std::string>("Inst","").compare("1"));
    BOOST_CHECK(!answer.get<std::string>("PHDVersion","").compare("2.6.3"));
    BOOST_CHECK(!answer.get<std::string>("PHDSubVer","").compare("MGenAutoguider"));
    BOOST_CHECK(!answer.get<std::string>("MsgVersion","").compare("1"));

    BOOST_TEST_MESSAGE("Reading application state message");

    BOOST_CHECK(!read_message(answer, s, buf));
    BOOST_CHECK(!answer.get<std::string>("Event","").compare("AppState"));
    BOOST_CHECK(!answer.get<std::string>("Host","").empty());
    BOOST_CHECK(!answer.get<std::string>("Inst","").compare("1"));
    BOOST_CHECK(!answer.get<std::string>("State","").compare("Stopped"));
}

BOOST_FIXTURE_TEST_CASE(request_connect, fixture)
{
    boost::asio::streambuf buf;
    boost::system::error_code e;
    boost::property_tree::iptree answer;

    BOOST_TEST_MESSAGE("Reading initial messages");

    BOOST_CHECK(!read_message(answer, s, buf));
    BOOST_CHECK(!read_message(answer, s, buf));

    BOOST_TEST_MESSAGE("Requesting device connection");

    boost::asio::write(s, boost::asio::buffer(std::string("{\"method\":\"set_connected\",\"params\":[1]}\n")), e);
    BOOST_CHECK(!e);

    BOOST_TEST_MESSAGE("Reading connection result");

    BOOST_CHECK(!read_message(answer, s, buf));
    BOOST_CHECK(!answer.get<std::string>("result","").compare("1"));
}

BOOST_FIXTURE_TEST_CASE(garbage, fixture)
{
    boost::asio::streambuf buf;
    boost::system::error_code e;
    boost::property_tree::iptree answer;

    BOOST_TEST_MESSAGE("Reading initial messages");

    BOOST_CHECK(!read_message(answer, s, buf));
    BOOST_CHECK(!read_message(answer, s, buf));

    BOOST_TEST_MESSAGE("Writing garbage");

    boost::asio::write(s, boost::asio::buffer(std::string("A random line\n")), e);
    BOOST_CHECK(!e);

    BOOST_TEST_MESSAGE("Reading result");

    BOOST_CHECK(!read_message(answer, s, buf));
    BOOST_CHECK(!answer.get<std::string>("error.code","").compare("2"));
    BOOST_CHECK(!answer.get<std::string>("error.message","").compare("Invalid"));
}

BOOST_FIXTURE_TEST_CASE(request_connect_with_callbacks, fixture)
{
    boost::asio::streambuf buf;
    boost::system::error_code e;
    boost::property_tree::iptree answer;

    BOOST_TEST_MESSAGE("Reading initial messages");

    BOOST_CHECK(!read_message(answer, s, buf));
    BOOST_CHECK(!read_message(answer, s, buf));

    FakeDriver d;
    server->device = &d;

    BOOST_TEST_MESSAGE("Requesting device connection with device callbacks");
    boost::asio::write(s, boost::asio::buffer(std::string("{\"method\":\"set_connected\",\"params\":[1]}\n")), e);
    BOOST_CHECK(!e);
    BOOST_TEST_MESSAGE("Reading connection result with device callbacks");
    BOOST_CHECK(!read_message(answer, s, buf));
    BOOST_CHECK(!answer.get<std::string>("result","").compare("0"));
}


