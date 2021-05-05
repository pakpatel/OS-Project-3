#ifndef _MEDIA_TRASNFER_H_
#define _MEDIA_TRASNFER_H_

#include <stdio.h>

/*
 * @param fp       - pointer to the media to be sent
 * @param sockfd   - client socke to send the media to  
 */
int send_media(int sockfd, const char *media_path, size_t length);

/*
 * @param sockfd   - client socket to receive the media on 
 * @param filename - filename to write received data to
 */
int receive_media(int sockfd, const char *media_path, size_t length);

/*
 * @param path        - sends lists all the media under this path
 * @param buffer      - place to store the listing to
 * @param buffer_size - size of the buffer passed
 * @returns
 *      1 if success, -1 if failure
 */
int get_media_list(const char *path, char *buffer, size_t buffer_size);

/*
 * @param client_socket - client socket to send header to 
 * @param port          - port socket is hosted on 
 * @param media_size    - size of media to be sent
 * @param media_type    - type of the media to be sent
 * @returns
 * 		1 if sucess, -1 if fail
 */
int send_header(int client_socket, int port, size_t media_size, const char *media_type, int status);

#endif