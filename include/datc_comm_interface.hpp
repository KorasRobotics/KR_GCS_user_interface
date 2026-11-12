/**
 * @file datc_comm_interface.hpp
 * @author Inhwan Yoon (inhwan94@korea.ac.kr)
 * @brief
 * @version 1.0
 * @date 2023-11-06
 *
 * @copyright Copyright (c) 2023
 *
 */
#ifndef DATC_COMM_INTERFACE_HPP
#define DATC_COMM_INTERFACE_HPP

#include "datc_ctrl.hpp"
#include <atomic>
#include <thread>
#include <QThread>
#include <chrono>
#include <boost/asio.hpp>
#include "socket/tcp_manager.hpp"

using namespace std;
using namespace boost::asio;
using namespace boost::asio::ip;

class DatcCommInterface : public QThread, public DatcCtrl {
    Q_OBJECT

public:
    DatcCommInterface(int argc, char **argv);
    virtual ~DatcCommInterface();

Q_SIGNALS:
    void rosShutdown();
    // TCP 명령 52(장치 통신속도 변경). 장치가 속도를 바꾸면 GCS도 새 속도로 다시 연결해야 하므로
    // UI 스레드(MainWindow::remoteSetDeviceBaudrate)가 처리한다. baudrate = value_1 × 100
    void deviceBaudrateRequested(int baudrate);

public:
    bool init(const char *port_name, uint16_t slave_address, int baudrate);
    void initTcp(const string addr, uint16_t socket_port);
    void releaseTcp();

    // 52 처리 결과를 상태 JSON의 last_query(command 52)로 알리고, 미뤄 둔 TCP 명령 처리를 다시 시작한다.
    // baudrate = 처리 뒤 GCS가 연결된 속도, result = changed / unchanged / reverted / no_response / not_sent
    void reportDeviceBaudrate(bool ok, int baudrate, const string &result);
    bool isPollingPaused() {return poll_pause_.load() > 0;}

    bool isSocketConnected() {return is_socket_connected_;}
    bool getTcpSendStatus() {return flag_tcp_send_status_;}
    void setTcpSendStatus(bool flag) {flag_tcp_send_status_ = flag;}

    // UI 스레드가 재연결·속도 검색처럼 버스를 연달아 쓰는 동안 50 Hz 폴링을 멈춘다(중첩 가능).
    // 폴링 스레드는 응답이 없으면 한 주기에 읽기를 2~3번(각각 응답 시간 초과까지) 연달아 하며
    // mutex_comm_을 거의 놓지 않는다. Windows 뮤텍스는 공정하지 않아 UI 스레드의 probe가 락을
    // 얻지 못하고, Release에서는 사실상 영원히 기다렸다(속도 자동 검색 '응답 없음', 2026-10-19).
    // pausePolling은 진행 중인 주기가 끝날 때까지 기다린다(최대 약 1.5 s). PollingPause로 쓴다.
    void pausePolling();
    void resumePolling();

private:
    void run();
    void sendStatus();
    void recvCommand();

    std::atomic<bool> flag_program_stop_{false};
    std::atomic<int>  poll_pause_{0};   // > 0이면 폴링 주기를 건너뛴다
    mutex             mutex_poll_;      // 폴링 한 주기 동안 잡는다 — pausePolling이 주기 끝을 기다린다

    // TCP socket related variables
    TcpServer *tcp_server_ = nullptr;
    std::thread tcp_thread_;

    bool flag_tcp_stop_        = false;
    bool is_socket_connected_  = false;
    bool flag_tcp_send_status_ = true;

    mutex mutex_tcp_;

    // 52를 UI 스레드에 넘긴 뒤 결과(reportDeviceBaudrate)가 올 때까지 다음 TCP 명령을 미룬다.
    // 결과가 30 s 안에 오지 않으면 다시 받는다(UI가 멈춘 경우 TCP 명령이 계속 막히지 않게).
    std::atomic<bool> baud_change_pending_{false};
    std::chrono::steady_clock::time_point baud_change_since_;

    // 조회 명령(217/221/223)과 통신속도 변경(52)의 결과 — 상태 JSON의 "last_query"로 함께 전송 (별도 메시지 형식을 만들지 않아
    // 상태만 읽는 기존 클라이언트와 호환). seq가 바뀌면 새 결과.
    void setLastQuery(Json::Value query);
    Json::Value last_query_;
    uint32_t    query_seq_ = 0;
    mutex       mutex_query_;
};

// 범위 안에서 폴링을 멈춘다: PollingPause pause(datc_interface_);
class PollingPause {
public:
    explicit PollingPause(DatcCommInterface *d) : d_(d) { d_->pausePolling(); }
    ~PollingPause() { d_->resumePolling(); }
    PollingPause(const PollingPause &) = delete;
    PollingPause &operator=(const PollingPause &) = delete;

private:
    DatcCommInterface *d_;
};

#endif // DATC_COMM_INTERFACE_HPP
