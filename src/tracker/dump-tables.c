/*
 * $Id: dump-tables.c,v 1.1 2012/02/28 05:09:42 phil Exp $
 *
 * @COPYRIGHT@
 * @COPYRIGHT@
 *
 * $Log: dump-tables.c,v $
 * Revision 1.1  2012/02/28 05:09:42  phil
 * New tracker based on sqlite. Still testing
 *
 * Revision 1.4  2010/12/17 21:51:06  bruno
 * put a string in each executable that tells us when it was built.
 *
 * Revision 1.3  2010/03/15 23:05:56  bruno
 * tweaks
 *
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <curl/curl.h>
#include <httpd/httpd.h>
#include <netinet/in.h>
#include "tracker.h"
#include <sys/socket.h>
#include <arpa/inet.h>

static char builton[] = { "Built on: " __DATE__ " " __TIME__ };

extern int init(uint16_t *, char *, in_addr_t *, uint16_t *, char *, uint16_t *,
	in_addr_t *);
extern int init_tracker_comm(int);
extern int send_msg(int, in_addr_t *, uint16_t);

int
main()
{
	uint16_t	num_trackers;
	in_addr_t	trackers[MAX_TRACKERS];
	uint16_t	maxpeers;
	uint16_t	num_pkg_servers;
	in_addr_t	pkg_servers[MAX_PKG_SERVERS];
	int		i;
	int		sockfd;
	char		trackers_url[256];
	char		pkg_servers_url[256];
	char		buf[256];
	FILE		*file;

	sprintf(trackers_url,"127.0.0.1");

	sprintf(pkg_servers_url,"127.0.0.1");

	fprintf(stderr, "main:trackers_url (%s)\n", trackers_url);
	fprintf(stderr, "main:pkg_servers_url (%s)\n", pkg_servers_url);
	
	if (init(&num_trackers, trackers_url, trackers, &maxpeers,
			pkg_servers_url, &num_pkg_servers, pkg_servers) != 0) {
		fprintf(stderr, "main:init failed\n");
		return(-1);
	}

	if ((sockfd = init_tracker_comm(0)) < 0) {
		fprintf(stderr, "main:init_tracker_comm failed\n");
		return(-1);
	}

	for (i = 0 ; i < num_trackers; ++i) {
		send_msg(sockfd, &trackers[i], DUMP_TABLES);
	}

	return(0);
}

