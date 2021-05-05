#include <arpa/inet.h>
#include <dirent.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "media_transfer.h"

#define LEN 1024

int send_media(int sockfd, const char *media_path, size_t length) {
    int n;
    char *data = malloc(length);

	FILE *fp = fopen(media_path, "rb");
	if(fp == NULL){
		printf("File: %s, not Found", media_path);
        return -1;
	}

    size_t sent = 0;
    fread(data, length, 1, fp);
    while(sent < length) {
        size_t t = send(sockfd, data, length, 0);
        if (t != -1) {
            sent += t;
        } else {
            perror("send_media");
            exit(1);
        }
    }

    fclose(fp);
    free(data);
    return 1;
}

int receive_media(int sockfd, const char *filename, size_t length) {
    unsigned int n = 0;
    size_t pos = 0;
    FILE *fp;
    char buffer[LEN];
    char *media = malloc(length);

    while (1) {
        n = read(sockfd, buffer, LEN);
        if (n < 0) continue;
        memcpy(media + pos, buffer, n);
        pos += n;
        if (pos >= length) break;
    }

    fp = fopen(filename, "w");
    fwrite(media, length, 1, fp);
    fclose(fp);
    free(media);

    return 1;
}

int get_media_list(const char *path, char *buffer, size_t buffer_size) {
    DIR *dh = opendir(path);
    struct dirent *d;
    struct stat fstat;

    int n = 0;
    n += sprintf(buffer, "\tSize\t\tName\n");
    while((d = readdir(dh)) != NULL) {
        stat(d->d_name, &fstat);
        n += sprintf(buffer + n, "\t%ld\t\t%s\n", fstat.st_size, d->d_name);
    }
    closedir(dh);
    return 1;
}

int send_header(int client_socket, int port, size_t media_size, const char *media_type, int status) {
	char host[256];
	char *IP;
	struct hostent *host_entry;
	int hostname;

	//find the host name
	hostname = gethostname(host, sizeof(host));
	if(hostname == -1) {
		printf("Cannot find host information");
	}

	//find host information
	host_entry = gethostbyname(host);
	if(host_entry == NULL) {
		printf("Cannot find the host from id\n");
	}

	//Convert into IP string
	IP = inet_ntoa(*((struct in_addr*) host_entry->h_addr_list[0]));

	// create the header
	char header[LEN];
	int n = 0;
	n += sprintf(header, "Status: %d\r\n", status);			        // req is valid
	n += sprintf(header + n, "Host: %s:%d\r\n", IP, port);		    // append host information
	n += sprintf(header + n, "Type: %s\r\n", media_type);		    // append file type
	n += sprintf(header + n, "Length: %ld\r\n\r\n", media_size);		// append file length

	// finally send the header packet
	if(send(client_socket, header, n, 0) == -1) {
		return -1;
	}
	else{
		return 0;
	}
}
