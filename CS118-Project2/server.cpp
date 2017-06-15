#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <stdlib.h>
#include <thread>
#include <fstream>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <ctime>
#include <thread>
#include <iostream>
#include <map>

//#define SS_THRESH 10000
#define A_pos 2 
#define S_pos 1 
#define F_pos 0
#define SYN 2
#define ACK 4
#define FIN 1
#define FIN_ACK 5 

fd_set write_fds;
fd_set read_fds;
struct timeval tv;
struct timeval ts_tv;

unsigned int timeout = 10;
unsigned int packet_size = 524;
unsigned int data_size = 512;
unsigned int CWND = 512;
unsigned int ss_thresh = 10000;

static void sig_handler(int signum)
{
  //printf("received signal\n");
  exit(0);
}

struct header
{
   uint32_t seq;
   uint32_t ack;
   uint16_t id;
   uint16_t flags;
};

typedef struct header header;

struct packet
{
  header pack_header;
  char data[512];
};

typedef struct packet packet;

struct connection_info
{
  packet pack;
  struct sockaddr src_addr;
  socklen_t addrlen;
  std::ofstream myfile;
  int is_FIN;
  clock_t timestamp;
};

typedef struct connection_info connection_info;

struct stat info;
//data structure: array of 10 to keep track of id 
connection_info all_connection[10];
/*A map to indicate the timestamp of each connection ID*/
std::map<int, time_t> last_timestamp;  
/*create a thread function along side with big while(1) to constantly check the timeout*/

bool is_timeout(clock_t timestamp, int connection_id)
{
  //if(connection_id != 0)
    //std::cout<<connection_id <<" time difference is " << (clock() - timestamp)/CLOCKS_PER_SEC <<std::endl;
  if((clock() - timestamp)/CLOCKS_PER_SEC > 9 && connection_id != 0)
  {
    return true;
  }
  return false;
}

void print_packet(std::string message, packet buf)
{
  std::string flag;
  switch(buf.pack_header.flags)
  {
    case 0:
      flag = " ";
    break;
    case 1:
      flag = "FIN";
    break;
    case 2:
      flag = "SYN";
    break;
    case 4:
      flag = "ACK";
    break;
    case 6:
      flag = "SYN ACK";
    break;
    case 5:
      flag = "FIN ACK";
    break;
  }
  if(message == "RECV")
  {
      std::cout << message << " " <<
       buf.pack_header.seq << " " <<
        buf.pack_header.ack << " " <<
         buf.pack_header.id << " " <<
         "512" << " " <<
         "10000" << " " <<
         flag << std::endl;
    }
    else if(message == "DROP")
    {
      std::cout << message << " " <<
       buf.pack_header.seq << " " <<
        buf.pack_header.ack << " " <<
         buf.pack_header.id << " " <<
         "512" << " " <<
         "10000" << " " <<
         flag << std::endl;
       }
    else if(message == "SEND")
    {
      std::cout << message << " " <<
       buf.pack_header.seq << " " <<
        buf.pack_header.ack << " " <<
         buf.pack_header.id << " " <<
         "512" << " " <<
         "10000" << " " <<
         flag << std::endl;
       }
}

int main(int argc, char *argv[])
{
  signal(SIGQUIT,sig_handler);
  signal(SIGTERM,sig_handler);
  signal(SIGINT,sig_handler);

  // create a socket using TCP IP
  std::string port_num, file_dir;
  //struct sigaction sa;
  
  port_num = argv[1];
  file_dir = argv[2];

  if(std::stoi(port_num) <= 1023 && std::stoi(port_num) >= 0)
    {
      perror("ERROR: incorrect port number");
      exit(1);
    }
  
  if(stat(file_dir.c_str(), &info) != 0)
  {
    std::cout << "ERROR:directory not exist" << std::endl;
    return 1;
  }
  else if(S_ISDIR(info.st_mode))
  {
    //std::cout << "directory exist" << std::endl;
  }
  else
  {
    std::cout << "ERROR:not a directory" << std::endl;
    return 1;
  }


  struct addrinfo hints, *server_info, *anchor;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE;

  if(getaddrinfo(NULL, port_num.c_str(), &hints, &server_info) != 0)
    {
      fprintf(stderr, "ERROR:getaddrinfo error");
      exit(1);
    }


  int sockfd;
  int yes = 1;
  for(anchor = server_info; anchor != NULL; anchor = anchor -> ai_next)
    {
      sockfd = socket(anchor -> ai_family,anchor -> ai_socktype, anchor-> ai_protocol);
      //fcntl(sockfd, F_SETFL, O_NONBLOCK);
      if(sockfd < 0)
      {
        continue;
      }

      setsockopt(sockfd, SOL_SOCKET,SO_REUSEADDR, &yes, sizeof(int));
      if(bind(sockfd, anchor -> ai_addr, anchor -> ai_addrlen) < 0)
      {
        close(sockfd);
        continue;
      }
      break;
    }

  if(anchor == NULL)
    {
      fprintf(stderr,"ERROR:bind fail");
      exit(2);
    }

  freeaddrinfo(server_info);

  struct sockaddr client_addr;
  socklen_t client_addrlen = 16;
  packet buf;


  // printf("before while\n");
  //std::thread timeout_thread(timeout_check, file_dir);
  while(1)
  {
    memset(&buf,0,sizeof(buf));
    int recv_byte = recvfrom(sockfd, &buf, sizeof(buf), 0, &client_addr, &client_addrlen);
    buf.pack_header.seq = ntohl(buf.pack_header.seq);
    buf.pack_header.id = ntohs(buf.pack_header.id);
    buf.pack_header.ack = ntohl(buf.pack_header.ack);
    buf.pack_header.flags = ntohs(buf.pack_header.flags);
    // for(int i = 0; i < 10; i++)
    // {
    //   if(is_timeout(all_connection[i].timestamp, all_connection[i].pack.pack_header.id))
    //   {
    //     std::cout << "ERROR: TIMEOUT" << std::endl;
    //     std::string file_path = file_dir + "/" + std::to_string(i + 1) + ".file";
    //     all_connection[i].myfile.close();
    //     all_connection[i].myfile.open(file_path,std::ofstream::trunc);
    //     all_connection[i].myfile.write("ERROR", 5);
    //     all_connection[i].myfile.close();
    //     all_connection[i].pack.pack_header.id = 0;
    //   }
    //   else if(all_connection[i].pack.pack_header.id == buf.pack_header.id && buf.pack_header.id != 0)
    //   {
    //     all_connection[i].timestamp = clock();
    //   }
    // }


    // if(recv_byte > 0)
    // {
    print_packet("RECV", buf);

    //SYN received
    if(buf.pack_header.flags == SYN)
    {    

      for(int i = 0; i < 10; i++)
      {
        if(all_connection[i].src_addr.sa_family != client_addr.sa_family)
        {
        if(all_connection[i].pack.pack_header.id == 0)
        {
          // printf("connection %d is available \n", i);
           std::string file_path = file_dir + "/" + std::to_string(i + 1) + ".file";
          // std::cout << " file path is " << file_path << std::endl;
          all_connection[i].pack.pack_header.flags = 6;
          all_connection[i].pack.pack_header.ack = buf.pack_header.seq + 1;
          all_connection[i].pack.pack_header.seq = 4321;
          all_connection[i].pack.pack_header.id = i + 1;
          all_connection[i].src_addr = client_addr;
          // std::cout <<" client addr is " << sizeof(client_addr << std::endl;
          all_connection[i].addrlen = client_addrlen;
          all_connection[i].myfile.open(file_path);
	  //For newly established connection, insert the timestamp
	  // time_t cur_time;
	  // time(&cur_time);
	  // last_timestamp.insert(std:: pair<int, time_t>(all_connection[i].pack.pack_header.id, cur_time));
	  

          buf.pack_header.seq = all_connection[i].pack.pack_header.seq;
          buf.pack_header.ack = all_connection[i].pack.pack_header.ack;
          buf.pack_header.id = all_connection[i].pack.pack_header.id;
          buf.pack_header.flags = all_connection[i].pack.pack_header.flags;

          buf.pack_header.seq = htonl(buf.pack_header.seq);
          buf.pack_header.ack = htonl(buf.pack_header.ack);
          buf.pack_header.id = htons(buf.pack_header.id);
          buf.pack_header.flags = htons(buf.pack_header.flags);

	  // sendto(sockfd,&all_connection[i].pack, 
			//    sizeof(all_connection[i].pack), 0, 
			//    &all_connection[i].src_addr,
			//    all_connection[i].addrlen);

        sendto(sockfd,&buf, 
         sizeof(all_connection[i].pack), 0, 
         &all_connection[i].src_addr,
         all_connection[i].addrlen);

    print_packet("SEND", all_connection[i].pack);
	  // std::cout<<chk<<std::endl;
		
          break;
        }
      }
      else
      {
        sendto(sockfd,&buf, 
         sizeof(all_connection[i].pack), 0, 
         &all_connection[i].src_addr,
         all_connection[i].addrlen);
      }
      }
    }
    //ACK = 1, SYN = 0, FIN = 0
    //Normal packets received
    else if(buf.pack_header.flags == ACK || buf.pack_header.flags == 0)
    {
      //receive some packet with id, go through all_connection[i] to find corresponding id,
      //compare seq with the ack in all_connection, if seq == ack, packet is in order
      //send packet back with id, ack, and seq
      //printf("receive data packet\n");
      for(int i = 0; i < 10; i++)
      {
        if(buf.pack_header.id == all_connection[i].pack.pack_header.id)//find the corresponding connection
        {
          //printf("found corresponding conneciton \n");
          if(all_connection[i].is_FIN == 1)//server sent a FIN
            {
              // all_connection[i].pack.pack_header.id = 0;
              all_connection[i].myfile.close();
              break;
            }
          if(buf.pack_header.seq == all_connection[i].pack.pack_header.ack)//packet is in order
          {
            //printf("correct packet\n");
            all_connection[i].pack.pack_header.ack += recv_byte-12;
            all_connection[i].pack.pack_header.ack %= 102401;

            if(buf.pack_header.ack == all_connection[i].pack.pack_header.seq)//packet to client is lost, resend
            {

            }
            else if(buf.pack_header.ack == all_connection[i].pack.pack_header.seq + 1)//packet sent to client is in order
            {
              //printf("packet is in order\n");
              all_connection[i].pack.pack_header.seq++;
            }
            all_connection[i].myfile.write(buf.data, recv_byte - 12);
            all_connection[i].myfile.flush();
          }
          else//packet is out of order, request for the same thing as go back n, drop
          {
            print_packet("DROP", buf);
            if(buf.pack_header.ack == all_connection[i].pack.pack_header.seq)//packet to client is lost, resend
            {

            }
            else if(buf.pack_header.ack == all_connection[i].pack.pack_header.seq + 1)//packet sent to client is in order
            {
              all_connection[i].pack.pack_header.seq++;
            }
            //all_connection[i].pack.pack_header.seq = buf.pack_header.ack;
          }
          all_connection[i].pack.pack_header.flags = 4;

              buf.pack_header.seq = all_connection[i].pack.pack_header.seq;
              buf.pack_header.ack = all_connection[i].pack.pack_header.ack;
              buf.pack_header.id = all_connection[i].pack.pack_header.id;
              buf.pack_header.flags = all_connection[i].pack.pack_header.flags;

              buf.pack_header.seq = htonl(buf.pack_header.seq);
              buf.pack_header.ack = htonl(buf.pack_header.ack);
              buf.pack_header.id = htons(buf.pack_header.id);
              buf.pack_header.flags = htons(buf.pack_header.flags);

          // sendto(sockfd,&all_connection[i].pack, 
          //   sizeof(all_connection[i].pack), 0, 
          //   &all_connection[i].src_addr,
          //   all_connection[i].addrlen);

    sendto(sockfd,&buf, 
         sizeof(all_connection[i].pack), 0, 
         &all_connection[i].src_addr,
         all_connection[i].addrlen);
          print_packet("SEND", all_connection[i].pack);

          break;
        }
      }
    }
    else if(buf.pack_header.flags == FIN)//receive FIN
    {
      //std::cout << " receive FIN \n" << std::endl;
      for(int i = 0; i < 10; i++)
      {
        if(buf.pack_header.id == all_connection[i].pack.pack_header.id)//find the corresponding connection
        {
          //std::cout << " find the corresponding connection " << buf.pack_header.seq << " " << all_connection[i].pack.pack_header.ack << std::endl;
          if(buf.pack_header.seq == all_connection[i].pack.pack_header.ack)//packet is in order
          {
            //std::cout << " packet is in order " << std::endl;
            all_connection[i].pack.pack_header.ack++;

          }
          else//packet is out of order, request for the same thing as go back n, drop
          {
           // all_connection[i].pack.pack_header.seq = buf.pack_header.ack;
          }
          all_connection[i].pack.pack_header.flags = 4;

          buf.pack_header.seq = all_connection[i].pack.pack_header.seq;
          buf.pack_header.ack = all_connection[i].pack.pack_header.ack;
          buf.pack_header.id = all_connection[i].pack.pack_header.id;
          buf.pack_header.flags = all_connection[i].pack.pack_header.flags;

          buf.pack_header.seq = htonl(buf.pack_header.seq);
          buf.pack_header.ack = htonl(buf.pack_header.ack);
          buf.pack_header.id = htons(buf.pack_header.id);
          buf.pack_header.flags = htons(buf.pack_header.flags);

          // sendto(sockfd,&all_connection[i].pack, 
          //   sizeof(all_connection[i].pack), 0, 
          //   &all_connection[i].src_addr,
          //   all_connection[i].addrlen);

    sendto(sockfd,&buf, 
         sizeof(all_connection[i].pack), 0, 
         &all_connection[i].src_addr,
         all_connection[i].addrlen);


          print_packet("SEND", all_connection[i].pack);
          // buf.pack_header.seq = htons(all_connection[i].pack.pack_header.seq);
          // buf.pack_header.ack = 0;
          // buf.pack_header.flags = 1;


          buf.pack_header.seq = all_connection[i].pack.pack_header.seq;
          buf.pack_header.ack = 0;
          buf.pack_header.id = all_connection[i].pack.pack_header.id;
          buf.pack_header.flags = 1;

          buf.pack_header.seq = htonl(buf.pack_header.seq);
          buf.pack_header.ack = htonl(buf.pack_header.ack);
          buf.pack_header.id = htons(buf.pack_header.id);
          buf.pack_header.flags = htons(buf.pack_header.flags);


    //       // sendto(sockfd,&all_connection[i].pack, 
    //       //   sizeof(all_connection[i].pack), 0, 
    //       //   &all_connection[i].src_addr,
    //       //   all_connection[i].addrlen);

    sendto(sockfd,&buf, 
         sizeof(all_connection[i].pack), 0, 
         &all_connection[i].src_addr,
         all_connection[i].addrlen);

          print_packet("SEND", buf);
          all_connection[i].is_FIN = 1;
          break;
        }
      }
    }
    }
  // }
}

//seg fault caused bby inappropriate use of struct sockaddr with pointers in struct
