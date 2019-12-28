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

#define THREAD_NUM 10
#define STACK_SIZE 4000

trnmnt_tree* tree;
char * command;
char *args[4];
int pid;
volatile int start;
volatile int global;
void * stack;
int mutex;
int sum;

#define FUNC_START(func1, func2) \
    global = start = 0;\
    if((pid = fork()) == 0){\
        func1();\
        func2();\
    }\
    else if(pid > 0){\
        wait();\
    }\
    else{\
        printf(1,"fork failed 1\n");\
        exit();\
    }
#define EXEC_START(func, program, args) \
    global = start = 0;\
    if((pid = fork()) == 0){\
        func(program, args);\
    }\
    else if(pid > 0){\
        return pid;\
    }\
    else{\
        printf(1,"fork failed 1\n");\
        exit();\
    }

void thread_func1 (void){
    while(!start){;} 
    kthread_exit();
}

void thread_func2(void){
    kthread_exit();
}

void thread_func3(void){
    for(;;);
    
}
void thread_func4(void){
    while(!start){;}
    sleep(200);
    kthread_exit();
}
void thread_func5(void){
    while(!start){;}
    if(kthread_mutex_lock(mutex)==-1){
        printf(1,"failed to lock mutex \n");
    }

    for(int i=0;i<100;i++){
        sum++;
    }
    if(kthread_mutex_unlock(mutex)==-1){
        printf(1,"failed to unlock mutex \n");
    }
    kthread_exit();
}

void thread_func6(void){
    int index = global;
    while(!start){;}
    if(trnmnt_tree_acquire(tree, index)==-1){
        printf(1,"failed to lock mutex \n");
    }
    for(int i=0;i<1000;i++){
        sum++;
    }
    if(trnmnt_tree_release(tree , index)==-1){
        printf(1,"failed to unlock mutex \n");
    }
    kthread_exit();
}

void check_thread_alloc(){
    for(int i = 0;i < 16;i++){
        stack = malloc(STACK_SIZE);
        global++;
        if (kthread_create(thread_func1, stack) == -1){
            printf(1, "thread number %d has not been created \n", global);
        }
        sleep(20);
    }
    start = 1;

}

void thread_func7(void){
    if(trnmnt_tree_release(tree , 0)==-1){
        printf(1,"failed to unlock mutex \n");
    }
    if(trnmnt_tree_release(tree , 9)==-1){
        printf(1,"failed to unlock mutex \n");
    }
    if(trnmnt_tree_acquire(tree , 9)==-1){
        printf(1,"failed to lock mutex \n");
    }
    kthread_exit();
}

void check_kthread_exit(){
    for(int i = 0;i < THREAD_NUM;i++){
        stack = malloc(STACK_SIZE);
        global++;
        if (kthread_create(thread_func2, stack) == -1){
            printf(1, "thread number %d has not been created \n", global);
        }
        sleep(30);
    }
    sleep(200);

}


void check_exit(){
    for(int i = 0;i < THREAD_NUM;i++){
        stack = malloc(STACK_SIZE);
        global++;
        if (kthread_create(thread_func3, stack) == -1){
            printf(1, "thread number %d has not been created \n", global);
        }
    }
}
void check_join(){
    int tids[THREAD_NUM];
    for(int i = 0;i < THREAD_NUM;i++){
        stack = malloc(STACK_SIZE);
        global++;
        if ((tids[i] = kthread_create(thread_func4, stack)) == -1){
            printf(1, "thread number %d has not been created \n", global);
        }
    }
    start = 1;
    for(int i = 0;i < THREAD_NUM;i++){
        if(kthread_join(tids[i])==-1)
            printf(1, "failed joining");
    }
}

void check_mutex_alloc(){
    command = "/helper1";
    args[0] = "/helper1";
    global = start = 0;\
    args[1] = 0;
    for(int i = 0;i <= 7;i++){
        if((pid = fork()) == 0)
            exec(command, args);
    }
    for(int i = 0;i <= 7;i++){
        wait();
    }
}

void check_mutex_lock_unlock(){
    int tids[THREAD_NUM];
    if((mutex = kthread_mutex_alloc())==-1){
        printf(1, "failed to allocate mutex \n");
    }
    for(int i = 0;i < THREAD_NUM;i++){
        stack = malloc(STACK_SIZE);
        global++;
        if ((tids[i] = kthread_create(thread_func5, stack)) == -1){
            printf(1, "thread number %d has not been created \n", global);
        }
    }
    start = 1;
    sleep(200);
    printf(1,"sum = %d \n",sum);
}

void check_double_lock(){
    if((mutex = kthread_mutex_alloc())==-1){
        printf(1, "failed to allocate mutex \n");
    }
    if(kthread_mutex_lock(mutex)==-1){
        printf(1, "failed to lock mutex \n");
    }
    if(kthread_mutex_lock(1000)==-1){
        printf(1, "failed to lock mutex \n");
    }
    if(kthread_mutex_lock(mutex)==-1){
        printf(1, "failed to lock mutex \n");
    }
    if(kthread_mutex_unlock(mutex)==-1){
        printf(1,"failed to unlock mutex \n");
    }
    if(kthread_mutex_unlock(mutex)==-1){
        printf(1,"failed to unlock mutex \n");
    }
    if(kthread_mutex_unlock(1000)==-1){
        printf(1,"failed to unlock mutex \n");
    }
    if(kthread_mutex_dealloc(1000)==-1){
        printf(1,"failed to dealloc mutex \n");
    }
    if(kthread_mutex_dealloc(mutex)==-1){
        printf(1,"failed to dealloc mutex \n");
    }
    if(kthread_mutex_dealloc(mutex)==-1){
        printf(1,"failed to dealloc mutex \n");
    }  
}


void check_tree_alloc(void){
    int result;
    for(int i = 1;i<8;i++)
    {
        if((tree = trnmnt_tree_alloc(i)) == 0){ 
            printf(1,"1 trnmnt_tree allocated unsuccessfully\n"); 
        } 
        result = trnmnt_tree_dealloc(tree); 
        if(result == -1){ 
            printf(1,"1 trnmnt_tree deallocated unsuccessfully\n"); 
        } 
    }
}

void check_tree_lock(){
    
    if((tree = trnmnt_tree_alloc(5))== 0){
        printf(1, "failed to allocate tree \n");
    }
    for(int i = 0;i < 8;i++){
        global++;
        stack = malloc(STACK_SIZE);
        if (kthread_create(thread_func6, stack) == -1){
            printf(1, "thread number %d has not been created \n", global);
        }
        sleep(20);
    }
    start = 1;
    sleep(200);
    printf(1,"sum = %d \n",sum);
    trnmnt_tree_dealloc(tree);
}

void check_tree_fail(){
    
    if((tree = trnmnt_tree_alloc(3))== 0){
        printf(1, "failed to allocate tree \n");
    }
    if(trnmnt_tree_acquire(tree, 0)==-1){
        printf(1,"failed to lock mutex \n");
    }
    
    stack = malloc(STACK_SIZE);
    if (kthread_create(thread_func7, stack) == -1){
        printf(1, "thread number %d has not been created \n", global);
    }
    sleep(20);
    if(trnmnt_tree_acquire(tree, 0)==-1){
        printf(1,"failed to lock mutex \n");
    }
    if(trnmnt_tree_release(tree, 0)==-1){
        printf(1,"failed to lock mutex \n");
    }
    trnmnt_tree_dealloc(tree);
}
int main(int argc, char *argv[]){

    printf(1,"#################################\n");
    printf(1,"starting threads allocation limit test: \n");
    FUNC_START(check_thread_alloc,kthread_exit);
    printf(1,"if only thread 16 has not been allocated threads allocation works fine!\n");

    printf(1,"#################################\n");
    printf(1,"starting kthread exit test: \n");
    FUNC_START(check_kthread_exit,kthread_exit);
    printf(1,"if nothing was printed kthread works fine!\n");

    printf(1,"#################################\n");
    printf(1,"starting exit test: \n");
    FUNC_START(check_exit,exit);
    printf(1,"if nothing was printed exit works fine!\n");

    printf(1,"#################################\n");
    printf(1,"starting join test: \n");
    FUNC_START(check_join,kthread_exit);
    printf(1,"if nothing was printed join works fine!\n");


   
    printf(1,"#################################\n");
    printf(1,"starting mutex_alloc_dealloc test: \n");
    if(kthread_mutex_alloc()==-1){
        printf(1,"mutex alloc failed \n");
    }
    check_mutex_alloc();
    check_mutex_alloc();
    check_mutex_alloc();
    printf(1,"if exactly three mutexes failed to be allocated mutex_alloc_dealloc works fine!\n");


    printf(1,"#################################\n");
    printf(1,"starting mutex_lock_unlock test: \n");
    FUNC_START(check_mutex_lock_unlock,kthread_exit);
    printf(1,"expected: sum = 1000  \n");
    FUNC_START(check_double_lock,kthread_exit);
    printf(1,"expected: fail twice for each of: lock, unlock and dealloc: \n");

    printf(1,"#################################\n");
    printf(1,"starting tree_alloc test: \n");
    FUNC_START(check_tree_alloc,kthread_exit);
    printf(1,"if nothing was printed tree alloc works fine!\n");
 
    printf(1,"#################################\n");
    printf(1,"starting tree_lock test: \n");
    FUNC_START(check_tree_lock,kthread_exit);
    printf(1,"expected: sum = 8000  \n");

    printf(1,"#################################\n");
    printf(1,"starting tree_fail_test: \n");
    FUNC_START(check_tree_fail,kthread_exit);
    printf(1,"expected: 2 lock failures and 2 unlock failures  \n");
    exit();

}

