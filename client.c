#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/timerfd.h>

#define SERVERPORT "4950" // the port users will be connecting to
#define DATA 996
#define MAXBUFLEN 2048
#define CWND 5120

struct packet
{
  int type;
  //0 = syn
  //1 = ack/synack
  //2 = data
  //3 = retransmission
  //4 = fin
  int seq_num;
  int ack_num;

  double time;

  int data_size;
  //end of file, end_of_file = 1
  //middle of file, end_of_file = 0
  int end_of_file;

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

  if (argc != 3)
  {
    fprintf(stderr, "usage: client hostname message\n");
    exit(1);
  }

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;

  if ((rv = getaddrinfo(argv[1], SERVERPORT, &hints, &servinfo)) != 0)
  {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  // loop through all the results and make a socket
  for (p = servinfo; p != NULL; p = p->ai_next)
  {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
    {
      perror("client: socket");
      continue;
    }

    break;
  }

  if (p == NULL)
  {
    fprintf(stderr, "client: failed to create socket\n");
    return 2;
  }

  int client_seq_num = 0;
  int expected_seq_num = 0;

  int closed = 0;
  while (!closed)
  {
    struct packet rcv_packet;
    //send initial three way handshake request
    struct packet syn_packet;
    syn_packet.type = 0; //SYN packet
    syn_packet.seq_num = client_seq_num;
    syn_packet.end_of_file = 1;
    if ((numbytes = sendto(sockfd, &syn_packet, sizeof(syn_packet), 0, p->ai_addr, p->ai_addrlen)) == -1)
    {
      perror("client: sendto");
      exit(1);
    }
    printf("Sending packet 0 5120 SYN\n");
    client_seq_num++;

    struct pollfd syn_poll[2];

    struct itimerspec syn_timeout;
    int syn_timer_fd = -1;

    syn_timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (syn_timer_fd <= 0)
    {
      perror("timerfd_create");
      exit(1);
    }

    syn_timeout.it_interval.tv_sec = 0;
    syn_timeout.it_interval.tv_nsec = 0;
    syn_timeout.it_interval.tv_sec = 0;
    syn_timeout.it_value.tv_nsec = 500000000;

    //set the timeout
    int ret = timerfd_settime(syn_timer_fd, 0, &syn_timeout, NULL);
    if (ret)
    {
      perror("timerfd_settime");
      exit(1);
    }

    syn_poll[0].events = POLLIN;
    syn_poll[0].fd = sockfd;

    syn_poll[1].events = POLLIN;
    syn_poll[1].fd = syn_timer_fd;

    //poll for the SYN
    while (1)
    {
      int ret = poll(syn_poll, 2, 0);
      if (ret < 0)
      {
        perror("poll");
        exit(1);
      }

      //received the ACK for the SYN
      if (syn_poll[0].revents & POLLIN)
      {
        break;
      }

      //SYN was lost
      if (syn_poll[1].revents & POLLIN)
      {
        //need to resend the SYN
        if ((numbytes = sendto(sockfd, &syn_packet, sizeof(syn_packet), 0, p->ai_addr, p->ai_addrlen)) == -1)
        {
          perror("client: sendto");
          exit(1);
        }
        printf("Sending packet 0 5120 Retransmission SYN\n");
      }
    }

    if ((numbytes = recvfrom(sockfd, &rcv_packet, MAXBUFLEN - 1, 0, (struct sockaddr *)p->ai_addr, &(p->ai_addrlen))) == -1)
    {
      perror("recvfrom");
      exit(1);
    }

    //checking for the SYNACK packet
    if (rcv_packet.type == 1 && rcv_packet.seq_num == expected_seq_num)
    {
      expected_seq_num++;
      printf("Receiving packet %d\n", expected_seq_num);
      //sending final ACK for the three-way handshake
      struct packet ack_synack_packet;
      ack_synack_packet.type = 1; //SYN
      ack_synack_packet.seq_num = client_seq_num;
      ack_synack_packet.end_of_file = 1;
      if ((numbytes = sendto(sockfd, &ack_synack_packet, sizeof(ack_synack_packet), 0, p->ai_addr, p->ai_addrlen)) == -1)
      {
        perror("server: sendto");
        exit(1);
      }
      printf("Sending packet %d 5120 SYN\n", ack_synack_packet.seq_num);
      client_seq_num++;
    }
    // Send filename over
    int fileNameBytes = 0;
    if ((fileNameBytes = sendto(sockfd, argv[2], strlen(argv[2]) + 1, 0, p->ai_addr, p->ai_addrlen)) == -1)
    {
      perror("server: sendto");
      exit(1);
    }

    //window to receive packets
    struct packet buffer[5];
    int itemsInBuffer = 0;
    int lastReceived = -1;
    int toWrite = 0; //next packet to write
    int index = 0;
    int bufferFull = 0;

    // Create file to write to
    int receivedDataFile = open("received.data", O_RDWR | O_CREAT | O_APPEND | O_TRUNC);
    if (receivedDataFile < 0)
    {
      perror("open");
      exit(1);
    }

    while (1)
    {
      //receive the packet
      struct packet pkt;
      int numBytes = 0;
      if ((numBytes = recvfrom(sockfd, &pkt, MAXBUFLEN - 1, 0, (struct sockaddr *)p->ai_addr, &(p->ai_addrlen))) == -1)
      {
        perror("recvfrom");
        exit(1);
      }
      //regular data packet or a retransmitted data packet
      if (pkt.type == 2 || pkt.type == 3)
      {
        //place packet into the buffer

        fprintf(stderr, "packet received: %d\n", pkt.seq_num);
        if (pkt.type == 3 && pkt.seq_num < expected_seq_num) //ACK to retransmitted packet that had been lost
        {
          ;
        }
        else //pkt.type == 2 or a retransmitted packet that had been lost
        {
          int pkt_seq_num = pkt.seq_num;
          //check if the packet is in the window
          if (pkt_seq_num > expected_seq_num + 5120)
          {
            continue;
          }
          if (pkt_seq_num == expected_seq_num)
          {
            expected_seq_num = pkt_seq_num + pkt.data_size;
          }

          //add packet into corresponding spot in the window
          index = ((pkt_seq_num - 1) / 1000) % 5;
          buffer[index] = pkt;
          itemsInBuffer++;

          // If packet's fin value is 1 - reached EOF OR we have a full buffer
          if (pkt.end_of_file || itemsInBuffer == 5)
          {
            bufferFull = 1;
          }

          if (lastReceived < index)
            lastReceived = index;
        }

        //send the ACK - no need to print "sending packet [ack num]"
        struct packet ACK;
        ACK.type = 1;
        ACK.ack_num = expected_seq_num;

        if ((numbytes = sendto(sockfd, &ACK, sizeof(ACK), 0, p->ai_addr, p->ai_addrlen)) == -1)
        {
          perror("server: sendto");
          exit(1);
        }

        printf("Receiving packet %d\n", expected_seq_num);

        // Write file data to received.data in the directory
        int i;

        if (bufferFull)
        {
          // Write buffer to file
          i = 0;

          while (i < itemsInBuffer)
          {
            write(receivedDataFile, &buffer[i].data, buffer[i].data_size);
            i++;
          }

          itemsInBuffer = 0;
          bufferFull = 0;
        }
      }

      //receiving a FIN packet
      if (pkt.type == 4)
      {
        struct packet server_fin_ack;
        server_fin_ack.type = 4;
        server_fin_ack.ack_num = pkt.seq_num + 1;
        if ((numbytes = sendto(sockfd, &server_fin_ack, sizeof(struct packet), 0, p->ai_addr, p->ai_addrlen)) == -1)
        {
          perror("client: sendto");
          exit(1);
        }
        printf("Receiving packet %d\n", pkt.seq_num + 1);
        sleep(1);
        fprintf(stderr, "done sleeping");
        closed = 1;
        break;
      }
    }
  }

  close(sockfd);

  return 0;
}