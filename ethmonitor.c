/*
 * Author: Matthew Ranostay <Matt_Ranostay@mentor.com>
 * Copyright (C) 2009 Mentor Graphics
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program  is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

//#define DEBUG 1

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <ethtool-copy.h>

#include <cutils/properties.h>
#include <private/android_filesystem_config.h>

int ifc_init();
void ifc_close();
int ifc_up(char *iname);
int ifc_down(char *iname);
int do_dhcp(char *iname);

static int thread_running = 0;

void dhcp_function(void *ptr)
{
	char buf[PROPERTY_KEY_MAX];
	char value[PROPERTY_VALUE_MAX];
	char *interface = (char *) ptr;

	thread_running = 1;

#ifdef DEBUG
	printf("Running DHCP request\n");
#endif

	do_dhcp(interface);

	sleep(1); /* beagleboard needs a second */

	/* DNS setting #1 */
	snprintf(buf, sizeof(buf), "net.%s.dns1", interface);
	property_get(buf, value, "");
	property_set("net.dns1", value);

	/* Report status of network connection */
	snprintf(buf, sizeof(buf), "net.%s.status", interface);
	property_set(buf, !!strcmp(value, "") ? "dhcp" : "up");

	/* DNS setting #2 */
	snprintf(buf, sizeof(buf), "net.%s.dns2", interface);
	property_get(buf, value, "");
	property_set("net.dns2", value);

	thread_running = 0;
}


int get_link_status(int fd, struct ifreq *ifr)
{
	struct ethtool_value edata;
	int err;

	edata.cmd = ETHTOOL_GLINK;
	ifr->ifr_data = (caddr_t)&edata;
	err = ioctl(fd, SIOCETHTOOL, ifr);

	if (err < 0) {
		return -EINVAL;
	} 

	return edata.data;
}

void monitor_connection(char *interface)
{
	struct ifreq ifr;

	char buf[PROPERTY_KEY_MAX];
	char value[PROPERTY_VALUE_MAX];

	int state = 0;
	int tmp_state = 0;
	int fd;

	pthread_t thread;


	while (1) {
		/* setup the control structures */
		memset(&ifr, 0, sizeof(ifr));
		strcpy(ifr.ifr_name, interface);

		fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (fd < 0) { /* this shouldn't ever happen */
			fprintf(stderr, "Cannot open control interface for %s.", interface);
			exit(errno);
		}

		tmp_state = get_link_status(fd, &ifr);

		if (tmp_state != state) { /* state changed */
			state = tmp_state;

			/* Used by JNI code to find current DHCP device */
			property_set("net.device", interface);

			snprintf(buf, sizeof(buf), "net.%s.status", interface);
			property_set(buf, state ? "up" : "down");

			if (thread_running && !tmp_state)
				exit(0); /* pthread_kill doesn't work correctly */

			if (state) { /* bring up connection */
#ifdef DEBUG
				printf("Connection up %s\n", interface);
#endif

				pthread_create (&thread, NULL, (void *) &dhcp_function, (void *) interface);

			} else { /* down connection */
#ifdef DEBUG
				printf("Connection down %s\n", interface);
#endif
			}

		}
		else {
			sleep(0.5); /* 500 milliseconds */
		}
	close(fd);
	}
}

int main(int argc, char *argv[])
{

	if (argc == 1) {
		printf("Usage: ./ethmonitor eth0\n");
		return 0;
	}
	if (argc != 2) {
		fprintf(stderr, "Invalid number of arguments\n");
		return -EINVAL;
	}

	if (ifc_init()) {
		fprintf(stderr, "ifc_init: Cannot perform requested operation\n");
		exit(EINVAL);
	}

	if (ifc_up(argv[1])) {
		fprintf(stderr, "ifc_up: Cannot bring the interface\n");
		return -EINVAL;
	}
	monitor_connection(argv[1]);

    ifc_close();

	return 0;
};
