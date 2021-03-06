#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h> 
#include <pthread.h> //multithreading
#include <sys/ioctl.h> //Window sizes
#include <ncurses.h> //ncurses
#include <math.h>
#include <signal.h>

int gottenVersion = 0;
char version[20];

#define maxSize 4000
int sockfd, portno;
struct sockaddr_in serv_addr;
struct hostent *server;

int maxX = 0;
int maxY = 0;


#define totalStoredMessages 500
char *previousMessages[totalStoredMessages];
int totalMessages = 0;

int viewMessagePosition = 1;
int bottomMessagePosition = 0;

int titleRows = 2;
int inputRows = 2;

int inputCursorPos = 0;
int backCharacterPos = 0;

#define maxRooms 100
char *rooms[maxRooms];
int totalRooms = 0;

int viewRoomPosition = 1;
int bottomRoomPosition = 1;

int inRoom = 0;

char currentRoomString[maxSize];
char currentName[maxSize]; 

//press escape to switch modes
int currentMode = 0;
//0: input
//1: view

char input[maxSize] = {0};

void writeServer(char *msg);

void refreshRooms() {
	writeServer("/refresh\n");
	memset(previousMessages, 0, sizeof(previousMessages));
	totalRooms = 0;
}

void closeApp(char *reason) {

	close(sockfd);
	endwin();
	printf("%s\n", reason);
	exit(0);
}

void addMessage(char *msg) {

	//Make sure the memory is not already allocated
	if(previousMessages[totalMessages] == NULL) { previousMessages[totalMessages] = malloc(maxSize+1); }

	//Make sure malloc worked
	if(!previousMessages[totalMessages]) {
		return;
	}

	if(totalMessages < totalStoredMessages) {
		strncpy(previousMessages[totalMessages], msg, maxSize);
		totalMessages++;
		bottomMessagePosition++;
	} else {
		for(int i=1; i<sizeof(previousMessages)-1; i++) {
			strncpy(previousMessages[i-1], previousMessages[i], maxSize);
		}
		strncpy(previousMessages[totalStoredMessages-1], msg, maxSize);
	}

}

void addRoom(char *room) {
	
	//Make sure the memory is not already allocated
	if(rooms[totalRooms] == NULL) { rooms[totalRooms] = malloc(maxSize+1); }

	//Make sure malloc worked
	if(!rooms[totalRooms]) {
		return;
	}

	if(totalMessages < maxRooms) {
		strncpy(rooms[totalRooms], room, maxSize);
		totalRooms++;
		bottomRoomPosition++;
	} else {
		for(int i=1; i<sizeof(rooms)-1; i++) {
			strncpy(rooms[i-1], rooms[i], maxSize);
		}
		strncpy(rooms[totalRooms-1], room, maxSize);
	}

}

void redrawScreen() {

	endwin();
	refresh();

	clear();

    struct winsize w;
    ioctl(0, TIOCGWINSZ, &w);
    maxX = w.ws_col;
    maxY = w.ws_row;

	wresize(stdscr, maxY, maxX);

	//getmaxyx(stdscr, maxY, maxX);

	//Draw messages / rooms
	int p=maxY-inputRows;
	if(inRoom == 1) {
		for(int i=bottomMessagePosition; i>0; i--) {
			if(p <= titleRows) { break; }
			if(previousMessages[i] != NULL) {
				p = p - 1 - floor(strlen(previousMessages[i])/maxX);
				if(viewMessagePosition == i) {
					attron(COLOR_PAIR(1));
					mvprintw(p, 0, "%s\n", previousMessages[i]);
					attroff(COLOR_PAIR(1));
				} else {
					mvprintw(p, 0, "%s\n", previousMessages[i]);
				}
			}
		}
	} else {
		//Draw messages
		for(int i=bottomMessagePosition; i>-1; i--) {
			if(p <= ceil((maxY-inputRows-titleRows)*(3.0/4) + titleRows)-1) { break; }
			if(previousMessages[i] != NULL) {
				p = p - 1 - floor(strlen(previousMessages[i])/maxX);
				//Do not do any highlighting here because you cannot scroll messages when looking for rooms
				mvprintw(p, 0, "%s\n", previousMessages[i]);
			}
		}

		p = ceil((maxY-inputRows-titleRows)*(3.0/4) + titleRows)-1;
	
		p--;

		//Draw separator
		for(int i=0; i<maxX; i++) mvprintw(p, i, "-"); //Separator
		mvprintw(p, maxX/2 - strlen("MESSAGES")/2, "MESSAGES");

		//Draw rooms	
		for(int i=bottomRoomPosition; i>-1; i--) {
			if(p <= titleRows) { break; }
			if(rooms[i] != NULL) {
				p = p - 1 - floor(strlen(rooms[i])/maxX);
				if(viewRoomPosition == i) {
					attron(COLOR_PAIR(1));
					mvprintw(p, 0, "%s\n", rooms[i]);
					attroff(COLOR_PAIR(1));
				} else {
					mvprintw(p, 0, "%s\n", rooms[i]);
				}
			}
		} 

		//If there are no rooms say so
		if(totalRooms == 0) {
			p = ceil((maxY-inputRows-titleRows)*(3.0/4)/2 + titleRows)-1;
			mvprintw(p, maxX/2 - strlen("No Rooms! /create [name] to make one")/2, "No Rooms! /create [name] to make one");
		}
	}

	//Draw Title
	mvprintw(0, maxX/2 - strlen("TALK")/2, "TALK"); //centered
	for(int i=0; i<maxX; i++) mvprintw(1, i, "-"); //Separator

	if(inRoom == 0) { mvprintw(1, maxX/2 - strlen("ROOMS - /refresh to refresh")/2, "ROOMS - /refresh to refresh"); }
	else { mvprintw(1, maxX/2 - strlen("MESSAGES")/2, "MESSAGES"); }

	//Name
	mvprintw(0, maxX - strlen(currentName), currentName);

	//Current mode
	if(currentMode == 0) {
		mvprintw(maxY-1, maxX - strlen("INSERT (ESC)"), "INSERT (ESC)");
		if(inRoom == 1) { 
			mvprintw(0,0,"%s", currentRoomString);
		} else {
			mvprintw(0,0,"%s:%d", inet_ntoa(serv_addr.sin_addr), portno);
		}
	} else if(currentMode == 1) {
		mvprintw(maxY-1, maxX - strlen("VIEW (ESC)"), "VIEW (ESC)");
		mvprintw(0,0,"Scroll: hjkl");
	}

	//Draw serparator
	for(int i=0; i<maxX; i++) mvprintw(maxY-2, i, "-"); //Separator

	//Below is the input area
	p=0;
	for(int i=backCharacterPos; i<strlen(input); i++) {
		if(p>=maxX-strlen("INSERT (ESC)")-1) break;
		if(i == 0 && strlen(input) > maxX) {
			i=strlen(input) - maxX;
		}
		mvprintw(maxY-1, p, "%c", input[i]);
		p++;
	}

	//Move the cursor to the right place
	move(maxY-1,inputCursorPos - backCharacterPos);

	refresh();

}

void writeServer(char *msg) {
	int n = 0;
	if((n = (write(sockfd, msg, strlen(msg)))) < 0) {
		closeApp("ERROR while writing message\n");
	} else if(n == 0) {
		closeApp("Connection lost (Server shutdown or force disconnect\n");
	}
}

void *readServer() {
	char buffer[maxSize];
	memset(buffer, 0, sizeof(buffer));
	char current[1];
	memset(current, 0, sizeof(current));
	int n = 0;
	int readingRooms = 0;
	int readingCurrentRoom = 0;
	int readingName = 0;
	char roomsTag[7] = "<rooms>";
	char joinedTag[8] = "<joined>";
	char newNameTag[9] = "<newName>";
	char leftTag[6] = "<left>";
	while(1) {
		//Read message character by character
		for(int i=0; i<maxSize; i++) {
			if((n = read(sockfd, current, 1)) < 0) {
				closeApp("ERROR while receiving message\n");	
			} else if (n == 0) {
				closeApp("Connection lost (Server shutdown or force disconnect)\n");
			} else {
				if(current[0] == '\n' || i == maxSize-1) {
					buffer[i+1] = '\0';
					//Check to make sure the server and client are the same version
					if(!gottenVersion) {
						if(strncmp(buffer, version, sizeof(version)) != 0) {
							char versionError[maxSize];
							memset(versionError, 0, sizeof(versionError));
							snprintf(versionError, sizeof(versionError), "Versions do not match! Server: %s, Client: %s\n", buffer, version);
							closeApp(versionError);
						} else {
							gottenVersion = 1;
							memset(buffer, 0, sizeof(buffer)); //No need to print this to the user
						}
					}
					if(readingRooms == 1) {
						if(strcmp(buffer, "") != 0) {
							addRoom(buffer);
						}
					} else if(readingCurrentRoom == 1) {
						if(strcmp(buffer, "") != 0) {
							strncpy(currentRoomString, buffer, sizeof(currentRoomString));
						}
					} else if(readingName == 1) {
						if(strcmp(buffer, "") != 0) {
							strncpy(currentName, buffer, sizeof(currentName));
						}
					} else {
						if(strcmp(buffer, "") != 0) {
							addMessage(buffer);
						}
					}
					redrawScreen();
					break;
				} else {
					//Determine if still reading the rooms
					if(readingCurrentRoom == 0 && readingName == 0 && strncmp(buffer, roomsTag, sizeof(roomsTag)-1) == 0) {
						memset(buffer, 0, sizeof(buffer));
						if(readingRooms == 1) {
							readingRooms = 0;	
						} else {
							memset(rooms, 0, sizeof(rooms));
							totalRooms = 0;
							readingRooms = 1;
						}
					} else if(readingRooms == 0 && readingName == 0 && strncmp(buffer, joinedTag, sizeof(joinedTag)-1) == 0) {
						//The user has successfully joined the room
						inRoom = 1;
						memset(previousMessages, 0, sizeof(previousMessages));
						totalMessages=0;
						memset(buffer, 0, sizeof(buffer));
						if(readingCurrentRoom == 1) {
							readingCurrentRoom = 0;	
						} else {
							//memset(currentRoomString, 0, sizeof(currentRoomString));
							readingCurrentRoom = 1;
						}
						redrawScreen();
					} else if(readingRooms == 0 && readingCurrentRoom == 0 && strncmp(buffer, newNameTag, sizeof(newNameTag)-1) == 0) {
						//The user has successfully changed their name
						memset(buffer, 0, sizeof(buffer));
						if(readingName == 1) {
							readingName = 0;	
						} else {
							readingName = 1;
						}
						redrawScreen();
					} else if(readingRooms == 0 && readingCurrentRoom == 0 && strncmp(buffer, leftTag, sizeof(leftTag)-1) == 0) {
						//The user has successfully left the room
						inRoom = 0;
						memset(buffer, 0, sizeof(buffer));
						memset(previousMessages, 0, sizeof(previousMessages));
						memset(currentRoomString, 0, sizeof(currentRoomString));
						totalMessages=0;
						refreshRooms();
						redrawScreen();
					}
					buffer[i] = current[0];
				}
				memset(current, 0, sizeof(current));
			}
		}
		memset(buffer, 0, sizeof(buffer));
		memset(current, 0, sizeof(current));
	}

}

void *getInput() {

	while(1) {
		while(1) {
			bool finished = false;
			int key = getch();
			if(key == KEY_RESIZE) { redrawScreen(); continue; }
			if(currentMode == 0) { //Insert mode
				switch(key) {
					case 27: //escape (switch modes)
						currentMode = 1;
						redrawScreen();
						continue;
						break;
					//if (c == 26) { continue; }
					case 127: //delete or backspace
					case 8:
						if(inputCursorPos > 0) {
							for(int i=inputCursorPos; i<strlen(input); i++) {
								input[i-1] = input[i];
							}
							input[strlen(input)-1] = 0;
							inputCursorPos--;
							redrawScreen();
						}	
						continue;
						break;
					case 10: //New line
						finished=true;
						break;
				}

				if(strlen(input) > 1) {
					char resize[3] = "~Z";
					char lastTwo[3];
					lastTwo[0] = input[strlen(input)-2];
					lastTwo[1] = input[strlen(input)-1];
					lastTwo[2] = '\0';
					if(strncmp(lastTwo, resize, 2) == 0) {
						input[strlen(input)-2] = '\0';
						input[strlen(input)-1] = 0;
					}
				}

				if(finished) { break; }
				if (!finished && strlen(input) < sizeof(input)-2) {
					if(inputCursorPos >= strlen(input)) {
						input[strlen(input)] = key;
					} else {
						for(int i=strlen(input)-1; i>inputCursorPos; i--) {
							input[i+1] = input[i];
						}
						input[inputCursorPos] = key;
					}
					inputCursorPos++;
					if(inputCursorPos == backCharacterPos+maxX-strlen("INSERT") - 1) { backCharacterPos++; }
				}	
				redrawScreen();
			} else if(currentMode == 1) { //View
				switch(key) {
					case 27: //escape (switch modes)
						currentMode = 0;
						redrawScreen();
						break;
					case 108: //l
						if(inputCursorPos < strlen(input)) { //right
							inputCursorPos++; 
							if(inputCursorPos == backCharacterPos+maxX-strlen("INSERT") - 1) { backCharacterPos++; }	
						}
						break;
					case 104: //h
						if(inputCursorPos > 0) { //left
							inputCursorPos--;
							if(inputCursorPos == backCharacterPos) { backCharacterPos--; }	
						}
						break;	
					case 107: //k
						if(inRoom == 1 && viewMessagePosition-1 > 0) { //Up ceil((maxY-inputRows-titleRows)*(3.0/4) + titleRows)-1
							viewMessagePosition--;
							if(viewMessagePosition <= bottomMessagePosition - maxY + titleRows + inputRows) { bottomMessagePosition-=1; }
						} else if(inRoom == 0 && viewRoomPosition-1 > -1) {
							viewRoomPosition--;
							if(viewRoomPosition <= bottomRoomPosition - ceil((maxY-inputRows-titleRows)*(3.0/4)) + titleRows) { bottomRoomPosition-=1; }
						}
						break;
					case 106: //j
						if(inRoom == 1 && viewMessagePosition+1 < totalMessages) { //Down
							viewMessagePosition++;
							if(viewMessagePosition >= bottomMessagePosition +1) { bottomMessagePosition+=1; }
						} else if(inRoom == 0 && viewRoomPosition+1 < totalRooms) { 
							viewRoomPosition++;
							if(viewRoomPosition >= bottomRoomPosition+1) { bottomRoomPosition+=1; }
						}
						break;	
				}
				redrawScreen();
			}
		}
		input[strlen(input)] = '\n';
		//input[strlen(input)+2] = '\0';

		writeServer(input);
		memset(input, 0, sizeof(input));
		inputCursorPos = 0;
		backCharacterPos = 0;
	}

}

int main(int argc, char *argv[]) {

	FILE *fp;

	if((fp = fopen("VERSION", "r")) == NULL) {
		printf("ERROR: No version file.\n");
		exit(1);
	}

	fscanf(fp, "%[^\n]", version);

    if (argc < 3) {
    	fprintf(stderr,"usage %s hostname port\n", argv[0]);
		exit(1);	
	}
    portno = atoi(argv[2]);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
		printf("ERROR opening socket\n");
		exit(1);	
	}

    server = gethostbyname(argv[1]);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
		exit(1);	
	}
    memset((char *) &serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(portno);
    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) {
        printf("ERROR connecting\n");
 		exit(0);	
	}

	initscr();
	noecho();

	struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = redrawScreen;
    sigaction(SIGWINCH, &sa, NULL);
	
	//create colors
	start_color();

	init_pair(1, COLOR_BLACK, COLOR_WHITE);

	//Create threads for reading and writing
	pthread_t readThread;
	pthread_t writeThread;	
	
	pthread_create(&readThread, NULL, readServer, NULL);
	refreshRooms();
	pthread_create(&writeThread, NULL, getInput, NULL);

	pthread_join(readThread, NULL);
	pthread_join(writeThread, NULL);

	redrawScreen();

	endwin();
}
