/** \file lookahead.cc
 * \brief source file for the emulation lookahead
 *
 * authors : Yuhao Zheng
 */

#include <simctrl/lookahead.h>

#include "fann.h"
#include "pktheader.h"

#define SLAVE_LOG

/* ANN parameters */

/* max gap to filter outdated data */
const int LookaheadANN::inputfilter = 100 * 1000 * 1000;

/* max lookahead amount */
const int LookaheadANN::outputfilter = 10 * 1000 * 1000;

/* default lookahead amount */
const int LookaheadANN::outputdefault = 1000;

/* # of data points in an entry */
const int LookaheadANN::npoint = 50;

/* # of features in a data point */
const int LookaheadANN::nfeature = 3;

/* # of ANN input nodes */
const int LookaheadANN::nanninput = LookaheadANN::npoint * (LookaheadANN::nfeature + 1);

/* # of ANN output nodes */
const int LookaheadANN::nannoutput = 1;

/* # of hidden layers of the ANN */
const int LookaheadANN::annlayers = 4;

/* # of nodes in each layer */
const int LookaheadANN::annlayersz[LookaheadANN::annlayers] = {nanninput, 30, 30, nannoutput};

/* min # of data points for training */
const int LookaheadANN::mintrainsetsz = 300;

/* max training epochs */
const int LookaheadANN::maxtrainepoch = 1000;

/* error momentum factor */
const double LookaheadANN::errmomentum = 0.99;

/* error threshold to retrain the ANN */
const double LookaheadANN::retrainerr = 1e6;

/* validation interval */
const int LookaheadANN::validinterval = 1;


/* data structure definition */
struct LookaheadANN::point
{
	ltime_t timestamp;
	fann_type feature[nfeature];
};

struct LookaheadANN::entry
{
	fann_type input[nanninput];
	fann_type output[nannoutput];
};


/* LookaheadANN constructor */
LookaheadANN::LookaheadANN(VEHandle *ve) : Lookahead(ve)
{
	/* read trained neural network */
	char fn[100];
	ann = fann_create_from_file(ann_filename(fn, "ann"));
	if (ann) {
		/* match test */
		bool match = true;
		match = match && (nanninput == (int)fann_get_num_input(ann));
		match = match && (nannoutput == (int)fann_get_num_output(ann));
		if (!match) {
			fann_destroy(ann);
			ann = NULL;
		}
	}
	predicterr = 0.0;
	validcount = 0;
	lastpredict = 0;
}


/* LookaheadANN destructor */
LookaheadANN::~LookaheadANN()
{
	fann_destroy(ann);
	deque_clear<point*>(sendbuf);
	deque_clear<point*>(recvbuf);
	deque_clear<point*>(history);
	deque_clear<entry*>(trainset);
}


/* update lookahead information */
void LookaheadANN::pre_cycle(ltime_t barrier)
{
	/* record recv packet into recv buffer */
	record_packet(ve->recvq, barrier, false);
}


/* predict lookahead amount */
void LookaheadANN::post_cycle(ltime_t barrier)
{
	/* step 1: record send packet into send buffer */
	record_packet(ve->sendq, barrier, true);

	bool historychanged = false;

	/* step 2: process new send/recv packets */
	while (!sendbuf.empty() || !recvbuf.empty())
	{
		/* find the packet with smallest timestamp */
		bool bsend;
		if (sendbuf.empty()) bsend = false;
		else if (recvbuf.empty()) bsend = true;
		else if (sendbuf.front()->timestamp > recvbuf.front()->timestamp) bsend = false;
		else bsend = true;

		point *pt;
		if (bsend) {
			pt = sendbuf.front();
			sendbuf.pop_front();
		}
		else {
			pt = recvbuf.front();
			recvbuf.pop_front();
		}

		/* check for old history points expiration */
		if (!history.empty() && pt->timestamp - history.back()->timestamp > inputfilter)
			history.clear();

		/* form a new lookahead entry if have enough history data */
		if (history.size() == npoint + 1) {
			entry *ent = new entry;
			entry_prepare(ent);
			ent->output[0] = -3.0 + log10(
				max((ltime_t)1, pt->timestamp-history.back()->timestamp));

			/* send the new entry for training or validation */
			if (ann) entry_valid(ent, bsend);
			else entry_train(ent);
		}

		/* push the packet into history */
		history.push_back(pt);
		historychanged = true;
		if (history.size() > npoint + 1) {
			pt = history.front();
			history.pop_front();
			delete pt;
		}
	}

	/* step 3: predit lookahead amount */
	ltime_t advance = history.empty() ? inputfilter + 1 :
				ve->get_clock() - history.back()->timestamp;
	if (advance > inputfilter) {
		ve->lookahead = outputdefault;
	}
	else if (!ann || history.size() < npoint + 1) {
		ve->lookahead = 0;
	}
	else {
		if (historychanged) {
			entry *ent = new entry;
			entry_prepare(ent);
			lastpredict = entry_predict(ent);
		}
		ve->lookahead = max( (ltime_t)0, lastpredict - advance );
		ve->lookahead = min( (ltime_t)outputfilter, ve->lookahead );
	}

	/* step 4: check VE idle information for potential larger lookahead */
	if (ann && ve->idle) {
		ltime_t firstevent = -1;
		if (!ve->timerq.empty()) firstevent = ve->timerq.top().due;
		if (!ve->recvq.empty()) {
			ltime_t temp = ve->recvq.front()->timestamp;
			if (firstevent < 0 || temp < firstevent) firstevent = temp;
		}
		ltime_t idlela = firstevent < 0 ? outputdefault : firstevent - ve->get_clock();
		ve->lookahead = max( ve->lookahead, idlela );
	}
}


/* for simctrl to get prediction error */
inline double LookaheadANN::get_error()
{
	return predicterr;
}


/* helper functino to clear a deque */
template<class type> void LookaheadANN::deque_clear(deque<type> &q)
{
	while (!q.empty()) {
		type item = q.front();
		q.pop_front();
		delete item;
	}
	deque<type>().swap(q);
}


/* record packet recv/send from VE */
void LookaheadANN::record_packet(deque<EmuPacket*> const &pktq, ltime_t barrier, bool send)
{
	for (size_t i = 0; i < pktq.size(); i++)
	{
		EmuPacket *pkt = pktq[i];
		if (pkt->timestamp > barrier) break;
		point *pt = new point;
		pt->timestamp = pkt->timestamp;

		/* extract features from packet */
		pt->feature[0] = send ? log10(pkt->len) : -log10(pkt->len);

		unsigned char *packet = pkt->data;
		sniff_ip *ip = (sniff_ip*)(packet + SIZE_ETHERNET);
		if (ip->ip_p != 6) { // not a TCP packet
			pt->feature[1] = -1;
			pt->feature[2] = -1;
		}
		else {
			int size_ip = IP_HL(ip) * 4; // TCP FIN/SYN flags
			sniff_tcp *tcp = (sniff_tcp*)(packet + SIZE_ETHERNET + size_ip);
			pt->feature[1] = (tcp->th_flags & TH_FIN) != 0;
			pt->feature[2] = (tcp->th_flags & TH_SYN) != 0;
		}

		/* push the packet into the buffer */
		if (send) sendbuf.push_back(pt);
		else recvbuf.push_back(pt);
	}
}

/* fill entry inputs from history */
void LookaheadANN::entry_prepare(entry *ent)
{
	assert(history.size() == npoint + 1);
	point *pt0 = history[0], *pt1;
	int k = 0;
	for (int i = 1; i <= npoint; i++) {
		pt1 = history[i];
		/* FIXME: use cache to avoid repeated calculation */
		ent->input[k++] = -3.0 + log10(
			max((ltime_t)1, pt1->timestamp - pt0->timestamp));
		for (int j = 0; j < nfeature; j++)
			ent->input[k++] = pt1->feature[j];
		pt0 = pt1;
	}
}


/* send entry for ANN training */
void LookaheadANN::entry_train(entry *ent)
{
	trainset.push_back(ent);
	if ((int)trainset.size() >= mintrainsetsz) ann_train();
}


/* send entry for ANN validation */
int LookaheadANN::entry_valid(entry *ent, bool send)
{
	validcount = (validcount + 1) % validinterval;
	if (validinterval > 1 && validcount) return 0;

	assert(ann);

	/* validate entry */
	fann_type *output = fann_run(ann, ent->input);
	ltime_t predict = (ltime_t)exp10(output[0] + 3.0);
	ltime_t correct = (ltime_t)exp10(ent->output[0] + 3.0);
	ltime_t diff = abs(predict - correct);

	int wrong = 1;
	if (!send && predict > correct) wrong = 0;
	else if (1) {
		if (diff < VTIME_VTIMESLICE) wrong = 0;
		else if (diff < correct / 10) wrong = 0;
	}
	double error = 1.0 * diff / correct;
	if (diff < VTIME_VTIMESLICE) error = 0.0;
	if (error > 1.0) error = 1.0;

	/* update prediction error and check for retraining */
	predicterr = errmomentum * predicterr + (1-errmomentum) * error;
#ifdef SLAVE_LOG
	if(pflog) fprintf(pflog, "%ld.%06ld\tve%d\tlook\t%ld\t%ld\n",
		ve->get_clock()/1000000, ve->get_clock()%1000000,
		ve->get_id(), correct, predict);
#endif
	if (predicterr > retrainerr) {
		printf("VE%d: ann needs retrain, predicterr=%f\n", ve->get_id(), predicterr);
		fann_destroy(ann);
		ann = NULL;
		predicterr = 0.0;
	}

	/* deallocate memory */
	delete ent;

	return wrong;
}


/* send entry for ANN prediction */
ltime_t LookaheadANN::entry_predict(entry *ent)
{
	assert(ann);
	fann_type *output = fann_run(ann, ent->input);
	ltime_t lookahead = (ltime_t)exp10(output[0] + 3.0);
	delete ent;
	return lookahead;
}


/* get saved ANN file name */
char* LookaheadANN::ann_filename(char *fn, const char *type)
{
	sprintf(fn, "ve%d.%s", ve->get_id(), type);
	return fn;
}


/* ANN training */
void LookaheadANN::ann_train()
{
	/* training parameters */
	static const double trainset_percent = 0.7;
	static const int valid_fail_count = maxtrainepoch / 10;
	static const int print_interval = 10;

	assert(!ann);
	assert((int)trainset.size() == mintrainsetsz);

	/* write trainset to file */

	char fn[100];
	ann_filename(fn, "train");
	int nentry = trainset.size();

	FILE *pf = fopen(ann_filename(fn, "train"), "w");
	fprintf(pf, "%d %d %d\n", nentry, nanninput, nannoutput);
	for (int i = 0; i < nentry; i++) {
		entry *ent = trainset[i];
		for (int j = 0; j < nanninput; j++) fprintf(pf, "%f ", ent->input[j]);
		fprintf(pf, "\n");
		for (int j = 0; j < nannoutput; j++) fprintf(pf, "%f ", ent->output[j]);
		fprintf(pf, "\n");
	}
	fclose(pf);


	/* read ANN trainset from file */
	struct fann_train_data *train_data;
	train_data = fann_read_train_from_file(fn);
	nentry = train_data->num_data;

	/* split data into train set and validation set */
	fann_shuffle_train_data(train_data);
	int ntrain = (int)(nentry * trainset_percent);
	struct fann_train_data *valid_data, *temp;
	valid_data = fann_subset_train_data(train_data, ntrain, nentry-ntrain);
	temp = fann_subset_train_data(train_data, 0, ntrain);
	fann_destroy_train(train_data);
	train_data = temp;

	/* ANN initialization */
	ann = fann_create_standard_array(annlayers, (unsigned int*)annlayersz);
	fann_set_training_algorithm(ann, FANN_TRAIN_RPROP);
	fann_set_activation_function_hidden(ann, FANN_SIGMOID_SYMMETRIC);
	fann_set_activation_function_output(ann, FANN_LINEAR);
	fann_init_weights(ann, train_data);

	ann_filename(fn, "ann");
	double minvalidmse = 1e308;
	int failcount = 0;
	
	/* ANN training */
	printf("VE%d: training ANN\n", ve->get_id());
	for (int epoch = 1; epoch <= maxtrainepoch && failcount < valid_fail_count; epoch++)
	{
		double trainmse = fann_train_epoch(ann, train_data);
		double validmse = fann_test_data(ann, valid_data);
		if (validmse < minvalidmse - 1e-10) {
			failcount = 0;
			minvalidmse = validmse;
			fann_save(ann, fn);
		}
		else {
			failcount++;
		}
		if (epoch % print_interval == 0 || failcount == valid_fail_count)
			printf("epoch=%d trainmse=%.6f validmse=%.6f bestmse=%.6f\n",
				epoch, trainmse, validmse, minvalidmse);
	}
	printf("VE%d: training ANN done\n", ve->get_id());

	/* read the saved (best) ANN from file */
	fann_destroy(ann);
	ann = fann_create_from_file(fn);

	/* training done */
	fann_reset_MSE(ann);
	predicterr = 0.0;
	deque_clear<entry*>(trainset);
	assert(trainset.size() == 0);
	fann_destroy_train(train_data);
	fann_destroy_train(valid_data);
}

