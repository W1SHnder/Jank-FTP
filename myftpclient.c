#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> 
#include <dirent.h>
#include <arpa/inet.h>
#include <regex.h>
 

int parse_data_ip(char* response, char* ip, int* port) 
{
  printf("Response: %s\n", response);
  regex_t regex;
  regmatch_t match[7];
  char* pattern = "227 Entering Passive Mode \\(([0-9]+),([0-9]+),([0-9]+),([0-9]+),([0-9]+),([0-9]+)\\)";
  if (regcomp(&regex, pattern, REG_EXTENDED) != 0) { 
    printf("Error compiling regex\n");
    return -1; 
  } 
  if (regexec(&regex, response, 7, match, 0)) { 
    printf("Error executing regex\n");
    return -1; 
  }

  int i;
  for (i = 1; i < 7; i++) { 
    int len = match[i].rm_eo - match[i].rm_so;
    if (i < 5) {
      char* temp = malloc(len + 1);
      strncpy(temp, response + match[i].rm_so, len);
      temp[len] = '\0';
      strcat(ip, temp);
      if (i < 4) { strcat(ip, "."); }
      free(temp);
    } else if (i == 5) {
      char* temp = malloc(len + 1);
      strncpy(temp, response + match[i].rm_so, len);
      temp[len] = '\0';
      *port = atoi(temp);
      free(temp);
    } else {
      char* temp = malloc(len + 1);
      strncpy(temp, response + match[i].rm_so, len);
      temp[len] = '\0';
      *port = *port * 256 + atoi(temp);
      free(temp);
    } //if
  } //for
  regfree(&regex);
  return 0;
} //parse_data_ip

//Add regex client side to parse responses. Maybe upgrade server too
//Create client side command functions
int data_init(int ctrl_sock) {
  //recieve data port
  char* response = (char*)malloc(512);
  recv(ctrl_sock, response, 1024, 0);
  int data_port;
  char* data_ip = (char*)malloc(32);
  if (parse_data_ip(response, data_ip, &data_port)) {
    printf("Error parsing data IP\n");
    free(response);
    return -1;
  } //if
  printf("Data IP: %s\n", data_ip);
  printf("Data Port: %d\n", data_port);
  struct sockaddr_in data_addr;
  data_addr.sin_family = AF_INET;
  data_addr.sin_addr.s_addr = inet_addr(data_ip);
  data_addr.sin_port = htons(data_port);
  int data_sock = socket(AF_INET, SOCK_STREAM, 0);
  //int data_conn = -1;
  if ((connect(data_sock, (struct sockaddr *) &data_addr, sizeof(data_addr))) < 0) {
    printf("Error connecting to data socket\n");
  } //if
  free(response);
  return data_sock;
} //data_init

int exec_ls(int sock) {
  
  char* cmd = "LIST\r\n";
  int sent = 0;
  if ((sent = send(sock, cmd, strlen(cmd), 0)) < 0) { return -1; }
  printf("Sent %d bytes\n", sent);
  int eof = 0;
  char response[512];
  while (!eof) {
    if(recv(sock, &response, sizeof(response), 0) > 0) {
      //printf("%d\n", strcmp(response, "EOF\r\n")); 
      if (strcmp(response, "EOF\r\n")) {
	char* entry = strtok(response, "\r\n");
	printf("%s\n", entry);
	memset(response, 0, sizeof(response));
      } else {
	eof = 1;
      } //if
    } else {
      return -1;
    } //if 
  } //while
  //free(response);
  return 0;
} //exec_ls

int exec_retr(int sock, char* filename) 
{
  char cmd[256];
  sprintf(cmd, "RETR %s\r\n", filename);
  printf("%s\n", cmd);
  if (send(sock, cmd, strlen(cmd), 0) < 0) { return -1; } 
  FILE* new_file = fopen(filename, "w");
  if (new_file == NULL) { 
    perror("fopen in RETR");
    return -1;
  } //if
  
  int data_sock = data_init(sock);
  printf("Data sock: %d\n", data_sock);
  char* buf = (char*)malloc(1024);
  int eof = 1;
  while (eof) {
    if(recv(data_sock, buf, 1024, 0) < 0) {
      perror("recv in RETR");
      fclose(new_file);
      free(buf);
      return -1;
    } //if
    if (strcmp(buf, "EOF\r\n")) { 
      printf("buf: %s\n", buf); 
      fwrite(buf, 1, strlen(buf), new_file);
      memset(buf, 0, 1024);
    } else {
      eof = 0;
    } //if
  } //while
  fclose(new_file);
  free(buf);
  return 0;
} //exec_retr


int exec_stor(int sock, char* filename)
{
  char cmd[256];
  sprintf(cmd, "STOR %s\r\n", filename);
  if (send(sock, cmd, strlen(cmd), 0) < 0) { return -1; }
  FILE* file = fopen(filename, "r");
  if (file == NULL) { 
    perror("fopen in STOR");
    return -1;
  } //if
  int data_sock = data_init(sock);
  char* buf = (char*)malloc(1024);
  int eof = 0;
  while (!eof) {
    int r = fread(buf, sizeof(char), 1024, file);
    printf("buf: %s\n", buf);
    if (r < 0) {
      perror("fread in STOR");
      fclose(file);
      free(buf);
      return -1;
    } //if
    if (r < 1024) {
      eof = 1;
    } //if
    if (send(data_sock, buf, r, 0) < 0) {
      perror("send in STOR");
      fclose(file);
      free(buf);
      return -1;
    } //if
    memset(buf, 0, 1024);
  } //while
  char* eof_msg = "EOF\r\n";
  if (send(data_sock, eof_msg, strlen(eof_msg), 0) < 0) {
    perror("send in STOR");
    fclose(file);
    free(buf);
    return -1;
  } //if
  fclose(file);
  free(buf);
  return 0;
} //exec_stor


int data_sock_test(int conn) {
  int data_sock = data_init(conn);
  if (data_sock < 0) {
    printf("Error initializing data socket\n");
    return -1;
  } //if
  char* cmd = "TEST\r\n";
  int r = -1;
  if ((r = send(data_sock, cmd, strlen(cmd), 0)) < 0) {
    printf("Error sending test command\n");
    return -1;
  } //if
  printf("Sent %d bytes\n", r);

  char response[64];
  if ((r = recv(data_sock, response, sizeof(response), 0)) < 0) {
    printf("Error recieving test response\n");
    return -1;
  } //if
  printf("Recieved %d bytes\n", r);
  printf("test msg: %s\n", response);
  return 0;
} //data_sock_test
    

int main(int argc, char *argv[]) 
  {
    char *serverName = argv[1];
    char *serverPort = argv[2];
    int portNum	= atoi(serverPort);
    
    int sock;
    struct sockaddr_in server;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock > 0) {
     server.sin_addr.s_addr = inet_addr(serverName);
     server.sin_family = AF_INET;
     server.sin_port = htons(portNum);
     if (connect(sock, (struct sockaddr *) &server, sizeof(server)) < 0) {
       printf("Connection Failed\n");
       exit(EXIT_FAILURE);
     } //if
    } else {
      printf("Failed to get socket\n");
      exit(EXIT_FAILURE);
    } //if 

    //data_sock_test(sock);

    int quit = -1;
    while (quit < 0)
      {
        printf("myftp>");
	char input[256];
	if (fgets(input, 256, stdin) != NULL)
	  {
	    char * temp = strtok(input, "\r\n");
	    char* command = strtok(temp, " ");
	    char *arg = strtok(NULL, " ");

	    if (strcmp(command, "quit") == 0) {
		printf("Quitting. Goodbye...\n");
	        char* cmd = "QUIT\r\n";
		send(sock, cmd, strlen(cmd), 0);
		close(sock);
		quit = 1;
            } else if (strcmp(command, "ls") == 0) {	
	      if (exec_ls(sock) < 0) {
		printf("Error executing ls\n");
	      } //if
	    } else if (strcmp(command, "pwd") == 0) { 
	      char* cmd = "PWD\r\n";
	      send(sock, cmd, strlen(cmd), 0);
	      listen(sock, 1);
	      char out[256];
	      recv(sock, out, 256, 0);
	      printf("%s\n", out);
	    } else if (strcmp(command, "cd") == 0) {
		char cmd[256]; 
		sprintf(cmd, "CWD %s\r\n", arg);
		send(sock, cmd, strlen(cmd), 0);
	    } else if (strcmp(command, "get") == 0) {
	        if (exec_retr(sock, arg)) {
		  printf("Error executing get\n"); 
		} //if
	    } else if (strcmp(command, "put") == 0) { 
	      if (exec_stor(sock, arg)) {
		printf("Error executing put\n");
	      } //if
	    } else if (strcmp(command, "delete") == 0) {
		char cmd[256];	
		sprintf(cmd, "DELE %s\r\n", arg);
		send(sock, cmd, strlen(cmd), 0);
	    } else if (strcmp(command, "mkdir") == 0) {
		char cmd[256];
		sprintf(cmd, "MKD %s\r\n", arg);
		send(sock, cmd, strlen(cmd), 0);
	    } else {
		printf("Command not recognized\n");
	    } //if
	  } else {
	    printf("STDIO Error\n");
          } //end if
	} //end while
  } //end main


