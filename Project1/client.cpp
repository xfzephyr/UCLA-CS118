#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/fcntl.h>
#include <sys/unistd.h>
#include <sys/time.h>
#include <iostream>

extern int errno;
int main(int argc, char* argv[])
{
  //sanity check
  if(argc != 4)
    {
      fprintf(stderr,"ERROR:Invalid number of arguments");
      exit(EXIT_FAILURE);
    }
  
  const char* ip_addr = argv[1];
  const char* port = argv[2];
  const char* filename = argv[3];
  //use getaddrinfo to validate the ip and port number
  int status;
  struct addrinfo hints;
  struct addrinfo *servinfo;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;//ipv4
  hints.ai_socktype = SOCK_STREAM; //TCP stream sockets
  status = getaddrinfo(ip_addr, port, &hints, &servinfo);
  if (status != 0)
    {
      fprintf(stderr, "ERROR: %s\n", gai_strerror(status));
      exit(EXIT_FAILURE);
    }

  // create a socket using TCP IP  
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if(sockfd == 0)
    {
      fprintf(stderr,"ERROR: Socket failed\n");
      exit(EXIT_FAILURE);
    }

  //set the socket to non-blocking
  if (fcntl(sockfd, F_SETFL, O_NONBLOCK))
    {
      fprintf(stderr, "ERROR: set nonblocking error\n");
      exit(EXIT_FAILURE);
    }

  //use select to test the timeout condition
  fd_set active_fd_set;
  struct timeval timeout;
  FD_ZERO(&active_fd_set);
  FD_SET(sockfd, &active_fd_set);
  timeout.tv_sec = 10; //set the timeout to 10s
  timeout.tv_usec = 0;
  
  // connect to the server
  int r = connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen); 
  if ( r < 0)
    {
      //test the timeout
      if(errno == EINPROGRESS)
	{
	  int rs = select(sockfd+1, NULL, &active_fd_set, NULL, &timeout);
	  if(rs < 0)
	    {
	      fprintf(stderr, "ERROR: Select failed\n");
	      exit(EXIT_FAILURE);
	    }
	  else if( rs == 0)
	    {
	      fprintf(stderr, "ERROR: Connection timeout\n");
	      exit(EXIT_FAILURE);
	    }
	  else
	    {
	      printf("Connection success.\n");	      
	    }
	}
      else
	{
	  fprintf(stderr,"ERROR: connection failed\n");
	  exit(EXIT_FAILURE);
	}
    }
  else
    {
      fprintf(stderr,"ERROR: Wrong with connect function\n");
      exit(EXIT_FAILURE);
    }
  //get the connection information
  //sleep(15);
  struct sockaddr_in clientAddr;
  socklen_t clientAddrLen = sizeof(clientAddr);
  if (getsockname(sockfd, (struct sockaddr *)&clientAddr, &clientAddrLen) == -1)
    {
      fprintf(stderr,"ERROR: getsockname failed\n");
      exit(EXIT_FAILURE);
    }
  
  char ipstr[INET_ADDRSTRLEN] = {'\0'};
  inet_ntop(clientAddr.sin_family, &clientAddr.sin_addr, ipstr, sizeof(ipstr));
  std::cout << "Set up a connection from: " << ipstr << ":" <<
    ntohs(clientAddr.sin_port) << std::endl;

  FILE* fp= fopen(filename,"rb");
  if(fp == NULL)
    {
      fprintf(stderr, "ERROR: Can't open the file\n");
      exit(EXIT_FAILURE);
    }
  //int counter = 1;
  while(1)
    {
      //counter++;
      int rs = select(sockfd+1, NULL, &active_fd_set, NULL, &timeout);
      if(rs < 0)
	{
	  fprintf(stderr, "ERROR: Select failed\n");
	  exit(EXIT_FAILURE);
	}
      else if( rs == 0)
	{
	  fprintf(stderr, "ERROR: Connection timeout\n");
	  exit(EXIT_FAILURE);
	}
      /* send/receive data to/from connection */
      //if(counter == 5)
      //sleep(15);
      char buf[1024];
      int buf_size = 1024;
      //read the file and send
      //while(1)
      memset(buf, '\0', buf_size);
      int read_byte = fread(buf,sizeof(char), buf_size, fp);
      if(read_byte == 0)
	{
	  fprintf(stdout, "No bytes are read\n");
	  fclose(fp);
	  break;
	} 
      else if (read_byte < 0)
	{
	  fprintf(stderr, "ERROR: Read file error\n");
	  exit(EXIT_FAILURE);
	}
      else
	{
	  int byte_sent = send(sockfd, buf, read_byte, 0);
	  fprintf(stdout, " %d bytes sent.\n", byte_sent);
	  if(byte_sent < 0)
	    {
	      fprintf(stderr, "ERROR: Error on sending files\n");
	      exit(EXIT_FAILURE);
	    }
	}
    } 
  close(sockfd);
  freeaddrinfo(servinfo);
  printf("Program Exited successfully\n");
  exit(0);
}

