/** \file dsimctrl.h
 * \brief header file for distributed VE controller
 *
 * authors : Yuhao Zheng
 *           Dong (Kevin) Jin
 */

#ifndef __VTIME_DSIMCTRL_H__
#define __VTIME_DSIMCTRL_H__

#include <simctrl/simctrl.h>


/**
 * Class for a single VE slave, for both master side and slave side.
 * A VE slave is a bundle of VEs running on a remote machine.
 * The distributed VE controller can control several VE slaves at a time
 * to run distributed emulation.
 */
class VESlave
{
public:
	/** constructor */
	VESlave();

	/** destructor */
	virtual ~VESlave();

	/** master side connect slave */
	int master_connect();

	/** master side disconnect slave */
	int master_disconnect();

	/** master side set VE IP address */
	int master_setip(int ctid, int ifid, const char *ip);

	/** master side exec VE command */
	int master_exec(int ctid, const char *cmd);

	/** master side initialization */
	int master_init();

	/** master side clean up */
	int master_fini(bool close);

	/** master side push packets */
	int master_pushpacket();

	/** master side test */
	int master_test();

	/** master side cycle */
	int master_cycle(bool newcycle);

	/** check if previous call has finished */
	int master_checkstatus(bool *ready);

	/** slave side main function */
	int slave_mainloop();

	/** ip address of the slave */
	char *addr;

	/** start VE ID of this slave (static range) */
	int fromid;
	
	/** end VE ID of this slave (static range) */
	int toid;
	
	/** VE ID offset (veid_on_slave = veid_on_host - veid_offset) */
	int idoffset;

	/** flag: is this slave selected? (for master side use) */
	bool selected;

	/** start VE ID being selected of this emulation setup */
	int idbase;
	
	/** total number of VEs being selected in this emulation setup */
	int nve;

	/** VE handles */
	vector<VEHandle*> ve;

	/** VE mapping */
	vector<int> vemap;

	/** number of interfaces of all VEs */
	vector<int> venif;

	/** number of packets pushed to master */
	vector<int> pktcount;

	/** for simctrl cycle use*/
	vector<bool> done;

	/** type of app lookahead algorithm */
	int la_type;

	/** flag: at least one VE reaches the barrier? */
	bool onedone;
	
	/** flag: all VEs reach the barrier? */
	bool alldone;
	
	/** flag: has the barrier been updated since last call? */
	bool newbarrier;

	/** number of finished VEs */
	int ndone;

	/** barrier given by S3F/S3FNet */
	ltime_t barrier;
	
	/** barrier to release VE safely, subject to mindelay constrains */
	ltime_t maxadv;

	/** timeval representation of barrier */
	struct timeval tvbarrier;

	/** multi-thread number for lookahead prediction */
	int nthread;

	/** lookahead error */
	int laerr;

private:
	/** master side message handler */
	int master_setip_gotmsg(void *msg);

	/** master side message handler */
	int master_exec_gotmsg(void *msg);

	/** master side message handler */
	int master_init_gotmsg(void *msg);

	/** master side message handler */
	int master_fini_gotmsg(void *msg);

	/** master side message handler */
	int master_test_gotmsg(void *msg);

	/** master side message handler */
	int master_cycle_gotmsg(void *msg);

	/** slave side message handler */
	int slave_gotmsg_setip(void *msg);

	/** slave side message handler */
	int slave_gotmsg_exec(void *msg);

	/** slave side message handler */
	int slave_gotmsg_init(void *msg);

	/** slave side message handler */
	int slave_gotmsg_fini(void *msg);

	/** slave side message handler */
	int slave_gotmsg_close(void *msg);

	/** slave side message handler */
	int slave_gotmsg_packet(void *msg);

	/** slave side message handler */
	int slave_gotmsg_test(void *msg);

	/** slave side message handler */
	int slave_gotmsg_cycle(void *msg);

	/** VE slave state enumeration, for master side */
	enum state_t { DISCONNECTED, READY, ERROR, SETIP, INIT, FINI, TEST, EXEC, CYCLE };

	/** current state of master */
	state_t state;

	/** asynchronous message handler for master-slave communication */
	void *amh;

	/** has init been called? */
	bool isinit;
};


/**
 * Class for the distributed VE controller (master side).
 * This class implements all function call interfaces of SimCtrl.
 * This is the master side of the distributed version, and doesn't require OpenVZ kernel.
 */
class DSimCtrl : public SimCtrl
{
public:
	/** constructor */
	DSimCtrl();

	/** destructor */
	virtual ~DSimCtrl();

	/** function call interface implementation, please refer to SimCtrl */
	virtual int set_ve_boundary(int idbase, int nve);

	/** function call interface implementation, please refer to SimCtrl */
	virtual int get_idbase() { return idbase; }

	/** function call interface implementation, please refer to SimCtrl */
	virtual int get_nve() { return nve; }

	/** function call interface implementation, please refer to SimCtrl */
	virtual VEHandle* get_vehandle(int ctid);

	/** function call interface implementation, please refer to SimCtrl */
	virtual int set_ve_mindelay(int ctid, ltime_t delay);

	/** function call interface implementation, please refer to SimCtrl */
	virtual ltime_t get_ve_mindelay(int ctid);

	/** function call interface implementation, please refer to SimCtrl */
	virtual int set_ve_nif(int ctid, int nif);

	/** function call interface implementation, please refer to SimCtrl */
	virtual int get_ve_nif(int ctid);

	/** function call interface implementation, please refer to SimCtrl */
	virtual int set_ve_ifip(int ctid, int ifid, const char* ip);

	/** function call interface implementation, please refer to SimCtrl */
	virtual int exec_ve_command(int ctid, const char *cmd);

	/** function call interface implementation, please refer to SimCtrl */
	virtual ltime_t get_clock() { return barrier; }

	/** function call interface implementation, please refer to SimCtrl */
	virtual int emu_init(int la_type = LOOKAHEAD_NONE);

	/** function call interface implementation, please refer to SimCtrl */
	virtual int emu_cycle(ltime_t barrier);

	/** function call interface implementation, please refer to SimCtrl */
	virtual int emu_done();

	/** function call interface implementation, please refer to SimCtrl */
	virtual int emu_reset(bool close);

private:
	/** flag: has emu_init() been called? */
	bool isinit;

	/** start ID of this emulation setup */
	int idbase;

	/** total number of VEs in this emulation setup */
	int nve;

	/** emulation lookahead method */
	int la_type;

	/** total number of VE slaves */
	int nslave;

	/** current barrier value */
	ltime_t barrier;

	/** last barrier value */
	ltime_t lastbarrier;

	/** number of interfaces of all VEs */
	vector<int> venif;

	/** VE handles */
	vector<VEHandle*> ve;

	/** VE slave handles */
	vector<VESlave*> veslave;

	/** VE to its slave mapping */
	vector<int> findslave;
};

#endif /* __VTIME_DSIMCTRL_H__ */

