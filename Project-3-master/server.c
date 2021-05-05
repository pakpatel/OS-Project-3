/* A simple echo server using TCP */
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <poll.h>
#include <unistd.h>
#include <time.h>

#include "parser.h"
#include "media_transfer.h"
#include "queue.h"

#define SERVER_TCP_PORT		3000	/* well-known port */
#define HEADERLEN			256		/* header packet length */
#define CONFIG_BUFFER		256     /* length of buffer for config line */

typedef enum sched_type {
	FIFO,
	RANDOM
} sched_type_e;

typedef struct hanlder_arg {
	int port;                       /* port number server is running on */
	int client_socket;              /* port number of client to deal with */
} handler_arg_t;

typedef struct {
	int sd;							/* server socket descriptor */
	int port;						/* port number server will listen on */
	int num_threads;		        /* no.of threads - can be modified using CL Arg #3 */
    int max_requests;		        /* max no.of client requests server can have at any time - can be modified using CL Arg number #4 */
    char * directory;		        /* place to look for media files */
	pthread_t *handlers;	        /* array of threads server can handle client requests */
	pthread_mutex_t lock;			/* mutex lock to do some thread safe functionanlities */
	pthread_cond_t cond;			/* condition variable */
	Queue *job_queue;				/* process client request queue */
	sched_type_e scheduling_type;	/* FIFO or RANDOM processing */
} server_config_t;

/*
 *	@param config: struct to store config value from rc script
 *	@param confgirc: file to read config information from
 *	@returns: success - 1 or failure - 0
 *	reads config file in to config struct
 */
int parse_configuration(server_config_t *config, char *configrc);

/*
 *	@param config: server configurtion struct
 *	@returns number of chars printed
 *	prints server configuration summary
 */
int print_configuration(server_config_t *config);

/*
 * @param filepath - name of the file for which extension is needed
 * @returns
 * 		point to first char in extension
 */
const char *get_file_ext(const char *filename);

/*
 * @param config: servert config struct
 * initializes threads, and locks for config struct
 */
int initialize_thread_pool(server_config_t *config);

/*
 * @param arg - handler args passing
 * @returns
 * 		1 if client wants to disconnect
 * 		0 if client wants to continue
 * Fulfils client requests
 * 
 */
void handle_request(void*arg);

/*
 * @param arg - server config arg will be passed
 * takes a job from job queue.
 */
void *watch_requests(void *arg);

int main(int argc, char * argv[]) {
	char * config_file = NULL;

	switch(argc) {
	case 1:
		config_file = "mserver.config";
		break;
	case 3:
		if (strcmp(argv[1], "-c") == 0) config_file = argv[2];
		break;
	}


	/* seed for random generator */
	srand(time(0));

	/* init server configuration */
	char pwd[BUFLEN];
	getcwd(pwd, BUFLEN);

	/* fill in default config */
	server_config_t config;
	config.port = SERVER_TCP_PORT;
	config.directory = pwd;
	config.num_threads = 4;
	config.max_requests = 10;
	config.job_queue = createQueue(config.max_requests);
	config.scheduling_type = RANDOM;

	/* override default config if file provided */
	if (argc == 3) {
		switch(parse_configuration(&config, config_file)) {
			case -1:
				fprintf(stderr, "Unable to open configuration file!\n");
				break;
			case -2:
				fprintf(stderr, "Configuration error!\n");
				break;
		}
	} else {
		parse_configuration(&config, config_file);
	}
	config.handlers  = (pthread_t*)malloc(sizeof(pthread_t)*(config.num_threads));

	/* switch current working dir to media dir */
	int ret = chdir(config.directory);
	if(ret != 0) {
		printf("cannot change to dir %s.\n", config.directory);
		exit(1);
	}

	struct sockaddr_in server;

	/* Create a stream socket */
	if ((config.sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		fprintf(stderr, "Can't create a socket\n");
		exit(1);
	}

	/* Bind an address to the socket */
	bzero((char *)&server, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons(config.port);
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(config.sd, (struct sockaddr *)&server, sizeof(server)) == -1) {
		fprintf(stderr, "Can't bind name to socket\n");
		exit(1);
	}

	/* initialize threads and mutex locks */
	initialize_thread_pool(&config);

	/* print sever configuration */
	print_configuration(&config);

	/* queue up to config.max_requests connect requests */
	listen(config.sd, config.max_requests);

	/* loop forever and add requests to queue */
	while(1) {

		/* get a new connection req on server socket*/
		int new_client_sd = accept(config.sd, NULL, NULL);

		/* if found a request, enqueue it for processing */
		if(new_client_sd > 0) {
			char enqueue_time[TIME_BUFFER_LEN];
			get_time_spec_to_string(enqueue_time, TIME_BUFFER_LEN);
			printf("\n%s: Main: Accepting New Connection: %d\n", enqueue_time, new_client_sd);

			handler_arg_t *arg = (handler_arg_t*)malloc(sizeof(handler_arg_t));
			arg->port = config.port;
			arg->client_socket = new_client_sd;

			printf("%s: Main: Adding New Client to the Job queue...\n", enqueue_time);
			/* Locks the queue to add job */
			pthread_mutex_lock(&(config.lock));

			/* add connectiong to queue */
			enqueue(config.job_queue, (void*) arg);

			/* give up the lock on the queue */
			pthread_mutex_unlock(&(config.lock));

			get_time_spec_to_string(enqueue_time, TIME_BUFFER_LEN);
			printf("%s: Main: Added New Client to the Job queue\n", enqueue_time);

		}
	}
}

int parse_configuration(server_config_t *config, char *configrc) {
	if (configrc == NULL) return -1;
	if (config == NULL) config = malloc(sizeof(server_config_t));

	FILE * c_file = fopen(configrc, "r");
	if (c_file == NULL) return -1;

	char buf[CONFIG_BUFFER];

	while (fgets(buf, CONFIG_BUFFER, c_file) != NULL) {
		if (strstr(buf, "#")) *(strstr(buf, "#")) = '\0';
		if (strstr(buf, "\n")) *(strstr(buf, "\n")) = '\0';
		if (buf[0] == '\0') continue;
		if (strstr(buf, ":") == NULL) return -2;

		char *split = strstr(buf, ": ");
		*split = '\0';

		char *key = buf;
		char *value = split + 2;

		// Fast and loose config parsing, only checking to see if config
		// line contains the key value, thus, if you have something like:
		// PortNumThreads: 5, it will match to PortNum and nothing else.
		if (strstr(key, "PortNum")) config->port = atoi(value);
		else if (strstr(key, "Threads")) config->num_threads = atoi(value);
		else if (strstr(key, "Sched")) {
			if (strcmp(value, "FIFO") == 0) config->scheduling_type = FIFO;
			else if (strcmp(value, "Random") == 0) config->scheduling_type = RANDOM;
		} else if (strstr(key, "Directory")) {
			config->directory = malloc(strlen(value));
			strcpy(config->directory, value);
		}
	}
}

int print_configuration(server_config_t *config) {
	printf("*******Server configuration*******\n");
	printf("Port Number: %d\n", config->port);
	printf("Num Threads: %d\n", config->num_threads);
	printf("Max Reqs: %d\n", config->max_requests);
	printf("Media Path: %s \n", config->directory);
	printf("**********************************\n");
	return 0;
}

const char *get_file_ext(const char *filename) {
    const char *dot_loc = strrchr(filename, '.');
    if(!dot_loc || dot_loc == filename) {
		return "Unknown";
	}
    return dot_loc + 1;
}

int initialize_thread_pool(server_config_t *config) {
	if (pthread_mutex_init(&(config->lock), NULL) != 0) { 
        printf("\n mutex init has failed\n"); 
        return -1;
    } 
	int i;
	for (i = 0; i < config->num_threads; ++i) {
		if(pthread_create(&(config->handlers[i]), NULL, watch_requests, (void*)config) != 0) {
			printf("Failed to create a thread");
			exit(1);
		}
	}
	return 0;
}

void *watch_requests(void *arg) {
	
	server_config_t *config = (server_config_t*)arg;
	
	void *job = NULL;
	
	while(1) {

		pthread_mutex_lock(&(config->lock));

		if(!isEmpty(config->job_queue)) {
			if(config->scheduling_type == FIFO) {
				job = dequeue(config->job_queue);
			}
			else {
				job = random_dequeue(config->job_queue);
			}
		}

		pthread_mutex_unlock(&(config->lock));

		if(job) {
			char time_processing_start[TIME_BUFFER_LEN];
			get_time_spec_to_string(time_processing_start, TIME_BUFFER_LEN);
			printf("%s: Watch Request: Thead %lu: Handling client %d\n", time_processing_start, pthread_self(), ((handler_arg_t*)job)->client_socket);
			handle_request(job);
		}

		job = NULL;
	}
}

void handle_request(void *client_sd)
{
	/* Some vairable declaration */
	char time_buf[TIME_BUFFER_LEN];
	handler_arg_t* info = ((handler_arg_t*)client_sd);

	/* Print out client information */
	struct sockaddr_in client_socket_addr;
	socklen_t len;
	len = sizeof(client_socket_addr);
	char client_ip[32];
	unsigned int ephemeral_port;

	bzero(&client_socket_addr, len);

	if (getsockname(info->client_socket, (struct sockaddr *)&client_socket_addr, &len) == 0) {
		/* get ip and the temp port*/
		inet_ntop(AF_INET, &client_socket_addr.sin_addr, client_ip, sizeof(client_ip));
		ephemeral_port = ntohs(client_socket_addr.sin_port);

		/* print contents of ss*/
		get_time_spec_to_string(time_buf, TIME_BUFFER_LEN);
		printf("%s: Handle Request: Client IP: %s Ephemeral Port: %d\n", time_buf, client_ip, ephemeral_port);
		fflush(stdout);
	}

	while(1) {
		char buf[BUFLEN] = {0};
		char *bp = buf;
		int  bytes_to_read = BUFLEN;
		int  n = 0;
		while ((n = read(info->client_socket, bp, bytes_to_read)) > 0) {
			bp += n;
			bytes_to_read -= n;
		}

		if (bp <= 0) {
			// client probably disconnected
			close(info->client_socket);
		}
		get_time_spec_to_string(time_buf, BUFLEN);
		printf("%s: Handle_Request: Client IP: %s Ephemeral Port: %d : Command Recevied string: %s", time_buf, client_ip, ephemeral_port, buf);

		/* put a null character at the end */
		int size = strlen(buf);
		buf[strcspn(buf, "\n")] = 0;

		switch(get_command_from_request(buf)) {
			case LIST: {
				char listing[1024];
				get_media_list(".", listing, 1024);
				// send the header packet
				send_header(info->client_socket, info->port, strlen(listing), "Text", 100);
				if(send(info->client_socket, listing, strlen(listing), 0) == -1) {
					get_time_spec_to_string(time_buf, TIME_BUFFER_LEN);
					printf("%s: Handle_Request: Client IP: %s Ephemeral Port: %d : Error sending list\n", time_buf, client_ip, ephemeral_port);
				}
				break;
			}
			case GET: {
				// get the length of the file needed to be read.
				FILE *fp = fopen(&(buf[4]), "rb");
				
				if (fp == NULL) {
					send_header(info->client_socket, info->port, 0, "", 404);
					break;
				}

				fseek(fp, 0L, SEEK_END);
				size_t len = ftell(fp);
				fseek(fp, 0L, SEEK_SET);
				fclose(fp);

				// get file extension
				const char *extension = get_file_ext(buf + 4);

				// send header information
				send_header(info->client_socket, info->port, len, extension, 100);

				get_time_spec_to_string(time_buf, TIME_BUFFER_LEN);
				printf("%s: Handle_Request: Client IP: %s Ephemeral Port: %d : Sent Header Information\n", time_buf, client_ip, ephemeral_port);

				// send requested media
				send_media(info->client_socket, buf + 4, len);

				get_time_spec_to_string(time_buf, TIME_BUFFER_LEN);
				printf("%s: Handle_Request: Client IP: %s Ephemeral Port: %d : Sent: %s\n", time_buf, client_ip, ephemeral_port, buf);
				break;
			}
			case EXIT:
				close(info->client_socket);
				get_time_spec_to_string(time_buf, TIME_BUFFER_LEN);
				printf("%s: Handle_Request: Client IP: %s Ephemeral Port: %d : Closed connection with client: %d\n", time_buf, client_ip, ephemeral_port, info->client_socket);
				return ;
			default:
				// invalid request header
				send_header(info->client_socket, info->port, 0, "", 301);
				get_time_spec_to_string(time_buf, TIME_BUFFER_LEN);
				printf("%s: Handle_Request: Client IP: %s Ephemeral Port: %d : Invalid request\n", time_buf, client_ip, ephemeral_port);
			break;
		}
	}
}
