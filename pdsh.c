#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<string.h>
#include<errno.h>
#include<signal.h>
#include<sys/file.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<fcntl.h>

#define MAX_CMD_LENGHT 500
#define MAX_CMD_ARGS_LENGHT 50

void getNextCommand(char* payload);
void parseCommandString(char* payload, char** payloadArgv, char** payloadArgv2, char* fileName, int* rdct, int* pCount);
void setSigHandler();
int execSimpleCommand(char** payloadArgv);
int execRedirectCommand(char* fileName, char** payloadArgv, int rdct);
int execPipeCommand(char** payloadArgv, char** payloadArgv2);
int runPipe(int pfd[], char** payloadArgv, char** payloadArgv2);



int main(){
    char payload[MAX_CMD_LENGHT];
    char fileName[256];
    char* payloadArgv[MAX_CMD_ARGS_LENGHT];
    char* payloadArgv2[MAX_CMD_ARGS_LENGHT];
    int rdct = -1, pCount = -1;

    setSigHandler();
    char* user = getlogin();


    while(1){
        printf("[%s]-->$", user);
        getNextCommand(payload);

        if(!strcmp(payload, "\n")) continue;

        if(!strcmp(payload, "close")) break;


        parseCommandString(payload, payloadArgv, payloadArgv2, fileName, &rdct, &pCount);

        if(rdct != -1){
            execRedirectCommand(fileName, payloadArgv, rdct);
            rdct = -1;
        }else if(pCount != -1){
            execPipeCommand(payloadArgv, payloadArgv2);
            pCount = -1;
        }else{
            execSimpleCommand(payloadArgv);
        }


    }

    return 0;
}

void setSigHandler(){
    void (*oldHandler)();

    oldHandler = signal(SIGINT, SIG_IGN);
    signal(SIGTERM, oldHandler);
}

void getNextCommand(char* payload){
    fgets(payload, MAX_CMD_LENGHT, stdin);

    if(payload[strlen(payload) - 1] == '\n'){
        payload[strlen(payload) - 1] = '\0';
    }
}

void parseCommandString(char* payload, char** payloadArgv, char** payloadArgv2, char* fileName, int* rdct, int* pCount){
    int payloadArgc = 0;


    char* buffer = strtok(payload, " ");

    while(buffer != NULL){
        payloadArgv[payloadArgc] = buffer;

        if(!strcmp(payloadArgv[payloadArgc], ">")){
            *rdct = 0;
            payloadArgv[payloadArgc] = NULL;
            buffer = strtok(NULL, " ");
            strcpy(fileName, buffer);
            break;
        }

        if(!strcmp(payloadArgv[payloadArgc], ">>")){
            *rdct = 1;
            payloadArgv[payloadArgc] = NULL;
            buffer = strtok(NULL, " ");
            strcpy(fileName, buffer);
            break;
        }

        if(!strcmp(payloadArgv[payloadArgc], "|")){
            (*pCount)++;
            payloadArgv[payloadArgc] = NULL;
            parseCommandString(NULL, payloadArgv2, NULL, NULL, NULL, NULL);
            break;
        }

        buffer = strtok(NULL, " ");
        payloadArgc++;
    }


    payloadArgv[payloadArgc] = NULL;

}


int execSimpleCommand(char** payloadArgv){
    pid_t pid = fork();

    // fork failed
    if(pid == -1){
        char* error = strerror(errno);
        printf("fork: %s\n", error);
        return -1;
    }
    //Child process
    else if(pid == 0){
        execvp(payloadArgv[0], payloadArgv);

        // execvp failed
        char* error = strerror(errno);
        printf("pdsh:%s:%s\n", payloadArgv[0], error);
        return -1;
    }
    // Parent process
    else{

        // Wait for child process to finish
        int childStatus;
        waitpid(pid, &childStatus, 0);
        return 0;
    }
}

int execRedirectCommand(char* fileName, char** payloadArgv, int rdct){
    int fd;

    pid_t pid = fork();

    // fork failed
    if(pid == -1){
        char* error = strerror(errno);
        printf("fork: %s\n", error);
        return -1;
    }
    //Child process
    else if(pid == 0){
        if(rdct == 0){
            fd = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0755);

        }else{
            fd = open(fileName, O_WRONLY | O_CREAT | O_APPEND, 0755);

        }
        if(fd == -1){
            char* error = strerror(errno);
            printf("open:%s\n", error);
            return -1;
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);

        execvp(payloadArgv[0], payloadArgv);

        // execvp failed
        char* error = strerror(errno);
        printf("pdsh:%s:%s\n", payloadArgv[0], error);
        return -1;
    }
    // Parent process
    else{
        // Wait for child process to finish
        int childStatus;
        waitpid(pid, &childStatus, 0);

        //fileName = NULL;

        return 0;
    }
}

int execPipeCommand(char** payloadArgv, char** payloadArgv2){
    int pfd[2];

    pipe(pfd);

    pid_t pid = fork();

    // fork failed
    if(pid == -1){
        char* error = strerror(errno);
        printf("fork: %s\n", error);
        return -1;
    }
    //Child process
    else if(pid == 0){
        runPipe(pfd, payloadArgv, payloadArgv2);

    }
    // Parent process
    else{
        // Wait for child process to finish
        int childStatus;
        waitpid(pid, &childStatus, 0);
        return 0;
    }
}

int runPipe(int pfd[], char** payloadArgv, char** payloadArgv2){
    pid_t pid = fork();

    // fork failed
    if(pid == -1){
        char* error = strerror(errno);
        printf("fork: %s\n", error);
        return -1;
    }
    //Child process
    else if(pid == 0){
        // open write end close read end
        dup2(pfd[0], 0);
        close(pfd[1]);

        execvp(payloadArgv2[0], payloadArgv2);

        // execvp failed
        char* error = strerror(errno);
        printf("pdsh:%s%s", payloadArgv2[0], error);
        return -1;
    }
    // Parent process
    else{
        // open read end close write end
        dup2(pfd[1], 1);
        close(pfd[0]);

        execvp(payloadArgv[0], payloadArgv);

        // execvp failed
        char* error = strerror(errno);
        printf("pdsh:%s%s", payloadArgv[0], error);
        return -1;

    }
}
