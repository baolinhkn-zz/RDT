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

#define SERVERPORT "4950" // the port users will be connecting to
#define DATA 1000
#define MAXBUFLEN 100
#define CWND 5120

struct packet
{
  int type;
  //0 = syn
  //1 = ack/synack
  //2 = data
  //3 = retransmission
  int seq_num;
  int ack_num;

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
    if ((sockfd = socket(p->ai_family, p->ai_socktype,
                         p->ai_protocol)) == -1)
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
  while (1)
  {
    struct packet rcv_packet;
    //send initial three way handshake request
    struct packet syn_packet;
    syn_packet.type = 0; //SYN packet
    syn_packet.seq_num = client_seq_num;
    syn_packet.fin = 1;
    if ((numbytes = sendto(sockfd, &syn_packet, sizeof(syn_packet), 0, p->ai_addr, p->ai_addrlen)) == -1)
    {
      perror("client: sendto");
      exit(1);
    }
    printf("Sending packet 0 SYN\n");
    client_seq_num++;

    if ((numbytes = recvfrom(sockfd, &rcv_packet, MAXBUFLEN - 1, 0,
                             (struct sockaddr *)p->ai_addr, &(p->ai_addrlen))) == -1)
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
      ack_synack_packet.fin = 1;
      if ((numbytes = sendto(sockfd, &ack_synack_packet, sizeof(ack_synack_packet), 0, p->ai_addr, p->ai_addrlen)) == -1)
      {
        perror("server: sendto");
        exit(1);
      }
      printf("Sending packet %d SYN\n", ack_synack_packet.seq_num);
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
    struct packet buffer[5] = {NULL, NULL, NULL, NULL, NULL};
    int lastReceived = -1;
    int toWrite = 0; //next packet to write
    int index = 0;
    int bufferFull = 0;

    // Create file to write to
    int receivedDataFile = open("received.data", O_RDWR | O_CREAT | O_APPEND);
    if (receivedDataFile < 0) {
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

      //printf("%s\n", pkt.data);

      //place packet into the buffer
      int pkt_seq_num = pkt.seq_num;
      if (pkt_seq_num == expected_seq_num)
      {
        expected_seq_num = pkt_seq_num + pkt.data_size;
      }

      // If packet's fin value is 1 - reached EOF
      if (pkt.fin)
        bufferFull = 1;

      //add packet into corresponding spot in the window
      index = ((pkt_seq_num - 1) / 1000) % 5;
      buffer[index] = pkt;

      if (lastReceived < index)
        lastReceived = index;

      printf("Receiving packet %d\n", expected_seq_num);

      //send the ACK - no need to print "sending packet [ack num]"
      struct packet ACK;
      ACK.type = 1;
      ACK.ack_num = expected_seq_num;

      if ((numbytes = sendto(sockfd, &ACK, sizeof(ACK), 0, p->ai_addr, p->ai_addrlen)) == -1)
        {
          perror("server: sendto");
          exit(1);
        }

      // Write file data to received.data in the directory
      int i;

      for (i = 0 ; i < 5; i++) {
        if (&buffer[i] != NULL && i == 4) {
          bufferFull = 1;
        }
      }

      if (bufferFull) {
        // Write buffer to file
        i = 0;
        while (&buffer[i] != NULL && i < 5) {
          //printf("%s", &buffer[i].data);
          write(receivedDataFile, &buffer[i].data, strlen(&buffer[i].data));
          i++;
        }
      }

    }

    /*
    // Receiving file
    struct packet file_packet;
    int fileBytes = 0;
    if ((fileBytes = recvfrom(sockfd, &file_packet, MAXBUFLEN - 1, 0, (struct sockaddr *)p->ai_addr, &(p->ai_addrlen))) == -1)
    {
      perror("recvfrom");
      exit(1);
    }

    if (file_packet.fin == 1)
    {
      file_packet.ack_num = file_packet.seq_num + file_packet.data_size + 16;
      printf("Receiving packet %d\n", file_packet.ack_num);
      //send the ACK
    }
    else //file_packet.fin == 0, there are more packets to receive
    {
      file_packet.ack_num = file_packet.seq_num + file_packet.data_size + 16;
      printf("Receiving packet %d\n", file_packet.ack_num);

      //send the ACK
      struct packet firstACK_packet;
      firstACK_packet.type = 0;
      firstACK_packet.seq_num = client_seq_num;
      firstACK_packet.fin = 1;
      if ((numbytes = sendto(sockfd, &firstACK_packet, sizeof(firstACK_packet), 0, p->ai_addr, p->ai_addrlen)) == -1)
      {
        perror("server: sendto");
        exit(1);
      }
      printf("Sending packet %d\n", firstACK_packet.seq_num);
      client_seq_num++;

      //continually receive packets and send ACKs until file is complete
      while (1)
      {
        struct packet large_file_packet;
        int fileBytes = 0;
        if ((fileBytes = recvfrom(sockfd, &large_file_packet, MAXBUFLEN - 1, 0, (struct sockaddr *)p->ai_addr, &(p->ai_addrlen))) == -1)
        {
          perror("recvfrom");
          exit(1);
        }

        large_file_packet.ack_num = large_file_packet.seq_num + large_file_packet.data_size + 16;
        printf("Receiving packet %d\n", large_file_packet.ack_num);

        //send the ACK
        struct packet ACK_packet;
        ACK_packet.type = 0;
        ACK_packet.seq_num = client_seq_num;
        ACK_packet.fin = 1;
        if ((numbytes = sendto(sockfd, &ACK_packet, sizeof(ACK_packet), 0, p->ai_addr, p->ai_addrlen)) == -1)
        {
          perror("server: sendto");
          exit(1);
        }
        printf("Sending packet %d\n", ACK_packet.seq_num);
        client_seq_num++;

        if (large_file_packet.fin == 1)
          break;
      }
    }
    */
    break;
  }

  close(sockfd);

  return 0;
}
