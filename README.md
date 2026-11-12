# KR_GCS_user_interface

## Overview
- KR_GCS_user_interface is the software that includes a GUI that can run KR-MATC of KORAS Robotics. It is cross-platform software that runs on Windows and Ubuntu, and user can create TCP socket communication server through KR_GCS_user_interface.

---
## Development Environment
- Windows 10
    - Qt 6.6.0
    - MinGW 11.2.0 64-bit
- Ubuntu 22.04 (Jammy Jellyfish)
    - Qt 6.6.0
    - GCC 64-bit

---
## Dependencies
- Boost v1.74
- libmodbus v3.1.10
- Qt
- Feather-icon
- Noto Sans

---
## Compatible Operating Systems
- Windows 10
- Ubuntu 22.04 (Jammy Jellyfish)

---
## Warning
- KR-MATC is a tooling system that changes and uses various grippers such as pneumatic grippers, 2-finger grippers, and 3-finger grippers. Therefore, there are many different precautions from using general grippers, so please read KORAS Robotics' KR-MATC user manual before using this software.
- Homepage: http://korasrobotics.com

---
## Build
- You can also build code with the command below via cmake, but it is recommended to build and run using Qt in most environments.
```shell
$ mkdir build
$ cd build
$ cmake -DCMAKE_BUILD_TYPE=Release ..
$ make
```

---
## Installation
Download and run compatible files on Windows and Ubuntu respectively from the GitHub Release tab.
- Windows 10
    - run exe file
- Ubuntu 22.04
    - run the shell script file

---
## Troubleshooting
- This section lists solutions to a set of possible errors which can happen when using the KR_GCS_user_interface.
#### qt.qpa.plugin error
- If you have seen an error such as **"qt.qpa.plugin: Could not load the Qt platform plugin "xcb" in "~/platforms:" even though it was found."** when you run the released KR_GCS_user_interface on Ubuntu 22.04, you can resolve it in the following way.
```shell
$ sudo apt update
$ sudo apt install pyqt5-dev*
```

---
## TCP Socket communication
- TCP socket server provided by KR_GCS_user_interface transmits status and receives commands through the Json format. The status and command format are as follows.

#### Status from server
- "finger_pos": Finger position of the MATC (0 ~ 1000 ‰ of the stroke (0: closed & 1000: open), also on MATC v3. In mm: finger_pos × gripper_stroke_mm / 1000)
- "motor_cur": Current of the MATC (mA)
- "motor_pos": Position of the motor (deg)
- "motor_vel": Velocity of the motor (rpm)
- "states": Status of the MATC
- "voltage": Voltage of the MATC (V)
- "fault_now": Current STM motor fault code
- "fault_occurred": Latched STM motor fault code
- "gripper_direction": Gripper direction flag
- "object_grasp": Object grasp detection flag
- "grip_hold": Grip hold flag
- "slip_detected": Slip detection flag (always false on MATC v3 — reserved bit)
- "gripper_connected": Gripper physically connected (MATC v3, register 18; always false on older firmware, which has no connection signal)
- "gripper_id", "gripper_stroke_mm", "gripper_force_n", "gripper_payload_x10": Connected gripper ID and specs (MATC v3, registers 20–23; gripper_id 128 = none)
- "gobj_result": Grip result (MATC v3, states bit 12–13): 0 moving, 1 object detected while closing, 2 object detected while opening, 3 at requested position (no object). Always 0 on older firmware
- "grip_force_n": Calculated grip force while gripping (N, MATC v3, register 19)
- "comm_overrun", "eep_save_fail": Latched warnings (MATC v3, states bit 14 / 15; older firmware reports the overrun in states bit 12 and has no eep_save_fail). Cleared by Error Clear (9)
- "last_query": Result of the latest Gripper Map Read (217) or Change Modbus Baud Rate (52), present after the first one. "seq" increases with each new result
    - For 52: "ok", "baudrate" (the baud rate KR_GCS is connected at after the change) and "result": "changed", "unchanged", "reverted" (the MATC kept the previous baud rate), "no_response" (no answer at either baud rate) or "not_sent" (not connected, invalid value, older firmware, or another reconnection in progress)
- "device_model", "fw_version": Connected controller model (10: MATC-10, 20: MATC-20 (GL60), 30: MATC-30 (GL80)) and firmware version (301 = v3.01), from registers 28–29 (firmware v3.01~). 0 if not reported (older firmware)
- "max_speed_rpm", "cur_full_scale_ma": Limits applied to Motor Velocity Control (6) and the 100 % value of Motor Current Control (7) for the connected model

```json
{
    "finger_pos":500,
    "fault_now":0,
    "fault_occurred":0,
    "grip_hold":false,
    "gripper_direction":false,
    "motor_cur":79,
    "motor_pos":-1259,
    "motor_vel":0,
    "object_grasp":false,
    "slip_detected":false,
    "states":5,
    "voltage":24,
    "gripper_connected":true,
    "gripper_id":0,
    "gripper_stroke_mm":100,
    "gripper_force_n":160,
    "gripper_payload_x10":40,
    "gobj_result":3,
    "grip_force_n":0,
    "comm_overrun":false,
    "eep_save_fail":false,
    "device_model":20,
    "fw_version":301,
    "max_speed_rpm":310,
    "cur_full_scale_ma":1350,
    "last_query":{"command":217,"seq":1,"ok":true,"index":6,
                  "stroke_mm":160,"force_n":150,"payload_x10":100}
}
```

**States of MATC**
| Bit   | Status                 | Value
| ----  | ----                   | ----
| 0     | Motor Enable           | 0: False, 1: True
| 1     | Gripper Initialize     | 0: False, 1: True
| 2     | Motor Position Control | 0: False, 1: True
| 3     | Motor Velocity Control | 0: False, 1: True
| 4     | Motor Current Control  | 0: False, 1: True
| 5     | Gripper Open           | 0: False, 1: True
| 6     | Gripper Close          | 0: False, 1: True
| 7     | Gripper Direction      | 0: Normal, 1: Reverse
| 8     | Object Grasp           | 0: False, 1: True
| 9     | Motor Fault            | 0: False, 1: True
| 10    | Grip Hold              | 0: False, 1: True
| 11    | Slip Detected          | 0 (reserved on MATC v3)
| 12-13 | Grip Object Status     | 0: Moving, 1: Object (closing), 2: Object (opening), 3: At target (MATC v3). Older firmware: bit 12 = Communication Overrun
| 14    | Communication Overrun  | 0: False, 1: True (latched until Error Clear)
| 15    | EEPROM Save Fail       | 0: False, 1: True (latched until Error Clear or next successful save)

#### Command to server
- If you want to change modbus address connected to KR_GCS_user_interface, send a Json message as below.
```json
{
    "change_slave": <desired_slave_number>
}
```

- If you want to control MATC, check out the list below.
    - If the "command" does not require "value_1" or "value_2", you do not need to send it.
    - The example below moves the fingers to 50 mm (MATC v3). The unit of each value is in the list below.

```json
{
    "command": 104,
    "value_1": 50,
    "value_2": 0
}
```

**List of "command"**
- Please refer to the KR-MATC user manual for a detailed description of each function.

| Function               | Command | Value 1 (Dec)
| ----                   | ----    | ----
| Motor Enable           | 1       | -
| Motor Stop             | 2       | -
| Motor Disable          | 4       | -
| Motor Position Control | 5       | Target position (deg), Value 2: duration (ms)
| Motor Velocity Control | 6       | Target velocity (rpm)
| Motor Current Control  | 7       | Target Current (mA)
| Error Clear            | 9       | -
| Change Modbus Address  | 50      | Desired address
| Change Modbus Baud Rate| 52      | Baud rate / 100 (96, 192, 384, 576, 1152). KR_GCS reconnects at the new baud rate (result in "last_query")
| Gripper Initialize     | 101     | -
| Gripper Open           | 102     | -
| Gripper Close          | 103     | -
| Set Finger Position    | 104     | 0 ~ stroke (mm, 0: closed; MATC v3). Older firmware: 0 ~ 1000 (‰, 1000: open)
| Vacuum Gripper On      | 106     | -
| Vacuum Gripper Off     | 107     | -
| Impedance On           | 108     | -
| Impedance Off          | 109     | -
| Set Impedance Params   | 110     | Slave number, Value 2: stiffness level
| Set Motor Torque       | 212     | Target grip force (1 ~ 1000 N, MATC v3). Older firmware: 25 ~ 100 (% of rated torque)
| Set Motor Speed        | 213     | Ratio of target motor speed to default speed (1 ~ 100 %)
| Gripper Map Write      | 216     | Index (0~127), Value 2: stroke (mm), Value 3: force (N), "value_4": payload (x0.1 kg)
| Gripper Map Read       | 217     | Index (0~127) — result in "last_query" (index, stroke_mm, force_n, payload_x10)
| Gripper Map Save       | 218     | - (failure is reported by "eep_save_fail")

Commands 52 and 216–218 exist only on MATC v3. While older firmware is connected, KR_GCS does not send them.

After Change Modbus Baud Rate (52), KR_GCS reconnects at the new baud rate. This takes about 2 seconds, and status messages pause in the meantime. Commands received during the reconnection are executed after it. If the MATC does not answer at the new baud rate, KR_GCS returns to the previous one.

#### Communication test using 'telnet'
- Activate TCP socket server using KR_GCS_user_interface
- Run 'telnet' in terminal (Window / Linux)
- If there is no 'telnet' command in Window OS, you can enable in Control Panel.
```shell
$ telnet localhost <socket_port>
```
- Example (command must be inserted into terminal running telnet)
```json
# Gripper initialize command
{"command":101}
# Gripper Open command
{"command":102}
# Gripper Close command
{"command":103}
# Set Finger Position command (50 mm on MATC v3; older firmware: 0 ~ 1000 ‰)
{"command":104,"value_1":50}
```

---
## Contact
E-mail: software@korasrobotics.com

Homepage: http://korasrobotics.com
