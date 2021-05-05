/* A simple echo server using TCP */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <poll.h>
#include <unistd.h>

#include "parser.h"
#include "media_transfer.h"
#include "queue.h"

#define SERVER_TCP_PORT		3000	/* well-known port */
#define HEADERLEN			256		/* header packet length */
#define THREADS				4

typedef struct hanlder_arg {
	int port;
	int client_socket;
} hanlder_arg_t;

typedef struct {
	int num_threads;		// no.of threads - can be modified using CL Args number 3
	pthread_t *handlers;	// array of threads

	int max_requests;		// max no.of client requests server can have at any time - can be modified using CL Args number 4
	Queue *job_queue;		// data structure to hold server reqs

	char * directory;		// place to look for media files 	
} server_config_t;


/*
 * @param sockfd   - client socket for hadlign request
 */
void *handle_request(void *arg);
void *watch_requests(void *config);

// Add a new file descriptor to the set
void add_to_pfds(struct pollfd *pfds[], int newfd, int *fd_count, int *fd_size)
{
    // If we don't have room, add more space in the pfds array
    if (*fd_count == *fd_size) {
        *fd_size *= 2; // Double it

        *pfds = realloc(*pfds, sizeof(**pfds) * (*fd_size));
    }

    (*pfds)[*fd_count].fd = newfd;
    (*pfds)[*fd_count].events = POLLIN; // Check ready-to-read

    (*fd_count)++;
}

// Remove an index from the set
void del_from_pfds(struct pollfd pfds[], int i, int *fd_count)
{
    // Copy the one from the end over this one
    pfds[i] = pfds[*fd_count-1];

    (*fd_count)--;
}

/*
 * @param filepath - name of the file for which extension is needed
 * @returns
 * 		point to first char in extension
 */
const char *get_file_ext(const char *filename);

pthread_mutex_t lock;
pthread_mutex_t th_lock;
pthread_cond_t th_cond;

void setup_handlers(server_config_t *config) {
	if (pthread_mutex_init(&lock, NULL) != 0) { 
        printf("\n mutex init has failed\n"); 
        return;
    } 

	for (int i = 0; i < config->num_threads; ++i) {
		if(pthread_create(&(config->handlers[i]), NULL, watch_requests, (void*)config) != 0) {
			printf("Failed to create a thread");
			exit(1);
		}
	}
}

int main(int argc, char **argv)
{
	int 	n, bytes_to_read;
	int 	sd, new_sd, port;
  	socklen_t client_len;
	struct 	sockaddr_in 	server, client;
	char 	*bp, buf[BUFLEN];
	int 	num_threads = 1;
	int		max_req = 1;
	char	*dir = ".";
	switch(argc) {
		case 1:
			port = SERVER_TCP_PORT;
			break;
		case 2:
			port = atoi(argv[1]);
			break;
		case 3:
			port = atoi(argv[1]);
			num_threads = atoi(argv[2]);
			max_req = atoi(argv[3]);
		case 4:
			port = atoi(argv[1]);
			num_threads = atoi(argv[2]);
			max_req = atoi(argv[3]);
			break;
		case 5:
			port = atoi(argv[1]);
			num_threads = atoi(argv[2]);
			max_req = atoi(argv[3]);	//in future, sched type will be set here
			break;
		case 6:
			port = atoi(argv[1]);
			num_threads = atoi(argv[2]);
			max_req = atoi(argv[3]);
			dir = argv[5];
			break;		
		default:
			fprintf(stderr, "Usage: %s [port]\n", argv[0]);
			exit(1);
	}

	// allocate num threads asked for
	int ret = 	chdir(dir);
	if(ret != 0) {
		printf("did not find the direcory specidifed: %s", dir);
		exit(1);
	}
	char pwd[BUFLEN];
	getcwd(pwd, BUFLEN);
	pthread_t threads[num_threads];
	server_config_t config;
	config.handlers = threads;
	config.num_threads = num_threads;
	config.max_requests = max_req;
	config.job_queue = createQueue(max_req);
	config.directory = pwd;

	printf("Server config set to:\n");
	printf("Num Threads: %d\n", config.num_threads);
	printf("Max Reqs: %d\n", config.max_requests);
	printf("Media Path: %s/ \n", config.directory);
	fflush(stdout);
	setup_handlers(&config);
		
	/* Create a stream socket */
	if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		fprintf(stderr, "Can't create a socket\n");
		exit(1);
	}

	if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
    	perror("setsockopt(SO_REUSEADDR)");
	
	if (setsockopt(sd, SOL_SOCKET, SO_KEEPALIVE, &(int){1}, sizeof(int)) < 0)
    	perror("setsockopt(SO_KEEPALIVE)");

	/* Bind an address to the socket */
	bzero((char *)&server, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sd, (struct sockaddr *)&server, 
	sizeof(server)) == -1) {
		fprintf(stderr, "Can't bind name to socket\n");
		exit(1);
	}

	/* queue up to 5 connect requests */
	listen(sd, 5);

	int fd_count = 0;
    int fd_size = 5;
    struct pollfd *pfds = malloc(sizeof *pfds * fd_size);

	// Add the listener to set
    pfds[0].fd = sd;
    pfds[0].events = POLLIN; // Report ready to read on incoming connection

	fd_count = 1;

	for (;;) {

        int poll_count = poll(pfds, fd_count, -1);

        if (poll_count == -1) {
            perror("poll");
            exit(1);
        }

        // Run through the existing connections looking for data to read
        for(int i = 0; i < fd_count; i++) {

            // Check if someone's ready to read
            if (pfds[i].revents & POLLIN) { // We got one!!

                if (pfds[i].fd == sd) {
                    // If listener is ready to read, handle new connection

                    client_len = sizeof client;
                    new_sd = accept(sd,
                        (struct sockaddr *)&client,
                        &client_len);

                    if (new_sd == -1) {
                        perror("accept");
                    } else {
                        add_to_pfds(&pfds, new_sd, &fd_count, &fd_size);

                        printf("server: new connection\n");
                    }
                } else {
                    // If not the listener, we're just a regular client
                    int sender_fd = pfds[i].fd;

					int error = recv(sender_fd, NULL, 1, MSG_PEEK | MSG_DONTWAIT);

					if (error == 0) {
						printf("socket hungup\n");
						close(pfds[i].fd); // Bye!

                        del_from_pfds(pfds, i, &fd_count);
					} else {
						hanlder_arg_t *arg = (hanlder_arg_t*)malloc(sizeof(hanlder_arg_t));
						arg->port = port;
						arg->client_socket = sender_fd;

						enqueue(config.job_queue, (void*) arg);
						pthread_mutex_lock(&th_lock);
						pthread_cond_broadcast(&th_cond);
						pthread_mutex_unlock(&th_lock);
					}

                }
            }
        }
    }

	pthread_mutex_destroy(&lock);
	return 0;
}

void *watch_requests(void *arg) {
	server_config_t *config = (server_config_t*)arg;
	void *job = NULL;
	for (;;) {
		pthread_mutex_lock(&th_lock);

		pthread_mutex_lock(&lock);
		
		if (!isEmpty(config->job_queue)) {
			job = dequeue(config->job_queue);
		}

		pthread_mutex_unlock(&lock);

		if (job != NULL) {
			hanlder_arg_t* info = ((hanlder_arg_t*)job);
			// printf("%d\n", info->client_socket);
			handle_request(job);
		}

		job = NULL;

		pthread_cond_wait(&th_cond, &th_lock);
		pthread_mutex_unlock(&th_lock);
	}
}

void *handle_request(void *client_sd)
{
	char buf[BUFLEN] = {0};
	hanlder_arg_t* info = ((hanlder_arg_t*)client_sd);

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

	int size = strlen(buf);
	printf("%d \n", size);
	printf("RCVD: %s", buf);
	printf("client socket: %d\n", info->client_socket);
	buf[size - 1] = '\0';

	switch(get_command_from_request(buf)) {
		case LIST: {
			char listing[1024];
			get_media_list(".", listing, 1024);
			// send the header packet
			send_header(info->client_socket, info->port, strlen(listing), "Text", 100);
			if(send(info->client_socket, listing, strlen(listing), 0) == -1) {
				printf("error sending list\n");
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

			// send requested media
			send_media(info->client_socket, buf + 4, len);

			printf("SEND: %s\n", buf);
			break;
		}
		default:
			send_header(info->client_socket, info->port, 0, "", 301);
		break;
	}
}

const char *get_file_ext(const char *filename) {
    const char *dot_loc = strrchr(filename, '.');
    if(!dot_loc || dot_loc == filename) {
		return "Unknown";
	}
    return dot_loc + 1;
}