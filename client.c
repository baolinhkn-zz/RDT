#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFFER 256
#define PORT 8080
#define MAXLINE 1024

int main (int argc, char* argv[])
{
  int sockfd;
  char buffer[BUFFER];
  char* hello = "hello from client";
  char* file = argv[1];

  struct sockaddr_in servaddr;

  //creating socket file descriptor
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
      perror("socket creation failed");
      exit(EXIT_FAILURE);
    }

  memset(&servaddr, 0, sizeof(servaddr));

  //filling server information
  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(PORT);
  servaddr.sin_addr.s_addr = INADDR_ANY;

  int n;

  socklen_t len;
  
  sendto(sockfd, (const char *) hello, strlen(hello), MSG_CONFIRM, (const struct sockaddr *) &servaddr, sizeof(servaddr));
  printf("Hello mssage send. \n");

  n = recvfrom(sockfd, (char*) buffer, MAXLINE, MSG_WAITALL, (struct sockaddr*) &servaddr, &len);
  buffer[n] = '\0';
  printf("Server: %s\n", buffer);

  close(sockfd);
  return 0;
}


