#include "param.h"
#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"
#include "fcntl.h"
#include "syscall.h"
#include "traps.h"
#include "memlayout.h"
#include "tournament_tree.h"
#include "kthread.h"

#define THREAD_NUM 8
#define STACK_SIZE 4000

int pid;
volatile int start;
int global;
void * stack;

#define FUNC_START(func1, func2) \
    global = start = 0;\
    if((pid = fork()) == 0){\
        func1();\
        sleep(1000);\
        func2();\
    }\
    else if(pid > 0){\
        wait();\
    }\
    else{\
        printf(1,"fork failed 1\n");\
        exit();\
    }

void thread_func5(void){
    while(!start){;}
    if(kthread_mutex_alloc()==-1){
        printf(1,"mutex alloc failed \n");
    }
    for(;;);
}

void check_mutex_alloc(){
    int tids[THREAD_NUM];
    for(int i = 0;i < THREAD_NUM;i++){
        stack = malloc(STACK_SIZE);
        global++;
        if ((tids[i] = kthread_create(thread_func5, stack)) == -1){
            printf(1, "thread number %d has not been created \n", global);
        }
    }
    start = 1;
}

int main(int argc, char *argv[]){

    FUNC_START(check_mutex_alloc,exit);
    exit();

}

