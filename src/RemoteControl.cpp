/*
   Copyright (C) 2007, 2008, 2009, 2010, 2011, 2012
   Her Majesty the Queen in Right of Canada (Communications Research
   Center Canada)

   Copyright (C) 2014
   Matthias P. Braendli, matthias.braendli@mpb.li
 */
/*
   This file is part of ODR-DabMod.

   ODR-DabMod is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as
   published by the Free Software Foundation, either version 3 of the
   License, or (at your option) any later version.

   ODR-DabMod is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with ODR-DabMod.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <list>
#include <string>
#include <iostream>
#include <string>
#include <boost/asio.hpp>
#include <boost/thread.hpp>

#include "RemoteControl.h"

using boost::asio::ip::tcp;
using namespace std;


void RemoteControllerTelnet::restart()
{
    m_restarter_thread = boost::thread(&RemoteControllerTelnet::restart_thread,
            this, 0);
}

// This runs in a separate thread, because
// it would take too long to be done in the main loop
// thread.
void RemoteControllerTelnet::restart_thread(long)
{
    m_running = false;

    if (m_port) {
        m_child_thread.interrupt();
        m_child_thread.join();
    }

    m_child_thread = boost::thread(&RemoteControllerTelnet::process, this, 0);
}

void RemoteControllerTelnet::process(long)
{
    m_welcome = "ODR-DabMod Remote Control CLI\nWrite 'help' for help.\n**********\n";
    m_prompt = "> ";

    std::string in_message;
    size_t length;

    try {
        boost::asio::io_service io_service;
        tcp::acceptor acceptor(io_service, tcp::endpoint(tcp::v4(), m_port));

        while (m_running) {
            in_message = "";

            tcp::socket socket(io_service);

            acceptor.accept(socket);

            boost::system::error_code ignored_error;

            boost::asio::write(socket, boost::asio::buffer(m_welcome),
                    boost::asio::transfer_all(),
                    ignored_error);

            while (m_running && in_message != "quit") {
                boost::asio::write(socket, boost::asio::buffer(m_prompt),
                        boost::asio::transfer_all(),
                        ignored_error);

                in_message = "";

                boost::asio::streambuf buffer;
                length = boost::asio::read_until( socket, buffer, "\n", ignored_error);

                std::istream str(&buffer); 
                std::getline(str, in_message);

                if (length == 0) {
                    std::cerr << "RC: Connection terminated" << std::endl;
                    break;
                }

                while (in_message.length() > 0 && 
                        (in_message[in_message.length()-1] == '\r' ||
                         in_message[in_message.length()-1] == '\n')) {
                    in_message.erase(in_message.length()-1, 1);
                }

                if (in_message.length() == 0) {
                    continue;
                }

                std::cerr << "RC: Got message '" << in_message << "'" << std::endl;

                dispatch_command(socket, in_message);
            }
            std::cerr << "RC: Closing socket" << std::endl;
            socket.close();
        }
    }
    catch (std::exception& e)
    {
        std::cerr << "Remote control caught exception: " << e.what() << std::endl;
        m_fault = true;
    }
}

void RemoteControllerTelnet::dispatch_command(tcp::socket& socket, string command)
{
    vector<string> cmd = tokenise_(command);

    if (cmd[0] == "help") {
        reply(socket,
                "The following commands are supported:\n"
                "  list\n"
                "    * Lists the modules that are loaded and their parameters\n"
                "  show MODULE\n"
                "    * Lists all parameters and their values from module MODULE\n"
                "  get MODULE PARAMETER\n"
                "    * Gets the value for the specified PARAMETER from module MODULE\n"
                "  set MODULE PARAMETER VALUE\n"
                "    * Sets the value for the PARAMETER ofr module MODULE\n"
                "  quit\n"
                "    * Terminate this session\n"
                "\n");
    }
    else if (cmd[0] == "list") {
        stringstream ss;

        if (cmd.size() == 1) {
            for (list<RemoteControllable*>::iterator it = m_cohort.begin();
                    it != m_cohort.end(); ++it) {
                ss << (*it)->get_rc_name() << endl;

                list< vector<string> >::iterator param;
                list< vector<string> > params = (*it)->get_parameter_descriptions();
                for (param = params.begin();
                        param != params.end();
                        ++param) {
                    ss << "\t" << (*param)[0] << " : " << (*param)[1] << endl;
                }
            }
        }
        else {
            reply(socket, "Too many arguments for command 'list'");
        }

        reply(socket, ss.str());
    }
    else if (cmd[0] == "show") {
        if (cmd.size() == 2) {
            try {
                stringstream ss;
                list< vector<string> > r = get_param_list_values_(cmd[1]);
                for (list< vector<string> >::iterator it = r.begin();
                        it != r.end(); ++it) {
                    ss << (*it)[0] << ": " << (*it)[1] << endl;
                }
                reply(socket, ss.str());

            }
            catch (ParameterError &e) {
                reply(socket, e.what());
            }
        }
        else
        {
            reply(socket, "Incorrect parameters for command 'show'");
        }
    }
    else if (cmd[0] == "get") {
        if (cmd.size() == 3) {
            try {
                string r = get_param_(cmd[1], cmd[2]);
                reply(socket, r);
            }
            catch (ParameterError &e) {
                reply(socket, e.what());
            }
        }
        else
        {
            reply(socket, "Incorrect parameters for command 'get'");
        }
    }
    else if (cmd[0] == "set") {
        if (cmd.size() >= 4) {
            try {
                stringstream new_param_value;
                for (size_t i = 3; i < cmd.size(); i++) {
                    new_param_value << cmd[i];

                    if (i+1 < cmd.size()) {
                        new_param_value << " ";
                    }
                }

                set_param_(cmd[1], cmd[2], new_param_value.str());
                reply(socket, "ok");
            }
            catch (ParameterError &e) {
                reply(socket, e.what());
            }
            catch (exception &e) {
                reply(socket, "Error: Invalid parameter value. ");
            }
        }
        else {
            reply(socket, "Incorrect parameters for command 'set'");
        }
    }
    else if (cmd[0] == "quit") {
        reply(socket, "Goodbye");
    }
    else {
        reply(socket, "Message not understood");
    }
}

void RemoteControllerTelnet::reply(tcp::socket& socket, string message)
{
    boost::system::error_code ignored_error;
    stringstream ss;
    ss << message << "\r\n";
    boost::asio::write(socket, boost::asio::buffer(ss.str()),
            boost::asio::transfer_all(),
            ignored_error);
}

