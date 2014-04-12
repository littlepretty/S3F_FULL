/** \file lookahead.h
 * \brief header file for the emulation lookahead
 *
 * authors : Yuhao Zheng
 */

#ifndef __VTIME_LOOKAHEAD_H__
#define __VTIME_LOOKAHEAD_H__

#include <s3f.h>


/**
 * Class of an lookahead handle, for emulation lookahead calculation.
 * This class is an abstract class which defines the function call interfaces.
 *
 * - pre_cycle() is call at the beginning of an emulation cycle.
 * - post_cycle() is call at the end of an emulation cycle.
 * - pre_cycle() may be null, but post_cycle() must calculate a lookahead for S3F/S3FNet.
 */
class Lookahead
{
public:
	/** constructor */
	Lookahead(VEHandle *ve) { ve->la = this; this->ve = ve; pflog = NULL; }

	/** destructor */
	virtual ~Lookahead() { ve->la = NULL; }

	/** set log file pointer, for test/debug/experiment purposes */
	virtual void set_pflog(FILE *pf) { pflog = pf; }

	/* ========== function call interfaces ========== */

	/** to be called at the beginning of an emulation cycle */
	virtual void pre_cycle(ltime_t barrier) = 0;

	/** to be call at the end of an emulation cycle */
	virtual void post_cycle(ltime_t barrier) = 0;

	/* get prediction error */
	virtual double get_error() = 0;

protected:
	/** host VE */
	VEHandle *ve;

	/** log file pointer */
	FILE *pflog;
};


/**
 * Class of an artificial neural network lookahead handle.
 * This class is a concrete lookahead handle using artificial neural network.
 */
class LookaheadANN : public Lookahead
{
public:
	/** constructor */
	LookaheadANN(VEHandle *ve);

	/** destructor */
	virtual ~LookaheadANN();

	/** function call interface implementation, please refer to Lookahead */
	virtual void pre_cycle(ltime_t barrier);

	/** function call interface implementation, please refer to Lookahead */
	virtual void post_cycle(ltime_t barrier);

	/** function call interface implementation, please refer to Lookahead */
	virtual double get_error();

private:
	/* ========== private constant parameters ========== */

	/** max gap to filter outdated data */
	static const int inputfilter;

	/** max lookahead amount */
	static const int outputfilter;

	/** default lookahead amount */
	static const int outputdefault;

	/** number of data points in an entry */
	static const int npoint;

	/** number of features in a data point */
	static const int nfeature;

	/** number of ANN input nodes */
	static const int nanninput;

	/** number of ANN output nodes */
	static const int nannoutput;

	/** number of hidden layers of the ANN */
	static const int annlayers;

	/** number of nodes in each layer */
	static const int annlayersz[];

	/** min number of data points for training */
	static const int mintrainsetsz;

	/** max training epochs */
	static const int maxtrainepoch;

	/** error momentum factor */
	static const double errmomentum;

	/** error threshold to retrain the ANN */
	static const double retrainerr;

	/** validation interval */
	static const int validinterval;
	
	/* ========== data structure declaration ========== */

	struct point;
	struct entry;
	typedef struct entry entry;
	typedef struct point point;

	/* ========== private data members ========== */

	/** fast artificial neural network */
	struct fann *ann;

	/** packet send in the last cycle */
	deque<point*> sendbuf;

	/** packet recv in the last cycle */
	deque<point*> recvbuf;

	/** recent packet send/recv history */
	deque<point*> history;

	/** training set */
	deque<entry*> trainset;

	/** prediction error */
	double predicterr;

	/** last ANN prediction output */
	ltime_t lastpredict;

	/** validation interval counter */
	int validcount;

	/* ========== private helper functions ========== */

	/** record packet queue from VE */
	void record_packet(deque<EmuPacket*> const &pktq, ltime_t barrier, bool send);

	/** helper function to clear a deque */
	template<class type> void deque_clear(deque<type> &q);

	/** fill entry inputs from history */
	void entry_prepare(entry *ent);

	/** send entry for ANN training */
	void entry_train(entry *ent);

	/** send entry for ANN validation */
	int entry_valid(entry *ent, bool send);

	/** send entry for ANN prediction */
	ltime_t entry_predict(entry *ent);

	/** get saved ANN file name */
	char* ann_filename(char *fn, const char *type);

	/** ANN training */
	void ann_train();
};

#endif /* __VTIME_LOOKAHEAD_H__ */

