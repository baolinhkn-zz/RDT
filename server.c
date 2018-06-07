#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <dirent.h>
#include <math.h>

#define MYPORT "4950" // the port users will be connecting to
#define DATA 1004
#define MAXBUFLEN 2048
#define CWND 5120

struct packet
{
  int type;
  //0 = syn
  //1 = ack/synack
  //2 = data
  //3 = retransission
  //4 = fin
  int seq_num;
  int ack_num;

  int data_size;
  //end of file, end_of_file = 1
  //middle of file, end_of_file = 0
  int end_of_file;

  char data[DATA];
};

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
  if (sa->sa_family == AF_INET)
  {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
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

  if ((rv = getaddrinfo(NULL, MYPORT, &hints, &servinfo)) != 0)
  {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  // loop through all the results and bind to the first we can
  for (p = servinfo; p != NULL; p = p->ai_next)
  {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
    {
      perror("server: socket");
      continue;
    }

    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
    {
      close(sockfd);
      perror("server: bind");
      continue;
    }

    break;
  }

  if (p == NULL)
  {
    fprintf(stderr, "server: failed to bind socket\n");
    return 2;
  }

  freeaddrinfo(servinfo);

  addr_len = sizeof their_addr;

  int server_seq_num = 0;

  while (1)
  {
    struct packet syn_packet;

    if ((numbytes = recvfrom(sockfd, &syn_packet, MAXBUFLEN - 1, 0, (struct sockaddr *)&their_addr, &addr_len)) == -1)
    {
      perror("recvfrom");
      exit(1);
    }

    //check for SYN packet
    if (syn_packet.type == 0 && syn_packet.seq_num == 0)
    {
      //successfully received syn packet
      syn_packet.ack_num = syn_packet.seq_num + 1;
      printf("Receiving packet %d\n", syn_packet.ack_num);

      //send SYNACK packet
      struct packet synack_packet;
      synack_packet.type = 1;
      synack_packet.seq_num = server_seq_num;
      synack_packet.end_of_file = 1;
      if ((numbytes = sendto(sockfd, &synack_packet, sizeof(synack_packet), 0, (struct sockaddr *)&their_addr, addr_len)) == -1)
      {
        perror("server: sendto");
        exit(1);
      }
      printf("Sending packet %d %d SYN\n", synack_packet.seq_num, CWND);
      server_seq_num++;
    }

    //final ACK of three way handshake
    struct packet three_way_ack_packet;
    if ((numbytes = recvfrom(sockfd, &three_way_ack_packet, MAXBUFLEN - 1, 0, (struct sockaddr *)&their_addr, &addr_len)) == -1)
    {
      perror("recvfrom");
      exit(1);
    }

    if (three_way_ack_packet.type == 1 && three_way_ack_packet.seq_num == 1)
    {
      three_way_ack_packet.ack_num = three_way_ack_packet.seq_num + 1;
      printf("Receiving packet %d\n", three_way_ack_packet.ack_num);
    }

    //get the file name from the client
    char filename_buff[100];
    if ((numbytes = recvfrom(sockfd, &filename_buff, 100, 0, (struct sockaddr *)&their_addr, &addr_len)) == -1)
    {
      perror("recvfrom");
      exit(1);
    }

    printf("Receiving filename %s\n", filename_buff);

    // Find the file in the current working directory
    DIR *dp;
    struct dirent *ep;
    dp = opendir("./");
    int validFile = 0;
    FILE *fp;

    if (dp != NULL)
    {
      while ((ep = readdir(dp)))
      {
        if (strcmp(ep->d_name, filename_buff) == 0)
        {
          fp = fopen(ep->d_name, "r");
          validFile = 1;
        }
      }
      (void)closedir(dp);
    }

    if (!validFile)
    {
      fprintf(stderr, "file not found!");
      exit(1);
    }

    if (fseek(fp, 0, SEEK_END) != 0)
    {
      fprintf(stderr, "error using fseek");
    }

    int file_length = ftell(fp);

    int totalPackets = file_length / DATA;
    if (file_length < DATA)
      totalPackets = 1;
    if (file_length % DATA != 0 && file_length > DATA)
      totalPackets++;

    struct packet *packets = (struct packet *)malloc(sizeof(struct packet) * totalPackets);

    fseek(fp, 0, SEEK_SET);

    //make packets and place inside the buffer
    int i = 0;
    while (i < totalPackets)
    {
      struct packet data_packet;
      int bytesRead = fread(packets[i].data, 1, DATA, fp);
      packets[i].seq_num = server_seq_num;
      server_seq_num = server_seq_num + bytesRead;
      packets[i].data_size = bytesRead;
      if (i != totalPackets - 1)
        packets[i].end_of_file = 0;
      else
        packets[i].end_of_file = 1;
      //packets[i] = data_packet;
      i++;
    }

    // for (i = 0; i < totalPackets; i++) {
    //   fprintf(stderr, "\n%d:\n%s", i, packets[i].data);
    // }

    //window size is 5 packets, can send five packets at a time
    int beginWindow = 0;
    int endWindow = (totalPackets < 5) ? totalPackets - 1 : 4;
    int nextPacket = 0;

    // Set up timer
    fd_set rfds;
    struct timeval tv;
    int retval;

    //sending packets
    while (1)
    {
      //send all packets in the window
      while (nextPacket <= endWindow && endWindow < totalPackets)
      {
        if ((numbytes = sendto(sockfd, &packets[nextPacket], sizeof(struct packet), 0, (struct sockaddr *)&their_addr, addr_len)) == -1)
        {
          perror("sendto");
          exit(1);
        }
        fprintf(stderr, "nextPacket: %d\n", nextPacket);
        nextPacket++;
      }

      // Watch sockfd to see when it has input
      FD_ZERO(&rfds);
      FD_SET(sockfd, &rfds);

      tv.tv_sec = 0.5;
      tv.tv_usec = 0;
      retval = select(1, &rfds, NULL, NULL, &tv);

      if (retval < 0)
        perror("select()");
      else if (retval)
      {
        // Haven't timed out
        //check for an ACK
        struct packet received_ack;
        if ((numbytes = recvfrom(sockfd, &received_ack, MAXBUFLEN - 1, 0, (struct sockaddr *)&their_addr, &addr_len)) == -1)
        {
          perror("recvfrom");
          exit(1);
        }

        //receive an ACK, check if the window should be moved
        if (received_ack.type == 1)
        {
          //move the window
          if (received_ack.ack_num >= packets[beginWindow].seq_num && received_ack.seq_num <= packets[endWindow].seq_num)
          {
            int newBegin = ((received_ack.ack_num - 1) / 1000);
            endWindow = endWindow + (newBegin - beginWindow);
            if (endWindow > totalPackets)
              endWindow = totalPackets;
            beginWindow = newBegin;
          }
        }
      }

      //client has received all of the data, break to begin TCP closing process
      if (beginWindow == endWindow)
        break;
    }

    //close the TCP connection

    break;
  }

  close(sockfd);

  return 0;
}
