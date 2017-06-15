#include <iostream>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
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
#include <thread>
#include <chrono>
#include <ctime>
#include <vector>

#define max_file_size 100*1024*1024
#define packet_size 524
#define data_size 512
#define MSS 512
#define MAX_retran 20
#define SYN_ACK 6
#define ACK_FIN 5
#define SYN 2
#define ACK 4
#define FIN 1

fd_set write_fds;

unsigned int SS_THRESH  = 10000;
unsigned int CWND = 512;
unsigned int last_seq = 0;
int max_seq_num = 102400;
int timer_flag = 0;

unsigned int seq_num = 12345;
unsigned int ack_num = 0;
unsigned int id_num = 0;
    
struct logger
{
    unsigned int E_ACK;
    unsigned int seq;
    std::chrono::steady_clock::time_point send_time;
    std::streampos file_pos; 
};

typedef struct logger logger;

std::vector<logger> CWND_logs; //vector for packet logs

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
    char data[data_size];
};

typedef struct packet packet;

void set_header(packet &pack, uint32_t S, uint32_t A, uint16_t I, uint16_t F){
    pack.pack_header.seq = htonl(S);
    pack.pack_header.ack = htonl(A);
    pack.pack_header.id = htons(I);
    pack.pack_header.flags = htons(F);
}

// function list
void ntoh_reorder(packet &pack); //network to host header byte reorder
void timer(int ms); //multithread 0.5 sec timer used in SYN and ENDING phase 
void standard_output(char format, uint32_t S, uint32_t A, uint16_t ID, int F, unsigned int window, unsigned int thresh); //output message format
void slidingWindow_data_trans(int sockfd, struct addrinfo* anchor, std::string file_name); //send packet in sliding window fashion
void handshake(int sockfd, struct addrinfo *anchor); //two way hadnshake
void ending(int sockfd, struct addrinfo *anchor); //client shut down
void sigpipe_handler(int s); //sigpipe signal handle, used to check network condition
int recv_pack(int sockfd, packet &recv_pack, struct addrinfo *anchor, uint32_t ack); //data receiving in stop and wait packet transfer, handshake and ending functions

// network to host byte order transfer
void ntoh_reorder(packet &pack){
    pack.pack_header.ack = ntohl(pack.pack_header.ack);
    pack.pack_header.seq = ntohl(pack.pack_header.seq);
    pack.pack_header.id = ntohs(pack.pack_header.id);
    pack.pack_header.flags = ntohs(pack.pack_header.flags);
}

// function implementation
void timer(int ms){
    //timeout for 0.5 second
    //timer evaluate the flag every 1 ms
    //if change due to ack received, timer terminates
    std::chrono::steady_clock::time_point time_out = std::chrono::steady_clock::now();
    
    while(timer_flag != 1)
    {
       if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - time_out).count() >= ms)
       {
           timer_flag = 1;
           break;
       }   
    }
}
void standard_output(char format, uint32_t S, uint32_t A, uint16_t ID, int F, unsigned int window, unsigned int thresh){
    std::string FLAG = ""; 
  
    if (format == 'S' || format == 'U'){
        S = ntohl(S); 
        A = ntohl(A);
        ID = ntohs(ID);
        F = ntohs(F);
    }

    switch(F){
        case 6:
            FLAG = "ACK SYN";
            break;
        case 2:
            FLAG = "SYN";
            break;
        case 1:
            FLAG = "FIN";
            break;
	    case 4:
	        FLAG = "ACK";
	        break;
        case 5:
            FLAG = "ACK FIN";
            break;
        default:
            break;
    }
    
    if (format == 'R')
        printf("RECV %u %u %u %u %u %s\n",S,A,ID,window,thresh,FLAG.c_str());   
    else if (format == 'D')
        printf("DROP %u %u %u %s\n",S,A,ID,FLAG.c_str());   
    else if (format == 'S')
        printf("SEND %u %u %u %u %u %s\n",S,A,ID,window,thresh,FLAG.c_str());    
    else if (format == 'U')
        printf("SEND %u %u %u %u %u %s DUP\n",S,A,ID,window,thresh,FLAG.c_str());    
} 

void slidingWindow_data_trans(int sockfd, struct addrinfo* anchor, std::string file_name)
{
    unsigned int CWND_space = CWND/512;
    unsigned int recv_ack = 0;
    unsigned last_seq = 0;
    int file_length = 0;
    int F = 4; //need a ACK for the first send
    std::chrono::steady_clock::time_point timeout_check;
    timer_flag = 0; //reset timer flag       
 
    std::ifstream myfile(file_name.c_str(), std::ios::binary|std::ios::ate);

    packet send_packet;
    packet recv_packet;
    
    if (myfile.good()){
         file_length = myfile.tellg();
         if (file_length > max_file_size){
             close(sockfd);
             std::cerr << "ERROR: File too large to be sent. Abort" << std::endl; 
             exit(EXIT_FAILURE);
         }
    }
    else {
          std::cerr << "ERROR: fail to open the file" << std::endl;
          exit(EXIT_FAILURE);
    }

    myfile.seekg(0, myfile.beg);
    timer_flag = 0; //flag for timeout
    
    while(1){
        // check time  
        std::chrono::steady_clock::time_point T = std::chrono::steady_clock::now();
        
        // check whether there is a timeout
        if (!CWND_logs.empty()){
            // 0.5 ceond timeout check
            if ((std::chrono::duration_cast<std::chrono::milliseconds>(T - CWND_logs.front().send_time).count() > 500)) 
            {
                // resume original pos in file and restore seq number  
                //printf("time out detected!\n");
                myfile.clear(); //clear the eof bit
                myfile.seekg(CWND_logs.front().file_pos);
                        
                last_seq = seq_num;
                seq_num = CWND_logs.front().seq;
                CWND_logs.erase(CWND_logs.begin());
                //CWND_logs.clear(); //remove all elements in CWND_logs vector 

                // reset CWND window 
                SS_THRESH = CWND/2;
                CWND = 512;    
                CWND_space = CWND/512; //clear CWND space for resend
            }
        }
        
       	// if there is still free space in CWND
	    if (CWND_space > 0 && !myfile.eof()){
            std::streampos file_pos = myfile.tellg(); //get cureent packet pos in file before read 
            myfile.read(send_packet.data, data_size);          
            
            if (myfile.fail() && !myfile.eof()){
              std::cerr << "ERROR: Error during reading from file" << std::endl; 
              close(sockfd);
              exit(EXIT_FAILURE);
            }

            // set right seq,ack,id and flags for packet to be sent (ACK number is 0, no flags) 
            set_header(send_packet, seq_num, ack_num, id_num, F);
	        ack_num = 0;
	        F = 0; // no flags ever since

            logger send_log; //create a log for this packet
            send_log.seq = seq_num; //logs its sequence num
            
            //increment seq number by the sent bytes
            seq_num += myfile.gcount();
            seq_num %= (102400+1); // max seq num is 102400   

            send_log.E_ACK = seq_num; //calculate the expected ack num for this packet
            send_log.file_pos = file_pos; //log its pos in file
            send_log.send_time = std::chrono::steady_clock::now(); //log the time it sent out
            CWND_logs.push_back(send_log); //put this log onto vector
            
            if (timer_flag == 0){
               timeout_check = send_log.send_time; //mark the time for 10 second timeout
               timer_flag = 1;
            }
	        
            // outout send infor
            if (ntohl(last_seq) >= ntohl(seq_num)){
                standard_output('U',send_packet.pack_header.seq,send_packet.pack_header.ack,send_packet.pack_header.id,send_packet.pack_header.flags,CWND,SS_THRESH);
                last_seq = seq_num;
            }
            else
                standard_output('S',send_packet.pack_header.seq,send_packet.pack_header.ack,send_packet.pack_header.id,send_packet.pack_header.flags,CWND,SS_THRESH);


            // execute packet sending  
            sendto(sockfd, &send_packet, 12+myfile.gcount(), 0, anchor->ai_addr, anchor->ai_addrlen);
            
            // decrement CWND space 
            CWND_space --;
        }
        
       // listen for incoming ACK packet
       int byte_s = recvfrom(sockfd, &recv_packet, packet_size, 0, anchor->ai_addr, &anchor->ai_addrlen); 
       if (byte_s > 0){ //check whether the receive ack is the expected
           ntoh_reorder(recv_packet);  
           recv_ack = recv_packet.pack_header.ack;
	       
	       // update the timer 
           timeout_check = std::chrono::steady_clock::now(); 
           
           // tranverse the queue
           for(std::vector<int>::size_type i = 0; i != CWND_logs.size(); i++){
               //if find this packet in queue, increment pop them from queue, increment window size 
               if (CWND_logs[i].E_ACK == recv_packet.pack_header.ack){
                   // outout received packet infor 
                   standard_output('R',recv_packet.pack_header.seq,recv_packet.pack_header.ack,recv_packet.pack_header.id,recv_packet.pack_header.flags,CWND,SS_THRESH);                   CWND_logs.erase(CWND_logs.begin(),CWND_logs.begin()+i+1); //erase all slots before and including it

                   //update CWND window and SS_THRESH
                   if (CWND < SS_THRESH){
                        CWND += 512;
                        CWND_space += 2;
                    }
                   else{ 
                        unsigned int dummy = CWND;
                        CWND += 512*512/CWND;
                        if ((CWND/512 - dummy/512) >= 1)
                            CWND_space += 2;
                        else 
                            CWND_space += 1;
                   }  
                   break;
              }
              if (i == CWND_logs.size()-1) 
                   standard_output('D',recv_packet.pack_header.seq,recv_packet.pack_header.ack,recv_packet.pack_header.id,recv_packet.pack_header.flags,CWND,SS_THRESH);
           }
           /*if (CWND_logs.front().E_ACK == recv_packet.pack_header.ack) //if found the expected ACK
           {
                if (CWND < SS_THRESH){
                    CWND += 512;
                    CWND_space += 2;
                }
                else{ 
                    unsigned int dummy = CWND;
                    CWND += 512*512/CWND;
                    if ((CWND/512 - dummy/512) >= 1)
                        CWND_space += 2;
                    else 
                        CWND_space += 1;
                }  
                // pop the first guy from the vector 
                CWND_logs.erase(CWND_logs.begin());
           }                  
           else //if ack is received but not the expected one 
           {
                //ack loss handling 
                ack_diff = recv_ack - seq_num;
           } */   
       }
       else{ // if no bytes are received   
	     // check 10 second timeout
             if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - timeout_check).count() >= 10)
             {
		        std::cerr << "ERROR: nothing receive from server for 10 second" << std::endl;
                close(sockfd);
                exit(EXIT_FAILURE);
             }  
       }
       
       // if reaches to the end of file and all ACKs are received
       if (myfile.tellg() == -1 && CWND_logs.empty()){
            break;
	   }	  
    }
}

//recv_pack function also takes the expected ack number as an argument
int recv_pack(int sockfd, packet &recv_pack, struct addrinfo *anchor, uint32_t ack)
{
    memset(&recv_pack, 0, packet_size);
    
    //invoke the timer 
    timer_flag = 0;
    std::thread timer_thread(timer, 500);

    //as long as timer is not timeout
    while(timer_flag == 0)
    {
       int byte_s = recvfrom(sockfd, &recv_pack, packet_size, 0, anchor->ai_addr, &anchor->ai_addrlen);
       if (byte_s >= 0){ //check whether the receive ack is the expected
           ntoh_reorder(recv_pack);

           // outout receive infor 
           standard_output('R',recv_pack.pack_header.seq,recv_pack.pack_header.ack,recv_pack.pack_header.id,recv_pack.pack_header.flags,CWND,SS_THRESH);
           if (ack == recv_pack.pack_header.ack || recv_pack.pack_header.flags == FIN) //if found the expected ACK
            {
                   // printf("Received the expected ACk packet!\n");
                    timer_flag = 1;
                    timer_thread.join();

                    if (CWND < SS_THRESH)
                        CWND += 512;

                    return byte_s;
            }  
            else //if ack is received but not the expected one 
            {
                continue;
            }
        }
    } 
    timer_thread.join();
    return -1; //indicating no expected ACK returns
}

void handshake(int sockfd, struct addrinfo *anchor)
{
    std::chrono::steady_clock::time_point start_time;

    //create data and packet
    packet send_packet;
    packet recv_packet;

    //combine header and data into packet 
    memset(&send_packet, 0, sizeof(send_packet));
    memset(&recv_packet,0, sizeof(recv_packet));

    set_header(send_packet, seq_num, ack_num, id_num, SYN);
    start_time = std::chrono::steady_clock::now(); //start the 10 second timer

    //send SYN and wait for SYN/ACK
    while (1){
        //check 10 second timeout
        if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-start_time).count() >= 10){
            std::cerr << "ERROR: Server receiving timeout! Nothing received for 10 sec" << std::endl;
            close(sockfd);
            exit(EXIT_FAILURE);
        }

         sendto(sockfd, &send_packet, packet_size, 0, anchor->ai_addr, anchor->ai_addrlen);

         // outout send infor 
         standard_output('S',send_packet.pack_header.seq,send_packet.pack_header.ack,send_packet.pack_header.id,send_packet.pack_header.flags,CWND,SS_THRESH);

        int recv_bytes = recv_pack(sockfd, recv_packet, anchor, 12346);

        if (recv_bytes >= 0)
        {
            start_time = std::chrono::steady_clock::now(); //update timer
            if (recv_packet.pack_header.flags == SYN_ACK && recv_packet.pack_header.seq == 4321) //last three bit is 110 (SYN/ACK flags are set)
            {
		        // increament sequence number and ack number and id
                seq_num = recv_packet.pack_header.ack;
                ack_num = recv_packet.pack_header.seq + 1;
                id_num = recv_packet.pack_header.id; 
                break;
            }
        }
        else{ 
            //std::cout << "SYN/ACK receiving timeout, resend SYN" << std::endl; 
            continue;
        }
    }
}

void ending(int sockfd, struct addrinfo *anchor)
{
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point send_time;
    int client_fin = 0;

    //create data and packet
    packet send_packet;
    packet recv_packet;

    //combine header and data into packet 
    memset(&send_packet, 0, sizeof(send_packet));
    memset(&recv_packet,0, sizeof(recv_packet));
    
    ack_num = 0;
    set_header(send_packet, seq_num, ack_num, id_num, FIN);
    
    start_time = std::chrono::steady_clock::now();
    send_time = std::chrono::steady_clock::now();
    sendto(sockfd, &send_packet, packet_size, 0, anchor->ai_addr, anchor->ai_addrlen);
    standard_output('S',send_packet.pack_header.seq,send_packet.pack_header.ack,send_packet.pack_header.id,send_packet.pack_header.flags,CWND,SS_THRESH);

    //send FIN and wait for FIN/ACK
    while (1){
        // check 10 second timeout
        if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-start_time).count() >= 10){
            std::cerr << "ERROR: Server FINACK receiving timeout! Nothing received for 10 sec" << std::endl;
            close(sockfd);
            exit(EXIT_FAILURE);
        }

        // if send 0.5 second timeout
        if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-send_time).count() >= 0.5){
            sendto(sockfd, &send_packet, packet_size, 0, anchor->ai_addr, anchor->ai_addrlen);
            send_time = std::chrono::steady_clock::now();
            standard_output('S',send_packet.pack_header.seq,send_packet.pack_header.ack,send_packet.pack_header.id,send_packet.pack_header.flags,CWND,SS_THRESH);
        }
              
        int recv_bytes = recvfrom(sockfd, &recv_packet, packet_size, 0, anchor->ai_addr, &anchor->ai_addrlen); 
        if (recv_bytes > 0) 
        {
            ntoh_reorder(recv_packet); 
            standard_output('R',recv_packet.pack_header.seq,recv_packet.pack_header.ack,recv_packet.pack_header.id,recv_packet.pack_header.flags,CWND,SS_THRESH);
            start_time = std::chrono::steady_clock::now(); //update timer

            //recv ack or fin_ack and client
            if ((recv_packet.pack_header.ack == seq_num + 1 || recv_packet.pack_header.flags == FIN))
            {
                // send ack for Fin or Fin_ACK
                if (recv_packet.pack_header.flags == ACK_FIN || recv_packet.pack_header.flags == FIN){
                    client_fin = 1;
                    ack_num = recv_packet.pack_header.seq + 1;
                    set_header(send_packet, seq_num, ack_num, id_num, ACK);
                    sendto(sockfd, &send_packet, packet_size, 0, anchor->ai_addr, anchor->ai_addrlen);//send the last ACK packet and close down
                    standard_output('S',send_packet.pack_header.seq,send_packet.pack_header.ack,send_packet.pack_header.id,send_packet.pack_header.flags,CWND,SS_THRESH);
                    start_time = std::chrono::steady_clock::now(); //update timer
                } 

                // enter the two second wait phase if received FIN/FIN_ACK/ACK
                while(client_fin)
                {
                    // if timeout exit this function
                    if (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-start_time).count() >= 2){
                        close(sockfd);
                        return;
                    }

                    if(recvfrom(sockfd, &recv_packet, packet_size, 0, anchor->ai_addr, &anchor->ai_addrlen) > 0){
                        ntoh_reorder(recv_packet); 
                        standard_output('R',recv_packet.pack_header.seq,recv_packet.pack_header.ack,recv_packet.pack_header.id,recv_packet.pack_header.flags,CWND,SS_THRESH);
                        // if I receive ACK before and now I receive FIN
                        if (recv_packet.pack_header.flags == FIN){
                            ack_num = recv_packet.pack_header.seq + 1;
                            set_header(send_packet, seq_num, ack_num, id_num, ACK);
                            sendto(sockfd, &send_packet, packet_size, 0, anchor->ai_addr, anchor->ai_addrlen);//send the last ACK packet and close down
                            standard_output('S',send_packet.pack_header.seq,send_packet.pack_header.ack,send_packet.pack_header.id,send_packet.pack_header.flags,CWND,SS_THRESH);
                        }
                        else //received some other packet
                            standard_output('D',recv_packet.pack_header.seq,recv_packet.pack_header.ack,recv_packet.pack_header.id,recv_packet.pack_header.flags,CWND,SS_THRESH);
                    } 
                }
            }
            else //if not ecpected packet 
                 standard_output('D',recv_packet.pack_header.seq,recv_packet.pack_header.ack,recv_packet.pack_header.id,recv_packet.pack_header.flags,CWND,SS_THRESH);
        }

        //old code
        /*int recv_bytes = recv_pack(sockfd, recv_packet, anchor, seq + 1);

        if (recv_bytes >= 0)
        {
            start_time = std::chrono::steady_clock::now(); //update timer
            //if FIN arrives first, send ACK directly and close fow
            if (recv_packet.pack_header.flags == FIN || recv_packet.pack_header.flags == ACK_FIN)
            {
                ack_num = recv_packet.pack_header.seq + 1;
                set_header(send_packet, seq_num, ack_num, id_num, ACK);
                sendto(sockfd, &send_packet, packet_size, 0, anchor->ai_addr, anchor->ai_addrlen);//send the last ACK packet and close down
                
                standard_output('S',send_packet.pack_header.seq,send_packet.pack_header.ack,send_packet.pack_header.id,send_packet.pack_header.flags,CWND,SS_THRESH);
                break;
            }
            else if (recv_packet.pack_header.flags == ACK && recv_packet.pack_header.ack == seq + 1) //last three bit is 100 (ACK flag is set)
            if (recv_packet.pack_header.ack == seq + 1)
            {
                 if ((recv_packet.pack_header.flags == FIN || recv_packet.pack_header.flags == ACK_FIN) && client_fin == 0){
                    memset(send_packet.data, 0, data_size);
                    
                    //std::cout << "Receive FIN from server" << std::endl;
                    ack_num = recv_packet.pack_header.seq + 1;
                    set_header(send_packet, seq_num, ack_num, id_num, ACK);
                    sendto(sockfd, &send_packet, packet_size, 0, anchor->ai_addr, anchor->ai_addrlen);//send the last ACK packet and close down
                    
                    // outout send infor 
                    standard_output('S',send_packet.pack_header.seq,send_packet.pack_header.ack,send_packet.pack_header.id,send_packet.pack_header.flags,CWND,SS_THRESH);
                    client_fin = 1;
                }

                // wait for 2 second (0.5 second each)
                for (int i = 0; i < 4; i++){ 
                    int recv_bytes2 = recv_pack(sockfd, recv_packet, anchor, 0);
                    if (recv_bytes2 > 0){
                        if ((recv_packet.pack_header.flags == FIN || recv_packet.pack_header.flags == ACK_FIN) && client_fin == 0){
                            //std::cout << "Receive FIN from server" << std::endl;
                            ack_num = recv_packet.pack_header.seq + 1;
                            set_header(send_packet, seq_num, ack_num, id_num, ACK);
			                sendto(sockfd, &send_packet, packet_size, 0, anchor->ai_addr, anchor->ai_addrlen);//send the last ACK packet and close down
                            
			                // outout send infor 
        		            standard_output('S',send_packet.pack_header.seq,send_packet.pack_header.ack,send_packet.pack_header.id,send_packet.pack_header.flags,CWND,SS_THRESH);
                            client_fin = 1;
                        }
                        else{
                            standard_output('D',send_packet.pack_header.seq,send_packet.pack_header.ack,send_packet.pack_header.id,send_packet.pack_header.flags);
                        }
                         standard_output('D',send_packet.pack_header.seq,send_packet.pack_header.ack,send_packet.pack_header.id,send_packet.pack_header.flags,CWND,SS_THRESH);
                    }
                }
               
                //close the socket 
                //printf("Client shut down gracefully\n");
		        close(sockfd);
                break;
            }
        }
        else{
                //std::cout << "ACK for FIN receiving timeout, resend FIN" << std::endl;
        }*/
    }
}

void sigpipe_handler(int s){
    if (s == SIGPIPE){
        std::cerr << "ERROR: Server is down. Abort." << std::endl;
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[])
{
  signal(SIGPIPE, sigpipe_handler);

   //check client argument number
   if (argc != 4)
   {
       perror("ERROR: incorrect number of arguments!");
       exit(EXIT_FAILURE);
   }

  std::string host_name_ip, port_num, file_name;
  host_name_ip = argv[1];
  port_num = argv[2];
  file_name = argv[3];

   //check port number
   if(std::stoi(port_num) <= 1023 && std::stoi(port_num) >= 0)
   {
       perror("ERROR: incorrect port num, please specify a number in the range [1024, 65535]");
       exit(EXIT_FAILURE);
   } 
 
  /*std::cout << "host name ip " << host_name_ip << std::endl <<
  " port num is "<< port_num << std::endl << 
  " file name is " << file_name << std::endl;
  */
  struct addrinfo hints, *server_info, *anchor;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;  //UDP protocol
  hints.ai_flags = AI_PASSIVE;

  if(getaddrinfo(host_name_ip.c_str(), port_num.c_str(), &hints, &server_info) != 0)
  {
      std::cerr << "ERROR: getaddrinfo error" << std::endl;
      exit(EXIT_FAILURE);
  }

  int sockfd;
  for(anchor = server_info; anchor != NULL; anchor = anchor -> ai_next)
  {
      sockfd = socket(anchor -> ai_family,anchor -> ai_socktype, anchor-> ai_protocol);
      if(sockfd < 0)
	    continue;

      break;
  }
  
  if(anchor == NULL)
  {
      std::cerr << "ERROR: bind failure" << std::endl;
      exit(EXIT_FAILURE);
  }

  //set socketfd non-blocking 
  int flags = fcntl(sockfd, F_GETFL, 0);
  fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

  handshake(sockfd, anchor);
  slidingWindow_data_trans(sockfd, anchor, file_name);
  ending(sockfd, anchor);
}
