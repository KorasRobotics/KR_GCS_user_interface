/**
 * @file tcp_manager.hpp
 * @author Inhwan Yoon (inhwan94@korea.ac.kr)
 * @brief : Individual sockets are created to handle the client's TCP communication
 * connection, and messages sent and received through each connected socket
 * @version 1.0
 * @date 2022-12-29
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef TCP_MANAGER_HPP
#define TCP_MANAGER_HPP

#include <boost/asio.hpp>
#include "message_manager.hpp"

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(WIN64) || defined(_WIN64) || defined(__WIN64__)
#include "../lib/json.h"
#else
#include <jsoncpp/json/json.h>
#endif

using namespace std;
using namespace tcp_communication;

namespace tcp_communication {

class TcpSocket {
    static constexpr int MAX_BUFFER = 1024; /**< Maximum size of buffer */
public:
    TcpSocket(boost::asio::io_service &io_service);
    ~TcpSocket();

public:
    boost::asio::ip::tcp::socket &getSocket();

    void start();
    void close();

    void writeHandler();
    void readHandler(const boost::system::error_code& err, size_t bytes_transferred);

private:
    bool parseJsonFromBuffer(Json::Value &json);

private:
    MessageHandler<Json::Value> &message_handler_;
    boost::asio::ip::tcp::socket socket_;
    string recevied_;
    char buffer_[MAX_BUFFER];
};

class TcpServer {
public:
    TcpServer(const int port = 8421);
    ~TcpServer();

public:
    void startAccept();
    void acceptHandler(TcpSocket *socket, const boost::system::error_code& err);

private:
    boost::asio::io_service io_service_;
    boost::asio::ip::tcp::acceptor acceptor_;
};
} // namespace tcp_comm
#endif
