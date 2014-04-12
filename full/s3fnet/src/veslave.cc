/* \file veslave.cc
 * \brief source file for the VE slave class.
 *
 * authors : Yuhao Zheng
 */

#include <s3f.h>

int main(int argc, char **argv)
{
	VESlave slave;
	if (argc > 1) slave.nthread = atoi(argv[1]);
	int ret = slave.slave_mainloop();
	if (ret) printf("error code = %s\n", SimCtrl::errcode2txt(ret));
	return 0;
}

