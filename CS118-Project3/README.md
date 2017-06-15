# CS118 Project 3: Simple Router

Team members:
Zhuoqi Li(504135743): IP packet forwarding & routing table 
Haoran Zhang(804586710): Ethernet frame handling & ICMP packet processing
Tianhao Zhao(404419775): ARP Cache handling

Router Architecture:
We create an FSM graph to help us better understand how the whole system work and here is its logic:
 
 Packet come in -> check ethernet -> if Arp: handle Arp, if IP: handle IP (and handle ICMP if there is one)
 Packet forward out -> check ARP cache, if not there broadcast Arp request else check forwarding table, determine interface to  send out -> replace dest and src Mac -> send out
 
 Most importantly:
  
  FOLLOW YOUR HEART 
 

Problem we faced:
   1. ARP cache deadlock
   solution: comment out the mutex (dont really need it)
   
   2. ARP cache segfaut 
   solution: fix the memcpy bug (memcpy to the wrong place)
   
   3. No reply after forwarding packet
   solution: fix the wrong checksum bug (checksum filed was not zero when calculate it)

List of libraries used:
Please refer to .h files

Reference:
None.
