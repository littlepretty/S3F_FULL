/**
 * \file ip_message.cc
 * \brief Source file for the IPMessage class.
 *
 * authors : Dong (Kevin) Jin
 */

#include "os/ipv4/ip_message.h"
#include "util/errhandle.h"

namespace s3f {
namespace s3fnet {

IPMessage::IPMessage() : src_ip(IPADDR_INVALID), dst_ip(IPADDR_INVALID), protocol_no(0), 
	time_to_live(0), identification(0), tos(0), flags(0), fragmentOffset(0), total_length(S3FNET_IPHDR_LENGTH){}

IPMessage::IPMessage(IPADDR src, IPADDR dest, uint8 protono, uint8 live) :
	src_ip(src), dst_ip(dest), protocol_no(protono), time_to_live(live), identification(0), 
	tos(0), flags(0), fragmentOffset(0), total_length(S3FNET_IPHDR_LENGTH){}

IPMessage::IPMessage(IPADDR src, IPADDR dest, uint8 protono, uint8 live, uint16 id, uint8 tos_ip, 
	uint8 flags_ip, uint16 fragmentOffset_ip, uint16 total_length_ip) :
	src_ip(src), dst_ip(dest), protocol_no(protono), time_to_live(live), identification(id), tos(tos_ip), 
	flags(flags_ip), fragmentOffset(fragmentOffset_ip), total_length(total_length_ip){}

IPMessage::IPMessage(const IPMessage& iph) :
  ProtocolMessage(iph), // important!!!
  src_ip(iph.src_ip), dst_ip(iph.dst_ip),
  protocol_no(iph.protocol_no), time_to_live(iph.time_to_live),
  identification(iph.identification), tos(iph.tos),
  flags(iph.flags), fragmentOffset(iph.fragmentOffset),
  total_length(iph.total_length)
{}

ProtocolMessage* IPMessage::clone()
{
  //printf("IPMessage cloned()\n");
  return new IPMessage(*this);
}

IPMessage::~IPMessage(){}

S3FNET_REGISTER_MESSAGE(IPMessage, S3FNET_PROTOCOL_TYPE_IPV4);

}; // namespace s3fnet
}; // namespace s3f
