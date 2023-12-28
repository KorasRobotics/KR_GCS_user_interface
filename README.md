# KR_GCS_user_interface

## Overview
- KR_GCS_user_interface is the software that includes a GUI that can run KR-DATC of KORAS Robotics. It is cross-platform software that runs on Windows and Ubuntu, and user can create TCP socket communication server through KR_GCS_user_interface.

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
- Boost
- libmodbus
- Qt
- Feather-icon
- Noto Sans

---
## Compatible Operating Systems
- Windows 10
- Ubuntu 22.04 (Jammy Jellyfish)

---
## Warning
- KR-DATC is a tooling system that changes and uses various grippers such as pneumatic grippers, 2-finger grippers, and 3-finger grippers. Therefore, there are many different precautions from using general grippers, so please read KORAS Robotics' DATC manual before using this software.
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
- "finger_pos": Finger position of the DATC (0 ~ 1000 (0: closed & 1000: open))
- "motor_cur": Current of the DATC (mA)
- "motor_pos": Position of the motor (deg)
- "motor_vel": Velocity of the motor (rpm)
- "states": Status of the DATC
- "voltage": Voltage of the DATC (V)

```json
{
    "finger_pos":500,
    "motor_cur":79,
    "motor_pos":-1259,
    "motor_vel":0,
    "states":5,
    "voltage":24
}
```

**States of DATC**
| Bit   | Status                 | Value
| ----  | ----                   | ----
| 0     | Motor Enable           | 0: False, 1: True
| 1     | Gripper Initialize     | 0: False, 1: True
| 2     | Motor Position Control | 0: False, 1: True
| 3     | Motor Velocity Control | 0: False, 1: True
| 4     | Motor Current Control  | 0: False, 1: True
| 5     | Gripper Open           | 0: False, 1: True
| 6     | Gripper Close          | 0: False, 1: True
| 7     | -                      | -
| 8     | -                      | -
| 9     | Motor Fault            | 0: False, 1: True
| 10-15 | -                      | -

#### Command to server
- If you want to change modbus address connected to KR_GCS_user_interface, send a Json message as below.
```json
{
    "change_slave": <desired_slave_number>
}
```

- If you want to control DATC, check out the list below.
    - If the "command" does not require "value_1" or "value_2", you do not need to send it.

```json
{
    "command": 104,
    "value_1": 500,
    "value_2": 0,
}
```

**List of "command"**
- Please refer to the DATC manual for a detailed description of each function.

| Function               | Command | Value 1 (Dec)
| ----                   | ----    | ----
| Motor Enable           | 1       | -
| Motor Stop             | 2       | -
| Motor Disable          | 4       | -
| Motor Velocity Control | 6       | Target velocity (rpm)
| Motor Current Control  | 7       | Target Current (mA)
| Change Modbus Address  | 50      | Desired address
| Gripper Initialize     | 101     | -
| Gripper Open           | 102     | -
| Gripper Close          | 103     | -
| Set Finger Position    | 104     | 0 ~ 1000 (0: closed & 1000: open)
| Vacuum Gripper On      | 106     | -
| Vacuum Gripper Off     | 107     | -
| Set Motor Torque       | 212     | Ratio of target motor torque to default torque (%)
| Set Motor Speed        | 213     | Ratio of target motor speed to default speed (%)

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
# Set Finger Position command
{"command":104,"value_1":500}
```

---
## Contact
E-mail: koras@korasrobotics.com

Homepage: http://korasrobotics.com