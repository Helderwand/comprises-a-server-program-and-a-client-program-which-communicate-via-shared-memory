#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <semaphore.h>
#include <sys/sem.h>

#define SHM_SIZE 1024
#define CMD_SIZE 1024

// structure for shared data
typedef struct {
    pid_t client_pid;   // store the PID of the client
    char command[CMD_SIZE]; // store the command sent by the client actually it is the communication between client and server with like response
    int counter_client; // counter for how many clients is working it increase and decrease 
    int max_clients; //max client number the given as argument
} SharedData;



int initialize_semaphore(int *sem_id) {
    *sem_id = semget(ftok("server", 'S'), 1, IPC_CREAT | IPC_EXCL | 0666); // create a semaphore with initial value 1 ( actually ,t is succesful one )
    if (*sem_id == -1) {
        return -1;
    }

    if (semctl(*sem_id, 0, SETVAL, 1) == -1) { // set the semaphore value to 1
        return -1;
    }

    return 0;
}

void destroy_semaphore(int sem_id) {
    semctl(sem_id, 0, IPC_RMID, NULL);
}



void handle_client(SharedData *shared_memory, char *dirname,int sem_id) {

    //process client request (access shared memory)

    char commands[CMD_SIZE];
    strcpy(commands, shared_memory->command); // copy the command from shared memory

    //printf("given : %s", commands);
    char response[CMD_SIZE];
    response[0] = '\0' ; // initialize the response variable
    
    if (strncmp(commands, "help\n", sizeof(commands)) == 0)
    {
        //printf("Received command: help\n"); 
        strcpy(response, "Available commands are: help, list, readF, writeT, upload, download, archServer, killServer, quit");
    }
    else if (strcmp(shared_memory->command, "list\n") == 0)
    {
        DIR *dir;
        struct dirent *entry;
        if ((dir = opendir(dirname)) != NULL)// open the directory for reading
        {
            while ((entry = readdir(dir)) != NULL)// iterate each entry in the directory
            {
                if (entry->d_type == DT_REG)// check if the entry is a regular file
                {
                    strcat(response, entry->d_name);// append the filename to the response string and after that we need newline
                    strcat(response, "\n");
                }
            }
            closedir(dir);// close the directory
        }
        else
        {
            perror("Failed to open directory");// print error message if failed to open dir
        }
    }
    else if (strncmp(shared_memory->command, "readF\n", 5) == 0)
    {
        char filename[CMD_SIZE]; // filename and line number from the command
        int line_number = 0;
        sscanf(shared_memory->command, "readF %s %d", filename, &line_number);
        FILE *file = fopen(filename, "r");// open the file for reading
        if (file)
        {
            if (line_number > 0)
            {
                char line[CMD_SIZE];// read each line until reaching line number
                for (int i = 0; i < line_number; i++)
                {
                    if (fgets(line, sizeof(line), file) == NULL)
                    {
                        strcpy(response, "Line number exceeds file length");
                        break;
                    }
                }
                strcpy(response, line);// copy the last read line to the response
            }
            else// read the content of the file if line number is non-positive
            {

                fseek(file, 0, SEEK_END);// move pointer to the end
                long file_size = ftell(file);// get the file size
                fseek(file, 0, SEEK_SET);// reset  pointer to the began
                if (file_size >= CMD_SIZE)
                { // check for potential truncation
                    perror("File content truncated");
                    file_size = CMD_SIZE - 1;
                }
                fread(response, 1, file_size, file);
                response[file_size] = '\0';// null at the end
            }
            fclose(file);//close file
        }
        else
        {
            perror("Failed to open file");
        }
    }
    else if (strncmp(shared_memory->command, "writeT\n", 6) == 0)
    {
        char filename[CMD_SIZE];// filename, line number, and string from the command
        int line_number;
        char string[CMD_SIZE];
        sscanf(shared_memory->command, "writeT %s %d %[^\n]", filename, &line_number, string);
        FILE *file = fopen(filename, "a");// open the file for appending
        if (file)
        {
            if (line_number > 0)
            {
                char temp[CMD_SIZE];
                int current_line = 0;
                while (fgets(temp, sizeof(temp), file))// iteration
                {
                    current_line++;
                    if (current_line == line_number)// if the current line matches the given line number
                    {
                        fprintf(file, "%s\n", string);
                        strcpy(response, "String written to file");
                        break;
                    }
                }
                if (current_line != line_number)// if the given line number exceeds the file length
                {
                    strcpy(response, "Line number exceeds file length");
                }
            }
            else // write the string to the file without considering line numbers
            {
                fprintf(file, "%s\n", string);
                strcpy(response, "String written to file");
            }
            fclose(file);
        }
        else
        {
            perror("Failed to open file");
        }
    }
    else if (strncmp(shared_memory->command, "upload\n", 6) == 0)
    {
        char filename[CMD_SIZE];// filename from the command
        sscanf(shared_memory->command, "upload %s", filename);

        char source[CMD_SIZE];// source path (current directory)
        int source_len = snprintf(source, CMD_SIZE, "%s/%s", ".", filename); // Use snprintf
        if (source_len >= CMD_SIZE)
        {
            perror("Source path truncated");
            return;
        }

        char dest[CMD_SIZE];// destination path (specified directory)
        int dest_len = snprintf(dest, CMD_SIZE, "%s/%s", dirname, filename); // Use snprintf
        if (dest_len >= CMD_SIZE)
        {
            perror("Destination path truncated");
            return;
        }

        int source_fd = open(source, O_RDONLY);// open the source file for reading
        if (source_fd == -1)
        {
            perror("Failed to open source file");
            strcpy(response, "Failed to open source file");
        }
        else
        {
            int dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);// destination file for writing
            if (dest_fd == -1)
            {
                perror("Failed to create destination file");
                strcpy(response, "Failed to create destination file");
            }
            else
            {
                char buffer[4096];
                ssize_t bytes_read;
                while ((bytes_read = read(source_fd, buffer, sizeof(buffer))) > 0)// read from the source and write to the destination
                {
                    ssize_t bytes_written = write(dest_fd, buffer, bytes_read);
                    if (bytes_written != bytes_read)
                    {
                        perror("Failed to write to destination file");
                        strcpy(response, "Failed to write to destination file");
                        break;
                    }
                }
                if (bytes_read == -1)
                {
                    perror("Failed to read from source file");
                    strcpy(response, "Failed to read from source file");
                }
                else
                {
                    strcpy(response, "File uploaded successfully");
                }
                close(dest_fd);// close the derstination file
            }
            close(source_fd); // close the source file
        }
    }
    else if (strncmp(shared_memory->command, "download\n", 8) == 0)
    {
        char filename[CMD_SIZE];// filename from the command
        sscanf(shared_memory->command, "download %s", filename);
        char source[CMD_SIZE];
        int source_len = snprintf(source, CMD_SIZE, "%s/%s", dirname, filename); // use snprintf
    
        if (source_len >= CMD_SIZE)// source path
        {
            perror("Source path truncated");
            return;
        }
        
        char dest[CMD_SIZE];// destination path
        int dest_len = snprintf(dest, CMD_SIZE, "%s/%s", ".", filename); // use snprintf
        if (dest_len >= CMD_SIZE)
        {
            perror("Destination path truncated");
            return;
        }
        
        int source_fd = open(source, O_RDONLY);// open the source file for reading
        if (source_fd == -1)
        {
            perror("Failed to open source file");
            strcpy(response, "Failed to open source file");
        }
        else
        {
            int dest_fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, 0644);// destination file for writing
            if (dest_fd == -1)
            {
                perror("Failed to create destination file");
                strcpy(response, "Failed to create destination file");
            }
            else
            {
                char buffer[4096];
                ssize_t bytes_read;
                while ((bytes_read = read(source_fd, buffer, sizeof(buffer))) > 0)// read from the source and write to the destination
                {
                    ssize_t bytes_written = write(dest_fd, buffer, bytes_read);
                    if (bytes_written != bytes_read)
                    {
                        perror("Failed to write to destination file");
                        strcpy(response, "Failed to write to destination file");
                        break;
                    }
                }
                if (bytes_read == -1)
                {
                    perror("Failed to read from source file");
                    strcpy(response, "Failed to read from source");
                }
                else
                {
                    strcpy(response, "File downloaded successfully");
                }
                close(dest_fd);// close the destination file
            }
            close(source_fd);// close the spurce file
        }
    }
    else if (strncmp(shared_memory->command, "archServer", 10) == 0)
    {
        char filename[CMD_SIZE];
        int space_after_cmd = strcspn(filename, "\n"); // space after command
        //printf("deneme\n");
        
        sscanf(shared_memory->command + space_after_cmd + 1, "%s", filename);
        //printf("deneme 3 : %s \n",filename);;
        pid_t child_pid = fork();
        if (child_pid == -1)
        {
            perror("Failed to fork process");
            strcpy(response, "Failed to create archive");
            
            return;
        }
        
        else if (child_pid == 0)
        {                           // child process
            char tar_cmd[CMD_SIZE]; 
            int written_bytes = snprintf(tar_cmd, CMD_SIZE, "tar -cvf %s %s/*", filename, dirname);
            if (written_bytes >= CMD_SIZE)
            {
                perror("tar command string truncated");
                exit(EXIT_FAILURE);
            }

            // execvp to avoid potential shell injection vulnerabilities
            execvp("tar", (char *[]){"tar", "-cvf", filename, dirname, NULL});
            perror("Failed to execute tar command");
            exit(EXIT_FAILURE); // if execvp fails, exit child process
        }
        
         // Parent process
        //  printf("succes fork \n");
            int wait_status;
            waitpid(child_pid, &wait_status, 0);
            if (WIFEXITED(wait_status) && WEXITSTATUS(wait_status) == 0)
            {
                strcpy(response, "Server files archived successfully");
            }
            else
            {
                strcpy(response, "Failed to create archive");
            }
        
    }

    
    else if (strcmp(shared_memory->command, "killServer\n") == 0)
    {
        printf("Kill signal received from client . Terminating...\n");
        strcpy(response, "Kill server");
        exit(EXIT_SUCCESS);//it is works like ctrl+c . like clean shut down .
    }
    else if (strcmp(shared_memory->command, "quit\n") == 0)//quit for clients 
    {
        strcpy(response, "Quitting server");
        shared_memory->counter_client = shared_memory->counter_client -1 ; //decrease counter
        //kill(-getpgrp(), SIGTERM);
    }
    
    // help for operations return response for directory
    else if (strcmp(shared_memory->command, "help readF\n") == 0)
    {
        strcpy(response, "readF <file> <line #>\n");
    }
    else if (strcmp(shared_memory->command, "help writeT\n") == 0)
    {
        strcpy(response, "writeT <file> <line #> <string>\n");
    }
    else if (strcmp(shared_memory->command, "help upload\n") == 0)
    {
        strcpy(response, "upload <file>\n");
    }
    else if (strcmp(shared_memory->command, "help download\n") == 0)
    {
        strcpy(response, "download <file>\n");
    }
    else if (strcmp(shared_memory->command, "help archServer\n") == 0)
    {
        strcpy(response, "archServer <fileName>.tar\n");
    }
    else
    {
        
        //printf("%s", shared_memory->command);
        if (strlen(shared_memory->command) == 0) {
            strcpy(response, "");
        }
        else{
            strcpy(response, "");
        }
        
    }

    // Write the response to the log file
    int log_fd = open("log.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (log_fd == -1) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }

    if (write(log_fd, response, strlen(response)) == -1) {
        perror("Failed to write to log file");
    }
    close(log_fd);

    // copy the response back to the shared memory command field
    strcpy(shared_memory->command, response);

}


int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <dirname> <max. #ofClients>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *dirname = argv[1];

    // create directory (if it doesn't exist)
    mkdir(dirname, 0777);

    // key for semaphore and shared memory
    key_t key = ftok("server", 'R');

    // create semaphore
    int sem_id;
    if (initialize_semaphore(&sem_id) == -1) {
        perror("Failed to initialize semaphore");
        exit(EXIT_FAILURE);
    }

    // create shared memory segment
    int shm_id = shmget(key, SHM_SIZE, IPC_CREAT | 0666);
    if (shm_id == -1) {
        perror("Failed to create shared memory segment");
        exit(EXIT_FAILURE);
    }

    // attach shared memory
    SharedData *shared_memory = (SharedData *)shmat(shm_id, NULL, 0);
    if (shared_memory == (void *)-1) {
        perror("Failed to attach shared memory");
        exit(EXIT_FAILURE);
    }

    printf("Server started PID %d\n", getpid());
    shared_memory->client_pid = getpid();
    shared_memory->counter_client = 0;
    shared_memory->max_clients = atoi(argv[2]);

    // main server loop
    while (1) {
        handle_client(shared_memory, dirname,sem_id);
        sleep(1); // sleep for server response
    }

    
    kill(-getpgrp(), SIGTERM);// cleanup 
    shmdt(shared_memory);
    shmctl(shm_id, IPC_RMID, NULL);
    destroy_semaphore(sem_id);

    return 0;
}


