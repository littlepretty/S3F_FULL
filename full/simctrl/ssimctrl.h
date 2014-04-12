/** \file ssimctrl.h
 * \brief header file for the single-machine VE controller
 *
 * authors : Yuhao Zheng
 *           Dong (Kevin) Jin
 */

#ifndef __VTIME_SSIMCTRL_H__
#define __VTIME_SSIMCTRL_H__

#include <simctrl/simctrl.h>

/**
 * Class for the single-machine VE controller.
 * This class implements all function call interfaces of SimCtrl.
 * As this is a single-machine version, it requires virtual-time OpenVZ kernel.
 */
class SSimCtrl : public SimCtrl
{
public:
	/** constructor */
	SSimCtrl();

	/** destructor */
	virtual ~SSimCtrl();

	/* ========== function call interfaces implementations ========== */

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
	virtual inline ltime_t get_clock() { return barrier; }

	/** function call interface implementation, please refer to SimCtrl */
	virtual int emu_init(int la_type);

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

	/** current barrier value */
	ltime_t barrier;

	/** last barrier value */
	ltime_t lastbarrier;

	/** VE mapping for system calls, as VEs in OpenVZ kernel may order arbitrarily */
	vector<int> vemap;

	/** number of interfaces of all VEs */
	vector<int> venif;

	/** VE handles */
	vector<VEHandle*> ve;
};

#endif /* __VTIME_SSIMCTRL_H__ */

