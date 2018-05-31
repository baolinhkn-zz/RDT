/*
** listener.c -- a datagram sockets "server" demo
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
#include <dirent.h>

#define MYPORT "4950"    // the port users will be connecting to
#define DATA 1008
#define MAXBUFLEN 100
#define CWND 5120

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


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void)
{
  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int rv;
  int numbytes;
  struct sockaddr_storage their_addr;
  char buf[MAXBUFLEN];
  socklen_t addr_len;
  char s[INET6_ADDRSTRLEN];

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE; // use my IP

  if ((rv = getaddrinfo(NULL, MYPORT, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  // loop through all the results and bind to the first we can
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype,
			 p->ai_protocol)) == -1) {
      perror("server: socket");
      continue;
    }

    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      perror("server: bind");
      continue;
    }

    break;
  }

  if (p == NULL) {
    fprintf(stderr, "server: failed to bind socket\n");
    return 2;
  }

  freeaddrinfo(servinfo);

  //  printf("server: waiting to recvfrom...\n");
  addr_len = sizeof their_addr;

  int server_seq_num = 0;
  
  while(1) {
    struct packet syn_packet;

    if ((numbytes = recvfrom(sockfd, &syn_packet, MAXBUFLEN-1 , 0,
			     (struct sockaddr *)&their_addr, &addr_len)) == -1) {
      perror("recvfrom");
      exit(1);
    }

    if (syn_packet.type == 0 && syn_packet.seq_num == 0)
      {
	//successfully received syn packet
	printf("Receiving packet %d\n", syn_packet.seq_num+1);
	//send SYNACK packet
	struct packet synack_packet;
	synack_packet.type = 1;
	synack_packet.seq_num = server_seq_num;
	synack_packet.fin = 1;
	if ((numbytes = sendto(sockfd, &synack_packet, sizeof(synack_packet), 0, (struct sockaddr *)&their_addr, addr_len)) == -1) {
	  perror("server: sendto");
	  exit(1);
	}
	printf("Sending packet %d %d SYN\n", synack_packet.seq_num, CWND);
	server_seq_num++;
      }

    //final ACK of three way handshake
    struct packet three_way_ack_packet;
    if ((numbytes = recvfrom(sockfd, &three_way_ack_packet, MAXBUFLEN-1 , 0,
			     (struct sockaddr *)&their_addr, &addr_len)) == -1) {
      perror("recvfrom");
      exit(1);
    }


    if (three_way_ack_packet.type == 0 && three_way_ack_packet.seq_num == 1)
      {	
	printf("Receiving packet %d\n", three_way_ack_packet.seq_num+1);
      }

    char filename_buff[100];
    if ((numbytes = recvfrom(sockfd, &filename_buff, 100, 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
      perror("recvfrom");
      exit(1);
    }

    printf("Receiving filename %s\n", filename_buff);
    
    // Find the file in the current working directory
    DIR *dp;
    struct dirent *ep;
    dp = opendir("./");
    int validFile = 0;
    FILE* fp;
    
    if (dp != NULL) {
      while ((ep = readdir(dp))) {
	if (strcasecmp(ep->d_name, filename_buff) == 0) {
	  fp = fopen(ep->d_name, "r");
	}
      }
      (void)closedir(dp);      
    }

    struct packet small_file_packet;

    if (fseek(fp, 0, SEEK_END) != 0) {
      fprintf(stderr, "error using fseek");
    }

    int file_length = ftell(fp);

    fread(small_file_packet.data, sizeof(char), file_length, fp);
    small_file_packet.data[file_length] = '\0';
    small_file_packet.seq_num = server_seq_num + 1;

    printf("Sending packet %d %d\n", small_file_packet.seq_num, CWND);
    server_seq_num = server_seq_num + sizeof(small_file_packet);
    printf("%d", server_seq_num);

    if ((numbytes = sendto(sockfd, &small_file_packet, sizeof(small_file_packet), 0, (struct sockaddr *)&their_addr, addr_len)) == -1) {
      perror("sendto");
      exit(1);
    }
    
    break;
    
  }  
  /*
  struct packet recv_packet;

  if ((numbytes = recvfrom(sockfd, &recv_packet, MAXBUFLEN-1 , 0,
			   (struct sockaddr *)&their_addr, &addr_len)) == -1) {
    perror("recvfrom");
    exit(1);
  }
  
  printf("server: got packet from %s\n",
	 inet_ntop(their_addr.ss_family,
		   get_in_addr((struct sockaddr *)&their_addr),
		   s, sizeof s));
  printf("server: packet is %d bytes long\n", numbytes);
  buf[numbytes] = '\0';
  printf("server: packet contains \"%s\"\n", recv_packet.data);
  */
  close(sockfd);

  return 0;
}
