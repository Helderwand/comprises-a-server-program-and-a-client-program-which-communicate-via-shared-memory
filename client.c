#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define SHM_SIZE 1024
#define CMD_SIZE 256


typedef struct // structure for shared data
{
    pid_t client_pid; // Store the PID of the client
    char command[CMD_SIZE]; // Store the command sent by the client actually it is the communication between client and server with like response
    int counter_client;
    int max_clients;
} SharedData;

void print_help()// print available commands
{
    printf("Available commands are: help, list, readF, writeT, upload, download, archServer, killServer, quit\n");
}

int main(int argc, char *argv[])
{
    // check if the correct number of arguments 
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <Connect/tryConnect> ServerPID\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    char *option = argv[1];// option: Connect or tryConnect
    int server_pid = atoi(argv[2]);// Server PID
    
    // create or get shared memory segment
    key_t key = ftok("server", 'R');
    int shm_id = shmget(key, SHM_SIZE, 0666);
    if (shm_id == -1)
    {
        perror("Failed to create shared memory segment");
        exit(EXIT_FAILURE);
    }

    // attach shared memory
    SharedData *shared_memory = (SharedData *)shmat(shm_id, NULL, 0);
    if (shared_memory == (void *)-1)
    {
        perror("Failed to attach shared memory");
        exit(EXIT_FAILURE);
    }
    if(server_pid!=shared_memory->client_pid){
        //printf("the pids are not match.")
        perror("The client pid is not match.");
        exit(EXIT_FAILURE);
    }
    // server PID to the shared memory
    shared_memory->client_pid=atoi(argv[2]);
    shared_memory->counter_client = shared_memory->counter_client +1 ; // increase counter 
    if(shared_memory->counter_client==shared_memory->max_clients){//check queue
        perror("Queue is FULL MAX CLIENTS !\n");
        exit(EXIT_FAILURE);
    }
    if (strcmp(option, "Connect") == 0)
    {
        // Connect option
        printf("Connected to server with PID %d\n", shared_memory->client_pid);
        print_help();
        while (1)
        {
            printf("Enter command: ");
            // get command from user
            fgets(shared_memory->command, CMD_SIZE, stdin);
            //printf("%s", shared_memory->command);
            if (strcmp(shared_memory->command, "quit\n") == 0)
            {
                break;// exit loop if user enters 'quit'
            }
            // wait for server response and print it 
            sleep(1);
            printf("Server response: %s\n", shared_memory->command);
        }
    }
    else if (strcmp(option, "tryConnect") == 0)
    {
        // tryConnect option
        printf("Connected to server with PID %d\n", shared_memory->client_pid);
        print_help();
    }
    else
    {
        fprintf(stderr, "Invalid option\n");// the other options
        exit(EXIT_FAILURE);
    }

    // detach shared memory
    shmdt(shared_memory);

    return 0;
}