/**
 * \file simctrl.h
 * \brief header file for the abstract VE controller
 *
 * authors : Yuhao Zheng
 *           Dong (Kevin) Jin
 */

#ifndef __VTIME_SIMCTRL_H__
#define __VTIME_SIMCTRL_H__

#ifndef __S3F_H__
#error "simctrl.h can only be included by s3f.h"
#endif

#include <linux/vtime_user.h>

#define EMULATION_LOCAL 0
#define EMULATION_DISTRIBUTED 1

#define LOOKAHEAD_NONE  0
#define LOOKAHEAD_ANN   1


/**
 * Class of a single emulation timer, for VE controller use.
 */
class EmuTimer
{
public:
	/** process ID */
	int pid;

	/** timer due time */
	ltime_t due;

	/** timer repeat interval */
	ltime_t repeat;

	/** operator for sorting */
	inline bool operator < (const EmuTimer b) const { return due > b.due; }
};


/**
 * Class of a single emulation packet, for VE controller use.
 * This class contains timestamp and memory space of the emulation packet.
 *
 * - In case of unicast, S3FNet can simply take the reference (pointer)
 * from VE controller, and pass it back on packet delivery, without
 * worrying about memory leak.
 *
 * - However, if a packet is dropped in S3FNet, it should be deleted by S3FNet
 * to avoid memory leak.
 *
 * - In case of multicast, S3FNet should use duplicate() to generate extra
 * copies of this packet.
 */
class EmuPacket
{
private:
	/** preset type check DNA number */
	static const int DNA = 11223344;

	/** DNA number for class identification */
	int dna;
public:
	/** timestamp of this packet, in virtual time, for both send and receive */
	ltime_t timestamp;

	/** packet length in bytes */
	int len;

	/** packet payload pointer */
	unsigned char *data;

	/** which interface of a VE does this packet belong, for both send and receive */
	int ifid;

	/** type checker: test whether a pointer is an instance of this class */
	static bool check(void *ptr);

	/** contructor */
	EmuPacket(int len);

	/** destructor */
	virtual ~EmuPacket();

	/** generate an extra copy of this packet, for multicast */
	EmuPacket* duplicate();
};


/**
 * Class of a single VE handle, for VE controller use.
 * This class contains all the information of a single emulation VE.
 *
 * - The sendq contains packets sent by this VE (with sending timestamp)
 * in the last emulation cycle. It should be taken and cleared by S3FNet
 * every time after an emulation cycle is done. If a packet is dropped in
 * S3FNet, it should be deleted by S3FNet to avoid memory leak.
 *
 * - The recvq contains packets to be delivered to this VE in the future,
 * (with delivery timestamp), and the VE controller will deliver them
 * according to the timestamp. This queue should be filled by the S3FNet before
 * an emulation cycle is called. In addition, it must be filled in ascending
 * order of delivery timestamp, as VE controller won't sort again for efficiency.
 *
 * - When enabled, emulation lookahead of this VE is provided at the end of an
 * emulation cycle. It is a predicted period within which this VE will not
 * send a packet. By exploiting lookahead, we may achieve higher execution
 * speed. However, emulatino lookahead could be wrong and may affect accuracy.
 * More details are provided in our WSC'13 paper.
 *
 * - When emulation lookahead is disabled, mindelay should be set by S3FNet.
 * This is an optimization for synchronization between simulation and emulation.
 * More details are provided in our JOS'13 paper.
 */
class VEHandle
{
public:
	/** constructor */
	VEHandle(int veid, int la_type, int nif);

	/** destructor */
	virtual ~VEHandle();

	/** get absolute ID of this VE */
	inline envid_t get_id() { return id; }

	/** get the total number of interfaces of this VE */
	inline int get_nif() { return nif; }

	/** get the current virtual time of this VE */
	inline ltime_t get_clock() { return clock; }

	/** get the last virtual time, for statistic purposes */
	inline ltime_t get_lastc() { return lastclock; }

	/** packet send queue, captured by the VE controller and for simulation use */
	deque<EmuPacket*> sendq;

	/** packet receive queue, for packet delivery to this VE */
	deque<EmuPacket*> recvq;

	/** timer list, maintains all active timers of this VE */
	priority_queue<EmuTimer> timerq;

	/** interface ID for pcap_dispatch() use */
	int nowif;

	/** flag: is this VE currently idle? */
	bool idle;

	/** emulation lookahead */
	ltime_t lookahead;

	/** emulation lookahead handle */
	void *la;

	/** timeline which the VE is aligned to, for S3FNet use */
	Timeline* tl;

private:
	/* friend class declarations */
	friend class SimCtrl;
	friend class SSimCtrl;
	friend class DSimCtrl;
	friend class VESlave;

	/** absolute ID of this VE */
	int id;

	/** how many interfaces does this VE have */
	int nif;

	/** current virtual time */
	ltime_t clock;

	/** virtual time of last emulation cycle, for statistic use */
	ltime_t lastclock;

	/** average runtime per release */
	ltime_t avgrun;

	/** for how long this VE has been idle */
	int idlecount;

	/** runtime offset adjustment */
	ltime_t offset;

	/** outgoing packet minimum delay of this VE, provided by S3FNet */
	ltime_t mindelay;

	/** pcap handles, one interface each */
	vector<void*> pcap;
};


/**
 * Class of the VE controller, previously called Sim/Control.
 * This class handles the emulation subsystem for the S3F/S3FNet.
 * This class is an abstract class which defines the function call interfaces.
 *
 * - Before emulation, emu_reset() and emu_init() should be called exactly once.
 * This makes the emulation VEs enter virtual time mode.
 *
 * - After emulation, emu_done() should be called exactly once. This makes the
 * emulation VEs return to normal mode.
 *
 * - During the emulation, emu_cycle() can be called to advance VEs to a barrier.
 * When lookahead is enabled, emulation clock will stop exactly at the barrier.
 * However, when lookahead is disabled, emulation may stop before the barrier,
 * according to the optimized scheduling algorithm presented in our JOS'13 paper.
 *
 * - Before calling emu_cycle(), packets to be delivered should be put in recvq.
 * After emu_cycle() returns, packets should be picked up from sendq.
 */
class SimCtrl
{
public:
	/** constructor */
	SimCtrl();

	/** destructor */
	virtual ~SimCtrl();

	/** obtain concrete VE controller handle, for S3F/S3FNet use
	  * @param type emulation type, either EMULATION_LOCAL or EMULATION_DISTRIBUTED
	  * @return a concrete VE controller handle */
	static SimCtrl* get_simctrl(int type = EMULATION_LOCAL);

	/** translate error code into human readable text 
	  * @param code error code
	  * @return error text in string */
	static const char* errcode2txt(int code);

	/** simulation interface pointer */
	Interface* siminf;

	/** inject emulation events to S3FNet */
	void inject_emu_event_to_s3fnet();

	/* ========== function call interfaces ========== */

	/** set VE boundary for emulation, must be called before emu_init()
	  * @param idbase start ID of this emulation setup
	  * @param nve total number of VEs in this emulation setup
	  * @return 0 if succeed, negative error code otherwise */
	virtual int set_ve_boundary(int idbase, int nve) = 0;

	/** get start ID of this emulation setup
	  * @return start ID of this emulation setup */
	virtual int get_idbase() = 0;

	/** get total number of VEs in this emulation setup
	  * @return total number of VEs in this emulation setup */
	virtual int get_nve() = 0;

	/** get a VE handle by its VE ID
	  * @param ctid VE ID
	  * @return pointer to VE handle, or NULL for invalid ctid */
	virtual VEHandle* get_vehandle(int ctid) = 0;

	/** after emu_init() and for no lookahead only, set the mindelay of a given VE
	  * @param ctid VE ID
	  * @param delay mindelay
	  * @return 0 if succeed, negative error code otherwise */
	virtual int set_ve_mindelay(int ctid, ltime_t delay) = 0;

	/** get mindelay of a given VE
	  * @param ctid VE ID
	  * @return mindelay of this VE */
	virtual ltime_t get_ve_mindelay(int ctid) = 0;

	/** set the number of interfaces of a VE, should be set before emu_init()
	  * @param ctid VE ID
	  * @param nif nubmer of interfaces of this VE
	  * @return 0 if succeed, negative error code otherwise */
	virtual int set_ve_nif(int ctid, int nif) = 0;

	/** get the number of interfaces of a given VE
	  * @param ctid VE ID
	  * @return nubmer of interfaces of this VE, or negative error code for invalide ctid */
	virtual int get_ve_nif(int ctid) = 0;

	/** set the IP address of an interface, should be called before emu_init()
	  * @param ctid VE ID
	  * @param ifid interface ID
	  * @param ip IP address in string
	  * @return 0 if succeed, negative error code otherwise */
	virtual int set_ve_ifip(int ctid, int ifid, const char* ip) = 0;

	/** execute a command on a VE for experiment purposes, should be called after emu_init()
	  * @param ctid VE ID
	  * @param cmd command to execute
	  * @return 0 if succeed, negative error code otherwise */
	virtual int exec_ve_command(int ctid, const char *cmd) = 0;

	/** get current emulation time
	  * @return current emulation time */
	virtual ltime_t get_clock() = 0;

	/** emulation reset, recommended to be called before emu_init and clean any errors
	  * @param close for distributed emulation only, whether to close the connection
	  * @return 0 if succeed, negative error code otherwise */
	virtual int emu_reset(bool close = false) = 0;

	/** emulation initialization, should be called before emu_cycle()
	  * @param la_type lookahead type, either LOOKAHEAD_NONE or LOOKAHEAD_ANN
	  * @return 0 if succeed, negative error code otherwise */
	virtual int emu_init(int la_type = LOOKAHEAD_NONE) = 0;

	/** emulation cycle: advance all VEs to a given barrier
	  * @param barrier barrier in virtual time (may stop before it if using LOOKAHEAD_NONE)
	  * @return 0 if succeed, negative error code otherwise */
	virtual int emu_cycle(ltime_t barrier) = 0;

	/** emulation clean up, should be called after emulation is over
	  * @return 0 if succeed, negative error code otherwise */
	virtual int emu_done() = 0;

};


#include <simctrl/ssimctrl.h>
#include <simctrl/dsimctrl.h>

#endif /* __VTIME_SIMCTRL_H__ */

