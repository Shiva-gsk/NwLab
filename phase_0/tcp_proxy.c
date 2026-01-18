#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/epoll.h> 

#define PORT 8080
#define BUFF_SIZE 10000
#define MAX_ACCEPT_BACKLOG 5
#define MAX_EPOLL_EVENTS 10
#define UPSTREAM_PORT 3000
#define MAX_SOCKS 10

int listen_sock_fd, epoll_fd;
struct epoll_event events[MAX_EPOLL_EVENTS];
int route_table[MAX_SOCKS][2], route_table_size = 0; //Define MAX_SOCKS=10 as a global variable

// Function to reverse a string
void strrev(char *str) {
  for (int start = 0, end = strlen(str) - 2; start < end; start++, end--) {
    char temp = str[start];
    str[start] = str[end];
    str[end] = temp;
  }
}

int create_server() {
  /* create listening socket and return it */
  int listen_sock_fd = socket(AF_INET, SOCK_STREAM, 0);

  // Setting sock opt reuse addr
  int enable = 1;
  setsockopt(listen_sock_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
  // Creating an object of struct socketaddr_in
  struct sockaddr_in server_addr;

  // Setting up server addr
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(PORT);

  // Binding listening sock to port
  bind(listen_sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));

  // Starting to listen
  listen(listen_sock_fd, MAX_ACCEPT_BACKLOG);
  printf("[INFO] Server listening on port %d\n", PORT);
  return listen_sock_fd;
}


int create_loop() {
  /* create epoll instance and return its fd */
  return epoll_create1(0);
}


void loop_attach(int epoll_fd, int fd, int events) {
  /* attach fd to epoll */
  struct epoll_event event;
  event.events = events;
  event.data.fd = fd;
  epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
}



int connect_upstream() {

  int upstream_sock_fd = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in upstream_addr;
  /* add upstream server details */
  upstream_addr.sin_family = AF_INET;
  upstream_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  upstream_addr.sin_port = htons(UPSTREAM_PORT);


  connect(upstream_sock_fd, (struct sockaddr *)&upstream_addr, sizeof(upstream_addr));

  return upstream_sock_fd;

}


void accept_connection(int listen_sock_fd) {

  int conn_sock_fd = accept(listen_sock_fd, NULL, NULL);
  printf("[INFO] New client connected to server\n");  
  
  /* add conn_sock_fd to loop using loop_attach() */
  loop_attach(epoll_fd, conn_sock_fd, EPOLLIN);

  // create connection to upstream server
  int upstream_sock_fd = connect_upstream();

  /* add upstream_sock_fd to loop using loop_attach() */
  loop_attach(epoll_fd, upstream_sock_fd, EPOLLIN);

  // add conn_sock_fd and upstream_sock_fd to routing table
  route_table[route_table_size][0] = conn_sock_fd;
  route_table[route_table_size][1] = upstream_sock_fd;
  route_table_size += 1;
}



void handle_client(int conn_sock_fd) {
  char buff[BUFF_SIZE];
  memset(buff, 0, BUFF_SIZE);

  int read_n = recv(conn_sock_fd, buff, BUFF_SIZE, 0);

  // client closed connection or error occurred
  if (read_n <= 0) {
    close(conn_sock_fd);
    return;
  }

  /* print client message (helpful for Milestone #2) */
  printf("[CLIENT MESSAGE] %s", buff);

  /* find the right upstream socket from the route table */
  int upstream_sock_fd;
  for(int i=0; i<route_table_size; i++){
    if(route_table[i][0] == conn_sock_fd){
      upstream_sock_fd = route_table[i][1];
      break;
    }
  }


  // sending client message to upstream
  int bytes_written = 0;
  int message_len = read_n;
  while (bytes_written < message_len) {
    int n = send(upstream_sock_fd, buff + bytes_written, message_len - bytes_written, 0);
    bytes_written += n;
  }

}

void handle_upstream(int upstream_sock_fd) {
  char buff[BUFF_SIZE];
  memset(buff, 0, BUFF_SIZE);

  int read_n = recv(upstream_sock_fd, buff, BUFF_SIZE, 0);

  // upstream closed connection or error occurred
  if (read_n <= 0) {
    close(upstream_sock_fd);
    return;
  }

  /* find the right client socket from the route table */
  int conn_sock_fd;
  for(int i=0; i<route_table_size; i++){
    if(route_table[i][1] == upstream_sock_fd){
      conn_sock_fd = route_table[i][0];
      break;
    }
  }

  // sending upstream message to client
  int bytes_written = 0;
  int message_len = read_n;
  while (bytes_written < message_len) {
    int n = send(conn_sock_fd, buff + bytes_written, message_len - bytes_written, 0);
    bytes_written += n;
  }

}

void loop_run(int epoll_fd) {
  while (1) {
    printf("[DEBUG] Epoll wait\n");

    /* epoll wait */

    int n_ready_fds = epoll_wait(epoll_fd, events, MAX_EPOLL_EVENTS, -1);
    for( int i = 0; i < n_ready_fds; i++) {
      int curr_fd = events[i].data.fd;
      if (curr_fd == listen_sock_fd) // event is on listen socket
        accept_connection(listen_sock_fd); // we will implement this later
      else {
        int found = 0;
        for (int r = 0; r < route_table_size; ++r) {
          if (curr_fd == route_table[r][0]) { // client socket
            found = 1;
            handle_client(curr_fd); // implement to accept fd or route index
            break;
          } 
          else if (curr_fd == route_table[r][1]) { // upstream socket
            found = 1;
            handle_upstream(curr_fd);

            break;
          }
        }
        if (!found) {
          // unknown fd -> clean up
          epoll_ctl(epoll_fd, EPOLL_CTL_DEL, curr_fd, NULL);
          close(curr_fd);
        }
      }
    }

  }
  
}



int main() {
  
  // Creating listening sock
  listen_sock_fd = create_server();

  epoll_fd = create_loop();
  loop_attach(epoll_fd, listen_sock_fd, EPOLLIN);

  loop_run(epoll_fd);
  }
