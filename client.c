#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

//int main()
//{
//    int sockfd;
//    int len;
//    struct sockaddr_in address;
//    int result;
//    char ch = 'A';
//    char request[]="GET /index.html\n\n";
//    char response[1024];

//    sockfd = socket(AF_INET, SOCK_STREAM, 0);
//    address.sin_family = AF_INET;
//    address.sin_addr.s_addr = inet_addr("127.0.0.1");
//    address.sin_port = htons(4000);
//    len = sizeof(address);
//    result = connect(sockfd, (struct sockaddr *)&address, len);

//    if (result == -1)
//    {
//        perror("oops: client1");
//        exit(1);
//    }
//    write(sockfd, request, strlen(request));
//    while(read(sockfd, response, 1024)>0)
//    {
//        printf("%s\n",response);
//    }
//    close(sockfd);
//    exit(0);
//}
