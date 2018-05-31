/*
** talker.c -- a datagram "client" demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define SERVERPORT "4950"    // the port users will be connecting to
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

int main(int argc, char *argv[])
{
  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int rv;
  int numbytes;

  if (argc != 3) {
    fprintf(stderr,"usage: talker hostname message\n");
    exit(1);
  }

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;

  if ((rv = getaddrinfo(argv[1], SERVERPORT, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  // loop through all the results and make a socket
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype,
			 p->ai_protocol)) == -1) {
      perror("talker: socket");
      continue;
    }

    break;
  }

  if (p == NULL) {
    fprintf(stderr, "talker: failed to create socket\n");
    return 2;
  }

  struct packet test_packet;
  memset((void*) &test_packet, 0, sizeof(struct packet));
  //  buffer = (struct packet*) malloc(sizeof(struct packet));

  test_packet.type = 0;
  test_packet.seq_num = 0;
  
  char* test = "TEST DATA";

  memcpy((void*) &test_packet.data, (void*) test, strlen(test)+1);
  test_packet.data_size = sizeof(test_packet.data);
  test_packet.fin = 1;

  if ((numbytes = sendto(sockfd, &test_packet, sizeof(test_packet), 0,
			 p->ai_addr, p->ai_addrlen)) == -1) {
    perror("talker: sendto");
    exit(1);
  }

  freeaddrinfo(servinfo);

  printf("talker: sent %d bytes to %s\n", numbytes, argv[1]);
  close(sockfd);

  return 0;
}
