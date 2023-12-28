/**
 * @file datc_comm_interface.cpp
 * @author Inhwan Yoon (inhwan94@korea.ac.kr)
 * @brief
 * @version 1.0
 * @date 2023-11-06
 *
 * @copyright Copyright (c) 2023
 *
 */
#include "datc_comm_interface.hpp"

const uint16_t kFreq = 50;

DatcCommInterface::DatcCommInterface(int argc, char **argv) {

}

DatcCommInterface::~DatcCommInterface() {
    flag_program_stop_ = true;
    usleep(1000);

    releaseTcp();
    modbusRelease();
}

bool DatcCommInterface::init(const char *port_name, uint16_t slave_address) {
    if (!modbusInit(port_name, slave_address)) {
        return false;
    }

    COUT("DATC ros interface init.");

    return true;
}

void DatcCommInterface::initTcp(const string addr, uint16_t socket_port) {
    unique_lock<mutex> lg(mutex_tcp_);

    flag_tcp_stop_ = false;

    tcp_server_ = new TcpServer(socket_port);
    tcp_thread_ = std::thread(bind(&DatcCommInterface::recvCommand, this));

    is_socket_connected_ = true;
}

void DatcCommInterface::releaseTcp() {
    unique_lock<mutex> lg(mutex_tcp_);

    is_socket_connected_ = false;
    flag_tcp_stop_       = true;

    usleep(100);

    if (tcp_thread_.joinable()) {
        tcp_thread_.join();
    }

    if (tcp_server_ != NULL) {
        tcp_server_->~TcpServer();
    }
}

void DatcCommInterface::sendStatus() {
    DatcStatus status = getDatcStatus();

    Json::Value json;

    json["states"]     = status.states;
    json["motor_pos"]  = status.motor_pos;
    json["motor_vel"]  = status.motor_vel;
    json["motor_cur"]  = status.motor_cur;
    json["finger_pos"] = status.finger_pos;
    json["voltage"]    = status.voltage;

    unique_lock<mutex> lg(mutex_tcp_);

    MessageManager<Json::Value>::getInstance().pushToAllClientQueue(json);
}

void DatcCommInterface::recvCommand() {
    auto checkValueFn = [] (const Json::Value json, string str) {
        if (json.isMember(str)) {
            return true;
        } else {
            COUT("[Error] \"" + str + "\" must be entered.");
            return false;
        }
    };

    const string cmd_change_slave = "change_slave";
    const string cmd_str          = "command";
    const string value_1_str      = "value_1";
    const string value_2_str      = "value_2";

    Json::Value json;

    while (!flag_tcp_stop_) {
        mutex_tcp_.lock();

        if (MessageManager<Json::Value>::getInstance().tryPopFromWokerQueue(json)) {
            mutex_tcp_.unlock();

            if (json.isMember(cmd_change_slave)) {
                modbusSlaveChange(json[cmd_change_slave].asUInt());
                continue;
            } else if (!json.isMember(cmd_str)) {
                continue;
            }

            switch ((DATC_COMMAND) json[cmd_str].asUInt()) {
                case DATC_COMMAND::MOTOR_ENABLE:
                    motorEnable();
                    break;

                case DATC_COMMAND::MOTOR_STOP:
                    motorStop();
                    break;

                case DATC_COMMAND::MOTOR_DISABLE:
                    motorDisable();
                    break;

                case DATC_COMMAND::MOTOR_POSITION_CONTROL:
                    if (!checkValueFn(json, value_1_str)) break;
                    if (!checkValueFn(json, value_2_str)) break;
                    motorPosCtrl(json[value_1_str].asInt(), json[value_2_str].asUInt());
                    break;

                case DATC_COMMAND::MOTOR_VELOCITY_CONTROL:
                    if (!checkValueFn(json, value_1_str)) break;
                    motorVelCtrl(json[value_1_str].asInt());
                    break;

                case DATC_COMMAND::MOTOR_CURRENT_CONTROL:
                    if (!checkValueFn(json, value_1_str)) break;
                    motorCurCtrl(json[value_1_str].asInt());
                    break;

                case DATC_COMMAND::CHANGE_MODBUS_ADDRESS:
                    if (!checkValueFn(json, value_1_str)) break;
                    setModbusAddr(json[value_1_str].asUInt());
                    break;

                case DATC_COMMAND::GRIPPER_INITIALIZE:
                    grpInitialize();
                    break;

                case DATC_COMMAND::GRIPPER_OPEN:
                    grpOpen();
                    break;

                case DATC_COMMAND::GRIPPER_CLOSE:
                    grpClose();
                    break;

                case DATC_COMMAND::SET_FINGER_POSITION:
                    if (!checkValueFn(json, value_1_str)) break;
                    setFingerPos(json[value_1_str].asUInt());
                    break;

                case DATC_COMMAND::VACUUM_GRIPPER_ON:
                    vacuumGrpOn();
                    break;

                case DATC_COMMAND::VACUUM_GRIPPER_OFF:
                    vacuumGrpOff();
                    break;

                case DATC_COMMAND::SET_MOTOR_TORQUE:
                    if (!checkValueFn(json, value_1_str)) break;
                    setMotorTorque(json[value_1_str].asUInt());
                    break;

                case DATC_COMMAND::SET_MOTOR_SPEED:
                    if (!checkValueFn(json, value_1_str)) break;
                    setMotorSpeed(json[value_1_str].asUInt());
                    break;

                default:
                    COUT("Error: Undefined command.");
            }
        } else {
            mutex_tcp_.unlock();
        }

        usleep(10000);
    }
}

// Main loop
void DatcCommInterface::run() {
    auto cycleFn([&] () {
        if (mbc_.getConnectionState()) {
            readDatcData();

            if (is_socket_connected_ && flag_tcp_send_status_) {
                sendStatus();
            }
        }
    });

    const std::chrono::duration<double> period(1 / (double) kFreq);

    while(!flag_program_stop_) {
        auto time_start = std::chrono::steady_clock::now();

        cycleFn();

        // Calculate the time elapsed and sleep for the remaining time
        auto time_end = std::chrono::steady_clock::now();
        auto time_elapsed = time_end - time_start;

        if (time_elapsed < period) {
            std::this_thread::sleep_for(period - time_elapsed);
        }
    }

    motorDisable();
    modbusRelease();
}

