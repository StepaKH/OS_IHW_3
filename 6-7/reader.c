#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>

#define ARRAY_SIZE 10

typedef struct {
    int id;
    const char* server_ip;
    int port;
} ReaderData;

sem_t rand_sem;

int factorial(int n) {
    if (n == 0) return 1;
    int result = 1;
    for (int i = 1; i <= n; i++) {
        result *= i;
    }
    return result;
}

void signal_handler(int signal) {
    printf("Caught signal %d, terminating reader clients...\n", signal);
    sem_destroy(&rand_sem);
    exit(0);
}

void* reader_task(void* arg) {
    ReaderData* reader_data = (ReaderData*)arg;
    int id = reader_data->id;
    const char* server_ip = reader_data->server_ip;
    int port = reader_data->port;

    int sock = 0;
    struct sockaddr_in serv_addr;

    while (1) {
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            printf("Reader %d: Socket creation error\n", id);
            continue;
        }

        memset(&serv_addr, '0', sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(port);

        if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
            printf("Reader %d: Invalid address/ Address not supported\n", id);
            close(sock);
            continue;
        }

        if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            printf("Reader %d: Connection failed\n", id);
            close(sock);
            continue;
        }

        int index;
        sem_wait(&rand_sem);
        srand(time(NULL) + id);
        index = rand() % ARRAY_SIZE;
        sem_post(&rand_sem);

        char read_request[50];
        snprintf(read_request, sizeof(read_request), "READ %d", index);

        int msg_len = strlen(read_request);
        if (send(sock, &msg_len, sizeof(msg_len), 0) == -1) {
            printf("Reader %d: Failed to send message length\n", id);
            close(sock);
            continue;
        }
        if (send(sock, read_request, msg_len, 0) == -1) {
            printf("Reader %d: Failed to send read request\n", id);
            close(sock);
            continue;
        }

        char buffer[1024] = { 0 };
        int valread = read(sock, buffer, 1024);
        if (valread <= 0) {
            printf("Reader %d: Read error or connection closed\n", id);
            close(sock);
            continue;
        }

        buffer[valread] = '\0';
        printf("Reader %d received: %s\n", id, buffer);

        int value;
        if (sscanf(buffer, "VALUE %d", &value) == 1) {
            int fact = factorial(value);
            printf("Reader %d: Factorial of %d is %d\n", id, value, fact);
        }

        close(sock);

        sleep(1);
    }
    return NULL;
}

int main(int argc, char const* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <IP> <PORT> <NUM_READERS>\n", argv[0]);
        return -1;
    }

    const char* SERVER_IP = argv[1];
    int PORT = atoi(argv[2]);
    int NUM_READERS = atoi(argv[3]);

    signal(SIGINT, signal_handler);

    if (sem_init(&rand_sem, 0, 1) != 0) {
        perror("sem_init() failed");
        return -1;
    }

    pthread_t* reader_threads = (pthread_t*)malloc(NUM_READERS * sizeof(pthread_t));
    if (!reader_threads) {
        perror("malloc() failed");
        sem_destroy(&rand_sem);
        return -1;
    }

    ReaderData* reader_data = (ReaderData*)malloc(NUM_READERS * sizeof(ReaderData));
    if (!reader_data) {
        perror("malloc() failed");
        free(reader_threads);
        sem_destroy(&rand_sem);
        return -1;
    }

    for (int i = 0; i < NUM_READERS; ++i) {
        reader_data[i].id = i + 1;
        reader_data[i].server_ip = SERVER_IP;
        reader_data[i].port = PORT;
        if (pthread_create(&reader_threads[i], NULL, reader_task, &reader_data[i]) != 0) {
            perror("pthread_create() failed");
            free(reader_threads);
            free(reader_data);
            sem_destroy(&rand_sem);
            return -1;
        }
    }

    for (int i = 0; i < NUM_READERS; ++i) {
        pthread_join(reader_threads[i], NULL);
    }

    free(reader_threads);
    free(reader_data);
    sem_destroy(&rand_sem);
    return 0;
}
