/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2017 Alexander Afanasyev
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by the Free Software Foundation, either version
 * 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include "simple-router.hpp"
#include "core/utils.hpp"

#include <fstream>

namespace simple_router {

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// IMPLEMENT THIS METHOD
//packethadnler gets called -----> look for iface by inIface name --------> print routing table
//from buffer packet get destination IP, Mac to find inIface and IP in routing table
void
SimpleRouter::handlePacket(const Buffer& packet, const std::string& inIface)
{
  temp_iface = inIface;
  std::cerr << "Got packet of size " << packet.size() << " on interface " << inIface << std::endl;
  struct ethernet_hdr ether_hdr;

  const Interface* iface = findIfaceByName(inIface);
  if (iface == nullptr) {
    std::cerr << "Received packet, but interface is unknown, ignoring" << std::endl;
    return;
  }

  std::cerr << getRoutingTable() << std::endl;
  std::cerr << getArp()<< std::endl;

  std::cout << inIface << std::endl;
  printIfaces(std::cout);

  getEthernetHeader(packet, ether_hdr);

  print_hdrs(packet);

  if(ether_hdr.ether_type == ntohs(0x0806))//ARP packet
  {
    handleArpPacket(packet, ether_hdr);
  }
  else if(ether_hdr.ether_type == ntohs(0x0800))
  {
    handleIpPacket(packet, ether_hdr);
    printf("receive IPv4 header\n");
  }
  //whenever receive an packet with IPv4 packet, check ARP, if not found, 
  //queue the packet and send ARP request to discover IP-MAC mapping
  //check ethernet packet type ARP / IPv4
  //if receive ARP reply, map IP-MAC in ARP cache, then send all corresponding queue packet
  //entries in ARP cache should timeout in 30 secs
  //send ARP request once per second until receive ARP reply
  //if not receiving ARP reply after 5 attempts, remove pending request and packets in queue 



  // FILL THIS IN


}

void SimpleRouter::getEthernetHeader(const Buffer &packet, struct ethernet_hdr &e_hdr)
{
  memcpy(&e_hdr, &packet[0], sizeof(e_hdr));
}

void SimpleRouter::getArpPacket(const Buffer &packet, struct arp_hdr &arp_header)
{
  memcpy(&arp_header, &packet[14], sizeof(arp_header));
}

void SimpleRouter::getIpPacket(const Buffer &packet, struct ip_hdr &ip_header)
{
  memcpy(&ip_header, &packet[14], sizeof(ip_header));
}

void SimpleRouter::getIcmpPacket(const Buffer &packet, struct icmp_hdr &icmp_header)
{
  memcpy(&icmp_header, &packet[34], sizeof(icmp_header));
}

void SimpleRouter::handleArpPacket(const Buffer &packet, struct ethernet_hdr &e_hdr)
{
  struct arp_hdr arp_packet; 
  getArpPacket(packet, arp_packet);

  if(arp_packet.arp_op == ntohs(0x0001))
  {//ARP request
      std::shared_ptr<simple_router::ArpEntry> lookup = NULL;
      lookup = m_arp.lookup(arp_packet.arp_sip);//check ARP cache, if already there, discard else record mapping  
      if (lookup == NULL)
      { //not found the mapping in cache 
        Buffer source_ip = std::vector<unsigned char>(6, 0);
        memcpy(&source_ip[0], &packet[6], ETHER_ADDR_LEN);
        m_arp.insertArpEntry(source_ip,arp_packet.arp_sip);
      }
    const Interface *myInterface = findIfaceByIp(arp_packet.arp_tip);
    if(findIfaceByIp(arp_packet.arp_tip) != nullptr)
    {//check if it is looking for client interface mac address then send reply
      Buffer replyPacket;
      assembleArpInterfaceReplyPacket(replyPacket, e_hdr, arp_packet);
      sendPacket(replyPacket, myInterface->name);
      printf("==================send ARP\n");
      print_hdrs(replyPacket);
    }
    else//it is not looking for interface mac address
    {

    }
  }
  else if(arp_packet.arp_op == htons(0x0002))
  {//ARP reply 
      std::shared_ptr<simple_router::ArpEntry> lookup = NULL;
      lookup = m_arp.lookup(arp_packet.arp_sip);//check ARP cache, if already there, discard else record mapping  
      if (lookup == NULL){ //not found the mapping in cache 
        Buffer source_ip = std::vector<unsigned char>(6, 0);
        memcpy(&source_ip[0], &packet[6], ETHER_ADDR_LEN);
        m_arp.insertArpEntry(source_ip,arp_packet.arp_sip);
        
        //It will remove the request after sending all packets
        m_arp.sendPendingPackets(arp_packet, arp_packet.arp_sip);
      }
      else 
          printf("ARP reply's mapping already in cache, ARP packet drop\n");
  }
}

void SimpleRouter::assembleArpInterfaceReplyPacket(Buffer &replyPacket, struct ethernet_hdr e_hdr, struct arp_hdr arp_header)
{
  const Interface* myInterface = findIfaceByIp(arp_header.arp_tip);

  memcpy(e_hdr.ether_dhost, e_hdr.ether_shost, sizeof(e_hdr.ether_shost));
  memcpy(e_hdr.ether_shost, &myInterface->addr[0], sizeof(e_hdr.ether_shost));

  arp_header.arp_op = htons(0x0002);
  
  memcpy(&arp_header.arp_tip, &arp_header.arp_sip, sizeof(arp_header.arp_tip));
  memcpy(&arp_header.arp_tha, &arp_header.arp_sha, sizeof(arp_header.arp_tha));
  memcpy(&arp_header.arp_sip, &myInterface->ip, sizeof(arp_header.arp_tip));
  memcpy(arp_header.arp_sha, &myInterface->addr[0], sizeof(arp_header.arp_sha));

  replyPacket = std::vector<unsigned char>(42, 0);
  memcpy(&replyPacket[0], &e_hdr, sizeof(e_hdr));
  memcpy(&replyPacket[14], &arp_header, sizeof(arp_header));
}

void SimpleRouter::assembleIPPacket(Buffer &replyPacket, struct ip_hdr ip_header, struct ethernet_hdr e_hdr)
{
    memcpy(&replyPacket[0], &e_hdr, sizeof(e_hdr)); //attached new ethernet header
    memcpy(&replyPacket[14], &ip_header, sizeof(ip_header)); //attached new ip header
}

void SimpleRouter::handleIpPacket(const Buffer &packet, struct ethernet_hdr &e_hdr)
{
  printf("#################### In handle IP packet\n");
  const Interface *myInterface;
  struct ip_hdr ip_packet;
  Buffer packet_send = packet;

  getIpPacket(packet, ip_packet);
  uint8_t TTL = ip_packet.ip_ttl - 1;

  
  // ip_packet.ip_sum = 0x0000;
  printf("check sum is %x\n", cksum(&ip_packet, sizeof(ip_packet)));
  
  // if(TTL <= 0)
  // {
  //   //send ICMP packet with time exceeded type 
  //   if(m_arp.lookup(ip_packet.ip_src) == nullptr)
  //     {
  //       Buffer source_mac = std::vector<unsigned char>(6, 0);
  //       memcpy(&source_mac[0], &packet[6], ETHER_ADDR_LEN);
  //       m_arp.insertArpEntry(source_mac ,ip_packet.ip_src);
  //     }
  //     handleIcmpPacket(packet, e_hdr, 1);
  // }
  // else 
  if(cksum(&ip_packet, sizeof(ip_packet)) == 0xffff)
  {
    ip_packet.ip_ttl = TTL;
    myInterface = findIfaceByIp(ip_packet.ip_dst);

    if(m_arp.lookup(ip_packet.ip_src) == nullptr)
    {
      printf("source not in ARP chache\n");
      Buffer source_mac = std::vector<unsigned char>(6, 0);
      memcpy(&source_mac[0], &packet[6], ETHER_ADDR_LEN);
      m_arp.insertArpEntry(source_mac ,ip_packet.ip_src);
    }
    else
    {
      printf("~~~~~~~~~~~~~~~~~~~~~~~source is in ARP \n");
    }

    if(myInterface != nullptr)
    {//may be ICMP packet
      if(ip_packet.ip_p == 0x01)//ICMP message
        handleIcmpPacket(packet, e_hdr, 0);
      else if(ip_packet.ip_p == 0x11)//type 3 traceroute router
        handleIcmpPacket(packet, e_hdr, 1);
    }
    else
    {//may be forwared packet

      if(TTL <= 0)
      {
      //send ICMP packet with time exceeded type 
        if(m_arp.lookup(ip_packet.ip_src) == nullptr)
        {
          Buffer source_mac = std::vector<unsigned char>(6, 0);
          memcpy(&source_mac[0], &packet[6], ETHER_ADDR_LEN);
          m_arp.insertArpEntry(source_mac ,ip_packet.ip_src);
        }
        handleIcmpPacket(packet, e_hdr, 1);
      }
      else if(m_arp.lookup(ip_packet.ip_dst) != nullptr)
      {//find arp entry in arp cache
        //if no matched forwarding entry, throw run_time error
        try{ 
          ip_packet.ip_ttl = TTL;
          RoutingTableEntry matched_entry = m_routingTable.lookup(ip_packet.ip_dst);
          ip_packet.ip_sum = 0x0000; //zero the checksum and then calculate
          ip_packet.ip_sum = cksum(&ip_packet,sizeof(ip_packet)); //update IP checksum                     
          printf("in forwarding ttl is %x\n", ip_packet.ip_ttl);
          std::shared_ptr<simple_router::ArpEntry> dest_mac;
          if (ipToString(matched_entry.dest).compare("0.0.0.0") == 0)
             dest_mac = m_arp.lookup(ip_packet.ip_dst);  
          else
             dest_mac = m_arp.lookup(matched_entry.dest);
          
          const Interface *interToForward = findIfaceByName(matched_entry.ifName);
          memcpy(e_hdr.ether_shost, &interToForward->addr[0], sizeof(e_hdr.ether_shost)); //change source mac address to be iface mac
          memcpy(e_hdr.ether_dhost, (dest_mac->mac).data(), sizeof(e_hdr.ether_dhost)); //change dest mac address

          assembleIPPacket(packet_send, ip_packet, e_hdr); //update IP header, including MAC address
          sendPacket(packet_send, matched_entry.ifName); //forward packet
          print_hdrs(packet_send);
        }
        catch (std::runtime_error& error){ //if not found in forwarding table
          printf("Packet discard because of no match in forwarding table\n");
        }
        printf("==============packet sent out================\n");
      }
      else //if no arp found in cache, packet must be pushed to queue
      {
        Buffer dummy = std::vector<unsigned char>(6,0);
        std::memcpy(&dummy[0],&e_hdr.ether_dhost,sizeof(e_hdr.ether_dhost));
        const Buffer dummy_2 = dummy;
        const Interface *myInter = findIfaceByMac(dummy_2);   
        m_arp.queueRequest(ip_packet.ip_dst, packet, myInter->name); 
        printf("Finish this function\n");
      }
    }
  }
  else
  {
    printf("Checksum is wrong\n");
  }
}

void SimpleRouter::handleIcmpPacket(const Buffer &packet, struct ethernet_hdr &e_hdr, int time_exceed)
{
  struct icmp_hdr icmp_packet;
  getIcmpPacket(packet, icmp_packet);
  if(icmp_packet.icmp_type == 8)//ICMP echo message
  {
    uint16_t padd_cksum = 0x0000;
    Buffer temp_packet = packet;
    memcpy(&temp_packet[36], &padd_cksum, sizeof(padd_cksum));
    printf("ICMP echo message\n");
    if(cksum(&temp_packet[34], (int)sizeof(temp_packet) - 34) == 0xffff)//valid ICMP packet
    {
      //ICMP Layer
      //change ICMP type
      //change checksum include data
      uint8_t icmp_type;
      if(time_exceed == 0)
        icmp_type = 0x00;
      else
        icmp_type = 0x0b;
      uint16_t icmp_sum = cksum(&temp_packet[34], (int)sizeof(temp_packet) - 34);
      memcpy(&temp_packet[34], &icmp_type, sizeof(icmp_type));
      memcpy(&temp_packet[36], &icmp_sum, sizeof(icmp_packet.icmp_sum));

      //IP layer
      //change dest ip
      //change source ip
      //change ip checksum ip header only
      const Interface* myInterface = findIfaceByName(temp_iface); 
      // uint32_t temp_src_ip = myInterface->ip;
      uint32_t temp_src_ip;
      uint8_t padd_ip_cksum = 0x00;
      memcpy(&temp_packet[24], &padd_ip_cksum, sizeof(padd_ip_cksum));
      uint8_t ip_sum = cksum(&temp_packet[14], sizeof(struct ip_hdr));
      memcpy(&temp_packet[24], &ip_sum, sizeof(ip_sum));
      memcpy(&temp_src_ip, &temp_packet[30], sizeof(uint32_t));
      memcpy(&temp_packet[30], &temp_packet[26], sizeof(uint32_t));
      memcpy(&temp_packet[26], &temp_src_ip, sizeof(uint32_t));

      //Ethernet layer
      //change destination address
      //change source address
      // const Interface* myInterface = findIfaceByIp(temp_src_ip);
      memcpy(&temp_packet[0], &temp_packet[6], sizeof(e_hdr.ether_shost));
      memcpy(&temp_packet[6], &myInterface->addr[0], sizeof(e_hdr.ether_shost));

      sendPacket(temp_packet, myInterface->name);
      printf("=================================== send ICMP packet \n");
      print_hdrs(temp_packet);
    }

  }
  else
  {
    if(time_exceed == 1)
    {
      Buffer temp_packet;
      uint8_t ip_protocol;
      memcpy(&ip_protocol, &packet[23], sizeof(uint8_t));


      temp_packet = std::vector<unsigned char>(70, 0);
      
      memcpy(&temp_packet[0], &packet[0], 34);
      memcpy(&temp_packet[42], &packet[14], 28);


      uint32_t temp_dest_ip;
      memcpy(&temp_dest_ip, &packet[30], sizeof(uint32_t));

      uint8_t icmp_type = 0x0b;
      uint8_t icmp_code = 0x00;
      if(findIfaceByIp(temp_dest_ip) != nullptr)
      {
        icmp_type = 0x03;
        icmp_code = 0x03;
      }

      memcpy(&temp_packet[34], &icmp_type, sizeof(icmp_type));
      memcpy(&temp_packet[35], &icmp_code, sizeof(icmp_code));

      uint16_t icmp_sum = cksum(&temp_packet[34], temp_packet.size() - 34);
      memcpy(&temp_packet[36], &icmp_sum, sizeof(icmp_packet.icmp_sum));

      // temp_packet = std::vector<unsigned char>(34 + sizeof(struct icmp_t3_hdr), 0);
      // struct icmp_t3_hdr icmp_header;
      // icmp_header.icmp_type = 0x0b;
      // icmp_header.icmp_code = 0x00;
      // icmp_header.icmp_sum = 0x0000;
      // icmp_header.unused= 0x0000;
      // icmp_header.next_mtu = 0x0000;
      // memcpy(icmp_header.data, &packet[14], ICMP_DATA_SIZE);


      // // memcpy(&temp_packet[34], &icmp_type, sizeof(icmp_type));
      // // memcpy(&temp_packet[35], &icmp_code, sizeof(icmp_code));

      // icmp_header.icmp_sum = cksum(&icmp_header, sizeof(struct icmp_t3_hdr));
      // memcpy(&temp_packet[34], &icmp_header, sizeof(struct icmp_t3_hdr));


      //IP layer
      //change dest ip
      //change source ip
      //change ip checksum ip header only
      const Interface* myInterface = findIfaceByName(temp_iface); 
      std::cout << "temp interface is " << temp_iface << std::endl;
      // uint32_t temp_src_ip = myInterface->ip;
      uint32_t temp_src_ip;
      uint16_t padd_ip_cksum = 0x00;
      uint8_t ip_ttl = 0x40;
      ip_protocol = 0x01;
      uint16_t ip_length;

      memcpy(&ip_length, &temp_packet[16], sizeof(ip_length));
      ip_length = ntohs(ip_length) - 0x04;
      ip_length = htons(ip_length);

      memcpy(&temp_packet[24], &padd_ip_cksum, sizeof(padd_ip_cksum));
      memcpy(&temp_packet[23], &ip_protocol, sizeof(ip_protocol));
      memcpy(&temp_packet[22], &ip_ttl, sizeof(ip_ttl));
      memcpy(&temp_packet[16], &ip_length, sizeof(ip_length));

      if(findIfaceByIp(temp_dest_ip) != nullptr)
        memcpy(&temp_src_ip, &temp_dest_ip, sizeof(uint32_t));
      else
        memcpy(&temp_src_ip, &myInterface->ip, sizeof(uint32_t));
      memcpy(&temp_packet[30], &temp_packet[26], sizeof(uint32_t));
      memcpy(&temp_packet[26], &temp_src_ip, sizeof(uint32_t));

      uint16_t ip_sum = cksum(&temp_packet[14], sizeof(struct ip_hdr));
      memcpy(&temp_packet[24], &ip_sum, sizeof(ip_sum));

      //Ethernet layer
      //change destination address
      //change source address
      // const Interface* myInterface = findIfaceByIp(temp_src_ip);
      memcpy(&temp_packet[0], &temp_packet[6], sizeof(e_hdr.ether_shost));
      memcpy(&temp_packet[6], &myInterface->addr[0], sizeof(e_hdr.ether_shost));

      sendPacket(temp_packet, myInterface->name);
      printf("=================================== send ICMP packet\n");
      print_hdrs(temp_packet);
      std::cerr << "Got packet of size " << temp_packet.size() << " on interface " << std::endl;






      // //set icmp type = 11 code = 0
      // uint32_t temp_dest_ip;
      // memcpy(&temp_dest_ip, &packet[30], sizeof(uint32_t));
      // uint8_t icmp_type = 0x0b;
      // uint8_t icmp_code = 0x00;

      // //create new temp paacket with size of original packet size + icmp size
      // //copy packet to temp packet
      // //copy data of ip header to position 38 to leave space for icmp
      // Buffer temp_packet = std::vector<unsigned char>(packet.size() + 8, 0);
      // memcpy(&temp_packet[0], &packet[0], 34);
      // memcpy(&temp_packet[42], &packet[34], packet.size() - 34);

      // if(findIfaceByIp(temp_dest_ip) != nullptr)
      // {
      //   icmp_type = 0x03;
      //   icmp_code = 0x03;
      // }


      // printf("IP packet time_exceed\n");
      

      

      // //check ip protocol type if udp, get udp dest ip
      // //if tcp get tcp dest ip to see whether it is sent to interface
      // memcpy(&temp_packet[34], &icmp_type, sizeof(icmp_type));
      // memcpy(&temp_packet[35], &icmp_code, sizeof(icmp_code));

      // uint16_t icmp_sum = cksum(&temp_packet[34], temp_packet.size() - 34);
      // memcpy(&temp_packet[36], &icmp_sum, sizeof(icmp_packet.icmp_sum));

      // //IP layer
      // //change dest ip
      // //change source ip
      // //change ip checksum ip header only
      // const Interface* myInterface = findIfaceByName(temp_iface); 
      // std::cout << "temp interface is " << temp_iface << std::endl;
      // // uint32_t temp_src_ip = myInterface->ip;
      // uint32_t temp_src_ip;
      // uint16_t padd_ip_cksum = 0x00;
      // uint8_t ip_ttl = 0x40;
      // uint8_t ip_protocol = 0x01;
      // uint16_t ip_length;

      // memcpy(&ip_length, &temp_packet[16], sizeof(ip_length));
      // ip_length = ntohs(ip_length) + 8;
      // ip_length = htons(ip_length);

      // memcpy(&temp_packet[24], &padd_ip_cksum, sizeof(padd_ip_cksum));
      // memcpy(&temp_packet[23], &ip_protocol, sizeof(ip_protocol));
      // memcpy(&temp_packet[22], &ip_ttl, sizeof(ip_ttl));
      // memcpy(&temp_packet[16], &ip_length, sizeof(ip_length));

      
      // memcpy(&temp_src_ip, &myInterface->ip, sizeof(uint32_t));
      // memcpy(&temp_packet[30], &temp_packet[26], sizeof(uint32_t));
      // memcpy(&temp_packet[26], &temp_src_ip, sizeof(uint32_t));

      // uint16_t ip_sum = cksum(&temp_packet[14], sizeof(struct ip_hdr));
      // memcpy(&temp_packet[24], &ip_sum, sizeof(ip_sum));

      // //Ethernet layer
      // //change destination address
      // //change source address
      // // const Interface* myInterface = findIfaceByIp(temp_src_ip);
      // memcpy(&temp_packet[0], &temp_packet[6], sizeof(e_hdr.ether_shost));
      // memcpy(&temp_packet[6], &myInterface->addr[0], sizeof(e_hdr.ether_shost));

      // sendPacket(temp_packet, myInterface->name);
      // printf("=================================== send ICMP packet with type 3\n");
      // print_hdrs(temp_packet);
      // std::cerr << "Got packet of size " << temp_packet.size() << " on interface " << std::endl;

    }
  }

}

void SimpleRouter::assembleIcmpReplyPacket(Buffer &replyPacket, struct ethernet_hdr e_hdr, struct icmp_hdr icmp_header, struct ip_hdr ip_header)
{
  //ethernet + ip + icmp

}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// You should not need to touch the rest of this code.
SimpleRouter::SimpleRouter()
  : m_arp(*this)
{
}

void
SimpleRouter::sendPacket(const Buffer& packet, const std::string& outIface)
{
  m_pox->begin_sendPacket(packet, outIface);
}

bool
SimpleRouter::loadRoutingTable(const std::string& rtConfig)
{
  return m_routingTable.load(rtConfig);
}

void
SimpleRouter::loadIfconfig(const std::string& ifconfig)
{
  std::ifstream iff(ifconfig.c_str());
  std::string line;
  while (std::getline(iff, line)) {
    std::istringstream ifLine(line);
    std::string iface, ip;
    ifLine >> iface >> ip;

    in_addr ip_addr;
    if (inet_aton(ip.c_str(), &ip_addr) == 0) {
      throw std::runtime_error("Invalid IP address `" + ip + "` for interface `" + iface + "`");
    }

    m_ifNameToIpMap[iface] = ip_addr.s_addr;
  }
}

void
SimpleRouter::printIfaces(std::ostream& os)
{
  if (m_ifaces.empty()) {
    os << " Interface list empty " << std::endl;
    return;
  }

  for (const auto& iface : m_ifaces) {
    os << iface << "\n";
  }
  os.flush();
}

const Interface*
SimpleRouter::findIfaceByIp(uint32_t ip) const
{
  auto iface = std::find_if(m_ifaces.begin(), m_ifaces.end(), [ip] (const Interface& iface) {
      return iface.ip == ip;
    });

  if (iface == m_ifaces.end()) {
    return nullptr;
  }

  return &*iface;
}

const Interface*
SimpleRouter::findIfaceByMac(const Buffer& mac) const
{
  auto iface = std::find_if(m_ifaces.begin(), m_ifaces.end(), [mac] (const Interface& iface) {
      return iface.addr == mac;
    });

  if (iface == m_ifaces.end()) {
    return nullptr;
  }

  return &*iface;
}

const Interface*
SimpleRouter::findIfaceByName(const std::string& name) const
{
  auto iface = std::find_if(m_ifaces.begin(), m_ifaces.end(), [name] (const Interface& iface) {
      return iface.name == name;
    });

  if (iface == m_ifaces.end()) {
    return nullptr;
  }

  return &*iface;
}

void
SimpleRouter::reset(const pox::Ifaces& ports)
{
  std::cerr << "Resetting SimpleRouter with " << ports.size() << " ports" << std::endl;

  m_arp.clear();
  m_ifaces.clear();

  for (const auto& iface : ports) {
    auto ip = m_ifNameToIpMap.find(iface.name);
    if (ip == m_ifNameToIpMap.end()) {
      std::cerr << "IP_CONFIG missing information about interface `" + iface.name + "`. Skipping it" << std::endl;
      continue;
    }

    m_ifaces.insert(Interface(iface.name, iface.mac, ip->second));
  }

  printIfaces(std::cerr);
}


} // namespace simple_router {
