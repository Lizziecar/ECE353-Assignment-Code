#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

// some reminders:
// Close file, directories, and file descriptors

// Linked list for PID storage

typedef struct Process {
    int pid;
    int ssp_id;
    char* name;
    int status;
    int waited; // 1 means that it was already waited

    struct Process* next;
} Process;

int ssp_id = 0;
Process* head = NULL;

// add a process to the linked list
int add_to_process_list(char* proc_name, int pid, int status) {
    // Record new process:
    Process* node = (Process*)malloc(sizeof(Process));
    char* name = (char*)malloc(sizeof(char)*strlen(proc_name));
    strcpy(name, proc_name);
    node->name = name;
    node->pid = pid;
    node->ssp_id = ssp_id;
    node->status = status; // running
    node->next = NULL;  
    
    // record the new process
    if (ssp_id > 0 && head != NULL) {
        // Link to linkedlist
        Process* curr = head;
        while (curr->next != NULL) {
            curr = curr->next;
        }
        curr->next = node;

        ssp_id++;

        return node->ssp_id;

    } else {
        // Make the start of the list
        head = node;
        ssp_id++;
        return node->ssp_id;
    }
}

// Subreaper processes
void handle_signal(int signum) { //sigchild is the signal when a child process finishes
    if (signum != SIGCHLD) {
        printf("Ignoring signal %d\n", signum);
    }
    
    int wstatus;
    pid_t wait_pid;
    while((wait_pid = waitpid(-1, &wstatus, WNOHANG)) != -1) {
        if (wait_pid == -1) {
            int err = errno;
            perror("wait_pid");
            exit(err);
        }
        if (wait_pid == 0) { // No children to wait for, stop polling
            break;
        }
        else if (WIFEXITED(wstatus)) {
            //printf("Wait returned for an exited process! pid: %d, status: %d\n", wait_pid, WEXITSTATUS(wstatus));
            Process* curr = head;
            if (curr != NULL) {
                while (curr != NULL && curr->pid != wait_pid) {
                    curr = curr->next;
                }
                if (curr != NULL) { //one of my processes
                    curr->status = WEXITSTATUS(wstatus); // return exit status
                } else {
                    //printf("Adopted Child\n");
                    add_to_process_list("<unknown>",wait_pid, WEXITSTATUS(wstatus));
                }
            } else {
                //printf("Adopted New\n");
                add_to_process_list("<unknown>",wait_pid, WEXITSTATUS(wstatus));
            }
        } else if (WIFSIGNALED(wstatus)) {
            Process* curr = head;
            while (curr != NULL && curr->pid != wait_pid) {
                curr = curr->next;
            }
            if (curr->pid == wait_pid) { //one of my processes
                curr->status = 128 + wstatus; // signal status result
            } else {
                add_to_process_list("<unknown>",wait_pid, WEXITSTATUS(wstatus));
            }
        }
        else {
            exit(ECHILD);
        }
    }
}

void register_signal(int signum) {
    struct sigaction new_action = {0};
    sigemptyset(&new_action.sa_mask);
    new_action.sa_handler = handle_signal;
    new_action.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(signum, &new_action, NULL) == -1) {
        int err = errno;
        perror("sigaction");
        exit(err);
    }
}

void ssp_init() {
    prctl(PR_SET_CHILD_SUBREAPER, 1);
    register_signal(SIGCHLD);
    return;
}

// Helper functions for ssp_create
// Error Check function from Lecture
static void check_error(int ret, const char *message) {
    if (ret != -1) {
        //printf("Success: %s", message);
        return;
    }
    int err = errno;
    perror(message);
    exit(err);
}

void check_error_directory(DIR* value, const char* errorMessage) {
    if (value != NULL) {
        return;
    }
    int error = errno;
    perror(errorMessage);
    exit(error);
}

void check_error_entry(struct dirent* value, const char* errorMessage) {
    if (value != NULL) {
        return;
    }
    int error = errno;
    perror(errorMessage);
    exit(error);
}

// mine
int ssp_create(char *const *argv, int fd0, int fd1, int fd2) {
    // fd is already open as a file descriptor

    // Fork to create parent and child process
    pid_t pid = fork();
    check_error(pid, "pid");
    if (pid > 0) { // parent
        return add_to_process_list(argv[0], pid, -1);
    }
    else { // child
        //printf("Child is working\n");
        // Set file descriptors:
        check_error(dup2(fd0,0), "fd0");
        check_error(dup2(fd1,1), "fd1");
        check_error(dup2(fd2,2), "fd2");

        // Close all other file descriptors
        DIR *dir;
        struct dirent *dir_entry;

        check_error_directory(dir = opendir("/proc/self/fd"),"Directory");
        check_error_entry(dir_entry = readdir(dir), "Entry");

        while ((dir_entry = readdir(dir)) != NULL) {
            //printf("%s\n", dir_entry->d_name);
            if (dir_entry->d_type == DT_LNK && atoi(dir_entry->d_name) > 2) {
                check_error(close(atoi(dir_entry->d_name)),"File Descriptor");
            }
        }
        
        // turn the child into another process
        check_error((execvp(argv[0], argv)), "execvp");

        closedir(dir);

        return 0;
    }
}

int ssp_get_status(int ssp_id) {
    // make sure not empty
    if (head == NULL) {
        return -1;
    } 
    Process* curr = head;
    // go to the appropriate entry
    while(curr->ssp_id != ssp_id) {
        curr = curr->next;
    }
    // return status
    return curr->status;
}

void ssp_send_signal(int ssp_id, int signum) {
    Process* curr = head;
    while(curr->ssp_id != ssp_id){
        curr = curr->next;
    }
    if (curr->status == -1){
        check_error(kill(curr->pid, signum), "kill error");
        curr->status = 128 + signum;
    }
    return;
}

void ssp_wait() {
    Process* curr = head;
    for (int i = 0; i < ssp_id; i++) { // fake blocking call
        while(curr->status == -1) { // go through all processes to make sure it isn't running
        }
        curr = curr->next;
    }

    return;
}

void ssp_print(void) {

    // Check length of names
    Process* curr = head;
    int name_length = 3;
    if (curr != NULL) {
        while (curr != NULL) {
            if (strlen(curr->name) > name_length) {
                name_length = strlen(curr->name);
            }

            curr = curr->next;
        }
    }

    // Print header
    printf("    PID CMD"); // header
    for (int i = 3; i < name_length; i++) {
        printf(" ");
    }
    printf(" STATUS\n");

    // Print data
    curr = head; // reset back to head
    while (curr != NULL) {
        
        // Print PID:
        printf("%7d ", curr->pid);
        
        // Print name and status
        printf("%s", curr->name);
            for (int i = strlen(curr->name); i < name_length; i++) {
                printf(" ");
            }
        printf(" %d\n", curr->status);
        curr = curr->next;
    }
}
