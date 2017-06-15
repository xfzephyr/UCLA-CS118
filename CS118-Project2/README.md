# CS118 Project 2

Template for for [CS118 Spring 2017 Project 2](http://web.cs.ucla.edu/classes/spring17/cs118/project-2.html) 

## Makefile

This provides a couple make targets for things.
By default (all target), it makes the `server` and `client` executables.

It provides a `clean` target, and `tarball` target to create the submission file as well.

You will need to modify the `Makefile` to add your userid for the `.tar.gz` turn-in at the top of the file.

## Provided Files

`server.cpp` and `client.cpp` are the entry points for the server and client part of the project.

## Academic Integrity Note

You are encouraged to host your code in private repositories on [GitHub](https://github.com/), [GitLab](https://gitlab.com), or other places.  At the same time, you are PROHIBITED to make your code for the class project public during the class or any time after the class.  If you do so, you will be violating academic honestly policy that you have signed, as well as the student code of conduct and be subject to serious sanctions.

## Wireshark dissector

For debugging purposes, you can use the wireshark dissector from `tcp.lua`. The dissector requires
at least version 1.12.6 of Wireshark with LUA support enabled.

To enable the dissector for Wireshark session, use `-X` command line option, specifying the full
path to the `tcp.lua` script:

    wireshark -X lua_script:./confundo.lua

## TODO
Team members:
Zhuoqi Li(504135743): Client Implementation and debugging
Haoran Zhang(804586710): Server Implementation and debugging
Tianhao Zhao(404419775): Server Implementation and debugging

Server Design:
The server consists three parts:
1. Three-way-handshaking(Handle SYN flag)
2. Packet receiving (Handle ACK flag)
3. Finish the connection(Handle FIN flag)

Client Design:
The client uses sliding window method to sending the packet
The client consists three parts:
1. Three-way-handshaking (Establish connection)
2. Data transmission using sliding window
3. Finish the connection

Problem we faced:
1. At server side, we have some issue with the timeout measurement. At first we use thread to make a timer but it consumes lots of CPUs. (almost 100%).
Later we've decided to include a timestamp in the connection_info class and check the timeout at the beginning of the while(1) loop.

2.

Additional Libraries:
<fstream>
<sys/stat.h>
<fcntl.h>
<signal.h>
<thread>
<chrono>
<ctime>
<vector>
