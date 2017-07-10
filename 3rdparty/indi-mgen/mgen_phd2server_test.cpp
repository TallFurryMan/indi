/*
 * mgen_phd2server_test.cpp
 *
 *  Created on: 21 juin 2017
 *      Author: TallFurryMan
 */

#include "mgen_phd2server.h"
using boost::asio::ip::tcp;

bool local_test=false;

int main(void)
{
    //std::cerr << "Instantiating server" << std::endl;
    MGenPHD2Server* server = new MGenPHD2Server();

    if(local_test)
    {
        boost::asio::io_service io_service;

        std::cerr << "Instantiating socket" << std::endl;
        tcp::socket s(io_service);

        std::cerr << "Instantiating resolver" << std::endl;
        tcp::resolver resolver(io_service);

        std::cerr << "Connecting 127.0.0.1:4400" << std::endl;
        boost::asio::connect(s, resolver.resolve({"127.0.0.1","4400"}));
        //boost::asio::connect(s, resolver.resolve({"192.168.0.160","4400"}));

        std::cerr << "Reading welcome" << std::endl;
        boost::asio::streambuf buf;
        boost::system::error_code e;
        std::string welcome;
        do
        {
            boost::asio::read_until(s, buf, "\r\n", e);
            std::getline(std::istream(&buf),welcome);
            //welcome.erase(welcome.rfind("\r"),1);
            std::cerr << "Did read '" << welcome << "' with error " << e << std::endl;
        }
        while(true);

        std::cerr << "Writing a random line" << std::endl;
        boost::asio::write(s, boost::asio::buffer(std::string("A random line\n")), e);

        std::cerr << "Reading back the random line" << std::endl;
        boost::asio::read_until(s, buf, '\n', e);
        std::getline(std::istream(&buf),welcome);
        std::cerr << "Did read '" << welcome << "' with error " << e << std::endl;

        std::cerr << "Closing client" << std::endl;
        s.close();
    }

    std::cerr << "Terminating server" << std::endl;
    delete server;
}
