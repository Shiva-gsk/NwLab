#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>

#define PORT 8080
#define BUFF_SIZE 10000
// struct sockaddr_in {
//   sa_family_t     sin_family;     /* AF_INET */
//   in_port_t       sin_port;       /* Port number */
//   struct in_addr  sin_addr;       /* IPv4 address */
// };

typedef struct 
{

    char message[BUFF_SIZE];
    struct sockaddr_in client_addr;
    int sockfd;
    socklen_t addr_len;

} client_data_t;

void* handle_client(void* arg) 
{
    client_data_t* data = (client_data_t*)arg;
    printf("[CLIENT MESSAGE] %s",data->message);

    // Reverse the string
    /* todo */
    for (int start = 0, end = strlen(data->message) - 1; start < end; start++, end--) 
    {
        char temp = data->message[start];
        data->message[start] = data->message[end];
        data->message[end] = temp;
    }
    
    // Send back the reversed string
    sendto(data->sockfd, data->message, strlen(data->message), 0,(struct sockaddr*)&(data->client_addr), data->addr_len);

    free(data); // Free the allocated memory
    pthread_exit(NULL);
}

int main()
{
    int sockfd=socket(AF_INET,SOCK_DGRAM,0);
    if(sockfd<0)
    {
        printf("[ERROR] Failed to create socket\n");
        return -1;
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family=AF_INET;
    server_addr.sin_port=htons(PORT);
    server_addr.sin_addr.s_addr=htonl(INADDR_ANY);

    
    if(bind(sockfd,(struct sockaddr*)&server_addr,sizeof(server_addr)) < 0)
    {
        printf("[ERROR] Bind failed\n");
        close(sockfd);
        return -1;
    }
    printf("[INFO] UDP server is listening on port %d\n", PORT);

    while(1)
    {
        char buffer[BUFF_SIZE];
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        ssize_t n = recvfrom(sockfd, buffer, BUFF_SIZE, 0,(struct sockaddr*)&client_addr, &len);

        client_data_t *client_data=(client_data_t*)malloc(sizeof(client_data_t));
        memcpy(client_data->message,buffer,n);
        client_data->message[n] = '\0'; 
        client_data->sockfd=sockfd;
        client_data->client_addr=client_addr;
        client_data->addr_len=len;
        printf("[INFO] Received message from client in while loop: %s\n", client_data->message);

        pthread_t thread_id;
        if(pthread_create(&thread_id,NULL,handle_client,(void*)client_data) != 0) 
        {
            printf("[ERROR] Failed to create thread\n");
            free(client_data);
        }

        pthread_detach(thread_id);
    }

    close(sockfd);


    return 0;
}