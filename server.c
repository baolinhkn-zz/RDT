#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/socket.h>

#define MAXLINE 1024
#define DATA 1008

struct packet
{
  int type;
  int seq_num;

  int data_size;
  //end of file, fin = 1                                                        
  //middle of file, fin = 0                                                     
  int fin;

  char data[DATA];
};

int main(int argc, char* argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Wrong number of arguments");
  }

  char* hostname = argv[1];
  int port_num = atoi(argv[2]);

  // listen on sock_fd, new connection on new_fd
  int sockfd;
  char buffer[MAXLINE];
  char* hello = "Hello from server";
  struct sockaddr_in servaddr, cliaddr;

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    fprintf(stderr, "Socket creation failed");
    exit(1);
  }

  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = INADDR_ANY;
  servaddr.sin_port = htons(port_num);

  if (bind(sockfd, (const struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
    fprintf(stderr, "Bind failed");
    exit(1);
  }

  int len, n;
  n = recvfrom(sockfd, (char*)buffer, MAXLINE, MSG_WAITALL, (struct sockaddr*)&cliaddr, &len);
  buffer[n] = '\0';
  printf("Client: %s\n", buffer);
  sendto(sockfd, (const char*)hello, strlen(hello), MSG_CONFIRM, (const struct sockaddr*)&cliaddr, len);
  
  exit(0);
}
