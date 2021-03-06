#include "types.h"
#include "stat.h"
#include "user.h"
#include <stddef.h>
#define CHILDREN_NUMBER 30

struct data {
    int CBTs[CHILDREN_NUMBER];        // CBTs for each child
    int pids[CHILDREN_NUMBER];        // pid for each child
    int turnarounds[CHILDREN_NUMBER]; // turnaround times for each child
    int waitings[CHILDREN_NUMBER];    // waiting times for each child
    int priorities[CHILDREN_NUMBER];  //priorities for each thread
};

struct info {
    int i;
    struct data *tdata;
};

void run(void* arg) {
    struct info *thread_info = (struct info*) arg;
    int priority = 6 - (thread_info->i / 5);
    setPriority(priority);
    
    for (int i = 0; i < 250; ++i){
        printf(1, "%d, %d\n", thread_info->i, i);
        sleep(10);
    }
    thread_info->tdata->CBTs[thread_info->i] = getCBT();
    thread_info->tdata->pids[thread_info->i] = thread_info->i;
    thread_info->tdata->turnarounds[thread_info->i] = getTurnAroundTime();
    thread_info->tdata->waitings[thread_info->i] = getWaitingTime();
    thread_info->tdata->priorities[thread_info->i] = priority;
    exit();
}

int main() {

    struct data *p_data = (struct data*) malloc(sizeof(p_data));

    if (changePolicy(2) == 0)
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
        printf(1, "child with priority: %d, Burst Time: %d, Waiting Time: %d, Turnaround Time: %d\n",
               thread_info[i].tdata->priorities[i], thread_info[i].tdata->CBTs[i], thread_info[i].tdata->waitings[i],
               thread_info[i].tdata->turnarounds[i]);
        avg_bt += thread_info[i].tdata->CBTs[i];
        avg_tt += thread_info[i].tdata->turnarounds[i];
        avg_wt += thread_info[i].tdata->waitings[i];
    }

    printf(1, "\n\n\n\n****Avg times per classes***\n");
    int TTPerClass[6] = {0};
    int WTPerClass[6] = {0};
    int CBTPerClass[6] = {0};
    for (int j = 0; j < CHILDREN_NUMBER; j++)
    {
        int childPriority = thread_info[j].tdata->priorities[j];
        TTPerClass[childPriority - 1] += thread_info[j].tdata->turnarounds[j];
        WTPerClass[childPriority - 1] += thread_info[j].tdata->waitings[j];
        CBTPerClass[childPriority - 1] += thread_info[j].tdata->CBTs[j];
    }
    for (int j = 0; j < 6; j++)
    {
        printf(1, "Priority class: %d ----> Avg Turnaround: %d, Avg Waiting: %d, Avg CBT: %d\n",
        j + 1,
        TTPerClass[j] / (CHILDREN_NUMBER / 6),
        WTPerClass[j] / (CHILDREN_NUMBER / 6),
        CBTPerClass[j] / (CHILDREN_NUMBER / 6));
    }

    printf(1, "\n\n\n\n****Total Avg times***\n");
    printf(1, "\n\n\nAvg: bt: %d, tt: %d, wt: %d\n", avg_bt / CHILDREN_NUMBER, avg_tt / CHILDREN_NUMBER, avg_wt / CHILDREN_NUMBER);
    exit();
}