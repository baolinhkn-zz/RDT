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
#define MAXBUFLEN 100
#define CWND 5120

struct packet
{
  int type;
  //0 = syn
  //1 = ack
  //2 = data
  //3 = retransmission
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
  struct sockaddr_storage their_addr;
  socklen_t addr_len = sizeof(their_addr);
  int rv;
  int numbytes;

  if (argc != 3) {
    fprintf(stderr,"usage: client hostname message\n");
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
      perror("client: socket");
      continue;
    }

    break;
  }

  if (p == NULL) {
    fprintf(stderr, "client: failed to create socket\n");
    return 2;
  }

  int client_seq_num = 0;


  while(1)
    {
      struct packet rcv_packet;
      //send initial three way handshake request
      struct packet syn_packet;
      syn_packet.type = 0;
      syn_packet.seq_num = client_seq_num;
      syn_packet.fin = 1;      
      if ((numbytes = sendto(sockfd, &syn_packet, sizeof(syn_packet), 0, p->ai_addr, p->ai_addrlen)) == -1) {
	perror("client: sendto");
	exit(1);
      }
      printf("Sending packet 0 SYN\n");
      client_seq_num++;

      if ((numbytes = recvfrom(sockfd, &rcv_packet, MAXBUFLEN-1 , 0,
			       (struct sockaddr *)p->ai_addr, &(p->ai_addrlen))) == -1) {
	perror("recvfrom");
	exit(1);
      }

      if (rcv_packet.type == 1 && rcv_packet.seq_num == 0)
	{
	  printf("Receiving packet %d\n", rcv_packet.seq_num+1);
	  struct packet ack_synack_packet;
	  ack_synack_packet.type = 0;
	  ack_synack_packet.seq_num = client_seq_num;
	  ack_synack_packet.fin = 1;
	  if ((numbytes = sendto(sockfd, &ack_synack_packet, sizeof(ack_synack_packet), 0, p->ai_addr, p->ai_addrlen)) == -1) {
	    perror("server: sendto");
	    exit(1);
	  }
	  printf("Sending packet %d SYN\n", ack_synack_packet.seq_num);
	}

      // Send filename over
      int fileNameBytes = 0;
      if ((fileNameBytes = sendto(sockfd, argv[2], strlen(argv[2]), 0, p->ai_addr, p->ai_addrlen)) == -1) {
	perror("server: sendto");
	exit(1);
      }

      // Receiving file
      struct packet file_packet;
      int fileBytes = 0;
      if ((fileBytes = recvfrom(sockfd, &file_packet, MAXBUFLEN-1, 0, (struct sockaddr *)p->ai_addr, &(p->ai_addrlen))) == -1) {
	perror("recvfrom");
	exit(1);
      }

      printf("Receiving packet %d\n", file_packet.seq_num+1);
      
      
      break;

    }
  /*
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
    perror("client: sendto");
    exit(1);
  }

  freeaddrinfo(servinfo);

  printf("client: sent %d bytes to %s\n", numbytes, argv[1]);*/
  close(sockfd);
  
  return 0;
}
