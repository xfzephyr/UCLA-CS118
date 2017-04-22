#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <iostream>

//global parameters
int connection_num = 0;
const char* target_dir;
//function to write to the file
void* connection_handler(void* param);
void signal_handler(int signo);

int main(int argc, char* argv[])
{
  //sanity check
  if(argc != 3)
    {
      fprintf(stderr,"ERROR:Invalid number of arguments\n");
      exit(EXIT_FAILURE);
    }
  const char* port = argv[1];
  target_dir = argv[2];

  //signal handle
  signal(SIGQUIT, signal_handler);
  signal(SIGTERM, signal_handler);
  
  //use getaddrinfo to validate the ip
  int status;
  struct addrinfo hints;
  struct addrinfo *servinfo;  // will point to the results
  memset(&hints, 0, sizeof hints); // make sure the struct is empty
  hints.ai_family = AF_INET;     // don't care IPv4 or IPv6
  hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
  hints.ai_flags = AI_PASSIVE;     // fill in my IP for me
  if ((status = getaddrinfo(NULL, port , &hints, &servinfo)) != 0)
    {
      fprintf(stderr, "ERROR:%s", gai_strerror(status));
      exit(EXIT_FAILURE);
    }
  
  // create a socket using TCP IP
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
 if(sockfd == 0)
    {
      fprintf(stderr, "ERROR: Socket failed\n");
      exit(EXIT_FAILURE);
    }

  // allow others to reuse the address
  // Also allow multiple connections
  int yes = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    {
      fprintf(stderr,"ERROR: setsockopt failed\n");
      exit(EXIT_FAILURE);
    }

  // bind address to socket
  if (bind(sockfd, servinfo->ai_addr, servinfo -> ai_addrlen) == -1)
    {
      fprintf(stderr, "ERROR: bind failed\n");
      exit(EXIT_FAILURE);
    }

  // set socket to listen status
  if (listen(sockfd, 10) == -1)
    {
      fprintf(stderr, "ERROR: listen failed\n");
      exit(EXIT_FAILURE);
    }

  // accept a new connection
  while(1)
    {
      /*int rs = select(sockfd+1, &active_fd_set, NULL, NULL, &timeout);
      if(rs < 0)
	{
	  fprintf(stderr, "Error: Select failed\n");
	  exit(EXIT_FAILURE);
	}
      else if( rs == 0)
	{
	  fprintf(stderr, "Error:Accept timeout\n");
	  exit(EXIT_FAILURE);
	  }*/

      struct sockaddr_in clientAddr;
      socklen_t clientAddrSize = sizeof(clientAddr);
      int clientSockfd = accept(sockfd, (struct sockaddr*)&clientAddr, &clientAddrSize);
      if (clientSockfd == -1)
	{
	      fprintf(stderr, "accept failed\n");
	      exit(EXIT_FAILURE);   
	}
      else //connection accepted
	{
	  connection_num++;//increment connection number
	  char ipstr[INET_ADDRSTRLEN] = {'\0'};
	  inet_ntop(clientAddr.sin_family, &clientAddr.sin_addr, ipstr, sizeof(ipstr));
	  std::cout << "Accept a connection from: " << ipstr << ":" <<
	    ntohs(clientAddr.sin_port) << std::endl;
	  
	  pthread_t thread_id;
	  if(pthread_create(&thread_id, NULL, connection_handler, (void*)&clientSockfd))
	    {
	      fprintf(stderr, "pthread error\n");
	      exit(EXIT_FAILURE);
	    }
	  
	  pthread_detach(thread_id);
	}	
    }
  freeaddrinfo(servinfo);
  exit(0);
}

void signal_handler(int signo)
{
  if (signo == SIGQUIT || signo == SIGTERM)
    exit(0);
}

void* connection_handler(void* param)
{
  char filepath[1000];
  sprintf(filepath,"%s/%d.file", target_dir, connection_num);
  printf("Enters connection handler\n");
  int clientsockfd = *(int*)param;
  if (fcntl(clientsockfd, F_SETFL, O_NONBLOCK))
    {
      fprintf(stderr, "ERROR: set nonblocking error\n");
      exit(EXIT_FAILURE);
      }
  fd_set active_fd_set;
  FD_ZERO(&active_fd_set);
  FD_SET(clientsockfd, &active_fd_set);
  struct timeval timeout;
  timeout.tv_sec = 10; //set the timeout to 10s
  timeout.tv_usec = 0;
  FILE* fp = fopen(filepath, "wb");
  printf("File %s created\n", filepath);
  if(fp == NULL)
    {
      fprintf(stderr, "ERROR: create file failed\n");
      exit(EXIT_FAILURE);
    }
  while(1)
    {
      //check the timeout from the client
      int rs = select(clientsockfd+1, &active_fd_set, NULL, NULL, &timeout);
	if(rs < 0)
	  {
	    fprintf(stderr, "ERROR: Select failed\n");
	    exit(EXIT_FAILURE);
	  }
	else if( rs == 0)
	  {
	    close(clientsockfd);
	    fprintf(stderr, "ERROR: Connection timeout\n");
	    fclose(fp);
	    fp= fopen(filepath, "wb");
	    printf("Reopen\n");
	    if(fp == NULL)
	      {
		fprintf(stderr, "ERROR: file reopen error\n");
		exit(EXIT_FAILURE);
	      }
	    char msg[20] = "ERROR";
	    fflush(fp);
	    int written = fwrite(msg, sizeof(char), 6, fp);
	    printf("%d written\n", written);
	    if( written < 0)
	      {
		fprintf(stderr, "ERROR: Write error failed\n");
		exit(EXIT_FAILURE);
	      }
	    fclose(fp);
	    return 0;
	  }
	//else//ready to write to the file
	
	//create the file	
	//recv the data and write to the file
	char buf[1024];
	int buf_size = 1024;
	memset(buf, '\0', buf_size);
	int byte_received = recv(clientsockfd, buf, buf_size, 0);
	printf("%d bytes received\n", byte_received);
	if(byte_received < 0)
	  {
	    fprintf(stderr, "ERROR: Receive data failed\n");
	    exit(EXIT_FAILURE);
	  }
	else if(byte_received == 0)
	  {
	    
	    printf("Nothing recieved\n");
	    fclose(fp);
	    break;
	  }
	else
	  {
	    printf("Received %d bytes\n", byte_received);
	    if(fwrite(buf, sizeof(char), byte_received, fp) ==  0)
	      {
		fprintf(stderr, "ERROR: write failed\n");
		exit(EXIT_FAILURE);
	      }
	  }
	    	
    }
  return 0; 
  
}
