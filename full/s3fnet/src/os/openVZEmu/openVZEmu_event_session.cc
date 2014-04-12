/**
 * \file openVZEmu_event_session.cc
 * \brief Source file for the OpenVZEventSession class.
 *
 * authors : Dong (Kevin) Jin
 */

#include <stdio.h>
#include <stdlib.h>
#include "os/openVZEmu/openVZEmu_event_session.h"
#include "os/openVZEmu/openVZEmu_message.h"
#include "util/errhandle.h" // defines error_quit() method
#include "net/host.h" // defines Host class
#include "os/base/protocols.h" // defines S3FNET_REGISTER_PROTOCOL macro
#include "os/ipv4/ip_interface.h" // defines IPPushOption and IPOptionToAbove classes
#include "os/simple_mac/simple_mac_message.h"
#include "os/ipv4/ip_message.h"
#include "net/net.h"
#include "env/namesvc.h"

#ifdef OPENVZ_EVENT_DEBUG
#define OPENVZ_EVENT_DUMP(x) printf("OPENVZ_EVENT: "); x
#else
#define OPENVZ_EVENT_DUMP(x)
#endif

namespace s3f {
namespace s3fnet {

S3FNET_REGISTER_PROTOCOL(OpenVZEventSession, OPENVZ_EVENT_PROTOCOL_CLASSNAME);

OpenVZEventSessionCallbackActivation::OpenVZEventSessionCallbackActivation
(ProtocolSession* sess, IPADDR _dst_ip, IPADDR _src_ip, EmuPacket* ppkt) :
ProtocolCallbackActivation(sess), dst_ip(_dst_ip), src_ip(_src_ip), packet(ppkt) {}

OpenVZEventSession::OpenVZEventSession(ProtocolGraph* graph) : ProtocolSession(graph),
		pkt_to_ve_counter(0), ip_session(0), dcac(0), callback_proc(0)
{
	// create your session-related variables here
	OPENVZ_EVENT_DUMP(printf("An OpenVZ EVENT protocol session is created.\n"));
}

OpenVZEventSession::~OpenVZEventSession()
{
	// reclaim your session-related variables here
	OPENVZ_EVENT_DUMP(printf("An OpenVZ EVENT protocol session is reclaimed.\n"));

	//printf("#packet out to VE = %ld\n", pkt_to_ve_counter);
}

void OpenVZEventSession::config(s3f::dml::Configuration* cfg)
{
#ifndef OPENVZ_EMULATION
	error_quit("OPENVZ_EMULATION is not defined, but DML file contains OpenVZEventSession layer.");
#endif
	// the same method at the parent class must be called
	ProtocolSession::config(cfg);
}

void OpenVZEventSession::init()
{
	// the same method at the parent class must be called
	ProtocolSession::init();

	// initialize the session-related variables here
	OPENVZ_EVENT_DUMP(printf("OpenVZ EVENT protocol session is initialized.\n"));

	// we couldn't resolve the IP layer until now
	ip_session = (IPSession*)inHost()->getNetworkLayerProtocol();
	if(!ip_session) error_quit("ERROR: can't find the IP layer; impossible!");

	Host* owner_host = inHost();
	callback_proc = new Process( (Entity *)owner_host, (void (s3f::Entity::*)(s3f::Activation))&OpenVZEventSession::callback);
}

void OpenVZEventSession::injectEvent(IPADDR dst_ip, IPADDR src_ip, EmuPacket* ppkt, int priority)
{
	OpenVZEventSessionCallbackActivation* oac = new OpenVZEventSessionCallbackActivation(this, dst_ip, src_ip, ppkt);
	Activation ac (oac);
	//schedule to send a packet
	Host* owner_host = inHost();
	ltime_t wait_time = ppkt->timestamp - getNow();
#ifdef OPENVZ_EMULATION_LOOKAHEAD
	//due to the new design of lookahead, it is possible that past emulation events are injected into simulator
	if(wait_time < 0) wait_time = 0;
	HandleCode h = owner_host->waitFor( callback_proc, ac, wait_time, priority);
#else
	if(wait_time >= 0)
	{
		HandleCode h = owner_host->waitFor( callback_proc, ac, wait_time, priority);
	}
	else
	{
		error_quit("inject an event whose time is in the past\n");
	}
#endif

	OPENVZ_EVENT_DUMP(char s1[50]; char s2[50];
	printf("[%s] [host=%s] OpenVZEventSession::injectEvent(), "
			"inject an event src_ip = %s, dst_ip = %s, len = %d, "
			"sending_time = %ld, iface_id = %d\n",
			getNowWithThousandSeparator(), inHost()->nhi.toString(),
			IPPrefix::ip2txt(src_ip, s2), IPPrefix::ip2txt(dst_ip, s1),
			ppkt->len, ppkt->timestamp, ppkt->ifid));
}

//sending a packet
void OpenVZEventSession::callback(Activation ac)
{
	OpenVZEventSession* sess =
			(OpenVZEventSession*)((OpenVZEventSessionCallbackActivation*)ac)->session;
	EmuPacket* packet = ((OpenVZEventSessionCallbackActivation*)ac)->packet;
	IPADDR dst_ip = ((OpenVZEventSessionCallbackActivation*)ac)->dst_ip;
	IPADDR src_ip = ((OpenVZEventSessionCallbackActivation*)ac)->src_ip;
	sess->callback_body(packet, dst_ip, src_ip);
}

void OpenVZEventSession::callback_body(EmuPacket* ppkt, IPADDR dst_ip, IPADDR src_ip)
{
	OpenVZMessage* msg = new OpenVZMessage(ppkt);
	Activation msg_ac (msg);

	//ip session will add src ip
	/* todo: ipopt is no needed in pure emulation case, only iface_id is needed, keep it just in case the design changes later */
	IPPushOption ipopt;
	ipopt.dst_ip = dst_ip;
	ipopt.src_ip = src_ip;
	ipopt.prot_id = S3FNET_PROTOCOL_TYPE_OPENVZ_EVENT;
	ipopt.ttl = DEFAULT_IP_TIMETOLIVE;
	ipopt.iface_id = ppkt->ifid;

	OPENVZ_EVENT_DUMP(printf("[%s] [host=%s] OpenVZEventSession::callback(), "
			"send a packet, dst_ip = %s, len = %d.\n",
			getNowWithThousandSeparator(), inHost()->nhi.toString(),
			IPPrefix::ip2txt(dst_ip), ppkt->len));

	ip_session->pushdown(msg_ac, this, (void*)&ipopt, sizeof(IPPushOption));
}

int OpenVZEventSession::control(int ctrltyp, void* ctrlmsg, ProtocolSession* sess)
{
	switch(ctrltyp)
	{
	default:
		return ProtocolSession::control(ctrltyp, ctrlmsg, sess);
	}
}

int OpenVZEventSession::push(Activation msg, ProtocolSession* hi_sess, void* extinfo, size_t extinfo_size)
{
	error_quit("ERROR: a message is pushed down to the openvz event input session from protocol layer above; it's impossible.\n");
	return 0;
}

int OpenVZEventSession::pop(Activation msg, ProtocolSession* lo_sess, void* extinfo, size_t extinfo_size)
{
	OPENVZ_EVENT_DUMP(printf("A message is popped up to the openVZ EVENT session from the IP layer.\n"));

	// the protocol message must be of OpenVZMessage type, and the extra info must be of IPOptionToAbove type
	OpenVZMessage* omsg = (OpenVZMessage*)msg;
	IPOptionToAbove* ipopt = (IPOptionToAbove*)extinfo;

	OPENVZ_EVENT_DUMP(char buf1[32]; char buf2[32];
	printf("[%s] OpenVZ EVENT session receives packet: \"%s\" with length %d (ip_src=%s, ip_dest=%s)\n",
			getNowWithThousandSeparator(), omsg->ppkt->data, omsg->ppkt->len,
			IPPrefix::ip2txt(ipopt->src_ip, buf1), IPPrefix::ip2txt(ipopt->dst_ip, buf2)); );

#ifdef OPENVZ_EMULATION
	//deliver packet to openVZ VE if the host is mapped to one
	if(inHost()->isOpenVZEmu() == true)
	{
		EmuPacket* ppkt = omsg->ppkt;
		ppkt->timestamp = getNow();
		ppkt->ifid = omsg->ifid;

		inHost()->getTopNet()->ve[inHost()->getVEID()]->recvq.push_back(ppkt);

		pkt_to_ve_counter++;
	}
#endif
	// returning 0 indicates success
	return 0;
}

}; // namespace s3fnet
}; // namespace s3f
