# CS118 Project 1

Template for for [UCLA CS118 Spring 2017 Project 1](http://web.cs.ucla.edu/classes/spring17/cs118/project-1.html)

## Makefile

This provides a couple make targets for things.
By default (all target), it makes the `server` and `client` executables.

It provides a `clean` target, and `tarball` target to create the submission file as well.

You will need to modify the `Makefile` to add your userid for the `.tar.gz` turn-in at the top of the file.

## Academic Integrity Note

You are encouraged to host your code in private repositories on [GitHub](https://github.com/), [GitLab](https://gitlab.com), or other places.  At the same time, you are PROHIBITED to make your code for the class project public during the class or any time after the class.  If you do so, you will be violating academic honestly policy that you have signed, as well as the student code of conduct and be subject to serious sanctions.

## Provided Files

`server.cpp` and `client.cpp` are the entry points for the server and client part of the project.

## Personal information
UID: 404419775
Name: Tianhao Zhao

## Overall Design:
Client:
The client first creates the socket and then connect to the server. To test the
timeout of connection, the socket is set to the nonblocking mode. After passing
socket to the connect(), if the errno is EINPROGRESS, the function will proceed
and select() is used to test timeout.
After the connection, I create a while loop to keep reading files into 1024 byte
buffer and send to the server. To measure timeout, I use select before the send() function.
If there is no data read from file, the client terminates

Server:
The server accepts multiple connections from different clients. In this submission, we use multithread method to handle each clients.
After listen all the connection, we create a while loop, create clientsocket through accpet. Once we get the clientsocket, the server create a thread and reading the file from client.
For each thread, we use select() to test the timeout of clientsocket in a while loop. If there is no timeout, the program will create the file and write the contents to it.

## Problem encountered
1. Server doesn't correctly recieved the file. When the diff command applied, the received file gives "No new line" information. The bug was fixed by set the write_bytes argument to the actual bytes received, not the buffer size.

2. Timeout
   To figure out how to test timeout, I spent tons of time reading the online document and chose select method.

3. write file bugs
   I use file pointer to handle file I/O. There is a bug for writing file after timeout. It hangs there and doesn't write anything after timeout. The bug was fixed by adding fflush(fp) before the fwrite. This often happens when fp is reused.


##List of Additional Libraries
 <errno.h> <fcntl.h> <signal.h> <pthread.h>


## References
http://stackoverflow.com/questions/21405204/multithread-server-client-implementation-in-c

