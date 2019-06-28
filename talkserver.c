#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/time.h>

struct user {

	struct sockaddr_in cli_addr;
	int id;
	int socket;
	struct user *next;

};

#define maxSize 4000

int sockfd, new_socket, portno, n;
socklen_t clilen;
struct sockaddr_in serv_addr;

#define maxUsers 100

struct user *root;
int usersConnected = 0;
int nextId = 0;

void writeToAll( struct user *user, char *msg);

void removeUser(struct user *user) {

	struct user *previous;
	struct user *current = root;
	while(current != NULL) {

		if(current->socket == user->socket) {
			if( previous != NULL) {
				if(current->next != NULL) {
					previous->next = current->next;
					current = NULL;
				} else {
					previous->next = NULL;
				}
			} else { //This must be the root
				if(current->next != NULL) {
					root = current->next;
					current = NULL;
				} else {
					root = NULL;
				}
			}
		}
		previous = current;
		current = current->next;

	}
	close(user->socket);
	usersConnected = usersConnected - 1;
}

void addUser(struct sockaddr_in cli_addr, int socket) {

	struct user *user = (struct user*)malloc(sizeof(*user));

	user->cli_addr = cli_addr;
	user->socket = socket;
	user->id = nextId;
	nextId++;

	//Adds it to the front
	user->next = root;
	root = user;

	printf("Client Connected: %s, ID: %d, Socket Descriptor: %d\n", inet_ntoa(user->cli_addr.sin_addr), user->id, socket);

	usersConnected = usersConnected + 1;

}

void disconnectUser(struct user *user) {

	printf("Client disconnected: %s, ID: %d\n", inet_ntoa(user->cli_addr.sin_addr), user->id);
	
	struct user *current = root;

	while(current != NULL) {
		if(current->socket != user->socket) {
			char *output;
			sprintf(output, "[SERVER]: %s(%d) disconnected\n", inet_ntoa(user->cli_addr.sin_addr), user->id);
			writeToAll(current, output);
		}
		current = current->next;
	}

	removeUser(user);

}

//Write to all users except the one specified
void writeToAll(struct user *except, char *msg) {

	msg[strlen(msg)] = '\n';
	//msg[strlen(msg)-1] = '\0';
	int n = 0;
	struct user *current = root;

	while(current != NULL) {

		if(current->socket != except->socket) {
			if((n = (write(current->socket, msg, strlen(msg)))) < 0) {
				printf("ERROR while writing to user: %s(%d)\n", inet_ntoa(current->cli_addr.sin_addr), current->id);
				removeUser(current);
			} else {
				//printf("Wrote to user: %s\n", msg);
			}
		}
		current = current->next;

	}

}

void writeToUser(struct user *user, char *msg, int addNewLine) {

	msg[strlen(msg)] = '\n';
	int n = 0;

	if((n = (write(user->socket, msg, strlen(msg)))) < 0) {
		printf("ERROR while writing to user: %s\n", inet_ntoa(user->cli_addr.sin_addr));
		removeUser(user);	
	}

}

int main(int argc, char *argv[]){
	
	//Check to make sure the port was given	
	if (argc < 2) {
        fprintf(stderr,"ERROR, no port provided\n");
        exit(1);
   	}

	//Open the socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
		printf("ERROR opening socket\n");

	//printf("%d", sockfd);

	//Bind the port	
	bzero((char *) &serv_addr, sizeof(serv_addr));
	portno = atoi(argv[1]);
	serv_addr.sin_family = AF_INET;   
    serv_addr.sin_addr.s_addr = INADDR_ANY;   
    serv_addr.sin_port = htons(portno);   
         
	if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
		printf("ERROR while binding\n");
		exit(0);
	}
         
    if (listen(sockfd, 5) < 0) {   
        printf("ERROR while listening for connections\n");   
   		exit(0); 
	} 
		

	while(1) {   

		fd_set readfds;

        //clear the socket set  
        FD_ZERO(&readfds);   
     
        //add master socket to set  
        FD_SET(sockfd, &readfds);   
        int max_sd = sockfd;
   		struct user *current = root;


		while(current != NULL) {

			FD_SET(current->socket, &readfds);				

			//printf("Added socket\n");

			current = current->next;

		}

        //wait for an activity on one of the sockets , timeout is NULL ,  
        //so wait indefinitely  
		//printf("Waiting for activity\n");
        if ((select( max_sd + usersConnected + 1 , &readfds , NULL , NULL , NULL)) < 0) {   
            printf("ERROR when waiting for messages\n");   
        }   
           
		struct sockaddr_in cli_addr;
		int clilen = sizeof(cli_addr);
        //If something happened on the master socket ,  
        //then its an incoming connection  
        if (FD_ISSET(sockfd, &readfds)) {   
            if ((new_socket = accept(sockfd, (struct sockaddr *)&cli_addr, (socklen_t*)&clilen))<0) { 
                printf("ERROR when accepting connection\n");   
            }   
            //send new connection greeting message  
			if(usersConnected < 100) {
				//Add new user
            	addUser(cli_addr, new_socket);
			} else {
				if( send(new_socket, "Server full", strlen("Server full"), 0) != strlen("Server full") ) {   
               		printf("ERROR when sending full message\n");   
					close(new_socket);
            	}
				close(new_socket);
			}
			//printf("Found connection\n");
		}   
             
        //else its some IO operation on some other socket 
		current = root;
		//printf("At message loop");
		while(current != NULL) {
			if (FD_ISSET( current->socket, &readfds)) {   
				char buffer[maxSize+2];
				char currentChar[1];
				//printf("At character loop");
              	for(int i=0; i<sizeof(buffer); i++) {
					//read message
					if((n = read(current->socket, currentChar, 1)) == -1) {
						printf("ERROR while recieving message\n");
						disconnectUser(current);	
					} else if(n == 0) {	//User disconnected
						disconnectUser(current);
					} else {
						if(currentChar[0] == '\n' || i == maxSize -1) {
							char output[maxSize+1+20];
							buffer[strlen(buffer)+1] = '\0';
							sprintf(output, "[%s(%d)]: %s", inet_ntoa(current->cli_addr.sin_addr), current->id, buffer);
							printf("%s\n", output);
							writeToAll(current, output);
							bzero(output, sizeof(output));
							break;
						} else {
							buffer[i] = currentChar[0];
						}
					}
				}
				bzero(currentChar, sizeof(currentChar));
				bzero(buffer, sizeof(buffer));
				//printf("In buffer: %s\n", buffer);
				break;
            } else {
				current = current->next;
			}
		}
	}
    close(sockfd);
    return 0; 
}