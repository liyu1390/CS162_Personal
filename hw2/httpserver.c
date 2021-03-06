#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "libhttp.h"

/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;

void send_data_helper(char *mime, struct stat path_stat, char* str, int fd){
  char buffer[64];
  http_start_response(fd, 200);
  http_send_header(fd, "Content-Type", http_get_mime_type(mime));
  sprintf(buffer, "%lu", path_stat.st_size);
  http_send_header(fd, "Content-Length", buffer);
  http_end_headers(fd);

  FILE* file = fopen(str, "rb");

  size_t bytes_read;
  while ( (bytes_read = fread(buffer, 1, 64, file)) == 64) {
      http_send_data(fd, buffer, bytes_read);
  }
  http_send_data(fd, buffer, bytes_read);
  close(fd);
}

/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 */
void handle_files_request(int fd) {

  /* YOUR CODE HERE (Feel free to delete/modify the existing code below) */
  struct http_request *request = http_request_parse(fd);
  struct stat path_stat;
  char str[64] = "";
  strcat(str,server_files_directory);
  strcat(str,request->path);
  stat(str,&path_stat);

  if(S_ISREG(path_stat.st_mode)){
    send_data_helper(request->path, path_stat, str, fd);

  }else if(S_ISDIR(path_stat.st_mode)){
    char dir[64];
    strcpy(dir,str);
    strcat(str, "index.html");
    stat(str,&path_stat);
    if(S_ISREG(path_stat.st_mode)){
      send_data_helper("index.html", path_stat, str, fd);
    }else{
      http_start_response(fd, 200);
      http_send_header(fd, "Content-Type", "text/html");
      http_end_headers(fd);

      struct dirent *pDirent;
      DIR *pDir;
      pDir = opendir(dir);
      char link[1248];
      while ((pDirent = readdir(pDir)) != NULL) {
        strcpy(link,"");
        strcat(link,"<a href='");
        strcat(link,pDirent->d_name);
        strcat(link,"'>");
        strcat(link,pDirent->d_name);
        strcat(link,"</a>\n");
        http_send_data(fd, link, strlen(link));
      }
      strcpy(link,"<a href='../'>Parent directory</a>");
      http_send_data(fd, link, strlen(link));
      http_end_headers(fd);
    }
  }else{
    http_start_response(fd, 404);
    http_end_headers(fd);
  }
}


/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */
void handle_proxy_request(int fd) {

  /* YOUR CODE HERE */
  struct sockaddr_in server_address;
  memset(&server_address, 0, sizeof(server_address));
  struct hostent *host = gethostbyname(server_proxy_hostname);
  int socket_fd = socket(PF_INET, SOCK_STREAM, 0);
  server_address.sin_family = AF_INET;
  inet_aton(host->h_addr,&server_address.sin_addr);
  server_address.sin_port = htons(server_proxy_port);
  connect(socket_fd, (struct sockaddr*)&server_address, sizeof(server_address));

  int nfds;
  int retval;
  fd_set rfds;
  struct timeval tv;
  tv.tv_sec = 2;
  tv.tv_usec = 0;
  if(fd > socket_fd){
    nfds = fd + 1;
  }else{
    nfds = socket_fd + 1;
  }
  char buffer[64];
  int size_read;

  while(1){
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    FD_SET(socket_fd, &rfds);
    retval = select(nfds, &rfds, NULL, NULL, &tv);
    if(retval == -1){
       perror("Select failed.\n");
    }
    else if(retval){
      if(FD_ISSET(fd, &rfds)){
        while ((size_read = read(fd, buffer, sizeof(buffer))) == sizeof(buffer)){
          write(socket_fd, buffer, size_read);
        }
        write(socket_fd, buffer, size_read);

      }
      if(FD_ISSET(socket_fd, &rfds)){
        while ((size_read = read(socket_fd, buffer, sizeof(buffer))) == sizeof(buffer)){
          write(fd, buffer, size_read);
        }
        write(fd, buffer, size_read);
      }
    }else{
       printf("No data written to pipe in 2 last seconds.\n");
    }
  }




}

/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *socket_number, void (*request_handler)(int)) {

  struct sockaddr_in server_address, client_address;
  size_t client_address_length = sizeof(client_address);
  int client_socket_number;
  pid_t pid;

  *socket_number = socket(PF_INET, SOCK_STREAM, 0);
  if (*socket_number == -1) {
    perror("Failed to create a new socket");
    exit(errno);
  }

  int socket_option = 1;
  if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
        sizeof(socket_option)) == -1) {
    perror("Failed to set socket options");
    exit(errno);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(server_port);

  if (bind(*socket_number, (struct sockaddr *) &server_address,
        sizeof(server_address)) == -1) {
    perror("Failed to bind on socket");
    exit(errno);
  }

  if (listen(*socket_number, 1024) == -1) {
    perror("Failed to listen on socket");
    exit(errno);
  }

  printf("Listening on port %d...\n", server_port);

  while (1) {

    client_socket_number = accept(*socket_number,
        (struct sockaddr *) &client_address,
        (socklen_t *) &client_address_length);
    if (client_socket_number < 0) {
      perror("Error accepting socket");
      continue;
    }

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);

    pid = fork();
    if (pid > 0) {
      close(client_socket_number);
    } else if (pid == 0) {
      // Un-register signal handler (only parent should have it)
      signal(SIGINT, SIG_DFL);
      close(*socket_number);
      request_handler(client_socket_number);
      close(client_socket_number);
      exit(EXIT_SUCCESS);
    } else {
      perror("Failed to fork child");
      exit(errno);
    }
  }

  close(*socket_number);

}

int server_fd;
void signal_callback_handler(int signum) {
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing socket %d\n", server_fd);
  if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
  exit(0);
}

char *USAGE =
  "Usage: ./httpserver --files www_directory/ --port 8000\n"
  "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  signal(SIGINT, signal_callback_handler);

  /* Default settings */
  server_port = 8000;
  void (*request_handler)(int) = NULL;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      free(server_files_directory);
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char *proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char *colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char *server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  if (server_files_directory == NULL && server_proxy_hostname == NULL) {
    fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                    "                      \"--proxy [HOSTNAME:PORT]\"\n");
    exit_with_usage();
  }

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
