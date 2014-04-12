#!/usr/bin/env python

# argument
# 1. total timelines
# 2. total hosts
# 3. run time
# 4. propagation delay
# 5. min transfer delay
# 6. Bandwidth
# 7. sim_lookahead, true of false 

import sys

if (len(sys.argv)==2 and sys.argv[1] == "--help"):
	print "usage: total timelines | total hosts | run time | propagation delay | min transfer delay | Bandwidth | sim_lookahead"
	sys.exit(0)

#default values of the parameters
num_timeline = 2
num_host = 2
run_time = 100
min_delay = 1e-6 #1us
prop_delay = 1e-3 #1ms
bw = 1e6 #1Mb/s
sim_lh = "false"

#other parameters, currently constant
tick = 6
is_openvz = "true"
latency = 0
ve_id_base = 101

#parameters from command line
if(len(sys.argv) > 1):
	num_timeline = int(sys.argv[1])
if(len(sys.argv) > 2):
	num_host = int(sys.argv[2])
if(len(sys.argv) > 3):
	run_time = int(sys.argv[3])
if(len(sys.argv) > 4):
	prop_delay = float(sys.argv[4])
if(len(sys.argv) > 5):
	min_delay = float(sys.argv[5])
if(len(sys.argv) > 6):
	bw = float(sys.argv[6])
if(len(sys.argv) > 7):
	sim_lh = sys.argv[7]
if(len(sys.argv) > 8):
	print "error: too many arguments.\n"
	sys.exit(0)

print("total timelines " + str(num_timeline))
print("total hosts " + str(num_host))
print("run time " + str(run_time))
print("propagation delay " + str(prop_delay))
print("min transfer delay " + str(min_delay))
print("Bandwidth " + str(bw))
print("sim_lookahead " + sim_lh)

#output dml file
fh = open("test.dml", "w")

#Beginning
fh.write("total_timeline " + str(num_timeline) + "\n")
fh.write("tick_per_second " + str(tick) + "\n")
fh.write("enable_openvz_emulation " + is_openvz + "\n")
fh.write("run_time " + str(run_time) + "\n")
fh.write("Net [ ve_id_base " + str(ve_id_base) + " number_ve " + str(num_host) + " sim_lookahead " + sim_lh + "\n")

#Net
timeline_round = -1
for i in range(0, num_host):
	if(i%num_timeline == 0):
		timeline_round = timeline_round + 1
	if(timeline_round%2 == 0):
		fh.write("\t Net [ id " + str(i) + " alignment " + str(i%num_timeline) + "\n")
	if(timeline_round%2 == 1):
		fh.write("\t Net [ id " + str(i) + " alignment " + str(num_timeline - 1 - i%num_timeline) + "\n")
	
    	fh.write("\t\t host [ id 0 veid " + str(i) + " veip 192.168.84." + str(ve_id_base+i) + "\n")
	fh.write("\t\t\t graph [\n")
	fh.write("\t\t\t ProtocolSession [ name openvz_event use \"s3f.os.openvz_event\" ]\n")
	fh.write("\t\t\t ProtocolSession [ name ip use \"s3f.os.ip\" ]\n")
	fh.write("\t\t\t ]\n")
	
	fh.write("\t\t\t interface [ id 0 _extends .dict.iface ]\n")
	fh.write("\t\t ]\n")
  	fh.write("\t ]\n\n")

#Link
for i in range(0, num_host, 2):
	fh.write("\t link [ attach " + str(i) +":0(0) attach " + str(i+1) + ":0(0) prop_delay " + str(prop_delay) + " min_delay " + str(min_delay) + " ]\n")

#End
fh.write("]\n\n");
fh.write("dict [ iface [  bitrate " + str(bw) + " latency " + str(latency) +" ] ]"); 	

fh.close();

