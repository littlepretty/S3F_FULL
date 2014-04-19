/**
 * \file dummy_session.cc
 * \brief Source file for the RandomDummySession class.
 *
 * authors : Dong (Kevin) Jin
 */

#include "os/random_dummy/random_dummy_session.h"
#include "os/random_dummy/random_dummy_message.h"
#include "os/ipv4/ip_session.h"
#include "util/errhandle.h" // defines error_quit() method
#include "net/host.h" // defines Host class
#include "os/base/protocols.h" // defines S3FNET_REGISTER_PROTOCOL macro
#include "os/ipv4/ip_interface.h" // defines IPPushOption and IPOptionToAbove classes
#include "net/net.h"
#include "env/namesvc.h"

#include "../../rng/rng.h"

#ifdef RANDOM_DUMMY_DEBUG
#define RANDOM_RANDOM_DUMMY_DUMP(x) printf("RANDOM_DUMMY: "); x
#else
#define RANDOM_RANDOM_DUMMY_DUMP(x)
#endif

namespace s3f {
namespace s3fnet {

S3FNET_REGISTER_PROTOCOL(RandomDummySession, RANDOM_DUMMY_PROTOCOL_CLASSNAME);

RandomDummySession::RandomDummySession(ProtocolGraph* graph) : ProtocolSession(graph)
{
  // create your session-related variables here
  RANDOM_DUMMY_DUMP(printf("A random dummy protocol session is created.\n"));
}

RandomDummySession::~RandomDummySession()
{
  // reclaim your session-related variables here
  RANDOM_DUMMY_DUMP(printf("A random dummy protocol session is reclaimed.\n"));
  if(callback_proc) delete callback_proc;
}

void RandomDummySession::config(s3f::dml::Configuration* cfg)
{
  // the same method at the parent class must be called
  ProtocolSession::config(cfg);

  // parameterize your protocol session from DML configuration

  // parse hello_message
  char* str = (char*)cfg->findSingle("hello_message");
  if(str)
  {
    if(s3f::dml::dmlConfig::isConf(str))
      error_quit("ERROR: RandomDummySession::config(), invalid HELLO_MESSAGE attribute.");
    else hello_message = str;
  }
  else hello_message = "hello there";

  // parse hello_interval
  double hello_interval_double;
  str = (char*)cfg->findSingle("hello_interval");
  if(!str) hello_interval_double = 0;
	  //error_quit("ERROR: RandomDummySession::config(), missing HELLO_INTERVAL attribute.");
  else if(s3f::dml::dmlConfig::isConf(str))
    error_quit("ERROR: RandomDummySession::config(), invalid HELLO_INTERVAL attribute.");
  else hello_interval_double = atof(str);
  if(hello_interval_double < 0)
    error_quit("ERROR: RandomDummySession::config(), HELLO_INTERVAL needs to be non-negative.");
  hello_interval = inHost()->d2t(hello_interval_double, 0);

  if(hello_interval > 0)
  {
    // parse hello_peer_nhi
	  str = (char*)cfg->findSingle("hello_peer_nhi");
	  if(str)
	  {
  		if(s3f::dml::dmlConfig::isConf(str))
  		  error_quit("ERROR: RandomDummySession::config(), invalid HELLO_PEER_NHI attribute.");
  		else hello_peer_nhi = str;
	  }

    // parse hello_peer_nhi
	  str = (char*)cfg->findSingle("hello_peer_ip");
	  if(str)
	  {
  		if(s3f::dml::dmlConfig::isConf(str))
  		  error_quit("ERROR: RandomDummySession::config(), invalid HELLO_PEER_IP attribute.");
  		else 
        hello_peer_ip = str;
	  }

    // nhi and ip should not both exist or non-exsist
	  if(!hello_peer_nhi.empty() && !hello_peer_ip.empty())
		  error_quit("ERROR: RandomDummySession::config(), HELLO_PEER_NHI and HELLO_PEER_IP both defined.");
	  else if(hello_peer_nhi.empty() && hello_peer_ip.empty())
		  error_quit("ERROR: RandomDummySession::config(), missing HELLO_PEER_NHI and HELLO_PEER_IP attribute.");

	  RANDOM_DUMMY_DUMP(printf("A random dummy session is configured in host of NHI=\"%s\":\n", inHost()->nhi.toString()););
	  RANDOM_DUMMY_DUMP(printf("=> hello message: \"%s\".\n", hello_message.c_str()););
	  if(!hello_peer_nhi.empty())
	  {
		  RANDOM_DUMMY_DUMP(printf("=> hello peer: nhi=\"%s\".\n", hello_peer_nhi.c_str()););
	  }
	  else
	  {
		  RANDOM_DUMMY_DUMP(printf("=> hello peer: ip=\"%s\".\n", hello_peer_ip.c_str()););
	  }

    // parse jitter
	  double jitter_double;
	  str = (char*)cfg->findSingle("jitter");
	  if(!str) jitter_double = 0;
	  else if(s3f::dml::dmlConfig::isConf(str))
	    error_quit("ERROR: RandomDummySession::config(), invalid jitter attribute.");
	  else 
      jitter_double = atof(str);
	  if(jitter_double < 0)
	    error_quit("ERROR: RandomDummySession::config(), jitter needs to be non-negative.");
	  jitter = inHost()->d2t(jitter_double, 0);
	  RANDOM_DUMMY_DUMP(printf("jitter = %ld\n", jitter););
  }
  RANDOM_DUMMY_DUMP(printf("=> hello interval: %ld.\n", hello_interval););
}

void RandomDummySession::init()
{
  // the same method at the parent class must be called
  ProtocolSession::init();

  // initialize the session-related variables here
  RANDOM_DUMMY_DUMP(printf("Random dummy session is initialized.\n"));
  pkt_seq_num = 0;

  // we couldn't resolve the NHI to IP translation in config method,
  // but now we can, since name service finally becomes functional
  if(!hello_peer_nhi.empty())
  {
    // host -> net -> name service
  	Host* owner_host = inHost();
  	Net* owner_net = owner_host->inNet();
  	hello_peer = owner_net->getNameService()->nhi2ip(hello_peer_nhi);
  }
  else 
    hello_peer = IPPrefix::txt2ip(hello_peer_ip.c_str());

  // similarly we couldn't resolve the IP layer until now
  ip_session = (IPSession*)inHost()->getNetworkLayerProtocol();
  if(!ip_session) error_quit("ERROR: can't find the IP layer; impossible!");

  Host* owner_host = inHost();
  callback_proc = new Process( (Entity *)owner_host, (void (s3f::Entity::*)(s3f::Activation))&RandomDummySession::callback);

  //hello interval = 0 means the dummy session only listens to messages
  if(hello_interval == 0) return;

  ltime_t wait_time = (ltime_t)getRandom()->Uniform(hello_interval-jitter, hello_interval+jitter);

  //schedule the first hello message
  dcac = new ProtocolCallbackActivation(this);
  Activation ac (dcac);
  HandleCode h = owner_host->waitFor( callback_proc, ac, wait_time, owner_host->tie_breaking_seed);
}

void RandomDummySession::callback(Activation ac)
{
  RandomDummySession* ds = (RandomDummySession*)((ProtocolCallbackActivation*)ac)->session;
  ds->callback_body(ac);
}

void RandomDummySession::callback_body(Activation ac)
{
  RandomDummyMessage* dmsg = new RandomDummyMessage(hello_message);
  Activation dmsg_ac(dmsg);

  IPPushOption ipopt;
  ipopt.dst_ip = hello_peer;
  ipopt.src_ip = IPADDR_INADDR_ANY;
  ipopt.prot_id = S3FNET_PROTOCOL_TYPE_RANDOM_DUMMY;
  ipopt.ttl = DEFAULT_IP_TIMETOLIVE;

  RANDOM_DUMMY_DUMP(char s[32]; printf("[%s] Random dummy session sends hello: \"%s\" to %s \n",
		  getNowWithThousandSeparator(), hello_message.c_str(),
		  IPPrefix::ip2txt(hello_peer, s)));

  ip_session->pushdown(dmsg_ac, this, (void*)&ipopt, sizeof(IPPushOption));

  // next hello interval time
  ltime_t wait_time = (ltime_t)getRandom()->Uniform(hello_interval - jitter, hello_interval + jitter);
  HandleCode h = inHost()->waitFor( callback_proc, ac, wait_time, inHost()->tie_breaking_seed );
}

int RandomDummySession::control(int ctrltyp, void* ctrlmsg, ProtocolSession* sess)
{
  switch(ctrltyp)
  {
  	case RANDOM_DUMMY_CTRL_COMMAND1:
  	case RANDOM_DUMMY_CTRL_COMMAND2:
  	  return 0; // dummy control commands, we do nothing here
  	default:
  	  return ProtocolSession::control(ctrltyp, ctrlmsg, sess);
  }
}

int RandomDummySession::push(Activation msg, ProtocolSession* hi_sess, void* extinfo, size_t extinfo_size)
{
  error_quit("ERROR: a message is pushed down to the random dummy session from protocol layer above; it's impossible.\n");
  return 0;
}

int RandomDummySession::pop(Activation msg, ProtocolSession* lo_sess, void* extinfo, size_t extinfo_size)
{
  RANDOM_DUMMY_DUMP(printf("A message is popped up to the random dummy session from the IP layer.\n"));
  char* pkt_type = "dummy";

  //check if it is a dummy packet 
  ProtocolMessage* message = (ProtocolMessage*)msg;
  if(message->type() != S3FNET_PROTOCOL_TYPE_RANDOM_DUMMY)
  {
	  error_quit("ERROR: the message popup to random dummy session is not S3FNET_PROTOCOL_TYPE_RANDOM_DUMMY.\n");
  }

  // the protocol message must be of RandomDummyMessage type, and the extra info must be of IPOptionToAbove type
  RandomDummyMessage* dmsg = (RandomDummyMessage*)msg;
  IPOptionToAbove* ipopt = (IPOptionToAbove*)extinfo;

  RNG rng;
  long will_drop = rng.Bernoulli_idf();
  RANDOM_DUMMY_DUMP(printf("RNG generate Bernoulli Number %d\n", will_drop);)

  if (will_drop == 0)
  {
    RANDOM_DUMMY_DUMP(
      char buf1[32]; char buf2[32];
      printf("[%s] Random ummy session receives hello: \"%s\" (ip_src=%s, ip_dest=%s)\n",
      getNowWithThousandSeparator(), dmsg->hello_message.c_str(),
      IPPrefix::ip2txt(ipopt->src_ip, buf1), IPPrefix::ip2txt(ipopt->dst_ip, buf2)); 
    );

  } else {
    RANDOM_DUMMY_DUMP(
      char buf1[32]; char buf2[32];
      printf("[%s] Random ummy session drops hello: \"%s\" (ip_src=%s, ip_dest=%s)\n",
      getNowWithThousandSeparator(), dmsg->hello_message.c_str(),
      IPPrefix::ip2txt(ipopt->src_ip, buf1), IPPrefix::ip2txt(ipopt->dst_ip, buf2)); 
    );
  }

  dmsg->erase_all();

  // returning 0 indicates success
  return 0;
}

}; // namespace s3fnet
}; // namespace s3f
