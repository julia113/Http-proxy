#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#define __USE_XOPEN
#include <time.h>
#include "cache.h"

#define REQUEST_SIZE 4096
#define BUFFER_SIZE 512
static int issue = 0; //400 error, need to close connection 
static int valid = 0; //continue reading while valid is 0

//default parameter
static int maxsize = 65536; //max file size for cache
static int cache_hit = -1; //0 if file in cache, 1 if it's not

static cache ch; //cache that holds all cache entries

//creates an object that stores all the parts of the client request
struct ClientReq {
	char command[REQUEST_SIZE]; 
	char filename[REQUEST_SIZE];
	char header[REQUEST_SIZE];
	ssize_t contentlen; 
	char buf[REQUEST_SIZE]; 	//store the rquest string here, changed from char
	char body[REQUEST_SIZE];
	char get_buf[BUFFER_SIZE];
	time_t time; 				//stores time in number form
	char time_buf[100];			 //stores time in written out form
	char forward[REQUEST_SIZE]; //forward get request when file exists in cache
}ClientReq; 

//clears request->body
void clearbuffer(struct ClientReq* request) {
	for(int i = 0; i < BUFFER_SIZE; i++)
	{
		request->body[i] = '\0';
	}
}

//parse response from server to proxy
void parse_response(char* buf, struct ClientReq* request) {
	char *word = "Content-Length";
	char *word2 = "Last-Modified";
	char *temp = strstr(buf, word);
	char *temp2 = strstr(buf, word2);
	
	if(temp != NULL) //buffer contains content length and is head/get
	{
		sscanf(buf, "%s %ld", request->header, &request->contentlen); //get content length
		return;
	}
	if(temp2 != NULL) //buffer contains last modified time
	{
		int j = 0;
		//printf("parse time\n");
		for(int i = 15; i < strlen(buf); i++)
		{
			request->time_buf[j] = buf[i];
			j++;
		}
		request->time_buf[strlen(request->time_buf)] = '\0'; //add null
		//printf("time buf:%s\n", request->time_buf);
		struct tm time;
		memset(&time, 0, sizeof(time)); //initialize 
		strptime(request->time_buf, "%a, %d %b %Y %T GMT", &time); 
		request->time = mktime(&time); //convert from tm to time_t
		return;
	}

}

//parse request and store data in object fields
int parse_req(struct ClientReq* request) {
	char *word = "Content-Length:";
	char *word2 = "HEAD";
	char *word3 = "PUT";
	char *word4 = "GET";
	char *temp = strstr(request->buf, word);
	char *temp2 = strstr(request->buf, word2);
	char *temp3 = strstr(request->buf, word3);
	char *temp4 = strstr(request->buf, word4);

	if(temp2 != NULL || temp3 != NULL || temp4 != NULL) //check if buffer contains head, put, or get
	{
		int scan1 = sscanf(request->buf, "%s %s", request->command, request->filename); //check which command
		
		if(scan1 != 2) //scan did not go through 
		{
			return 3;
		}
		for(int i = 0; i < strlen(request->filename); i++) //get rid of \ in front of filename
		{
			request->filename[i] = request->filename[i+1];
		}
		request->filename[strlen(request->filename)] = '\0';
		
		if(strcmp(request->command, "GET") == 0 || strcmp(request->command, "HEAD") == 0) //done parsing
		{
			//printf("command: %s, file: %s\n", request->command, request->filename);
			return 1;
		}
		if(strcmp(request->command, "GET") == 1 && strcmp(request->command, "HEAD") == 1 && strcmp(request->command, "PUT") == 1) //command not found
		{
			return -1;
		}
		return 0;
	}
	else //command is PUT
	{
		if(temp != NULL) //parse content length header
		{
			int scan2 = sscanf(request->buf, "%s %ld",request->header, &request->contentlen); //parse content length
			if(scan2 != 2) //contentlen is not valid number
			{
				return 3;
			}
			//printf("comm: %s, file: %s, length: %d\n", request->command, request->filename, request->contentlen);
			return 2;
		}
		return 0;
	} 	
}

//reads from client request into message, parse string into different parts
void read_req(int socketfd, int serverfd, struct ClientReq* request) {
	int bytes_read = 0;
	int seenR = 0;
	int flag = 0;
	int flag2 = 0;
	int currbyte = 0; //position in buffer of current byte we are reading
	int index = 0;
	char * end = "\r\n"; 

	while(1)
	{
		bytes_read = recv(socketfd, &request->buf[currbyte], 1, 0); //reading one byte at a time
	//	printf("finished recv()\n");
		if(bytes_read == 0) //client closed connection
		{
		//	printf("bytes read is 0\n");
			valid = 1;	 
			return;
		}
		if(bytes_read < 0) //read all of client's responses
		{
			warn("read failure");
			valid = 1;
			return;	
		}
		if(request->buf[currbyte] == '\r' && currbyte == 0) 
		{
			flag = 1;	
		}
		else if (flag == 1 && request->buf[currbyte] == '\n') //checking for \r\n\r\n (end of entire request)
		{
			//printf("read EOC\n");
			send(serverfd, end, strlen(end), 0); 
			strcat(request->forward, "\r\n"); //add another /r/n
			if(strcmp(request->command, "PUT") == 0)
			{
				break; //not done reading body for put yet
			}
			return; // end of request for head/get
		}
		else
		{
			flag = 0;
		}
		if(request->buf[currbyte] == '\r')
		{
			seenR = 1;	
		}
		else if (seenR == 1 && request->buf[currbyte] == '\n')
		{
			request->buf[currbyte-1] = '\0';	
			currbyte = 0; //reset 
			int done = parse_req(request);	
			if(strcmp(request->command, "HEAD") == 0 || strcmp(request->command, "PUT") == 0) //forward to server
			{
				send(serverfd, request->buf, strlen(request->buf), 0);
				send(serverfd, end, strlen(end), 0); //send \r\n
				//printf("forwarded: %s\n", request->buf);
			}
			else //get request
			{	
				if(get(ch, request->filename) == NULL) //file doesn't exists in cache
				{
			//		printf("file doesn't exist in cache\n");
					cache_hit = 1;
					//forward get request to server
					send(serverfd, request->buf, strlen(request->buf), 0);
					send(serverfd, end, strlen(end), 0); //send \r\n
					//printf("GET forwarded dne: %s\n", request->buf);
					
				}
				else //file does exist in cache
				{
				//	printf("file does exist in cache\n");
					cache_hit = 0;
					//format and send head request to server
					if(strstr(request->buf, "GET"))
					{
						strcat(request->forward, request->buf);
						strcat(request->forward, "\r\n");
						char* c = strstr(request->buf, " ");
						if(c != NULL)
						{
							char head_req[REQUEST_SIZE];
							memset(&head_req, 0, REQUEST_SIZE);
							strcpy(head_req, "HEAD");
							strcat(head_req, c);
							send(serverfd, head_req, strlen(head_req), 0);
							send(serverfd, end, strlen(end), 0);
						//	printf("GET forwarded 1:%s\n", head_req);
						}	
					}
					else
					{
						strcat(request->forward, request->buf);
						strcat(request->forward, "\r\n");
						//printf("request forward: %s\n", request->forward);
						send(serverfd, request->buf, strlen(request->buf), 0);
						send(serverfd, end, strlen(end), 0);
						//printf("GET forwarded exist:%s\n", request->buf);
					}		

				}
			}
			if(done == -1)
			{
				return; //error
			}
			if(done == 0) //scanned first part of PUT request
			{
				continue; 
			}	
			if(done == 1)
			{
				continue; //read entire head/put request
			}	
			if(done == 2) 
			{
				break;
			}
			if(done == 3) //contentlen is not valid number
			{
				issue = 1;
				return;	
			}
		}
		else
		{
			seenR = 0; //reset seenR
		}
		currbyte++;		
	}


	int clear = 0;
	while(1) //look for \r\n\r\n
	{
		bytes_read = recv(socketfd, &request->body[currbyte], 1, 0);
		if(bytes_read <= 0)
		{
		//	printf("recv error \n");
			break;
		}
		if(clear == 0 && request->body[currbyte] == '\r')
		{
			clear++;
		}
		else if(clear == 1 && request->body[currbyte] == '\n')
		{
			clear++;
		}
		else if(clear == 2 && request->body[currbyte] == '\r')
		{
			clear++;
		}
		else if(clear == 3 && request->body[currbyte] == '\n')
		{
			send(serverfd, end, strlen(end), 0); //send \r\n
			break; //found end of command
		}
		else
		{
			clear = 0;
		}
	}

	//send body of put request to server
	int tempbytes = request->contentlen; //total bytes in file
	if(tempbytes == 0)
	{
		return; //no body to forward
	}
	while(tempbytes > BUFFER_SIZE) //code from asg0
	{
		clearbuffer(request);
		int x = recv(socketfd, request->body, BUFFER_SIZE, 0);
		if(x == -1)
		{
			issue = 1;
			return;	
		}
		int c = send(serverfd, request->body, BUFFER_SIZE, 0); //send body to server
	//	printf("forwarded body: %s\n", request->body);
		tempbytes -= BUFFER_SIZE; //ddecrement tempbytes by buffer size
		if(x == 0)
		{
			flag2 = 1;
		}
	}
	if(!flag2) //tempbytes < buffer size
	{	
		if(tempbytes != 0)
		{
			clearbuffer(request);
			int a = recv(socketfd, request->body, tempbytes, 0);
			if(a == -1)
			{
				issue = 1;
				return;	
			}
			send(serverfd, request->body, tempbytes, 0); //send body to server
		} 
		//printf("last forwarded body: %s\n", request->body);
	}
	return;
}

void handle_get(int socketfd, int serverfd, struct ClientReq* request) {
	int n = 0;
	int j = 0;
	int h = 0;
	int i = 0;
	int flag = 0;
	int k = 0;
	int seen = 0;
	int found = 0;
	char reply[REQUEST_SIZE];
	memset(&reply, 0, REQUEST_SIZE);
	char* recv_content;
	char tempbuf[REQUEST_SIZE];
	memset(&tempbuf, 0, REQUEST_SIZE);
	if(cache_hit == 1) //file dne in cache
	{
		cache_hit = -1; //reset flag 
		while(1) //read in get response from server
		{
			n = recv(serverfd, &reply[j], 1, 0); //read one byte at a time
			if(n <= 0)
			{
		//		printf("error in recv() 1\n");
				break;
			}
			if(found == 0)
			{
				tempbuf[h] = reply[j];
				h++;
			}
			if(seen == 0 && reply[j] == '\r')
			{
				seen++;
			}
			else if(seen == 1 && reply[j] == '\n')
			{
				//printf("temp buf : %s\n", tempbuf);
				parse_response(tempbuf, request); //parse for content length/time
				h = 0; 
				memset(&tempbuf, 0, sizeof(tempbuf)); //reset tempbuf
				seen++;
			}
			else if(seen == 2 && reply[j] == '\r')
			{
				seen++;
			}
			else if(seen == 3 && reply[j] == '\n')
			{
				found = 1; //found /r/n/r/n
				break; 
			}
			else
			{
				seen = 0;
			}
			j++;
			if(j == REQUEST_SIZE)
			{
				send(socketfd, reply, strlen(reply), 0); //forward response back to client
				memset(&reply, 0, sizeof(reply));
				j = 0;
			}
		}
		send(socketfd, reply, strlen(reply), 0); //forward response back to client
		char* recv_content = (char*)malloc(sizeof(char) * (request->contentlen+1)); //stores file contents
		
		//recv body
		int tempbytes = request->contentlen; //total bytes in file
		int index = 0; //keeps track of index in recv_content
		clearbuffer(request);
		while(tempbytes > BUFFER_SIZE) //if there are more bytes in file than buffer size
		{
			memset(&request->get_buf, 0, sizeof(request->get_buf));
			int x = recv(serverfd, request->get_buf, BUFFER_SIZE, 0);
			if(x == -1) //read error
			{
				return;
			}
			memcpy(recv_content + index, request->get_buf, BUFFER_SIZE); //add chunk to file contents, not sure if it works with binary
			index = index + BUFFER_SIZE;
			send(socketfd, request->get_buf, BUFFER_SIZE, 0); //send part of body
			memset(&request->get_buf, 0, sizeof(request->get_buf)); //reset body
			tempbytes -= BUFFER_SIZE; //subtract bytes read
			if(x == 0)
			{
				flag = 1;
			}
		}
		if(!flag)
		{
			if(tempbytes != 0)
			{
				memset(&request->get_buf, 0, sizeof(request->get_buf));
				int a = recv(serverfd, request->get_buf, tempbytes, 0);
				if(a == -1)
				{
					return;
				}
				memcpy(recv_content + index, request->get_buf, tempbytes); 	//add chunk to file contents
				index = index + tempbytes;  								//increment index by tempbytes
				send(socketfd, request->get_buf, tempbytes, 0); 			//send body to  server
				memset(&request->get_buf, 0, sizeof(request->get_buf)); 	//reset buffer
			}	
		}
		recv_content[index] = '\0';

		//check size to see if we need to cache
		if(request->contentlen <= maxsize) //file is within max size = need to cache
		{
			//add to cache
			cache_entry* entry = create_entry(request->filename, request->time, recv_content); //initialize this earlier nad add to file content
			//printf("file: %s time: %ld\n", entry->file, entry->edit_time);
			add_cache(ch, entry); //memory
			//printf("things in cache: %d\n", ch->occupied);
		}
		else
		{
			free(recv_content); //don't need string so free it
		}
		return;		
	}	
	else //cache exists
	{
		memset(&reply, 0, sizeof(reply));
		memset(&tempbuf, 0, sizeof(tempbuf));
		cache_hit = -1; //reset flag

		//read in head response, check for last modified header and parse last edit time
		while(1)
		{
			n = recv(serverfd, &reply[j], 1, 0); //recv head response
			if(n <= 0)
			{
		//		printf("error in recv() 2\n");
				break;
			}
			tempbuf[h] = reply[j];
			h++;
			if(seen == 0 && reply[j] == '\r')
			{
				seen++;
			}
			else if(seen == 1 && reply[j] == '\n') //reached /r/n
			{
			//	printf("tempbuf: %s\n", tempbuf);
				parse_response(tempbuf, request);
				h = 0; 
				memset(&tempbuf, 0, sizeof(tempbuf)); //reset tempbuf
				seen++;
			}
			else if(seen == 2 && reply[j] == '\r')
			{
				seen++;
			}
			else if(seen == 3 && reply[j] == '\n')
			{
		//		printf("handle get EOC\n");
				break; //reached end of response /r/n/r/n
			}
			else
			{
				seen = 0;
			}
			j++;	
			if(j == REQUEST_SIZE) //if there are more than 4096 bytes to read
			{
				memset(&reply, 0, sizeof(reply));
				j = 0;
			}
		}

		//compare time
		cache_entry* existing_entry = get(ch, request->filename); //get matching cache entry
		//printf("request time: %ld existing time: %ld\n", request->time, existing_entry->edit_time);
		double diff = difftime(request->time, existing_entry->edit_time); //compare times
		if(diff > 0) //server updated it more recently
		{
			//cache new version
			send(serverfd, request->forward, strlen(request->forward), 0); //send new get request
			
			//recv get response from server
			memset(&reply, 0, sizeof(reply));
			memset(&tempbuf, 0, sizeof(reply));
			int n = 0;
			int h = 0;
			int j = 0;
			int seen = 0;
			cache_hit = -1; //reset flag 
			while(1) //read in get response from server
			{
				n = recv(serverfd, &reply[j], 1, 0); //read one byte at a time
				//printf("reply by letter: %c\n", reply[j]);
				if(n <= 0)
				{
		//			printf("error in recv() 3\n");
					break;
				}
				if(found == 0)
				{
					tempbuf[h] = reply[j];
					h++;
				}
				if(seen == 0 && reply[j] == '\r')
				{
					seen++;
				}
				else if(seen == 1 && reply[j] == '\n')
				{
					//printf("temp buf : %s\n", tempbuf);
					parse_response(tempbuf, request); //get new content length/time
					h = 0; 
					memset(&tempbuf, 0, sizeof(tempbuf)); //reset tempbuf
					seen++;
				}
				else if(seen == 2 && reply[j] == '\r')
				{
					seen++;
				}
				else if(seen == 3 && reply[j] == '\n')
				{
					found = 1; //found /r/n/r/n
					break; 
				}
				else
				{
					seen = 0;
				}
				j++;
				if(j == REQUEST_SIZE) //if there are more than 4096 bytes to read
				{
					send(socketfd, reply, strlen(reply), 0);
					memset(&reply, 0, sizeof(reply));
					j = 0;
				}
			}
			//printf("get response: %s\n", reply);

			char* recv_content = (char*)malloc((sizeof(char) * (request->contentlen+1))); //stores file contents		
			send(socketfd, reply, strlen(reply), 0); //forward response back to client
		
			//recv body
			int tempbytes = request->contentlen; //total bytes in file
			int index = 0;
			clearbuffer(request);
			while(tempbytes > BUFFER_SIZE) //if there are more bytes in file than buffer size
			{
				memset(&request->get_buf, 0, sizeof(request->get_buf));
				int x = recv(serverfd, request->get_buf, BUFFER_SIZE, 0);
				if(x == -1) //read error
				{
					char error[] = "HTTP/1.1 500 INTERNAL SERVICE ERROR\r\nContent-Length: 23\r\n\r\nINTERNAL SERVICE ERROR\n";
					send(socketfd, error, strlen(error),0);
					return;
				}
				memcpy(recv_content + index, request->get_buf, BUFFER_SIZE); //add chunk to file contents, not sure if it works with binary
				index = index + BUFFER_SIZE;
				send(socketfd, request->get_buf, BUFFER_SIZE, 0); //send part of body
				memset(&request->get_buf, 0, sizeof(request->get_buf)); //reset body
				tempbytes -= BUFFER_SIZE; //subtract bytes read
				if(x == 0)
				{
					flag = 1;
				}
			}
			if(!flag)
			{
				if(tempbytes != 0)
				{
					memset(&request->get_buf, 0, sizeof(request->get_buf));
					int a = recv(serverfd, request->get_buf, tempbytes, 0);
					if(a == -1)
					{
						char error[] = "HTTP/1.1 500 INTERNAL SERVICE ERROR\r\nContent-Length: 23\r\n\r\nINTERNAL SERVICE ERROR\n";
						send(socketfd, error, strlen(error),0);
						return;
					}
					memcpy(recv_content + index, request->get_buf, tempbytes); //add chunk to file contents
					index = index + tempbytes; 
					send(socketfd, request->get_buf, tempbytes, 0);
					memset(&request->get_buf, 0, sizeof(request->get_buf));
				}
			}
			recv_content[index] = '\0';
			free(existing_entry->content); 				//free old content string
			existing_entry->content = recv_content; 	//set new content string
			existing_entry->edit_time = request->time; 	//update time
	
		}
		else //proxy responds directly
		{
			existing_entry->edit_time = request->time; //update cache entry's edit time 
			//create response here 
			sprintf(reply, "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\nLast Modified: %s\r\n", request->contentlen, request->time_buf);
			send(socketfd, reply, strlen(reply), 0);
			send(socketfd, existing_entry->content, request->contentlen, 0); //sending body separately
		}
		return;		
	}

}

//create response depending on command
void respond(int socketfd, int serverfd, struct ClientReq* request) {
	//printf("filename: %s command: %s\n", request->filename, request->command);
	int n = 0;
	int j = 0;
	int i = 0;
	int k = 0;
	int h = 0;
	int flag = 0;
	int seen = 0;
	int found = 0;
	char * end = "\r\n";
	int errnum = 0; //errno value
	struct stat buf;
	struct stat s; //get file size 
	char reply[REQUEST_SIZE];
	memset(&reply, 0, sizeof(reply));
	char tempbuf[REQUEST_SIZE];
	memset(&tempbuf, 0, sizeof(tempbuf));
	if(strcmp(request->command,"GET") == 0)
	{
		handle_get(socketfd, serverfd, request);
	}
	else if(strcmp(request->command, "PUT") == 0)//read and forward put response to client
	{
		while(1)
		{
			n = recv(serverfd, &reply[j], 1, 0); //read one byte at a time
			if(n <= 0)
			{
		//		printf("error in recv() 4\n");
				break;
			}
			if(seen == 0 && reply[j] == '\r')
			{
				seen++;
			}
			else if(seen == 1 && reply[j] == '\n')
			{
				seen++;
			}
			else if(seen == 2 && reply[j] == '\r')
			{
				seen++;
			}
			else if(seen == 3 && reply[j] == '\n')
			{
				found = 1; //found /r/n
			}
			else if(reply[j] == '\n' && found == 1) //read until find \n after \r\n\r\n
			{
			//	printf("end of response\n");
				break;
			}
			else
			{
				seen = 0;
			}	
			j++;
			if(j == REQUEST_SIZE)
			{
				send(socketfd, reply, strlen(reply), 0); //send portion of reply
				memset(&reply, 0, sizeof(reply));
				j = 0;
			}	
		}		
		send(socketfd, reply, strlen(reply), 0); //send last part
	//	printf("server reply to proxy: %s\n", reply); 
	}
	else if(strcmp(request->command, "HEAD") == 0) //read and forward head response to client
	{
		//pritnf("contentlen = %ld time buf = %s\n", request->contentlen, request->time_buf);
		while(1)
		{
			n = recv(serverfd, &reply[j], 1, 0); //read one byte at a time
			if(n <= 0)
			{
			//	printf("error in recv() 5\n");
				break;
			}
			if(seen == 0 && reply[j] == '\r')
			{
				seen++;
			}
			else if(seen == 1 && reply[j] == '\n')
			{
				seen++;
			}
			else if(seen == 2 && reply[j] == '\r')
			{
				seen++;
			}
			else if(seen == 3 && reply[j] == '\n')
			{
				break; //reached end of response
			}
			else
			{
				seen = 0;
			}
			j++;
			if(j == REQUEST_SIZE) //if there are more than 4096 bytes to read
			{
				send(socketfd, reply, strlen(reply), 0); //send portion of reply
				memset(&reply, 0, sizeof(reply));
				j = 0;
			}	
		}
		send(socketfd, reply, strlen(reply), 0); //send last part of reply
	}
	else
	{
		return; //nothing to respond to
	}
	return;		
}

 //reset object
void clear (struct ClientReq* request) {
	request->contentlen = 0;
	request->time = 0;
	for(int i = 0; i < REQUEST_SIZE; i++)
	{
		request->command[i] = '\0';
		request->filename[i] = '\0';
		request->header[i] = '\0';
		request->buf[i] = '\0';	
		request->forward[i] = '\0';
	}
	for(int j = 0; j< BUFFER_SIZE; j++)
	{
		request->body[j] = '\0';	
		request->get_buf[j] = '\0';
	}
	memset(&request->time_buf, 0, sizeof(request->time_buf));
}

/**
   Creates a socket for connecting to a server running on the same
   computer, listening on the specified port number.  Returns the
   socket file descriptor on success.  On failure, returns -1 and sets
   errno appropriately.
 */
int create_client_socket(uint16_t port) {
	int clientfd = socket(AF_INET, SOCK_STREAM, 0);
	if (clientfd < 0) 
	{
		return -1;
	}
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(port);
	if (connect(clientfd, (struct sockaddr*) &addr, sizeof addr)) 
	{
		return -1;
	}
	return clientfd;
}
/**
   Converts a string to an 16 bits unsigned integer.
   Returns 0 if the string is malformed or out of the rangeint16_t strtouint16(char number[]) //skeleton code
*/
uint16_t strtouint16(char number [])
{
	char *last;
	long num = strtol(number, &last, 10);
	if (num <= 0 || num > UINT16_MAX || *last != '\0') 
	{
		return 0;
	}
	return num;
}

/**
   Creates a socket for listening for connections.
   Closes the program and prints an error message on error.
 */
int create_listen_socket(uint16_t port) //skeleton code
{
	struct sockaddr_in addr;
	int listenfd = socket(AF_INET, SOCK_STREAM, 0);
	if (listenfd < 0) 
	{
		err(EXIT_FAILURE, "socket error");
	}

	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htons(INADDR_ANY);
	addr.sin_port = htons(port);
	if (bind(listenfd, (struct sockaddr*)&addr, sizeof addr) < 0) 
	{
		err(EXIT_FAILURE, "bind error");
	}

	if (listen(listenfd, 500) < 0) 
	{
		err(EXIT_FAILURE, "listen error");
	}

	return listenfd;
}

//call reading, parsing, responding
void handle_connection(int connfd, int serverfd) {
	//printf("in handle conn\n");
	struct ClientReq A;
	clear(&A);
	while(valid == 0)
	{
		if(issue == 1)
		{
			issue = 0;
			break;
		}
		read_req(connfd, serverfd, &A);
		respond(connfd, serverfd, &A);
		clear(&A);
		//printf("valid = %d\n", valid);
	}
	//reset static ints
	valid = 0; 
	close(connfd);   //closing socket
}

int main(int argc, char *argv[]) {
	int listenfd = 0;
	int clientfd = 0;
  	uint16_t port = 0;			//start httprpoxy on this port
  	uint16_t serverport = 0; 	//communicates with server running on this port
  	int opt = 0;

  	//default parameters
  	int capacity = 3; 	//capacity of cache 
  	int mode = 0; 		//0 = FIFO, 1 = LRU

  	if (argc < 3 || argc > 8) //check number of args 
  	{
  		errx(EXIT_FAILURE, "wrong arguments: %s port_num", argv[0]);
  	}
  	int count = 0;
  	//parse args from command line
  	while((opt = getopt(argc, argv, "c:m:u")) != -1)
  	{
  		switch(opt)
  		{
  			case 'c':
  					capacity = atoi(optarg); //change cache capacity
  					break;
  			case 'm':
  					maxsize = atoi(optarg); //change max file size
  					break;
  			case 'u':
  					mode = 1; 				//use LRU instead of FIFO
  					default:
 			break;
  		}
  	}

  	//printf("c: %d m: %d u: %d\n", capacity, maxsize, lru);
  	ch = cache_init(capacity, mode); //initialize cache
  	//parse both ports, check for error?
  	port = strtouint16(argv[argc-2]);
  	serverport = strtouint16(argv[argc-1]);

  	//printf("port: %d serverport: %d\n", port, serverport);

  	if(port == 0 || serverport == 0) 
  	{
  		errx(EXIT_FAILURE, "invalid port number: %s", argv[1]); //change arv[1]
  	}

  	listenfd = create_listen_socket(port);
  	clientfd = create_client_socket(serverport); 


  	while(1) 
  	{
  		int connfd = accept(listenfd, NULL, NULL);
  	//	printf("connfd: %d\n", connfd);
  		if (connfd < 0) 
  		{
  			warn("accept error");
  			continue;
  		}
  		handle_connection(connfd, clientfd); //processes requests from listenfd
  	}
  	return EXIT_SUCCESS;
  }
