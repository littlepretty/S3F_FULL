/**
 * \file tcp_dump.cc
 * \brief Methods implementing tcp dump in the TCPSession class.
 *
 * authors : Dong (Kevin) Jin
 */

#ifdef S3FNET_TCP_DUMP

#include "os/tcp/tcp_session.h"
#include "os/tcp/tcp_message.h"

namespace s3f {
namespace s3fnet {

void TCPSession::dump_rex()
{
  if(tcp_master->fp_rex_dump)
  {
    /*fprintf(tcp_master->fp_rex_dump, "%6f  %f  %f  %f  %f  %d\n",
	    now().second(), rtt_measured*tcp_master->getSlowTimeout().second(),
	    (rtt_smoothed>>TCP_DEFAULT_RTT_SHIFT)*tcp_master->getSlowTimeout().second(),
	    timeout_value().second(), rxmit_timer_count.second(), socket);*/
    fprintf(tcp_master->fp_rex_dump, "%ld  %ld  %ld  %ld  %ld  %d\n",
		    getNow(), rtt_measured*tcp_master->getSlowTimeout(),
		    (rtt_smoothed>>TCP_DEFAULT_RTT_SHIFT)*tcp_master->getSlowTimeout(),
		    timeout_value(), rxmit_timer_count, socket);
    fflush(tcp_master->fp_rex_dump);
  }
}

void TCPSession::dump_rtt(byte mask)
{
  if(tcp_master->fp_rtt_dump)
  {
    /*fprintf(tcp_master->fp_rtt_dump, "%d  %6f  %f  %f  %d\n",
	    (mask&TCPMessage::TCP_FLAG_SYN) ? 0 : 
	    ((mask&TCPMessage::TCP_FLAG_ACK) ? 1 : 0),
	    now().second(), rtt_measured*tcp_master->getSlowTimeout().second(),
	    timeout_value().second(), socket);*/
	fprintf(tcp_master->fp_rtt_dump, "%d  %ld  %ld  %ld  %d\n",
		    (mask&TCPMessage::TCP_FLAG_SYN) ? 0 :
		    ((mask&TCPMessage::TCP_FLAG_ACK) ? 1 : 0),
		    getNow(), rtt_measured*tcp_master->getSlowTimeout(),
		    timeout_value(), socket);*/
    fflush(tcp_master->fp_rtt_dump);
  }
}

void TCPSession::dump_cwnd()
{
  if(tcp_master->fp_cwnd_dump)
  {
    /*fprintf(tcp_master->fp_cwnd_dump, "%6f %u %u %u %d\n", now().second(),
	    cwnd, ssthresh, rcvwnd_size, socket);*/
    fprintf(tcp_master->fp_cwnd_dump, "%ld %ld %ld %ld %d\n",
    		getNow(), cwnd, ssthresh, rcvwnd_size, socket);*/
	fflush(tcp_master->fp_cwnd_dump);
  }
}

void TCPSession::dump_sack(int num, uint32* buffer, bool is_send)
{
  if(tcp_master->fp_sack_dump)
  {
	fprintf(tcp_master->fp_sack_dump, "%ld %s ", getNow(), is_send?"_send":"_receive");

    for(int i=0; i<num; i++)
      fprintf(tcp_master->fp_sack_dump, "%u %u ", buffer[2*i], buffer[2*i+1]);

    fprintf(tcp_master->fp_sack_dump, "\n");
    fflush(tcp_master->fp_sack_dump);
  }
}

}; // namespace s3fnet
}; // namespace s3f

#endif /*S3FNET_TCP_DUMP*/
