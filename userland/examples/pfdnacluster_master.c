/*
 *
 * (C) 2012 - Luca Deri <deri@ntop.org>
 *            Alfredo Cardigliano <cardigliano@ntop.org>
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#define _GNU_SOURCE
#include <signal.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <sys/poll.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <net/ethernet.h>     /* the L2 protocols */
#include <sys/time.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "pfring.h"

#define ALARM_SLEEP             1

pfring *pd;
pfring_dna_cluster *dna_cluster_handle;

char *in_dev = NULL;
u_int8_t wait_for_packet = 1, do_shutdown = 0;
socket_mode mode = recv_only_mode;

static struct timeval startTime;

void bind2core(u_int core_id) {
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);

  if(pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0)
    fprintf(stderr, "Error while binding to core %u\n", core_id);
}

/* *************************************** */

double delta_time (struct timeval * now,
		   struct timeval * before) {
  time_t delta_seconds;
  time_t delta_microseconds;

  delta_seconds      = now -> tv_sec  - before -> tv_sec;
  delta_microseconds = now -> tv_usec - before -> tv_usec;

  if(delta_microseconds < 0) {
    delta_microseconds += 1000000;  /* 1e6 */
    -- delta_seconds;
  }
  return((double)(delta_seconds * 1000) + (double)delta_microseconds/1000);
}

/* ******************************** */

void print_stats() {
  struct timeval endTime;
  double deltaMillisec;
  static u_int8_t print_all;
  static struct timeval lastTime;
  char buf0[64], buf1[64], buf2[64];
  u_int64_t RXdiff, TXdiff, RXProcdiff;
  static u_int64_t lastRXPkts = 0, lastTXPkts = 0, lastRXProcPkts = 0;
  unsigned long long nRXPkts = 0, nTXPkts = 0, nRXProcPkts = 0;
  u_int64_t rx_packets, tx_packets, rx_processed_packets;

  if(startTime.tv_sec == 0) {
    gettimeofday(&startTime, NULL);
    print_all = 0;
  } else
    print_all = 1;

  gettimeofday(&endTime, NULL);
  deltaMillisec = delta_time(&endTime, &startTime);

  if(dna_cluster_stats(dna_cluster_handle, &rx_packets, &tx_packets, &rx_processed_packets) == 0) {
    nRXPkts  = rx_packets;
    nTXPkts  = tx_packets;
    nRXProcPkts  = rx_processed_packets;

    fprintf(stderr, "---\nAbsolute Stats:");
 
    if (mode != send_only_mode) {
      fprintf(stderr, " RX %s pkts", pfring_format_numbers((double)nRXPkts, buf1, sizeof(buf1), 0));
      if(print_all) fprintf(stderr, " [%s pkt/sec]", pfring_format_numbers((double)(nRXPkts*1000)/deltaMillisec, buf1, sizeof(buf1), 1));
      
      fprintf(stderr, " RX Processed %s pkts", pfring_format_numbers((double)nRXProcPkts, buf1, sizeof(buf1), 0));
        if(print_all) fprintf(stderr, " [%s pkt/sec]", pfring_format_numbers((double)(nRXProcPkts*1000)/deltaMillisec, buf1, sizeof(buf1), 1));
    }
	   
    if (mode != recv_only_mode) {
      fprintf(stderr, " TX %s pkts", pfring_format_numbers((double)nTXPkts, buf1, sizeof(buf1), 0));
      if(print_all) fprintf(stderr, " [%s pkt/sec]", pfring_format_numbers((double)(nTXPkts*1000)/deltaMillisec, buf1, sizeof(buf1), 1));
    }
	        
    fprintf(stderr, "\n");

    if(print_all && (lastTime.tv_sec > 0)) {
      deltaMillisec = delta_time(&endTime, &lastTime);
      RXdiff = nRXPkts - lastRXPkts;
      TXdiff = nTXPkts - lastTXPkts;
      RXProcdiff = nRXProcPkts - lastRXProcPkts;

      fprintf(stderr, "Actual Stats:  ");

      if (mode != send_only_mode) {
        fprintf(stderr, " RX %s pkts [%s ms][%s pps]",
	        pfring_format_numbers((double)RXdiff, buf0, sizeof(buf0), 0),
	        pfring_format_numbers(deltaMillisec, buf1, sizeof(buf1), 1),
	        pfring_format_numbers(((double)RXdiff/(double)(deltaMillisec/1000)),  buf2, sizeof(buf2), 1));
			   
        fprintf(stderr, " RX Processed %s pkts [%s ms][%s pps]",
	        pfring_format_numbers((double)RXProcdiff, buf0, sizeof(buf0), 0),
                pfring_format_numbers(deltaMillisec, buf1, sizeof(buf1), 1),
                pfring_format_numbers(((double)RXProcdiff/(double)(deltaMillisec/1000)),  buf2, sizeof(buf2), 1));
      }
						    
      if (mode != recv_only_mode) {
        fprintf(stderr, " TX %llu pkts [%s ms][%s pps]",
	        (long long unsigned int)TXdiff,
	        pfring_format_numbers(deltaMillisec, buf1, sizeof(buf1), 1),
                pfring_format_numbers(((double)TXdiff/(double)(deltaMillisec/1000)),  buf2, sizeof(buf2), 1));
      }

      fprintf(stderr, "\n");
    }

    lastRXPkts = nRXPkts;
    lastTXPkts = nTXPkts;
    lastRXProcPkts = nRXProcPkts;
  }

  lastTime.tv_sec = endTime.tv_sec, lastTime.tv_usec = endTime.tv_usec;
}

/* ******************************** */

void my_sigalarm(int sig) {
  if(do_shutdown)
    return;

  print_stats();
  alarm(ALARM_SLEEP);
  signal(SIGALRM, my_sigalarm);
}

/* ******************************** */

void sigproc(int sig) {
  static int called = 0;

  fprintf(stderr, "Leaving...\n");

  if(called) return; else called = 1;
  
  dna_cluster_disable(dna_cluster_handle);
  
  do_shutdown = 1;
}

/* *************************************** */

void printHelp(void) {
  printf("pfdnacluster_master\n(C) 2012 Deri Luca <deri@ntop.org>, Alfredo Cardigliano <cardigliano@ntop.org>\n\n");

  printf("pfdnacluster_master [-a] -i dev\n");
  printf("-h              Print this help\n");
  printf("-i <device>     Device name\n");
  printf("-c <cluster>    Cluster ID\n");
  printf("-n <num app>    Number of applications\n");
  printf("-s              Enable TX\n");
  printf("-r <core_id>    Bind the RX thread to a core\n");
  printf("-t <core_id>    Bind the TX thread to a core\n");
  printf("-a              Active packet wait\n");
  exit(0);
}

/* *************************************** */

int main(int argc, char* argv[]) {
  char c;
  u_int32_t version;
  int rx_bind_core = 0, tx_bind_core = 1;
  int cluster_id = -1, num_app = 1;

  startTime.tv_sec = 0;

  while((c = getopt(argc,argv,"ac:r:st:hi:n:")) != -1) {
    switch(c) {
    case 'a':
      wait_for_packet = 0;
      break;
    case 'r':
      rx_bind_core = atoi(optarg);
      break;
    case 't':
      tx_bind_core = atoi(optarg);
      break;
    case 'h':
      printHelp();      
      break;
    case 's':
      mode = send_and_recv_mode;
      break;
    case 'i':
      in_dev = strdup(optarg);
      break;
    case 'c':
      cluster_id = atoi(optarg);
      break;
    case 'n':
      num_app = atoi(optarg);
      break;
    }
  }

  if(in_dev == NULL || cluster_id < 0 || num_app < 1)  printHelp();

  printf("Reading packets from %s\n", in_dev);

  pd = pfring_open(in_dev, 1500 /* snaplen */, PF_RING_PROMISC);
  if(pd == NULL) {
    printf("pfring_open %s error [%s]\n", in_dev, strerror(errno));
    return(-1);
  }

  pfring_version(pd, &version);
  printf("Using PF_RING v.%d.%d.%d\n", (version & 0xFFFF0000) >> 16, 
	 (version & 0x0000FF00) >> 8, version & 0x000000FF);

  pfring_set_application_name(pd, "pfdnacluster_master");

  signal(SIGINT, sigproc);
  signal(SIGTERM, sigproc);
  signal(SIGINT, sigproc);

  signal(SIGALRM, my_sigalarm);
  alarm(ALARM_SLEEP);

  /* Create the DNA cluster */
  if ((dna_cluster_handle = dna_cluster_create(cluster_id, num_app)) == NULL) {
    fprintf(stderr, "Error creating DNA Cluster\n");
    return(-1);
  }
  
  /* Setting the cluster mode */
  dna_cluster_set_mode(dna_cluster_handle, mode);

  /* Add the ring we created to the cluster */
  if (dna_cluster_register_ring(dna_cluster_handle, pd) < 0) {
    fprintf(stderr, "Error registering rx socket\n");
    dna_cluster_destroy(dna_cluster_handle);
    return -1;
  }

  /* Setting up important details... */
  dna_cluster_set_wait_mode(dna_cluster_handle, !wait_for_packet /* active_wait */);
  dna_cluster_set_cpu_affinity(dna_cluster_handle, rx_bind_core, tx_bind_core);

  /*
    Let's use the standard distribution function that allows to balance
    per IP in a coherent mode (not like RSS that does not do that)
  */
  /* dna_cluster_set_distribution_function(dna_cluster_handle, func); */

  /* Now enable the cluster */
  if (dna_cluster_enable(dna_cluster_handle) < 0) {
    fprintf(stderr, "Error powering ON the engine\n");
    dna_cluster_destroy(dna_cluster_handle);
    return -1;
  }

  printf("The DNA cluster [id: %u][num slave apps: %u] is now running...\n", 
	 cluster_id, num_app);
  printf("You can now attach to DNA cluster up to %d slaves as follows:\n", num_app);
  printf("\tpfcount -i dnacluster:%d\n", cluster_id);

  while (!do_shutdown) sleep(1); /* do something in the main */
 
  dna_cluster_destroy(dna_cluster_handle);

  sleep(2);
  return(0);
}
