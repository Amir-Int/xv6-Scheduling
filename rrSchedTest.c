#include "types.h"
#include "stat.h"
#include "user.h"
#include <stddef.h>
#define CHILDREN_NUMBER 10

struct data {
    int CBTs[CHILDREN_NUMBER];        // CBTs for each child
    int pids[CHILDREN_NUMBER];        // pid for each child
    int turnarounds[CHILDREN_NUMBER]; // turnaround times for each child
    int waitings[CHILDREN_NUMBER];    // waiting times for each child
};

struct info {
    int i;
    struct data *tdata;
};

void run(void* arg) {
    struct info *thread_info = (struct info*) arg;
    for (int i = 0; i < 1000; ++i){
        printf(1, "%d, %d\n", thread_info->i, i);
        sleep(10);
    }
    thread_info->tdata->CBTs[thread_info->i] = getCBT();
    thread_info->tdata->pids[thread_info->i] = thread_info->i;
    thread_info->tdata->turnarounds[thread_info->i] = getTurnAroundTime();
    thread_info->tdata->waitings[thread_info->i] = getWaitingTime();
    exit();
}

int main() {

    struct data *p_data = (struct data*) malloc(sizeof(p_data));

    if (changePolicy(1) == 0)
        printf(1, "changing Policy  was succes\n");
    else
        printf(1, "changing Policy  was failed\n");
  
    struct info thread_info[CHILDREN_NUMBER];

    for (int i = 0; i < CHILDREN_NUMBER; i++) {
        thread_info[i].i = i;
        thread_info[i].tdata = p_data;
        thread_creator(&run, (void*) &thread_info[i]);
    }

    while (thread_wait() > 0);

    int avg_bt = 0;
    int avg_tt = 0;
    int avg_wt = 0;

    for (int i = 0; i < CHILDREN_NUMBER; ++i) {
        printf(1, "Pid: %d, Burst Time: %d, Waiting Time: %d, Turnaround Time: %d\n",
               i, thread_info[i].tdata->CBTs[i], thread_info[i].tdata->waitings[i],
               thread_info[i].tdata->turnarounds[i]);
        avg_bt += thread_info[i].tdata->CBTs[i];
        avg_tt += thread_info[i].tdata->turnarounds[i];
        avg_wt += thread_info[i].tdata->waitings[i];
    }

    printf(1, "\n\n\nAvg: bt: %d, tt: %d, wt: %d\n", avg_bt / CHILDREN_NUMBER, avg_tt / CHILDREN_NUMBER, avg_wt / CHILDREN_NUMBER);
    exit();
}