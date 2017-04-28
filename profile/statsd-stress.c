#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

void diep(char *s)
{
	perror(s);
	exit(1);
}

int run_threads = 1;
int metrics = 1024;

#define SERVER_IP "127.0.0.1"
#define PORT 8125

size_t myrand(size_t max) {
	int loops = max / RAND_MAX;
	int i;

	size_t ret = rand();
	for(i = 0; i < loops ;i++)
		ret += rand();

	return ret % max;
}

struct thread_data {
	int id;
	struct sockaddr_in *si_other;
	int slen;
	size_t counter;
};

static void *report_thread(void *__data) {
	struct thread_data *data = (struct thread_data *)__data;

	size_t last = 0;
	for (;;) {
		int i;
		size_t total = 0;
		for(i = 0; i < run_threads ;i++)
			total += data[i].counter;

		printf("%zu metrics/s\n", total-last);
		last = total;

		sleep(1);
		printf("\033[F\033[J");
	}

	return NULL;
}

#define STATSD_METRIC_TYPES 6
char *types[STATSD_METRIC_TYPES] = {"g", "c", "m", "ms", "h", "s"};

static void *spam_thread(void *__data) {
	struct thread_data *data = (struct thread_data *)__data;

	int s, i;
	char packet[1024];

	if ((s = socket(AF_INET, SOCK_DGRAM, 0))==-1)
		diep("socket");

	char **packets = malloc(sizeof(char *) * metrics);
	size_t *lengths = malloc(sizeof(size_t) * metrics);

	for(i = 0; i < metrics ;i++) {
		lengths[i] = sprintf(packet, "github.test.packet.%zu:%zu.%zu|%s", myrand(metrics), myrand(metrics), myrand(metrics), types[myrand(STATSD_METRIC_TYPES)]);
		packets[i] = strdup(packet);
		// printf("packet %d, of length %zu: '%s'\n", i, lengths[i], packets[i]);
	}

	for (;;) {
		for(i = 0; i < metrics ;i++) {
			if (sendto(s, packets[i], lengths[i], 0, (void *)data->si_other, data->slen) < 0) {
				printf("C ==> DROPPED\n");
				return NULL;
			}
			data->counter++;
		}
	}

	close(s);
	return NULL;
}

int main(int argc, char *argv[])
{
	if (argc != 5) {
		fprintf(stderr, "Usage: '%s THREADS METRICS IP PORT'\n", argv[0]);
		exit(-1);
	}

	run_threads = atoi(argv[1]);
	metrics = atoi(argv[2]);
	char *ip = argv[3];
	int port = atoi(argv[4]);

	struct thread_data data[run_threads];
	struct sockaddr_in si_other;
	pthread_t threads[run_threads], report;
	int i;

	srand(time(NULL));

	memset(&si_other, 0, sizeof(si_other));
	si_other.sin_family = AF_INET;
	si_other.sin_port = htons(port);
	if (inet_aton(ip, &si_other.sin_addr)==0) {
		fprintf(stderr, "inet_aton() of ip '%s' failed\n", ip);
		exit(1);
	}

	for (i = 0; i < run_threads; ++i) {
		data[i].id       = i;
		data[i].si_other = &si_other;
		data[i].slen     = sizeof(si_other);
		data[i].counter  = 0;
		pthread_create(&threads[i], NULL, spam_thread, &data[i]);
	}

	printf("\n");
	printf("THREADS     : %d\n", run_threads);
	printf("METRICS     : %d\n", metrics);
	printf("DESTINATION : %s:%d\n", ip, port);
	printf("\n");
	pthread_create(&report, NULL, report_thread, &data);

	for (i =0; i < run_threads; ++i)
		pthread_join(threads[i], NULL);

	return 0;
}
