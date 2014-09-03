//
// blocking_tcp_echo_server.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2013 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
/*
#include <cstdlib>
#include <iostream>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <vector>
#include <ctime>   // localtime
#include <sstream> // stringstream
#include <iomanip>
#include <thread>
#include <memory>
#include <set>
#include "common/chunk.h"
#include "pcmEncoder.h"
#include "oggEncoder.h"
#include <syslog.h>
*/

#include <cstdlib>
#include <iostream>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <vector>
#include <ctime>   // localtime
#include <sstream> // stringstream
#include <iomanip>
#include <thread>
#include <memory>
#include <set>
#include <sstream>
#include "common/timeUtils.h"
#include "common/queue.h"
#include "common/signalHandler.h"
#include "common/utils.h"
#include "common/sampleFormat.h"
#include "../server/pcmEncoder.h"
#include "../server/oggEncoder.h"
#include "common/message.h"


using boost::asio::ip::tcp;
namespace po = boost::program_options;


typedef boost::shared_ptr<tcp::socket> socket_ptr;
using namespace std;
using namespace std::chrono;


bool g_terminated = false;

/*
int main(int argc, char* argv[])
{
	TestMessage* chunk = new TestMessage(1, (char*)"Hallo");
	stringstream ss;
	chunk->serialize(ss);

	BaseMessage header;
	ss.seekg(0, ss.beg);
	header.read(ss);
//	cout << ss.str() << "\n";
//	ss.read(reinterpret_cast<char*>(&header), sizeof(MessageHeader));
	cout << "Header: " << header.type << ", " << header.size << "\n";
	delete chunk;
	chunk = new TestMessage();
	chunk->read(ss);
	cout << "Header: " << chunk->type << ", " << chunk->size << ", " << (int)chunk->logLevel << ", " << chunk->text << "\n";

	
	chunk->tv_sec = 21;
	chunk->tv_usec = 2;
	chunk->payloadSize = 5;
	chunk->payload = (char*)malloc(5);
chunk->payload[0] = 99;
	char* stream = chunk->serialize();
cout << "1\n";
for (size_t n=0; n<24; ++n)
	cout << (int)stream[n] << " ";
	delete chunk;
cout << "\n3\n";
	chunk = new WireChunk();
cout << "4\n";
	chunk->deserialize(stream);
cout << "5\n";
	cout << chunk->tv_sec << ", " << chunk->tv_usec << ", " << chunk->payloadSize << ", " << chunk->payload << "\n"; 	

return 0;
}
*/


class Session
{
public:
	Session(socket_ptr sock) : active_(false), socket_(sock)
	{
	}

	void sender()
	{
		try
		{
			for (;;)
			{
				shared_ptr<BaseMessage> message(messages.pop());
/*				char* stream = chunk->serialize();
				size_t written(0);
				size_t toWrite = sizeof(stream);
				do
				{
					written += boost::asio::write(*socket_, boost::asio::buffer(stream + written, toWrite - written));//, error);
				}
				while (written < toWrite);
*/			}
		}
		catch (std::exception& e)
		{
			std::cerr << "Exception in thread: " << e.what() << "\n";
			active_ = false;
		}
	}

	void start()
	{
		active_ = true;
		senderThread = new thread(&Session::sender, this);
//		readerThread.join();
	}

	void send(shared_ptr<BaseMessage> message)
	{
		if (!message)
			return;

		while (messages.size() > 100)//* chunk->getDuration() > 10000)
			messages.pop();
		messages.push(message);
	}

	bool isActive() const
	{
		return active_;
	}

private:
	bool active_;
	socket_ptr socket_;
	thread* senderThread;
	Queue<shared_ptr<BaseMessage>> messages;
};


class Server
{
public:
	Server(unsigned short port) : port_(port), headerChunk(NULL)
	{
	}

	void acceptor()
	{
		tcp::acceptor a(io_service_, tcp::endpoint(tcp::v4(), port_));
		for (;;)
		{
			socket_ptr sock(new tcp::socket(io_service_));
			a.accept(*sock);
//			cout << "New connection: " << sock->remote_endpoint().address().to_string() << "\n";
			Session* session = new Session(sock);
cout << "Sending header: " << headerChunk->payloadSize << "\n";
			session->send(headerChunk);
			session->start();
			sessions.insert(shared_ptr<Session>(session));
		}
	}

	void setHeader(shared_ptr<HeaderMessage> header)
	{
		if (header)
			headerChunk = shared_ptr<HeaderMessage>(header);
	}

	void send(shared_ptr<BaseMessage> message)
	{
		for (std::set<shared_ptr<Session>>::iterator it = sessions.begin(); it != sessions.end(); ) 
		{
    		if (!(*it)->isActive())
			{
				cout << "Session inactive. Removing\n";
		        sessions.erase(it++);
			}
		    else
		        ++it;
	    }

		for (auto s : sessions)
			s->send(message);
	}

	void start()
	{
		acceptThread = new thread(&Server::acceptor, this);
	}

	void stop()
	{
//		acceptThread->join();
	}

private:
	set<shared_ptr<Session>> sessions;
	boost::asio::io_service io_service_;
	unsigned short port_;
	shared_ptr<HeaderMessage> headerChunk;
	thread* acceptThread;
};


class ServerException : public std::exception
{
public:
	ServerException(const std::string& what) : what_(what)
	{
	}

	virtual ~ServerException() throw()
	{
	}

	virtual const char* what() const throw()
	{
		return what_.c_str();
	}

private:
	std::string what_;
};


int main(int argc, char* argv[])
{
	try
	{
		string sampleFormat;

        size_t port;
        string fifoName;
		string codec;
		bool runAsDaemon;

        po::options_description desc("Allowed options");
        desc.add_options()
            ("help,h", "produce help message")
            ("port,p", po::value<size_t>(&port)->default_value(98765), "port to listen on")
	        ("sampleformat,s", po::value<string>(&sampleFormat)->default_value("48000:16:2"), "sample format")
	        ("codec,c", po::value<string>(&codec)->default_value("ogg"), "transport codec [ogg|pcm]")
            ("fifo,f", po::value<string>(&fifoName)->default_value("/tmp/snapfifo"), "name of fifo file")
            ("daemon,d", po::bool_switch(&runAsDaemon)->default_value(false), "daemonize")
        ;

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help"))
        {
            cout << desc << "\n";
            return 1;
        }


		if (runAsDaemon)
		{
			daemonize();
			syslog (LOG_NOTICE, "First daemon started.");
		}

		openlog ("firstdaemon", LOG_PID, LOG_DAEMON);

		using namespace std; // For atoi.
		Server* server = new Server(port);
		server->start();

		timeval tvChunk;
		gettimeofday(&tvChunk, NULL);
		long nextTick = getTickCount();

        mkfifo(fifoName.c_str(), 0777);
size_t duration = 50;

		SampleFormat format(sampleFormat);
		std::auto_ptr<Encoder> encoder;
		if (codec == "ogg")
			encoder.reset(new OggEncoder(sampleFormat));
		else if (codec == "pcm")
			encoder.reset(new PcmEncoder(sampleFormat));
		else
		{
			cout << "unknown codec: " << codec << "\n";
			return 1;
		}

///		shared_ptr<HeaderMessage> header(encoder->getHeader());
//		server->setHeader(header);

        while (!g_terminated)
        {
            int fd = open(fifoName.c_str(), O_RDONLY);
            try
            {
                shared_ptr<PcmChunk> chunk;//(new WireChunk());
                while (true)//cin.good())
                {
                    chunk.reset(new PcmChunk(format, duration));//2*WIRE_CHUNK_SIZE));
                    int toRead = chunk->payloadSize;
                    int len = 0;
                    do
                    {
                        int count = read(fd, chunk->payload + len, toRead - len);
                        if (count <= 0)
                            throw ServerException("count = " + boost::lexical_cast<string>(count));

                        len += count;
                    }
                    while (len < toRead);

                    chunk->tv_sec = tvChunk.tv_sec;
                    chunk->tv_usec = tvChunk.tv_usec;
					double chunkDuration = 50;//encoder->encode(chunk.get());
					if (chunkDuration > 0)
	                    server->send(chunk);
cout << chunk->tv_sec << ", " << chunk->tv_usec / 1000 << "\n";
//                    addUs(tvChunk, 1000*chunk->getDuration());
                    addUs(tvChunk, chunkDuration * 1000);
                    nextTick += duration;
                    long currentTick = getTickCount();
                    if (nextTick > currentTick)
                    {
                        usleep((nextTick - currentTick) * 1000);
                    }
                    else
                    {
                        gettimeofday(&tvChunk, NULL);
                        nextTick = getTickCount();
                    }
                }
            }
            catch(const std::exception& e)
            {
				std::cerr << "Exception: " << e.what() << std::endl;
            }
            close(fd);
        }

		server->stop();
	}
	catch (const std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << std::endl;
	}

	syslog (LOG_NOTICE, "First daemon terminated.");
    closelog();
}




