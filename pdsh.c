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

// Constants
#define MAX_CMD_LENGHT 500
#define MAX_CMD_ARGS_LENGHT 50
#define MAX_FILENAME_LENGHT 

// Function prototypes
void getNextCommand(char* payload);
void parseCommandString(char* payload, char** payloadArgv, char** payloadArgv2, char* fileName, int* rdct, int* pCount);
void setSigHandler();
void setChildSigHandler();
int execSimpleCommand(char** payloadArgv);
int execRedirectCommand(char* fileName, char** payloadArgv, int rdct);
int execPipeCommand(char** payloadArgv, char** payloadArgv2);
int runPipe(int pfd[], char** payloadArgv, char** payloadArgv2);



int main(){
    char payload[MAX_CMD_LENGHT + 1]; // raw user input
    char fileName[MAX_FILENAME_LENGHT + 1]; // filename for redirects
    char* payloadArgv[MAX_CMD_ARGS_LENGHT + 1]; // 1st command argument array
    char* payloadArgv2[MAX_CMD_ARGS_LENGHT + 1]; // 2nd command argument array
    int rdct = -1, pCount = -1; // Flags for redirect and pipe commands

    setSigHandler(); // Set the signal handler
    char* user = getlogin(); // Get the login name

	// Loop until users sends close
    while(1){
        printf("[%s]-->$", user);//print the prompt
        getNextCommand(payload);//get the next command from user

        if(!strcmp(payload, "\n")) continue; // if it is an empty command, ignore it

        if(!strcmp(payload, "close")) break; // if it is close, exit


        parseCommandString(payload, payloadArgv, payloadArgv2, fileName, &rdct, &pCount); // parse the input string in arguments

		// If we have a redirection execute the redirectCommand
        if(rdct != -1){
            execRedirectCommand(fileName, payloadArgv, rdct);
            rdct = -1;
		// If we have a pipe execute the pipeCommand
        }else if(pCount != -1){
            execPipeCommand(payloadArgv, payloadArgv2);
            pCount = -1;
		// else execute the command we were given
        }else{
            execSimpleCommand(payloadArgv);
        }
    }

    return 0;
}

void setSigHandler(){
    void (*oldHandler)();

    oldHandler = signal(SIGINT, SIG_IGN); // parent need to ignore SIGINT AND SIGTERM siganls
    signal(SIGTERM, oldHandler);
}

void setChildSigHandler(){
	// Revert the signals back to the default from the parent
	signal(SIGINT, SIG_DFL);
	signal(SIGTERM, SIGINT);
}

void getNextCommand(char* payload){
    fgets(payload, MAX_CMD_LENGHT, stdin);// scan for input

	//fgets reads the newline so we need to drop it
    if(payload[strlen(payload) - 1] == '\n'){
        payload[strlen(payload) - 1] = '\0';
    }
}

void parseCommandString(char* payload, char** payloadArgv, char** payloadArgv2, char* fileName, int* rdct, int* pCount){
    int payloadArgc = 0; //Array counter


    char* buffer = strtok(payload, " ");//tokenize the string with space as delim

	//While we haven't parsed the whole string
    while(buffer != NULL){
        payloadArgv[payloadArgc] = buffer;//store the argument 

		//Check for redirection
        if(!strcmp(payloadArgv[payloadArgc], ">")){
            *rdct = 0; //flag for execution
            payloadArgv[payloadArgc] = NULL;//execvp needs the last arg to be NULL
            buffer = strtok(NULL, " ");//take the filename from the string
            strcpy(fileName, buffer);//store it
            break;
        }
		//Repeat for append
        if(!strcmp(payloadArgv[payloadArgc], ">>")){
            *rdct = 1;
            payloadArgv[payloadArgc] = NULL;
            buffer = strtok(NULL, " ");
            strcpy(fileName, buffer);
            break;
        }
		//Check for pipe
        if(!strcmp(payloadArgv[payloadArgc], "|")){
            (*pCount)++;//flag for execution
            payloadArgv[payloadArgc] = NULL;// NULL the last arg for fgets
            parseCommandString(NULL, payloadArgv2, NULL, NULL, NULL, NULL); //call the function recursivly to get the 2nd arg. payload MUST be NULL for strtok to continue from where it left of.
            break;
        }

        buffer = strtok(NULL, " "); // tokenize next arg
        payloadArgc++;//increase array counter
    }


    payloadArgv[payloadArgc] = NULL;//NULL out last arg

}

int execSimpleCommand(char** payloadArgv){
    pid_t pid = fork(); // fork our process

    // fork failed
    if(pid == -1){
        char* error = strerror(errno);
        printf("fork: %s\n", error);
        return -1;
    }
    //Child process
    else if(pid == 0){
		setChildSigHandler();//set the child signal handler
        execvp(payloadArgv[0], payloadArgv);//execute command

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

    pid_t pid = fork();//fork our process

    // fork failed
    if(pid == -1){
        char* error = strerror(errno);
        printf("fork: %s\n", error);
        return -1;
    }
    //Child process
    else if(pid == 0){
		setChildSigHandler();//set child signal handler
		//Check if we need to overwrite or append to file
        if(rdct == 0){
            fd = open(fileName, O_WRONLY | O_CREAT | O_TRUNC, 0755);

        }else{
            fd = open(fileName, O_WRONLY | O_CREAT | O_APPEND, 0755);
        }
		// open failed
        if(fd == -1){
            char* error = strerror(errno);
            printf("open:%s\n", error);
            return -1;
        }
        dup2(fd, STDOUT_FILENO); //redirect STDOUT to the file
        close(fd);//close the file

        execvp(payloadArgv[0], payloadArgv);//execute command

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

int execPipeCommand(char** payloadArgv, char** payloadArgv2){
    int pfd[2];//stores READ&WRITE ends of our pipe

    pipe(pfd);//create pipe

    pid_t pid = fork();//fork our process

    // fork failed
    if(pid == -1){
        char* error = strerror(errno);
        printf("fork: %s\n", error);
        return -1;
    }
    //Child process
    else if(pid == 0){
		setChildSigHandler();
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
    pid_t pid = fork();//fork our process

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

        execvp(payloadArgv2[0], payloadArgv2);//execute command

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

        execvp(payloadArgv[0], payloadArgv);//execute command

        // execvp failed
        char* error = strerror(errno);
        printf("pdsh:%s%s", payloadArgv[0], error);
        return -1;

    }
}
