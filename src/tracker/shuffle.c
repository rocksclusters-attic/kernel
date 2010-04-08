#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/time.h>
#include "tracker.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <mysql/mysql.h>

char *
gethostattr(char *ip, char *attr)
{
	MYSQL_ROW	row;
	MYSQL		mysql;
	MYSQL_RES	*result;
	char		query[1024];;
	char		*value = NULL;

	if (mysql_init(&mysql) == NULL) {
		fprintf(stderr, "gethostattr:mysql_init:failed: errno %d\n",
			errno);
	}

	if (mysql_options(&mysql, MYSQL_READ_DEFAULT_FILE,
			"/opt/rocks/etc/my.cnf") != 0) {

		fprintf(stderr, "gethostattr:mysql_options:failed: %s\n",
			mysql_error(&mysql));
	}

	if (mysql_real_connect(&mysql, "localhost", "apache", NULL, "cluster",
			0, NULL, 0) == NULL) {

		fprintf(stderr, "gethostattr:mysql_real_connect:failed: %s\n",
			mysql_error(&mysql));
	}

	/*
	 * first see if there is a host attribute
	 */
	sprintf(query, "select a.value from node_attributes a, nodes n, \
		networks net where net.ip = '%s' and net.name = n.name and \
		n.id = a.node and a.attr = '%s'", ip, attr);

	if (mysql_real_query(&mysql, query, strlen(query)) != 0) {
		fprintf(stderr, "gethostattr:mysql_real_query:failed: %s\n",
			mysql_error(&mysql));
	}

	if ((result = mysql_store_result(&mysql)) == NULL) {
		fprintf(stderr, "gethostattr:mysql_store_result:failed: %s\n",
			mysql_error(&mysql));
	}

	if ((row = mysql_fetch_row(result)) != NULL) {
		value = strdup(row[0]);
	}

	mysql_free_result(result);

	if (value != NULL) {
		mysql_close(&mysql);
		return(value);
	}

	/*
	 * there is no host attribute, see if there is an appliance attribute
	 */
	sprintf(query, "select a.value from appliance_attributes a, \
		nodes n, networks net, memberships m, appliances app \
		where net.ip = '%s' and net.name = n.name and \
		n.membership = m.id and m.appliance = app.id and \
		a.appliance = app.id and a.attr = '%s'", ip, attr);

	if (mysql_real_query(&mysql, query, strlen(query)) != 0) {
		fprintf(stderr, "gethostattr:mysql_real_query:failed: %s\n",
			mysql_error(&mysql));
	}

	if ((result = mysql_store_result(&mysql)) == NULL) {
		fprintf(stderr, "gethostattr:mysql_store_result:failed: %s\n",
			mysql_error(&mysql));
	}

	if ((row = mysql_fetch_row(result)) != NULL) {
		value = strdup(row[0]);
	}

	mysql_free_result(result);

	if (value != NULL) {
		mysql_close(&mysql);
		return(value);
	}

	/*
	 * there is no host and no appliance attribute, see if there is an
	 * OS attribute
	 */
	sprintf(query, "select a.value from os_attributes a, nodes n, \
		networks net where net.ip = '%s' and net.name = n.name and \
		a.os = n.os and a.attr = '%s'", ip, attr);

	if (mysql_real_query(&mysql, query, strlen(query)) != 0) {
		fprintf(stderr, "gethostattr:mysql_real_query:failed: %s\n",
			mysql_error(&mysql));
	}

	if ((result = mysql_store_result(&mysql)) == NULL) {
		fprintf(stderr, "gethostattr:mysql_store_result:failed: %s\n",
			mysql_error(&mysql));
	}

	if ((row = mysql_fetch_row(result)) != NULL) {
		value = strdup(row[0]);
	}

	mysql_free_result(result);

	if (value != NULL) {
		mysql_close(&mysql);
		return(value);
	}

	/*
	 * there is no host, no appliance and no OS attribute, see
	 * if there is a global attribute
	 */
	sprintf(query, "select value from global_attributes where attr = '%s'",
		attr);

	if (mysql_real_query(&mysql, query, strlen(query)) != 0) {
		fprintf(stderr, "gethostattr:mysql_real_query:failed: %s\n",
			mysql_error(&mysql));
	}

	if ((result = mysql_store_result(&mysql)) == NULL) {
		fprintf(stderr, "gethostattr:mysql_store_result:failed: %s\n",
			mysql_error(&mysql));
	}

	if ((row = mysql_fetch_row(result)) != NULL) {
		value = strdup(row[0]);
	}

	mysql_free_result(result);

	if (value != NULL) {
		mysql_close(&mysql);
		return(value);
	}

	/*
	 * if none of the above work, then return the rack number for this host
	 */
	sprintf(query, "select n.rack from nodes n, networks net where \
		net.ip = '%s' and net.node = n.id", ip);

	if (mysql_real_query(&mysql, query, strlen(query)) != 0) {
		fprintf(stderr, "gethostattr:mysql_real_query:failed: %s\n",
			mysql_error(&mysql));
	}

	if ((result = mysql_store_result(&mysql)) == NULL) {
		fprintf(stderr, "gethostattr:mysql_store_result:failed: %s\n",
			mysql_error(&mysql));
	}

	if ((row = mysql_fetch_row(result)) != NULL) {
		value = strdup(row[0]);
	}

	mysql_free_result(result);

	mysql_close(&mysql);
	return(value);
}


static void
random_shuffle(peer_t *peers, uint16_t numpeers)
{
	peer_t	temp;
	int	i, j;

	if (numpeers < 2) {
		return;
	}

	for (i = 0 ; i < numpeers - 1 ; ++i) {
		j = i + rand() / (RAND_MAX / (numpeers - i) + 1);

		memcpy(&temp, &peers[j], sizeof(temp));
		memcpy(&peers[j], &peers[i], sizeof(peers[j]));
		memcpy(&peers[i], &temp, sizeof(peers[i]));
	}
}

unsigned long long
stampit()
{
	struct timeval		now;
	unsigned long long	n;

	gettimeofday(&now, NULL);
	n = (now.tv_sec * 1000000) + now.tv_usec;
	return(n);
}

dt_table_t	*dt_table = NULL;

int
grow_dt_table(int size)
{
	uint32_t	oldsize;
	uint32_t	newsize;
	int		len;

#ifdef	DEBUG
	fprintf(stderr, "grow_dt_table:size %d\n", size);
#endif

	if (dt_table == NULL) {
		oldsize = 0;
		newsize = size;
	} else {
		oldsize = dt_table->size;
		newsize = size + dt_table->size;
	}

	len = sizeof(dt_table_t) + sizeof(download_timestamp_t) * newsize;

	if ((dt_table = realloc(dt_table, len)) == NULL) {
		perror("grow_dt_table:malloc failed:");
		return(-1);
	}

	bzero(&dt_table[oldsize], (size * sizeof(download_timestamp_t)));
	dt_table->size = newsize;

	return(0);
}

unsigned long long
add_to_dt_table(in_addr_t host, char *coop)
{
	struct in_addr	in;
	int		i;

	in.s_addr = host;

#ifdef	DEBUG
	fprintf(stderr, "add_to_dt_table:host %s : coop %s\n", inet_ntoa(in),
		coop);
#endif

	/*
	 * first check if the table is not created yet or if it is full
	 */
	if ((dt_table == NULL) ||
			(dt_table->entry[dt_table->size - 1].host != 0)) {
		grow_dt_table(DT_TABLE_ENTRIES);
	}

	for (i = 0 ; i < dt_table->size ; ++i) {
		if (dt_table->entry[i].host == 0) {
			/*
			 * this entry is free
			 */
			dt_table->entry[i].host = host;
			dt_table->entry[i].timestamp = 0;
			dt_table->entry[i].coop = gethostattr(inet_ntoa(in),
				"coop");
			coop = dt_table->entry[i].coop;
			break;
		}
	}

#ifdef	DEBUG
	fprintf(stderr, "add_to_dt_table:i (%d), size (%d)\n", i,
		dt_table->size);
#endif

	return(dt_table->entry[i].timestamp);
}

static unsigned long long
lookup_timestamp(in_addr_t host, char *coop)
{
	unsigned long long	timestamp = 0;
	int			i;

#ifdef	DEBUG
{
	struct in_addr	in;

	in.s_addr = host;
	fprintf(stderr, "lookup_timestamp:host (%s)", inet_ntoa(in));
	if (coop != NULL) {
		fprintf(stderr, " : coop %s", coop);
	}
	fprintf(stderr, "\n");
}
#endif

	if (dt_table == NULL) {
		if (grow_dt_table(DT_TABLE_ENTRIES) < 0) {
			return(0);
		}
	}

	for (i = 0 ; i < dt_table->size ; ++i) {
		if (dt_table->entry[i].host == 0) {
			/*
			 * at the last entry. this host was not found.
			 */
			break;
		}

		if (dt_table->entry[i].host == host) {
			timestamp = dt_table->entry[i].timestamp;
			coop = dt_table->entry[i].coop;
			break;
		}
	}

	if (timestamp == 0) {
		/*
		 * didn't find the host in the table. let's add it.
		 */
		timestamp = add_to_dt_table(host, coop);
	}

	return(timestamp);
}

static void
update_timestamp(in_addr_t host)
{
	int	i;

	if (dt_table == NULL) {
		return;
	}

	for (i = 0 ; i < dt_table->size ; ++i) {
		if (dt_table->entry[i].host == 0) {
			/*
			 * at the last entry. this host was not found.
			 */
			break;
		}

		if (dt_table->entry[i].host == host) {
			dt_table->entry[i].timestamp = stampit();
			break;
		}
	}

	return;
}

char	*mycoop = NULL;

static int
timestamp_compare(const void *a, const void *b)
{
	peer_timestamp_t	*key1 = (peer_timestamp_t *)a;
	peer_timestamp_t	*key2 = (peer_timestamp_t *)b;

	if ((mycoop != NULL) && (key1->coop != NULL) && (key2->coop != NULL)) {
		if (strcmp(key1->coop, key2->coop) != 0) {
			/*
			 * they're not in the same coop group. let's see
			 * if one of them is in my coop group.
			 */
			if (strcmp(key1->coop, mycoop) != 0) {
				return(-1);
			}
			if (strcmp(key2->coop, mycoop) != 0) {
				return(1);
			}
		}
	}

	/*
	 * if we made it here, then the two keys are in the same coop group,
	 * so let's sort by state and timestamp
	 */

	if (key1->peer.state == key2->peer.state) {
		if (key1->timestamp < key2->timestamp) {
			return(-1);
		} else if (key1->timestamp > key2->timestamp) {
			return(1);
		}
	} else {
		if (key1->peer.state == DOWNLOADING) {
			return(1);
		} else if (key2->peer.state == DOWNLOADING) {
			return(-1);
		}
	}

	/*
	 * the keys must be equal
	 */
	return(0);
}

void
shuffle(peer_t *peers, uint16_t numpeers, char *coop)
{
	peer_timestamp_t	*list;
	int			i;

	if ((list = (peer_timestamp_t *)malloc(
			numpeers * sizeof(peer_timestamp_t))) == NULL) {
		/*
		 * if this fails, just do a random shuffle
		 */
		random_shuffle(peers, numpeers);

		/*
		 * need to touch all the timestamps in the download timestamps
		 * table
		 */

		return;
	}

	for (i = 0 ; i < numpeers ; ++i) {
		memcpy(&list[i].peer, &peers[i], sizeof(list[i].peer));
		list[i].timestamp = lookup_timestamp(list[i].peer.ip,
			list[i].coop);
	}

	if (numpeers > 1) {

#ifdef	DEBUG
		fprintf(stderr, "shuffle:before sort\n");

		for (i = 0 ; i < numpeers ; ++i) {
			struct in_addr	in;

			in.s_addr = list[i].peer.ip;
	
			fprintf(stderr,
				"\thost %s : state %c : timestamp %lld\n",
				inet_ntoa(in),
				(list[i].peer.state == DOWNLOADING ? 'd' : 'r'),
				list[i].timestamp);
		}
#endif

		mycoop = coop;

		/*
		 * sort the list by timestamps
		 */
		qsort(list, numpeers, sizeof(peer_timestamp_t),
			timestamp_compare);

#ifdef	DEBUG
		fprintf(stderr, "shuffle:after sort\n");

		for (i = 0 ; i < numpeers ; ++i) {
			struct in_addr	in;

			in.s_addr = list[i].peer.ip;
	
			fprintf(stderr,
				"\thost %s : state %c : timestamp %lld\n",
				inet_ntoa(in),
				(list[i].peer.state == DOWNLOADING ? 'd' : 'r'),
				list[i].timestamp);
		}
#endif

		/*
		 * now copy the sorted list back into peers
		 */
		for (i = 0 ; i < numpeers ; ++i) {
			memcpy(&peers[i], &list[i].peer, sizeof(peers[i]));
		}

		/*
		 * update the timestamp on only the first entry in the new list
		 */ 
		update_timestamp(peers[0].ip);
	}

	free(list);
	return;
}

