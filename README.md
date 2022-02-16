# SocketCAN-Applications
As part of a project work with the topic 'CAN-Bus and Automotive Security', applications for replay and denial of service attacks and CAN fuzzing were created based on Linux SocketCAN.

## Compilation
```
gcc <can_app>.c lib.c -o <can_app>
```

## Usage
### canreplay
Replays CAN messages
```
./canreplay <options>

-p <seconds>    Capturing period (Default 60 seconds)
-d <seconds>    Delay before replaying messages
-i <hex value>  Filter messages by hexadecimal Arbitration ID
-t              Replay with exact time differences between messages
-g <seconds>    Define gap time between sending messages (Default 0.01s)
-f <FILE>       Use logfile input for replaying messages
-l              Keep temporarily created logfile
-n <ifname>     Set the interface (Default 'vcan0')
```

### canfuzzer
Fuzzing CAN messages
```
./canfuzzer <options>

-i <hex value>  Arbitration ID with which messages are sent
-g <seconds>    Gap time between sending messages (Default 0.01s)
-b <int>        Number of data bytes (Default 8)
-m <r, s or i>  Mode for data generation: Random/Sweep/Increment (Default Random)
-l              Save sent messages to logfile
-n <ifname>     Set the interface (Default 'vcan0')
```

### candos
Inject the highest priority CAN message of ID 0
```
./candos <option>
 -n <ifname>     Set the interface (Default 'vcan0')
```
