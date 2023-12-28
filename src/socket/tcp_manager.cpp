#include "tcp_manager.hpp"

//#include <boost/thread.hpp>
//#include <boost/bind.hpp>
#include <iostream>
#include <system_error>
#include <unistd.h>

TcpServer::TcpServer(const int port)
        :acceptor_(io_service_, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)) {
    startAccept();

//    boost::thread io_service_thread(boost::bind(&boost::asio::io_service::run, &io_service_));
    std::thread io_service_thread([&] () {io_service_.run();});
    io_service_thread.detach();
}

TcpServer::~TcpServer() {
    acceptor_.close();
    io_service_.stop();

    while (!io_service_.stopped()) {
        usleep(1000);
    }

    usleep(1000000);
}

void TcpServer::startAccept() {
    TcpSocket *socket = new TcpSocket(io_service_);
//    acceptor_.async_accept(socket->getSocket(), boost::bind(&TcpServer::acceptHandler, this, socket, boost::asio::placeholders::error));
    acceptor_.async_accept(socket->getSocket(), std::bind(&TcpServer::acceptHandler, this, socket, std::placeholders::_1));
}

void TcpServer::acceptHandler(TcpSocket* socket, const boost::system::error_code& err) {
    if (!err) {
        socket->start();
    } else {
        delete socket;
    }

    usleep(50000);
    startAccept();
    cout << "Tcp connected" << endl;
}

TcpSocket::TcpSocket(boost::asio::io_service &io_service)
    :message_handler_(MessageManager<Json::Value>::getInstance()), socket_(io_service) {

}

TcpSocket::~TcpSocket() {
    close();
}

boost::asio::ip::tcp::socket &TcpSocket::getSocket() {
    return socket_;
}

void TcpSocket::start() {
    message_handler_.createClientQueue(socket_.native_handle());
//    boost::thread parse_thread(boost::bind(&TcpSocket::writeHandler, this));
    std::thread parse_thread(std::bind(&TcpSocket::writeHandler, this));
    parse_thread.detach();
//    socket_.async_read_some(boost::asio::buffer(buffer_, MAX_BUFFER), boost::bind(&TcpSocket::readHandler, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
    socket_.async_read_some(boost::asio::buffer(buffer_, MAX_BUFFER), std::bind(&TcpSocket::readHandler, this, std::placeholders::_1, std::placeholders::_2));
}

void TcpSocket::close() {
    try {
        message_handler_.deleteClientQueue(socket_.native_handle());
        if (socket_.is_open()) {
            socket_.close();
        }
    } catch (boost::system::system_error const& e) {
        cerr << "Close error: " << e.what() << "\n";
    }
}

void TcpSocket::writeHandler() {
    while (socket_.is_open()) {
        Json::Value json;

        if (message_handler_.tryPopFromClientQueue(socket_.native_handle(), json)) {
            Json::FastWriter writer;
            string data = writer.write(json);
            boost::system::error_code error;

            boost::asio::write(socket_, boost::asio::buffer(data, data.size()), error);
            // boost::asio::write(socket_, boost::asio::buffer(data, 2048), error);

            if (error) {
                cout << "Write error: " << error.message() << endl;
                socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
                close();
            }
        }
        usleep(1000);
    }
}

void TcpSocket::readHandler(const boost::system::error_code& err, size_t bytes_transferred) {
    if (!err) {
        recevied_ += string(buffer_, buffer_ + bytes_transferred);

        Json::Value json;
        while (parseJsonFromBuffer(json)) {
            Json::FastWriter writer;
            string data = writer.write(json);

            message_handler_.pushToWorkerQueue(json);
        }

        usleep(1000);
//        socket_.async_read_some(boost::asio::buffer(buffer_, MAX_BUFFER), boost::bind(&TcpSocket::readHandler, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
        socket_.async_read_some(boost::asio::buffer(buffer_, MAX_BUFFER), std::bind(&TcpSocket::readHandler, this, std::placeholders::_1, std::placeholders::_2));
    } else {
        boost::system::error_code error_temp = err;
        cout << "Read error: " << err.message() << endl;
        socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error_temp);
        close();
    }
}

bool TcpSocket::parseJsonFromBuffer(Json::Value &json) {
    string json_str;
    size_t index = recevied_.find('{');
    if (index == string::npos) {
        recevied_.clear();
        return false;
    }
    recevied_.erase(0, index);

    index = recevied_.find('}');
    if (index == string::npos) {
        return false;
    }

    json_str = recevied_.substr(0, index + 1);
    recevied_.erase(0, index + 1);

    Json::Reader reader;
    if (!reader.parse(json_str, json)) {
        return false;
    }

    return true;
}
