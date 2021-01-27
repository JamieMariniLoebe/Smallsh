#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>

//Allocate memory statically
#define BUFFER_SIZE 2048 
#define MAX_ARGS 512

int isBlankLine(char* command) {
    char c;
    int argCount = 0;

    while((c = *command)) {
        if(c != ' ' && c != '\t' && c != '\n') { //Spaces, tabs, and newlines
            return 0; //return false
        }
        command++; //increment to next char
    }
    return 1; //return true
}

int allowBackground = 1; //toggle background
int atPrompt = 0; //fgets 

void toggleBackgroundProcess(int signal) {
    if(allowBackground == 1) {
        allowBackground = 0;
    }
    else {
        allowBackground = 1;
    }
    if(atPrompt == 1) {
        putc('\n', stdout);
        putc('\n', stdin);
    }
}

int main() {
    struct sigaction original_sig_int_handler = {0};
    struct sigaction sig_ignore_handler = {
        .sa_handler = SIG_IGN //sig ignore
    };
    
    struct sigaction sig_tstp_handler = {
        .sa_handler = toggleBackgroundProcess
    };

    char command[BUFFER_SIZE+1] = {0}; //Max length of characters
    char* args[MAX_ARGS+2] = {0}; //array of args
    //Command + number of args + null pointer
    char* inputFileName = NULL;
    char* outputFileName = NULL;
    int status = 0;
    
    pid_t pid = getpid();

    if(sigaction(SIGINT, &sig_ignore_handler, &original_sig_int_handler) != 0) {
        perror("sigaction sigint");
        exit(1);
    }
    //save original sig handler for foreground child process
    if(sigaction(SIGTSTP, &sig_tstp_handler, NULL) != 0) {
        perror("sigaction sigint");
        exit(1);
    }
    
    int originalAllowBackground = allowBackground;

    while(1) { 
        if (allowBackground != originalAllowBackground) {
            if(allowBackground == 1) {
                printf("Exiting foreground-only mode\n");
            }
            else {
                printf("Entering foreground-only mode (& is now ignored)\n");
            }
        }
        originalAllowBackground = allowBackground;

        int i=0; //counter

        for(i=0; i<MAX_ARGS+2 && args[i] != NULL; i++) {
            free(args[i]);
            args[i] = NULL;
        }
        
        memset(command,0,sizeof(command));

        if(inputFileName) { //Free memory each time
            if(strcmp(inputFileName, "/dev/null") != 0) 
            {
                free(inputFileName);
            }
            
            inputFileName = NULL;
        }

        if(outputFileName) { //Free memory each times
            if(strcmp(outputFileName, "/dev/null") != 0)
            {
                free(outputFileName);
            }
            outputFileName = NULL;
        }

        int childStatus = 0;
        int childID = 0;
        
        while((childID = waitpid(-1, &childStatus, WNOHANG)) > 0) //looping thru all child processes
        {
            if(WIFEXITED(childStatus)) 
            {
                printf("Background pid %d is done: exit value %d\n", childID, WEXITSTATUS(childStatus));
            }
            else if(WIFSIGNALED(childStatus))
            {
                printf("Background pid %d is done: terminated by signal %d\n", childID, WTERMSIG(childStatus));
            }
        } //Waiting for any child

        printf(": ");
        atPrompt = 1;
        fgets(command, sizeof(command), stdin); //Get input from user
        atPrompt = 0;

        //Check if empty
        if(isBlankLine(command) || command[0] == '#'){ 
            //Ignore blank lines and comments
            continue;
        } 
        else {
            char* token = NULL;
            int argCount = 0;
            int inputFileExpected = 0;
            int outputFileExpected = 0;
            int ampersand = 0;
            int val = 10;
            pid_t spawnpid = 0;
            
            token = strtok(command, " \t\n"); //space, tab, newline are delimiters

            //Parse args
            while(token != NULL) { //Will break loop when no more tokens
                if(ampersand == 1) {
                    ampersand = 0;
                    args[argCount] = calloc(2, sizeof(char)); //Calloc initalizes to 0
                    sprintf(args[argCount], "%c", '&'); //If reached, ampersand found was not last word in list/not the background
                    argCount++; //Check next word
                }

                if(strcmp(token, "$$") == 0) {
                    args[argCount] = calloc(10, sizeof(char)); //Calloc initalizes to 0
                    sprintf(args[argCount], "%d", pid); //Converting pid to string
                    argCount++;
                }
                else if(strcmp(token, "<") == 0) {
                    inputFileExpected = 1;

                } else if(strcmp(token, ">") == 0) {
                    outputFileExpected = 1;

                } else if(strcmp(token, "&") == 0) {
                    ampersand = 1; //If set after while loop, this command needs to be put in background (& last word)
                }
                else if(inputFileExpected) {
                    inputFileExpected = 0;
                    inputFileName = calloc(BUFFER_SIZE+1, sizeof(char)); //Calloc initalizes to 0
                    strcpy(inputFileName,token); //copy in filename
                }
                else if(outputFileExpected) {
                    outputFileExpected = 0;
                    outputFileName = calloc(BUFFER_SIZE+1, sizeof(char)); //Calloc initalizes to 0
                    strcpy(outputFileName,token);
                }
                else {
                    args[argCount] = calloc(BUFFER_SIZE+1, sizeof(char)); //Calloc initalizes to 0
                    strcpy(args[argCount], token);
                    argCount++;
                }

                token = strtok(NULL, " \t\n");
            }
            
            if(argCount == 0) {
                continue; //Nothing to be done, no args
            }
            if(allowBackground == 0) {
                ampersand = 0;
            }
            if (ampersand == 1) //ampersand last, background
            {
                if (inputFileName == NULL) 
                {
                    inputFileName = "/dev/null";
                }
                if (outputFileName == NULL) {
                    outputFileName = "/dev/null";
                }
            }
            //if user input exit, terminate program
            if(strcmp(args[0], "exit") == 0) { 
                exit(0);
            }

            else if(strcmp(args[0], "cd") == 0) {
                if(argCount == 1) {
                    char* homeDir = getenv("HOME");
                    chdir(homeDir); //Change directory to directory located in homeDir
                }
                else {
                    char* dir = args[1]; //Directory to change to
                    status = chdir(dir); //Use status to check success or fail (-1)
                    if(status == -1) {
                        printf("Directory not found!\n");
                    }
                    //Change directory to whichever specified directory
                }
            }
            else if(strcmp(args[0], "status") == 0) {
                if(WIFEXITED(status)) 
                {
                    printf("Exit value %d\n", WEXITSTATUS(status));
                }
                if(WIFSIGNALED(status)) 
                {
                    printf("Terminated by signal %d\n", WTERMSIG(status));
                }
            }
            else {
                int i = 0;

                spawnpid = fork(); //If successful, spawnpid will be 0 in the child, and will be the child's pid in the parent
                switch(spawnpid) {
                    case -1:
                        perror("fork() failed!");
                        status = 1; //set status to 1 for exit
                        break;
                    case 0:
                        if(ampersand == 0) { //Only handle sigint for FOREGROUND processes
                            if(sigaction(SIGINT, &original_sig_int_handler, NULL) != 0) {
                                perror("sigaction");
                                exit(1);
                            }
                        }

                        if(sigaction(SIGTSTP, &sig_ignore_handler, NULL) != 0) {
                            perror("sigaction sigtstp");
                            exit(1);
                        }
                        
                        //printf("Child (%d) is now running...\n", getpid());
                        if(inputFileName != NULL) {
                            //printf("Redirect stdin to %s\n", inputFileName);
                            int fd = open(inputFileName, O_RDONLY); // i/o file redirection, read only
                            if(fd == -1) {
                                perror("Stdin open");
                                exit(1);
                            }
                            if(dup2(fd, STDIN_FILENO) == -1) { //DUPLICATE FILE TO STDIN
                                perror("Stdin redirect failed");
                                exit(1);
                            }
                            close(fd);
                        }
                        if(outputFileName != NULL) {
                            //printf("Redirect stdout to %s\n", outputFileName);
                            int flags = O_WRONLY;
                            int fd = 0;
                            if(strcmp(outputFileName, "/dev/null") != 0) { //Check outputFileName is dev/null
                                flags |= O_TRUNC | O_CREAT;
                                fd = open(outputFileName, flags, 0666); //i/o file redirection, write only. truncate, create
                            }
                            else 
                            {
                                fd = open(outputFileName, flags);
                            }
                            if(fd == -1) {
                                perror("Stdout open");
                                exit(1);
                            }
                            if(dup2(fd, STDOUT_FILENO) == -1) { //DUPLICATE FILE TO STDOUT
                                perror("Stdout redirect failed");
                                exit(1);
                            }
                            close(fd);
                        }
                        if(execvp(args[0], args) == -1) {
                            perror("Exec child....");
                            exit(1); //Child must exit
                        }
                        exit(1);
                    default: //Adding square brackets to avoid 'label' error
                        {
                            if(ampersand == 0) {
                                int retpid = 0;
                                do {
                                    retpid = waitpid(spawnpid, &status, 0);
                                    if(WIFSIGNALED(sgit tatus)) {
                                        printf("\nTerminated by signal %d\n", WTERMSIG(status));
                                        break;
                                    }
                                    if(WIFEXITED(status)) {
                                        break;
                                    }
                                } while (retpid == spawnpid);
                            }
                            else {
                                printf("Background pid is %d\n", spawnpid);
                            }
                        }
                }
            }
        }

    }
    return 0;
}

