# MAVLink ping utility

Implements [MAVLink ping protocol](https://mavlink.io/en/services/ping.html) for a UDP endpoints.

## Requirements

1. Linux

## Build

```console
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make && sudo make install
```

## Usage

```console
mavlink-ping [-d] [-h] [-c <count>] [-t <timeout>] [-i <interval>] [-l <value>] -I <ip> -p <port> <id> <comp>
Options:
	-d - print debug output,
	-c - number of pings to send,
	-t - ping response timeout,
	-i - interval between pings,
	-I - UDP endpoint target IP,
	-p - UDP endpoint target port,
	-l - lost messages maximum,
	-h - print this help.

	<id> - MAVLink ID,
	<comp> - MAVLink component ID.
```

## Example

`mavlink-ping -I 192.168.20.3 -p 14592 -d 1 1`

Description:

*Ping MAVLink component **MAV_COMP_ID_AUTOPILOT1** (**1**) of the vehicle **1** on the UDP endpoint at 
**192.168.20.3:14592**.*

## Serial endpoints

To ping a serial endpoint you need [MAVLink serial bridge](https://github.com/CopterExpress/mavlink-serial-bridge).
