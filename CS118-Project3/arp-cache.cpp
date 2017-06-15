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

#include "arp-cache.hpp"
#include "core/utils.hpp"
#include "core/interface.hpp"
#include "simple-router.hpp"

#include <algorithm>
#include <iostream>
//#include <vector>

namespace simple_router {
void
ArpCache::sendPendingPackets(struct arp_hdr &reply_arp_hdr, uint32_t dst_ip)
{
  //find the corresponding request
  std::shared_ptr<ArpRequest> request = nullptr;
  for(const auto& entry : m_arpRequests)
    {
      if(entry->ip == dst_ip)
        {
          request = entry;
          break;
        }
    }
  
  if(request != nullptr)
    {
      for(const auto& pendingPacket: request->packets)
        {
          int packet_size = pendingPacket.packet.size();
          Buffer sendpacket (packet_size,0);
          struct ethernet_hdr eth_hdr;
          memcpy(eth_hdr.ether_dhost, &reply_arp_hdr.arp_sha[0], ETHER_ADDR_LEN);
          memcpy(eth_hdr.ether_shost, &reply_arp_hdr.arp_tha[0], ETHER_ADDR_LEN);
          eth_hdr.ether_type = htons(0x0800);
          //Assemble packet
          memcpy(&sendpacket[0], &eth_hdr, sizeof(eth_hdr));
          memcpy(&sendpacket[14], &pendingPacket.packet[14], packet_size - sizeof(eth_hdr));
          std::string interfaceName = m_router.getRoutingTable().lookup(dst_ip).ifName;
          const Interface* sendInterface = m_router.findIfaceByName(interfaceName);
          m_router.sendPacket(sendpacket, sendInterface->name);

          printf("=====Pending packets sent=====\n");
          std::cout << "Interface:" << sendInterface->name << std::endl;
          print_hdrs(sendpacket);
          printf("=============================\n");
        }
       m_arpRequests.remove(request);
      //removeRequest(request);
    }
}
void
ArpCache::handleArpRequest(std::shared_ptr<ArpRequest> req, bool &isRemoved)
{

    printf("In handleArpRequest\n");
    if(steady_clock::now() - req->timeSent > seconds(1))
    {
      if(req->nTimesSent >= MAX_SENT_TIME)//reqeust time out(After 5 times retransmission)
        {
          printf("TimesSent:%d\n", req->nTimesSent);
          printf("~~~~~~~~~~~~~Remove the request\n");
          //send icmp host unreachable to source addr of all pkts waiting
          /*That's an extra credit, leave for later*/
          m_arpRequests.remove(req);
          isRemoved = true;
          return;
          //printf("Size:%d", m_arpRequests.size());
          //removeRequest(req);
          //printf("Request removed\n");
        }
      else//send arp request
        {
          struct arp_hdr arp_header;
          struct ethernet_hdr eth_header;
          Buffer request_packet (42,0); //Sending packet

          //const Interface* sendInterface = m_router.findIfaceByIp(req->ip);
          //get the interface
          std::string interfaceName = m_router.getRoutingTable().lookup(req->ip).ifName;
          const Interface* sendInterface = m_router.findIfaceByName(interfaceName);

          //Ethernet Frame 
          memset(eth_header.ether_dhost, 255, ETHER_ADDR_LEN);//Broadcast
          memcpy(eth_header.ether_shost, &sendInterface->addr[0], ETHER_ADDR_LEN);
          //memset(&eth_header.ether_type, htons(0x0806),sizeof(eth_header.ether_type));
          eth_header.ether_type = htons(0x0806);
          printf("Assembled Ethernet\n");
          print_hdr_eth((uint8_t*)&eth_header);
          
          //Arp header
          arp_header.arp_hrd = htons(0x0001);
          arp_header.arp_pro = htons(0x0800);
          arp_header.arp_hln = 6;
          arp_header.arp_pln = 4;
          arp_header.arp_op = htons(0x0001);
          memcpy(arp_header.arp_sha, &sendInterface->addr[0], ETHER_ADDR_LEN);
          memcpy(&arp_header.arp_sip, &sendInterface->ip, sizeof(arp_header.arp_sip));
          memset(arp_header.arp_tha, 255, ETHER_ADDR_LEN);
          memcpy(&arp_header.arp_tip, &req->ip, sizeof(arp_header.arp_tip));
          printf("Assembled Arp\n");
          print_hdr_arp((uint8_t*)&arp_header);

          //Assemble to packet and send
          memcpy(&request_packet[0], &eth_header, sizeof(eth_header));
          memcpy(&request_packet[14], &arp_header, sizeof(arp_header));
          m_router.sendPacket(request_packet, sendInterface->name);

          printf("================Sent Arp reqeust==================\n");
          std::cout << "Interface:" << sendInterface->name << std::endl; 
          print_hdrs(request_packet);
          printf("===============================================\n");
          req->timeSent = steady_clock::now();
          req->nTimesSent++;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
// IMPLEMENT THIS METHOD
void
ArpCache::periodicCheckArpRequestsAndCacheEntries()
{
  bool isRemoved = false;
  std::vector<std::shared_ptr<ArpEntry>> recordForRemoval;
      for(const auto& req: m_arpRequests)
        {
          handleArpRequest(req, isRemoved);
          if(isRemoved) //Avoid segfault
            break;
        }

  //Find timeout cache
  for(const auto& entry: m_cacheEntries)
    {
      if(!(entry->isValid))
        {
          // m_cacheEntries.remove(entry);
          recordForRemoval.push_back(entry);
        }
    }

  for(const auto& entry: recordForRemoval)
    {
      m_cacheEntries.remove(entry);
    }

}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// You should not need to touch the rest of this code.

ArpCache::ArpCache(SimpleRouter& router)
  : m_router(router)
  , m_shouldStop(false)
  , m_tickerThread(std::bind(&ArpCache::ticker, this))
{
}

ArpCache::~ArpCache()
{
  m_shouldStop = true;
  m_tickerThread.join();
}

std::shared_ptr<ArpEntry>
ArpCache::lookup(uint32_t ip)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  for (const auto& entry : m_cacheEntries) {
    if (entry->isValid && entry->ip == ip) {
      return entry;
    }
  }

  return nullptr;
}

std::shared_ptr<ArpRequest>
ArpCache::queueRequest(uint32_t ip, const Buffer& packet, const std::string& iface)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  auto request = std::find_if(m_arpRequests.begin(), m_arpRequests.end(),
                           [ip] (const std::shared_ptr<ArpRequest>& request) {
                             return (request->ip == ip);
                           });

  if (request == m_arpRequests.end()) {
    request = m_arpRequests.insert(m_arpRequests.end(), std::make_shared<ArpRequest>(ip));
  }
  
  // Add the packet to the list of packets for this request
  print_hdrs(packet);

  (*request)->packets.push_back({packet, iface});

  printf("Done push back\n");
  return *request;
}

void
ArpCache::removeRequest(const std::shared_ptr<ArpRequest>& entry)
{
  std::lock_guard<std::mutex> lock(m_mutex);
  m_arpRequests.remove(entry);
 }

std::shared_ptr<ArpRequest>
ArpCache::insertArpEntry(const Buffer& mac, uint32_t ip)
{
  std::lock_guard<std::mutex> lock(m_mutex);

  auto entry = std::make_shared<ArpEntry>();
  entry->mac = mac;
  entry->ip = ip;
  entry->timeAdded = steady_clock::now();
  entry->isValid = true;
  m_cacheEntries.push_back(entry);

  auto request = std::find_if(m_arpRequests.begin(), m_arpRequests.end(),
                           [ip] (const std::shared_ptr<ArpRequest>& request) {
                             return (request->ip == ip);
                           });
  if (request != m_arpRequests.end()) {
    return *request;
  }
  else {
    return nullptr;
  }
}

void
ArpCache::clear()
{
  std::lock_guard<std::mutex> lock(m_mutex);

  m_cacheEntries.clear();
  m_arpRequests.clear();
}

void
ArpCache::ticker()
{
  while (!m_shouldStop) {
    std::this_thread::sleep_for(std::chrono::seconds(1));

    {
      std::lock_guard<std::mutex> lock(m_mutex);

      auto now = steady_clock::now();

      for (auto& entry : m_cacheEntries) {
        if (entry->isValid && (now - entry->timeAdded > SR_ARPCACHE_TO)) {
          entry->isValid = false;
        }
      }

      periodicCheckArpRequestsAndCacheEntries();
    }
  }
}

std::ostream&
operator<<(std::ostream& os, const ArpCache& cache)
{
  std::lock_guard<std::mutex> lock(cache.m_mutex);

  os << "\nMAC            IP         AGE                       VALID\n"
     << "-----------------------------------------------------------\n";

  auto now = steady_clock::now();
  for (const auto& entry : cache.m_cacheEntries) {

    os << macToString(entry->mac) << "   "
       << ipToString(entry->ip) << "   "
       << std::chrono::duration_cast<seconds>((now - entry->timeAdded)).count() << " seconds   "
       << entry->isValid
       << "\n";
  }
  os << std::endl;
  return os;
}

} // namespace simple_router
