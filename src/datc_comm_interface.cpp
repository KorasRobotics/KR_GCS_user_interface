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
    (void) argc;
    (void) argv;
}

DatcCommInterface::~DatcCommInterface() {
    flag_program_stop_ = true;
    wait(1000);

    releaseTcp();
    modbusRelease();
}

bool DatcCommInterface::init(const char *port_name, uint16_t slave_address, int baudrate) {
    if (!modbusInit(port_name, slave_address, baudrate)) {
        return false;
    }

    COUT("MATC interface init.");

    return true;
}

void DatcCommInterface::initTcp(const string addr, uint16_t socket_port) {
    (void) addr;
    unique_lock<mutex> lg(mutex_tcp_);

    flag_tcp_stop_ = false;

    tcp_server_ = new TcpServer(socket_port);
    tcp_thread_ = std::thread(bind(&DatcCommInterface::recvCommand, this));

    is_socket_connected_ = true;
}

void DatcCommInterface::releaseTcp() {
    {
        unique_lock<mutex> lg(mutex_tcp_);
        is_socket_connected_ = false;
        flag_tcp_stop_       = true;
    }

    usleep(100);

    if (tcp_thread_.joinable()) {
        tcp_thread_.join();
    }

    unique_lock<mutex> lg(mutex_tcp_);

    if (tcp_server_ != nullptr) {
        delete tcp_server_;
        tcp_server_ = nullptr;
    }
}

void DatcCommInterface::sendStatus() {
    DatcStatus status = getDatcStatus();

    Json::Value json;

    // Motor / gripper feedback (registers 10–17)
    json["states"]     = status.states;
    json["motor_pos"]  = status.motor_pos;
    json["motor_vel"]  = status.motor_vel;
    json["motor_cur"]  = status.motor_cur;
    json["finger_pos"] = status.finger_pos;
    json["fault_now"]         = status.fault_now;
    json["fault_occurred"]    = status.fault_occurred;
    json["gripper_direction"] = status.gripper_direction;
    json["object_grasp"]      = status.object_grasp;
    json["grip_hold"]         = status.grip_hold;
    json["slip_detected"]     = status.slip_detected;
    json["voltage"]    = status.voltage;

    // Gripper connection (register 18, MATC v3) — CONNECT_CHECK/PG0
    json["gripper_connected"]   = status.gripper_connected;

    // Gripper physical specs (registers 20–23, MATC v3)
    // gripper_id == 128 means no gripper attached
    json["gripper_id"]          = status.gripper_id;
    json["gripper_stroke_mm"]   = status.gripper_stroke_mm;
    json["gripper_force_n"]     = status.gripper_force_n;
    json["gripper_payload_x10"] = status.gripper_payload_x10;

    // Grip result & calculated force (register 10 bit 12-13, register 19, MATC v3)
    json["gobj_result"]  = status.gobj_result;        // 0=이동중 1=물체(닫는중) 2=물체(여는중) 3=목표 도달
    json["grip_force_n"] = status.grip_force_calc_n;  // 파지 중 계산 파지력 [N]

    // Latched warnings (register 10 bit 14-15, MATC v3) — Error Clear(9)로 해제
    json["comm_overrun"]  = status.comm_overrun;
    json["eep_save_fail"] = status.eep_save_fail;

    // Device info (registers 28–33, MATC v3.01~). device_model 0 = 미지원(이전 펌웨어) 또는 아직 읽기 전
    {
        const DeviceInfo info = getDeviceInfo();
        json["device_model"]       = info.valid ? info.model : 0;
        json["fw_version"]         = info.valid ? info.fw_version : 0;
        json["max_speed_rpm"]      = getVelMaxRpm();   // 속도 제어(6) 상한 — GCS가 실제로 적용하는 값
        json["cur_full_scale_ma"]  = getCurMaxMa();    // 전류 제어(7) 100%
        json["elangle_calibrated"] = info.valid && info.elangleCalibrated();
        json["elangle_model_mismatch"] = info.valid && info.elangleModelMismatch();
        // 엔코더 자동 보정 상태 워드 (reg 33, v3.02~) — 장치 정보 폴링(약 1 s) 값
        json["enc_autocal_status"] = info.valid ? info.auto_cal : 0;
        // 일체형 그리퍼 고정 ID (reg 34, v3.04~) — 128 = 없음
        json["fixed_gripper_id"]   = info.valid ? info.fixed_gripper_id : kFixedGripperIdNone;
    }

    {
        unique_lock<mutex> lq(mutex_query_);
        if (!last_query_.isNull()) {
            json["last_query"] = last_query_;
        }
    }

    unique_lock<mutex> lg(mutex_tcp_);

    MessageManager<Json::Value>::getInstance().pushToAllClientQueue(json);
}

void DatcCommInterface::setLastQuery(Json::Value query) {
    unique_lock<mutex> lq(mutex_query_);
    query["seq"] = ++query_seq_;
    last_query_ = query;
}

void DatcCommInterface::reportDeviceBaudrate(bool ok, int baudrate, const string &result) {
    Json::Value q;
    q["command"]  = (int) DATC_COMMAND::CHANGE_MODBUS_BAUDRATE;
    q["ok"]       = ok;
    q["baudrate"] = baudrate;
    q["result"]   = result;
    setLastQuery(q);

    baud_change_pending_ = false;
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
    const string value_3_str      = "value_3";

    Json::Value json;

    while (!flag_tcp_stop_) {
        // UI가 재연결·속도 검색·장치 속도 변경 중(폴링 일시정지)이거나 TCP 52를 처리하는 동안에는 다음 명령을
        // 꺼내지 않고 끝난 뒤 처리한다 — 그사이 보내면 바뀌는 중인 속도로 나가 실패한다
        if (baud_change_pending_ &&
            (std::chrono::steady_clock::now() - baud_change_since_ > std::chrono::seconds(30))) {
            COUT("[TCP 52] No result within 30 s - resuming TCP commands.");
            baud_change_pending_ = false;
        }
        if (baud_change_pending_ || (poll_pause_.load() > 0)) {
            usleep(10000);
            continue;
        }

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

                case DATC_COMMAND::MOTOR_EMERGENCY_STOP:
                    motorEmergencyStop();
                    break;

                case DATC_COMMAND::MOTOR_DISABLE:
                    motorDisable();
                    break;

                case DATC_COMMAND::RESET_POSITION:
                    resetPosition();
                    break;

                case DATC_COMMAND::ERROR_CLEAR:
                    errorClear();
                    break;

                case DATC_COMMAND::SYSTEM_RESET:
                    systemReset();
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

                case DATC_COMMAND::CHANGE_MODBUS_BAUDRATE:
                    // {"command":52,"value_1":576} — 속도 / 100 (96, 192, 384, 576, 1152). 바꾼 뒤 GCS가 새 속도로 다시
                    // 연결해야 하므로 UI 스레드에 넘긴다. 결과는 다음 상태 JSON의 "last_query"(command 52)로 전달
                    if (!checkValueFn(json, value_1_str)) break;
                    baud_change_since_   = std::chrono::steady_clock::now();
                    baud_change_pending_ = true;
                    Q_EMIT deviceBaudrateRequested(json[value_1_str].asInt() * 100);
                    break;

                case DATC_COMMAND::CHANGE_ELECTRICAL_ANGLE:
                    if (!checkValueFn(json, value_1_str)) break;
                    changeElectricalAngle(json[value_1_str].asInt());
                    break;

                case DATC_COMMAND::GRIPPER_SET_DIRECTION:
                    if (!checkValueFn(json, value_1_str)) break;
                    grpSetDirection(json[value_1_str].asUInt() != 0);
                    break;

                case DATC_COMMAND::GRIPPER_INITIALIZE:
                    grpInitialize();
                    break;

                case DATC_COMMAND::GRIPPER_INITIALIZE2:
                    if (!checkValueFn(json, value_1_str)) break;
                    grpInitialize2(json[value_1_str].asInt());
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

                case DATC_COMMAND::IMPEDANCE_ON:
                    impedanceOn();
                    break;

                case DATC_COMMAND::IMPEDANCE_OFF:
                    impedanceOff();
                    break;

                case DATC_COMMAND::SET_IMPEDANCE_PARAMS:
                    if (!checkValueFn(json, value_1_str)) break;
                    if (!checkValueFn(json, value_2_str)) break;
                    setImpedanceParams(json[value_1_str].asInt(), json[value_2_str].asInt());
                    break;

                case DATC_COMMAND::SET_IMPEDANCE_MBK:
                    if (!checkValueFn(json, value_1_str)) break;
                    if (!checkValueFn(json, value_2_str)) break;
                    if (!checkValueFn(json, value_3_str)) break;
                    setImpedanceMBK(json[value_1_str].asInt(),
                                    json[value_2_str].asInt(),
                                    json[value_3_str].asInt());
                    break;

                case DATC_COMMAND::SET_MAX_VALUE:
                    if (!checkValueFn(json, value_1_str)) break;
                    if (!checkValueFn(json, value_2_str)) break;
                    setMaxValue(json[value_1_str].asInt(), json[value_2_str].asInt());
                    break;

                case DATC_COMMAND::SET_POSITION_PID_GAIN:
                    if (!checkValueFn(json, value_1_str)) break;
                    if (!checkValueFn(json, value_2_str)) break;
                    if (!checkValueFn(json, value_3_str)) break;
                    setPositionPidGain(json[value_1_str].asInt(),
                                       json[value_2_str].asInt(),
                                       json[value_3_str].asInt());
                    break;

                case DATC_COMMAND::SET_MOTOR_TORQUE:
                    if (!checkValueFn(json, value_1_str)) break;
                    setMotorTorque(json[value_1_str].asUInt());
                    break;

                case DATC_COMMAND::SET_MOTOR_SPEED:
                    if (!checkValueFn(json, value_1_str)) break;
                    setMotorSpeed(json[value_1_str].asUInt());
                    break;

                case DATC_COMMAND::SET_CURRENT_PI_GAIN:
                    if (!checkValueFn(json, value_1_str)) break;
                    if (!checkValueFn(json, value_2_str)) break;
                    setCurrentPiGain(json[value_1_str].asInt(), json[value_2_str].asInt());
                    break;

                case DATC_COMMAND::SET_VELOCITY_PI_GAIN:
                    if (!checkValueFn(json, value_1_str)) break;
                    if (!checkValueFn(json, value_2_str)) break;
                    setVelocityPiGain(json[value_1_str].asInt(), json[value_2_str].asInt());
                    break;

                // Gripper mapping table (MATC v3)
                case DATC_COMMAND::WRITE_MAP_ENTRY:
                    // {"command":216,"value_1":index,"value_2":stroke_mm,"value_3":force_n,"value_4":payload_x10}
                    if (!checkValueFn(json, value_1_str)) break;
                    if (!checkValueFn(json, value_2_str)) break;
                    if (!checkValueFn(json, value_3_str)) break;
                    {
                        const string value_4_str = "value_4";
                        uint16_t payload = json.isMember(value_4_str) ? json[value_4_str].asUInt() : 0;
                        writeMapEntry(json[value_1_str].asUInt(),
                                      json[value_2_str].asUInt(),
                                      json[value_3_str].asUInt(),
                                      payload);
                    }
                    break;

                case DATC_COMMAND::READ_MAP_ENTRY:
                    // 결과는 다음 상태 JSON의 "last_query"로 전달 (reg 24~27 응답 확인 후)
                    if (!checkValueFn(json, value_1_str)) break;
                    {
                        GripperMapEntry entry;
                        Json::Value q;
                        q["command"] = (int) DATC_COMMAND::READ_MAP_ENTRY;
                        q["ok"]      = queryMapEntry(json[value_1_str].asUInt(), entry);
                        q["index"]       = entry.index;
                        q["stroke_mm"]   = entry.stroke_mm;
                        q["force_n"]     = entry.force_n;
                        q["payload_x10"] = entry.payload_x10;
                        setLastQuery(q);
                    }
                    break;

                case DATC_COMMAND::SAVE_MAP:
                    saveGripperMap();
                    break;

                // Encoder harmonic compensation (MATC v3)
                case DATC_COMMAND::SET_ENC_HARM:
                    // {"command":219,"value_1":k,"value_2":A,"value_3":B} — k=0이면 전체 해제. A, B는 부호 있는 정수
                    if (!checkValueFn(json, value_1_str)) break;
                    if (json[value_1_str].asUInt() == 0) {
                        clearEncoderHarmonics();
                        break;
                    }
                    if (!checkValueFn(json, value_2_str)) break;
                    if (!checkValueFn(json, value_3_str)) break;
                    setEncoderHarmonic(json[value_1_str].asUInt(),
                                       (int16_t) json[value_2_str].asInt(),
                                       (int16_t) json[value_3_str].asInt());
                    break;

                case DATC_COMMAND::SAVE_ENC_HARM:
                    saveEncoderHarmonics();
                    break;

                case DATC_COMMAND::READ_ENC_HARM:
                    // 결과는 다음 상태 JSON의 "last_query"로 전달
                    if (!checkValueFn(json, value_1_str)) break;
                    {
                        EncHarmEntry entry;
                        Json::Value q;
                        q["command"] = (int) DATC_COMMAND::READ_ENC_HARM;
                        q["ok"]      = queryEncoderHarmonic(json[value_1_str].asUInt(), entry);
                        q["k"]              = entry.k;
                        q["a"]              = entry.a;
                        q["b"]              = entry.b;
                        q["active"]         = entry.active;
                        q["last_save_ok"]   = entry.last_save_ok;
                        q["last_save_fail"] = entry.last_save_fail;
                        setLastQuery(q);
                    }
                    break;

                // Encoder harmonic auto calibration (MATC v3.02~)
                case DATC_COMMAND::AUTO_ENC_HARM:
                    // {"command":222,"value_1":1} 시작, {"command":222,"value_1":0} 취소. 진행은 상태 JSON enc_autocal_status
                    if (!checkValueFn(json, value_1_str)) break;
                    if (json[value_1_str].asUInt() == 1) {
                        startAutoEncHarmonic();
                    } else {
                        cancelAutoEncHarmonic();
                    }
                    break;

                // Integrated gripper fixed ID (MATC v3.04~) — 생산·정비용. 결과는 상태 JSON fixed_gripper_id
                case DATC_COMMAND::SET_FIXED_GRIPPER_ID:
                    if (!checkValueFn(json, value_1_str)) break;
                    setFixedGripperId((uint16_t) json[value_1_str].asUInt());
                    break;

                case DATC_COMMAND::READ_AUTO_ENC_HARM:
                    // 결과는 다음 상태 JSON의 "last_query"로 전달
                    if (!checkValueFn(json, value_1_str)) break;
                    {
                        AutoEncHarmResult r;
                        Json::Value q;
                        q["command"]     = (int) DATC_COMMAND::READ_AUTO_ENC_HARM;
                        q["ok"]          = queryAutoEncHarmonicResult(json[value_1_str].asUInt(), r);
                        q["k"]           = r.k;
                        q["before_mdeg"] = r.before_mdeg;
                        q["after_mdeg"]  = r.after_mdeg;
                        q["status"]      = r.status;
                        setLastQuery(q);
                    }
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

// 폴링 일시정지 (헤더 설명 참고). 카운터를 올린 뒤 mutex_poll_을 한 번 잡아, 이미 시작된 주기가
// 끝나기를 기다린다. 그 뒤로 폴링 스레드는 카운터를 보고 주기를 건너뛰므로 락 경쟁이 없다.
void DatcCommInterface::pausePolling() {
    ++poll_pause_;
    unique_lock<mutex> lg(mutex_poll_);
}

void DatcCommInterface::resumePolling() {
    if (poll_pause_.load() > 0) {
        --poll_pause_;
    }
}

// Main loop
void DatcCommInterface::run() {
    auto cycleFn([&] () {
        if (poll_pause_.load() > 0) {
            return;
        }
        unique_lock<mutex> lg(mutex_poll_);
        if (poll_pause_.load() > 0) {   // 락을 얻는 사이 일시정지됐을 수 있다
            return;
        }
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
        } else {
            // 주기가 늘어진 경우(시간 초과·저속 통신)에도 틈을 둬, 명령을 보내려고 mutex_comm_을
            // 기다리는 다른 스레드가 락을 얻을 수 있게 한다
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    motorDisable();
    modbusRelease();
}
