#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <pthread.h>
#include <semaphore.h>


#define MAX_REQUEST 10
enum MODE {PASV, PORT};

int num_requests = 0;
int request[MAX_REQUEST];
enum MODE mode = PASV;

sem_t full;
sem_t empty;
pthread_mutex_t mutex;
pthread_mutex_t data_mutex;

char* server_addr = "127.0.0.1";
int ctrl_port = 2121;


int ctrl_sock_init(int port)
{
  int sock;
  struct sockaddr_in addr;
  sock = socket(AF_INET, SOCK_STREAM, 0);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = inet_addr(server_addr);
  addr.sin_port = htons(port);
  int r;
  r = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
  if (r < 0) {
    perror("Error binding socket:\n");
    return -1;
  } 
  r = listen(sock, 1);
  if (r < 0) { sock = r; }
  return sock;
} //ctrl_sock_init


int data_init_actv(int ctrl_sock)
{
  char* err = "ERR 501\r\n";
  send(ctrl_sock, err, strlen(err), 0);
  printf("Not yet implemented\n");
  return 0;
} //data_init_actv
  

int data_init_pasv(int ctrl_sock)
{
  int data_sock;
  struct sockaddr_in data_addr;
  data_sock = socket(AF_INET, SOCK_STREAM, 0);
  data_addr.sin_family = AF_INET;
  data_addr.sin_addr.s_addr = inet_addr(server_addr);
  
  //get random port
  int data_port;
  data_port = rand() % 47998;
  data_port = data_port + 2000;
  int r;
  do {
    data_port++;
    data_addr.sin_port = htons(data_port);
    r = bind(data_sock, (struct sockaddr*)&data_addr, sizeof(data_addr));
  } while (r < 0);
  r = listen(data_sock, 1);
  if (r < 0) {
    perror("Error listening on socket:\n");
    char* err = "ERR 245\r\n";
    send(ctrl_sock, err, strlen(err), 0);
    return -1;
  } //if
  printf("Data port: %d\n", data_port);
  //send data address to client and accept
  char data_ip[32];
  strncpy(data_ip, server_addr, strlen(server_addr));
  int h1, h2, h3, h4;
  h1 = atoi(strtok(data_ip, "."));
  h2 = atoi(strtok(NULL, "."));
  h3 = atoi(strtok(NULL, "."));
  h4 = atoi(strtok(NULL, "."));
  char buf[1024];
  sprintf(buf, "227 Entering Passive Mode (%d,%d,%d,%d,%d,%d)\r\n", h1, h2, h3, h4, data_port/256, data_port%256);
  printf("%s", buf);
  if (send(ctrl_sock, buf, strlen(buf), 0) < 0) {
    return -1;
  } //if
  r = accept(data_sock, NULL, NULL); 
  return r; 
} //data_init_passive


int data_sock_init(int ctrl_sock)
{
  int ret = -1;
  if (mode == PASV) {
    printf("MODE: PASSIVE\n");
    ret = data_init_pasv(ctrl_sock);
  } else if (mode == PORT) {
    printf("MODE: ACTIVE\n");
    ret = data_init_pasv(ctrl_sock);
  } else {
    perror("Internal Server Error [Init Data Plane]");
  } //if
  return ret;
} //data_init

/*
int data_init_active(char* addr, int port)
{
  int sock;
  struct sockaddr_in data_addr;
  data_addr.sin_family = AF_INET;
  data_addr.sin_addr.s_addr = inet_addr(addr);
  data_addr.sin_port = htons(port);
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (connect(sock, (struct sockaddr*)&data_addr, sizeof(data_addr)) < 0) { return -1; }
  return 0;
} //data_init_active
*/


int ftp_list(int conn)
{
  char* name = ".";
  DIR *dir = opendir(name);
  struct dirent *entry;
  while((entry = readdir(dir)) != NULL) {
    strcat(entry->d_name, "\r\n");
    if (send(conn, entry->d_name, strlen(entry->d_name), 0) < 0) {
      perror("send");
      char* msg = "ERR 550\r\n";
      send(conn, msg, strlen(msg), 0);
      return -1;
    } //if
  } //while 
  char* msg = "EOF\r\n";
  send(conn, msg, strlen(msg), 0);
  closedir(dir);
  return 0;
} //ftp_list



int ftp_pwd(int conn)
{
  char buf[1024];
  getcwd(buf, sizeof(buf));
  strcat(buf, "\r\n");
  if (send(conn, buf, strlen(buf), 0) < 0) {
    perror("send");
    //send error message to client 
    return -1;
  } //if
  return 0;
} //ftp_pwd


int ftp_cwd(int conn, char* arg)
{
  if (chdir(arg) < 0) {
    perror("chdir");
    //send error message to client
    return -1;
  } //if
  //ftp_pwd(conn); //print in client
  return 0;
} //ftp_cwd


int ftp_retr(int conn, char* arg) {
  int data = data_sock_init(conn);
  if (!data) {
    perror("data_sock_init");
    char* msg = "ERR 425\r\n";
    send(conn, msg, strlen(msg), 0);
    return -1;
  } //if
  FILE* file = fopen(arg, "r");
  if (file == NULL) {
    perror("fopen in RETR");
    char* msg = "ERR 550\r\n";
    send(conn, msg, strlen(msg), 0);
    close(data);
    return -1;
  } //if

  char* buf = (char*)malloc(1024);
  int eof = 0;
  while(!eof) {
    int r = fread(buf, sizeof(char), 1024, file);
    if (r < 0) {
      perror("fread in RETR");
      char* msg = "ERR 451\r\n";
      send(data, msg, strlen(msg), 0);
      close(data);
      return -1;
    } //if
    if (r < 1024) { eof = 1; }
    printf("len: %d, buf: %s\n", r, buf);
    if (send(data, buf, r, 0) < 0) {
      perror("Failed data send in RETR");
      return -1;
    } //if
    memset(buf, 0, 1024);
  } //while
  fclose(file);
  printf("reached EOF\n");
  char* eofmsg = "EOF\r\n";
  send(data, eofmsg, strlen(eofmsg), 0);
  close(data);
  return 0;
} //ftp_retr

int ftp_stor(int conn, char* arg) {
  int data = data_sock_init(conn);
  if (data < 0) {
    perror("data_sock_init");
    char* msg = "ERR 425\r\n";
    send(conn, msg, strlen(msg), 0);
    return -1;
  } //if
  FILE* file = fopen(arg, "w");
  if (file == NULL) {
    perror("fopen in STOR");
    char* msg = "ERR 425\r\n";
    send(conn, msg, strlen(msg), 0);
    close(data);
    return -1;
  } //if
  char* buf = (char*)malloc(1024);
  int eof = 0;
  while(!eof) {
    int r = recv(data, buf, 1024, 0);
    if (r < 0) {
      perror("recv in STOR");
      char* msg = "ERR 451\r\n";
      send(conn, msg, strlen(msg), 0); //send over ctrl when multithread for data is implemented
      close(data);
      return -1;
    } //if
    if (r < 1024) { eof = 1; }
    printf("len: %d, buf: %s\n", r, buf);
    if (fwrite(buf, sizeof(char), r, file) < 0) {
      perror("Failed fwrite in STOR");
      return -1;
    } //if 
    memset(buf, 0, 1024);
  } //while
  fclose(file);
  close(data);
  return 0;
} //ftp_stor

int ftp_del(int conn, char* arg) {
  if (remove(arg) < 0) {
    perror("Remove failed");
    //Send message to client
    return -1;
  } //if
  return 0;
} //ftp_del

int ftp_mkd(int conn, char* arg) {
  if (mkdir(arg, 0777) < 0) {
    perror("mkdir");
    //send error message to client
    return -1;
  } //if
  return 0;
} //ftp_mkd

void ftp_quit(int conn) {
  close(conn);
} //ftp_quit


int ftp_error(int conn, char* msg) {
  if (send(conn, msg, strlen(msg), 0) < 0) {
    perror("send");
    //send error message to client
    return -1;
  } //if
  return 0;
} //ftp_error


int data_sock_test(int conn) {
  int data = data_sock_init(conn);
  if (data < 0) {
    perror("data_sock_test");
    return -1;
  } //if
  printf("surely this will work\n");
  char msg[32];
  recv(data, msg, sizeof(msg), 0);
  printf("test msg: %s\n", msg);
  char* msg2 = "TEST\r\n";
  if(send(data, msg2, strlen(msg2), 0) < 0) {
    perror("send");
    return -1;
  } //if
  close(data);
  return 0;
} //data_sock_test

int ftp_request_handler(int conn) 
{
  printf("Request handler\n");
  if (conn < 0) 
  {
    perror("Connection failed\n");
    return -1;
  }
 
  /*
  if (!data_sock_test(conn)) {
    printf("Data test passed\n");
  } else {
    printf("Data test failed\n");
  } //if
  */

  int quit = -1;

  while(quit < 0) 
  {
    char buf[512];
    int len;
    len = recv(conn, buf, sizeof(buf), 0);
    if (len < 0) 
    {
      perror("recv");
    } //if
  
    char* command = strtok(buf, "\r\n");
    char* cmd = strtok(command, " ");
    char* arg = strtok(NULL, " ");
    printf("cmd: %s, arg: %s\n", cmd, arg); 
    if (len != 0)
    {
      if (!strcmp(cmd, "PASV")) {
	mode = PASV;
      } else if (!strcmp(cmd, "PORT")) {
	mode = PORT;
      } else if (strcmp(cmd, "LIST") == 0) {
        if (ftp_list(conn)) { return -1; }
      } else if (strcmp(cmd, "PWD") == 0) {
        if (ftp_pwd(conn)) { return -1; }
      } else if (strcmp(cmd, "CWD") == 0) {
        if (ftp_cwd(conn, arg)) { return -1; }
      } else if (strcmp(cmd, "RETR") == 0) {
        if (ftp_retr(conn, arg)) { return -1; }
      } else if (strcmp(cmd, "STOR") == 0) {
        if (ftp_stor(conn, arg)) { return -1; }
      } else if (strcmp(cmd, "DEL") == 0) {
        if (ftp_del(conn, arg)) { return -1; }
      } else if (strcmp(cmd, "MKD") == 0) {
        if (ftp_mkd(conn, arg)) { return -1; }
      } else if (strcmp(cmd, "QUIT") == 0) {
        ftp_quit(conn);
	quit = 1;
      } else {
        char* msg = "ERR 501\r\n";
        ftp_error(conn, msg);
      } //if
    } //if
  } //while
  return 0;
} //ftp_request_handler


int listener(void* arg) 
{
  int conn;
  //make a struct to hold socket and client address
  while(1) 
  {
    sem_wait(&empty);
    int sock = *(int*)arg;
    conn = accept(sock, NULL, NULL);
    if (conn < 0) {
      perror("Fail on accept");
      exit(1);
    }
    pthread_mutex_lock(&mutex);
    printf("Connection accepted\n");
    request[num_requests] = conn;
    num_requests++;
    pthread_mutex_unlock(&mutex);
    sem_post(&full);
  } //while
} //listener


int worker() 
{
  while(1)
  {
    int conn = -1;

    sem_wait(&full);
    pthread_mutex_lock(&mutex);
    conn = request[num_requests-1];
    num_requests--;
    pthread_mutex_unlock(&mutex);
    sem_post(&empty);

    ftp_request_handler(conn);
  } //while
} //worker


int server(int ctrl_sock, int num_threads) 
{
  pthread_mutex_init(&mutex, NULL);
  sem_init(&full, 0, 0);
  sem_init(&empty, 0, MAX_REQUEST);

  printf("Finished sync init\n");
  pthread_t* workers = malloc(sizeof(pthread_t) * num_threads);
  pthread_t listener_thread;
  if (pthread_create(&listener_thread, NULL, (void*)&listener, &ctrl_sock) < 0) {
    perror("Failed pthread_create for listener");
    exit(1);
  } //if

  printf("Listener created\n");
  
  int i;
  for (i = 0; i < num_threads; i++) {
    if (pthread_create(&workers[i], NULL, (void*)&worker, NULL) < 0) {
      perror("Failed pthread_create for worker");
      exit(1);
    } //if
  } //for


  printf("Workers created\n");

  /*
  int j = 0;
  int* retval;
  *retval = -1;
  while(1) {
    if (j >= num_threads) { j = 0; }
    pthread_tryjoin_np(workers[j], (void**)&retval);
    printf("tryjoin success\n");
    if (retval == 0) {
      workers[j] = 0;
      if (pthread_create(&workers[j], NULL, (void*)&worker, NULL) < 0) {
	perror("Failed pthread_create for worker");
	exit(1);
      } //if
      printf("Thread %d crashed. Replacing...\n", j);
      retval = -1;
    } //if
    j++;
  } //while
  */

  for (i = 0; i < num_threads; i++) {
    pthread_join(workers[i], NULL);
  } //for
  free(workers);
  pthread_join(listener_thread, NULL);
  return 0;
} //server


int main(int argc, char** argv) 
{
  int port;
  int ctrl_sock = -1;
  if (argc != 2) {
    printf("Usage: %s <port>\n", argv[0]);
    exit(1);
  } //if

  port = atoi(argv[1]);
  ctrl_sock = ctrl_sock_init(port);
  if (ctrl_sock < 0) {
    perror("Socket initialization failed");
    exit(1);
  } //if

  server(ctrl_sock, 5);

}//main
