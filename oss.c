#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <getopt.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <string.h>
#include <locale.h>
#include <sys/time.h>
#include <time.h>
#include <sys/queue.h>
#include <math.h>
#include <errno.h>

#include "common.h"

#define QUEUESIZE 18

struct Queue {
        int q[QUEUESIZE+2];		        /* body of queue */
        int first;                      /* position of first element */
        int last;                       /* position of last element */
        int count;                      /* number of queue elements */
};

void init_queue(struct Queue *q);
void enqueue(struct Queue *q, int x);
int dequeue(struct Queue *q);
bool empty(struct Queue *q);
void print_queue(struct Queue *q);
int peek(struct Queue *q);
char* get_queue_string(struct Queue *q);
bool bankers_algorithm(struct resource_table* rsc_tbl, int pid, int requested_resource);
unsigned int* get_work_arr(unsigned int* available_resources);
bool* get_can_finish();
bool check_for_safe_sequence(bool* can_finish);
unsigned int** get_needs_matrix(struct resource_table* rsc_tbl);


void wait_for_all_children();
void add_signal_handlers();
void handle_sigint(int sig);
void handle_sigalrm(int sig);
void cleanup_and_exit();
void fork_child(char** execv_arr, unsigned int pid);
struct clock get_time_to_fork_new_proc(struct clock sysclock);
unsigned int get_nanoseconds();
unsigned int get_available_pid();
struct message parse_msg(char* mtext);
void unblock_process_if_possible(int resource, struct msgbuf rsc_msg_box, struct clock* time_blocked);
unsigned int get_work();
void print_blocked_queue();
void print_statistics(unsigned int num_requests);
float pct_requests_granted(unsigned int num_requests);

unsigned int num_resources_granted = 0, num_bankers_ran = 0;

// Globals used in signal handler
int simulated_clock_id, rsc_tbl_id, rsc_msg_box_id;
struct clock* sysclock;
struct clock* total_time_blocked;                                 
struct resource_table* rsc_tbl;
int cleaning_up = 0;
pid_t* childpids;
FILE* fp;
struct Queue* blocked;

struct message {
    int pid;
    char txt[10];
    int resource;
};

/* Clock */
#define ONE_BILLION 1000000000

void increment_clock(struct clock* clock, int increment) {
    clock->nanoseconds += increment;
    if (clock->nanoseconds >= ONE_BILLION) {
        clock->seconds += 1;
        clock->nanoseconds -= ONE_BILLION;
    }
}

struct clock add_clocks(struct clock c1, struct clock c2) {
    struct clock out = {
        .seconds = 0,
        .nanoseconds = 0
    };
    out.seconds = c1.seconds + c2.seconds;
    increment_clock(&out, c1.nanoseconds + c2.nanoseconds);
    return out;
}

int compare_clocks(struct clock c1, struct clock c2) {
    if (c1.seconds > c2.seconds) {
        return 1;
    }
    if ((c1.seconds == c2.seconds) && (c1.nanoseconds > c2.nanoseconds)) {
        return 1;
    }
    if ((c1.seconds == c2.seconds) && (c1.nanoseconds == c2.nanoseconds)) {
        return 0;
    }
    return -1;
}

long double clock_to_seconds(struct clock c) {
    long double seconds = c.seconds;
    long double nanoseconds = (long double)c.nanoseconds / ONE_BILLION; 
    seconds += nanoseconds;
    return seconds;
}

struct clock seconds_to_clock(long double seconds) {
    struct clock clk = { .seconds = (int)seconds };
    seconds -= clk.seconds;
    clk.nanoseconds = seconds * ONE_BILLION;
    return clk;
}

struct clock calculate_avg_time(struct clock clk, int divisor) {
    long double seconds = clock_to_seconds(clk);
    long double avg_seconds = seconds / divisor;
    return seconds_to_clock(avg_seconds);
}

struct clock subtract_clocks(struct clock c1, struct clock c2) {
    long double seconds1 = clock_to_seconds(c1);
    long double seconds2 = clock_to_seconds(c2);
    long double result = seconds1 - seconds2;
    return seconds_to_clock(result);
}

void print_clock(char* name, struct clock clk) {
    printf("%-15s: %'ld:%'ld\n", name, clk.seconds, clk.nanoseconds);
}

struct clock nanoseconds_to_clock(int nanoseconds) {
    // Assumes nanoseconds is less than 2 billion
    struct clock clk = { 
        .seconds = 0, 
        .nanoseconds = 0 
    };

    if (nanoseconds >= ONE_BILLION) {
        nanoseconds -= ONE_BILLION;
        clk.seconds = 1;
    }

    clk.nanoseconds = nanoseconds;
    
    return clk;
}

struct clock get_clock() {
    struct clock out = {
        .seconds = 0,
        .nanoseconds = 0
    };
    return out;
}

void reset_clock(struct clock* clk) {
    clk->seconds = 0;
    clk->nanoseconds = 0;
}

int get_shared_memory() {
    int shmemid;

    shmemid = shmget(IPC_PRIVATE, getpagesize(), IPC_CREAT | S_IRUSR | S_IWUSR);

    if (shmemid == -1) {
        perror("shmget");
        exit(1);
    }
    
    return shmemid;
}

void* attach_to_shared_memory(int shmemid, unsigned int readonly) {
    void* p;
    int shmflag;

    if (readonly) {
        shmflag = SHM_RDONLY;
    }
    else {
        shmflag = 0;
    }

    p = (void*)shmat(shmemid, 0, shmflag);

    if (!p) {
        perror("shmat");
        exit(1);
    }

    return p;

}

void cleanup_shared_memory(int shmemid, void* p) {
 detach_from_shared_memory(p);
    deallocate_shared_memory(shmemid);
}

void detach_from_shared_memory(void* p) {
    if (shmdt(p) == -1) {
        perror("shmdt");
        exit(1);
    }
}

void deallocate_shared_memory(int shmemid) {
    if (shmctl(shmemid, IPC_RMID, 0) == 1) {
        perror("shmctl");
        exit(1);
    }
}

#define BUF_SIZE 1800

void print_rsc_summary(struct resource_table* rsc_tbl, FILE* fp) {
    unsigned int* total_resources = get_total_resources(rsc_tbl);
    unsigned int* allocated_resources = get_allocated_resources(rsc_tbl);
    unsigned int* available = get_available_resources(rsc_tbl);

    char buffer[100];

    sprintf(buffer, "%48s", "Resource Summary\n");
    print_and_write(buffer, fp);

    print_resources(total_resources, "Total Resources\n", fp);
    print_resources(allocated_resources, "Allocated Resources\n", fp);
    print_resources(available, "Available Resources\n", fp);

    free(total_resources);
    free(allocated_resources);
    free(available);
}

void print_resources(unsigned int* resources, char* title, FILE* fp) {
    int i;
    char buffer[BUF_SIZE];
    
    // Print title
    sprintf(buffer, "\n");
    sprintf(buffer + strlen(buffer), "%s", title);
    
    // Print column titles
    sprintf(buffer + strlen(buffer), "  ");
    for (i = 0; i < NUM_RSC_CLS; i++) {
        sprintf(buffer + strlen(buffer),"R%-3d", i+1);
    }
    sprintf(buffer + strlen(buffer),"\n");
    
    // Print data
    sprintf(buffer + strlen(buffer), "  ");
    for (i = 0; i < NUM_RSC_CLS; i++) {
        sprintf(buffer + strlen(buffer),"%-4d", resources[i]);
    }
    sprintf(buffer + strlen(buffer),"\n");
    print_and_write(buffer, fp);
}

void print_allocated_rsc_tbl(struct resource_table* rsc_tbl, FILE* fp) {
    int i, j;
    char buffer[BUF_SIZE];
    sprintf(buffer,"\n");
    sprintf(buffer + strlen(buffer),"%61s", "Current (Allocated) System Resources\n");
    sprintf(buffer + strlen(buffer),"     ");
    // print column titles
    for (i = 0; i < NUM_RSC_CLS; i++) {
        sprintf(buffer + strlen(buffer),"R%-3d", i+1);
    }
    sprintf(buffer + strlen(buffer),"\n");
    for (i = 1; i <= MAX_PROC_CNT; i++) {
        sprintf(buffer + strlen(buffer),"P%-4d", i);
        // print all resources allocated for process i
        for (j = 0; j < NUM_RSC_CLS; j++) {
            sprintf(buffer + strlen(buffer),"%-4d", rsc_tbl->rsc_descs[j].allocated[i]);
        }
        sprintf(buffer + strlen(buffer),"\n");
    }
    sprintf(buffer + strlen(buffer),"\n");
    print_and_write(buffer, fp);
}

void allocate_rsc_tbl(struct resource_table* rsc_tbl) {
    int i;
    for (i = 0; i < NUM_RSC_CLS; i++) {
        rsc_tbl->rsc_descs[i] = get_rsc_desc();
    }
}

struct resource_descriptor get_rsc_desc() {
    struct resource_descriptor rsc_desc = {
        .total = get_num_resources(),
    };
    init_allocated(rsc_desc.allocated);
    return rsc_desc;
}

unsigned int get_num_resources() {
    return (rand() % 10) + 1; // 1 - 10 inclusive
}

void init_allocated(unsigned int* allocated) {
    int i;
    for (i = 1; i <= MAX_PROC_CNT; i++) {
        allocated[i] = 0;
    }
}

unsigned int get_max_resource_claims() {
    return (rand() % MAX_CLAIMS) + 1;
}

void release_resources(struct resource_table* rsc_tbl, int pid) {
    unsigned int i;
    for (i = 0; i < NUM_RSC_CLS; i++) {
        rsc_tbl->rsc_descs[i].allocated[pid] = 0; 
    }
}

unsigned int* get_current_alloced_rscs(int pid, struct resource_table* rsc_tbl) {
    // Returns an array of all resource classes that are currently allocated
    unsigned int num_resources, num_resource_classes = 0;
    unsigned int i, j;

    num_resource_classes = get_number_of_allocated_rsc_classes(pid, rsc_tbl);

    unsigned int* allocated_resources = malloc(sizeof(unsigned int) * num_resource_classes);
    j = 0;
    for (i = 0; i < NUM_RSC_CLS; i++) {
        num_resources = rsc_tbl->rsc_descs[i].allocated[pid];
        if (num_resources > 0) {
            allocated_resources[j++] = i;
        }
    }
    return allocated_resources;
}

unsigned int get_number_of_allocated_rsc_classes(int pid, struct resource_table* rsc_tbl) {
    unsigned int num_resources, num_resource_classes = 0;
    unsigned int i;
    for (i = 0; i < NUM_RSC_CLS; i++) {
        num_resources = rsc_tbl->rsc_descs[i].allocated[pid];
        if (num_resources > 0) {
            num_resource_classes++;
        }
    }
    return num_resource_classes;
}

bool has_resource(int pid, struct resource_table* rsc_tbl) {
    unsigned int i;
    unsigned int num_resources = 0;
    for (i = 0; i < NUM_RSC_CLS; i++) {
        num_resources = rsc_tbl->rsc_descs[i].allocated[pid];
        if (num_resources > 0) {
            return 1;
        }
    }
    return 0;
}

bool resource_is_available(struct resource_table* rsc_tbl, int requested_resource) {
    unsigned int* allocated_resources = get_allocated_resources(rsc_tbl);
    unsigned int currently_allocated = allocated_resources[requested_resource];
    unsigned int total = rsc_tbl->rsc_descs[requested_resource].total;
    free(allocated_resources);
    if (currently_allocated == total) {
        // All resources in this resource class have already been allocated so
        // the resource is not available
        return 0;
    }
    return 1;
}

unsigned int* get_available_resources(struct resource_table* rsc_tbl) {
    // Subtract the two to get the total available resources
    unsigned int i;
    unsigned int* allocated_resources = get_allocated_resources(rsc_tbl);
    unsigned int* total_resources = get_total_resources(rsc_tbl);
    unsigned int* available_resources = malloc(sizeof(unsigned int) * NUM_RSC_CLS);
    for (i = 0; i < NUM_RSC_CLS; i++) {
        available_resources[i] = total_resources[i] - allocated_resources[i];
    }
    
    free(allocated_resources);
    free(total_resources);
    
    return available_resources;
}

unsigned int* get_total_resources(struct resource_table* rsc_tbl) {
    unsigned int i;
    unsigned int* total_resources = malloc(sizeof(unsigned int) * NUM_RSC_CLS);
    for (i = 0; i < NUM_RSC_CLS; i++) {
        total_resources[i] = rsc_tbl->rsc_descs[i].total;
    }
    return total_resources;
}

unsigned int* get_allocated_resources(struct resource_table* rsc_tbl) {
    unsigned int i, j;
    unsigned int* allocated_resources = malloc(sizeof(unsigned int) * NUM_RSC_CLS);
    for (i = 0; i < NUM_RSC_CLS; i++) {
        allocated_resources[i] = 0;
    }

    for (i = 0; i < NUM_RSC_CLS; i++) {
        for (j = 1; j <= MAX_PROC_CNT; j++) {
            allocated_resources[i] += rsc_tbl->rsc_descs[i].allocated[j];
        }
    }
    return allocated_resources;
}

/* Queue */
void init_queue(struct Queue *q) {
    q->first = 0;
    q->last = QUEUESIZE-1;
    q->count = 0;
}

void enqueue(struct Queue *q, int x) {
    if (q->count >= QUEUESIZE)
    printf("Warning: queue overflow enqueue x=%d\n",x);
    else {
        q->last = (q->last+1) % QUEUESIZE;
        q->q[ q->last ] = x;    
        q->count = q->count + 1;
    }
}

int dequeue(struct Queue *q) {
    int x;

    if (q->count <= 0) printf("Warning: empty queue dequeue.\n");
    else {
        x = q->q[ q->first ];
        q->first = (q->first+1) % QUEUESIZE;
        q->count = q->count - 1;
    }

    return(x);
}

int peek(struct Queue *q) {
    int x;

    if (q->count <= 0) printf("Warning: empty queue peek.\n");
    else {
        x = q->q[ q->first ];
    }

    return(x);
}

bool empty(struct Queue *q) {
    if (q->count <= 0) return (1);
    else return (0);
}

void print_queue(struct Queue *q) {
    int i;

    i = q->first; 
    
    while (i != q->last) {
        printf("%2d ", q->q[i]);
        i = (i+1) % QUEUESIZE;
    }

    printf("%2d ",q->q[i]);
    printf("\n");
}

char* get_queue_string(struct Queue *q) {
    char* out = malloc(sizeof(char) * 255);
    
    sprintf(out, " ");

    int i;

    i = q->first; 
    
    while (i != q->last) {
        sprintf(out + strlen(out), "%2d ", q->q[i]);
        i = (i+1) % QUEUESIZE;
    }

    sprintf(out + strlen(out), "%2d ",q->q[i]);
    sprintf(out + strlen(out), "\n");
    return out;
}

/* Message Queue */
int get_message_queue() {
    int msgqid;

    msgqid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);

    if (msgqid == -1) {
        perror("msgget");
        exit(1);
    }

    return msgqid;
}

void receive_msg(int msgqid, struct msgbuf* mbuf, int mtype) {
    if (msgrcv(msgqid, mbuf, sizeof(mbuf->mtext), mtype, 0) == -1) {
        perror("msgrcv");
        exit(1);
    }
}

void receive_msg_no_wait(int msgqid, struct msgbuf* mbuf, int mtype) {
    sprintf(mbuf->mtext, "0");
    if (msgrcv(msgqid, mbuf, sizeof(mbuf->mtext), mtype, IPC_NOWAIT) == -1) {
        if (errno == ENOMSG) {
            // No message of type mtype
            return;
        }
        perror("msgrcv");
        exit(1);
    }
}

void send_msg(int msgqid, struct msgbuf* mbuf, int mtype) {
    mbuf->mtype = mtype;
    if (msgsnd(msgqid, mbuf, sizeof(mbuf->mtext), IPC_NOWAIT) < 0) {
        perror("msgsnd");
        exit(1);
    }
}

void remove_message_queue(int msgqid) {
    if (msgctl(msgqid, IPC_RMID, NULL) == -1) {
        perror("msgctl");
        exit(1);
    }
}

char** split_string(char* str, char* delimeter) {
    char** strings = malloc(10 * sizeof(char*));
    char* substr;

    substr = strtok(str, delimeter);

    int i = 0;
    while (substr != NULL)
    {
        strings[i] = substr;
        substr = strtok(NULL, delimeter);
        i++;
    }

    return strings;

}

char* get_timestamp() {
    char* timestamp = malloc(sizeof(char)*10);
    time_t rawtime;
    struct tm* timeinfo;

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    sprintf(timestamp, "%d:%d:%d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    
    return timestamp;
}

bool parse_cmd_line_args(int argc, char* argv[]) {
    int option;
    bool verbose = 0;
    while ((option = getopt (argc, argv, "hv")) != -1)
    switch (option) {
        case 'h':
            print_usage();
            break;
        case 'v':
            verbose = 1;
            break;
        default:
            print_usage();
    }
    return verbose;
}

void print_usage() {
    fprintf(stderr, "Usage: oss [-v for verbose mode]\n");
    exit(0);
}

void set_timer(int duration) {
    struct itimerval value;
    value.it_interval.tv_sec = duration;
    value.it_interval.tv_usec = 0;
    value.it_value = value.it_interval;
    if (setitimer(ITIMER_REAL, &value, NULL) == -1) {
        perror("setitimer");
        exit(1);
    }
}

bool event_occured(unsigned int pct_chance) {
    unsigned int percent = (rand() % 100) + 1;
    if (percent <= pct_chance) {
        return 1;
    }
    else {
        return 0;
    }
}

unsigned int** create_array(int m, int n) {
    unsigned int* values = calloc(m*n, sizeof(unsigned int));
    unsigned int** rows = malloc(n*sizeof(unsigned int*));
    int i;

    for (i=0; i<n; ++i)
    {
        rows[i] = values + i*m;
    }
    return rows;
}

void destroy_array(unsigned int** arr) {
    free(*arr);
    free(arr);
}

void print_and_write(char* str, FILE* fp) {
    fputs(str, stdout);
    fputs(str, fp);
}

/* Bankers Algo */
#define NUM_RSC_CLS 20
#define MAX_PROC_CNT 18

bool bankers_algorithm(struct resource_table* rsc_tbl, int pid, int requested_resource) {
    unsigned int i, j;
    
    // We grant the request and then check if there is a safe state
    rsc_tbl->rsc_descs[requested_resource].allocated[pid]++;

    unsigned int* available_resources = get_available_resources(rsc_tbl); 
    unsigned int** needs = get_needs_matrix(rsc_tbl);
    unsigned int* work = get_work_arr(available_resources);
    bool* can_finish = get_can_finish();

    /*
     *  Determine if there is a safe sequence
     */
    unsigned int num_that_could_finish = 0;

    do {
        num_that_could_finish = 0;

        // Check if each process can finish executing
        // If it can then add its allocated resources to the work vector
        for (i = 1; i <= MAX_PROC_CNT; i++) {
            if (can_finish[i]) {
                // We've already determined that this process can finish
                continue;
            }
            // For process i
            for(j = 0; j < NUM_RSC_CLS; j++) {
                // Check if needs is greater than available
                if (needs[i][j] > available_resources[j]) {
                    // If it is then we cannot finish executing
                    can_finish[i] = 0;
                    break;
                }
            }
            if (can_finish[i]) {
                // Can finish so add process i's allocated resources to the work vector
                for (j = 0; j < NUM_RSC_CLS; j++) {
                    work[j] += rsc_tbl->rsc_descs[j].allocated[i];
                }
                num_that_could_finish++;
            }
        }

    } while (num_that_could_finish > 0);

    bool safe_sequence_exists = check_for_safe_sequence(can_finish);
    
    // Restore resource table state
    rsc_tbl->rsc_descs[requested_resource].allocated[pid]--;
    
    free(available_resources);
    free(work);
    free(can_finish);
    free(needs);

    return safe_sequence_exists;
}

unsigned int* get_work_arr(unsigned int* available_resources) {
    unsigned int i;
    unsigned int* work = malloc(sizeof(unsigned int) * NUM_RSC_CLS);
    for (i = 0; i < NUM_RSC_CLS; i++) {
        work[i] = available_resources[i];
    }
    return work;
}

bool* get_can_finish() {
    unsigned int i;
    bool* can_finish = malloc(sizeof(bool) * MAX_PROC_CNT+1);
    for (i = 1; i <= MAX_PROC_CNT; i++) {
        can_finish[i] = 1;
    }
    return can_finish;
}

unsigned int** get_needs_matrix(struct resource_table* rsc_tbl) {
    unsigned int i, j;
    unsigned int** needs = create_array(MAX_PROC_CNT+1, NUM_RSC_CLS);
    unsigned int max_processes, allocated_processes;
    for (i = 1; i <= MAX_PROC_CNT; i++) {
        for (j = 0; j < NUM_RSC_CLS; j++) {
            max_processes = rsc_tbl->max_claims[i];                   // Max number of resources for process i
            allocated_processes = rsc_tbl->rsc_descs[j].allocated[i]; // Number of allocated resources for process i
            needs[i][j] = max_processes - allocated_processes;
        }
    }
    return needs;
}

bool check_for_safe_sequence(bool* can_finish) {
    unsigned int i;
    for (i = 1; i <= MAX_PROC_CNT; i++) {
        if (!can_finish[i]) {
            return 0;
        }
    }
    return 1;
}

int main (int argc, char* argv[]) 
{
    /*
     *  Setup program before entering main loop
     */
    const unsigned int TOTAL_RUNTIME = 2;       // Max seconds oss should run for

    set_timer(MAX_RUNTIME);                     // Set timer that triggers SIGALRM
    add_signal_handlers();          
    setlocale(LC_NUMERIC, "");                  // For comma separated integers in printf
    srand(time(NULL) ^ getpid());

    bool verbose = parse_cmd_line_args(argc, argv);

    unsigned int i, pid = 0, num_messages = 0;
    char buffer[255];                           // Used to hold output that will be printed and written to log file
    unsigned int elapsed_seconds = 0;           // Holds total real-time seconds the program has run
    struct timeval tv_start, tv_stop;           // Used to calculated real elapsed time
    gettimeofday(&tv_start, NULL);
    blocked = malloc(sizeof(struct Queue) * NUM_RSC_CLS);          // Array of blocked queues (1 for each resource)
    for (i = 0; i < NUM_RSC_CLS; i++) {
        struct Queue bq;
        init_queue(&bq);
        blocked[i] = bq;
    }

    unsigned int proc_cnt = 0;                  // Holds total number of active child processes

    struct clock time_to_fork = get_clock();    // Holds time to schedule new process

    // Setup execv array to pass initial data to children processes
    char* execv_arr[EXECV_SIZE];                
    execv_arr[0] = "./user";
    execv_arr[EXECV_SIZE - 1] = NULL;
    
    /*
     *  Setup shared memory
     */
    // Shared logical clock
    simulated_clock_id = get_shared_memory();
    sysclock = (struct clock*) attach_to_shared_memory(simulated_clock_id, 0);
    reset_clock(sysclock);
    // Shared Resource Table 
    rsc_tbl_id = get_shared_memory();
    rsc_tbl = (struct resource_table*) attach_to_shared_memory(rsc_tbl_id, 0);
    allocate_rsc_tbl(rsc_tbl);
    // Shared resource message box for user processes to request/release resources 
    rsc_msg_box_id = get_message_queue();
    struct msgbuf rsc_msg_box;

    // Holds all childpids
    childpids = malloc(sizeof(pid_t) * (MAX_PROC_CNT + 1));
    for (i = 1; i <= MAX_PROC_CNT; i++) {
        childpids[i] = 0;
    }

    // Open log file for writing
    if ((fp = fopen("./oss.log", "w")) == NULL) {
        perror("fopen");
        exit(1);
    }

    // Get a time to fork first process at
    time_to_fork = get_time_to_fork_new_proc(*sysclock);
    
    // Increment current time so it is time to fork a user process
    *sysclock = time_to_fork;

    // Declare more variables needed in main loop
    struct msqid_ds msgq_ds;
    msgq_ds.msg_qnum = 0;

    struct message msg;
    int resource;
    bool rsc_granted, rsc_is_available;
    // char reason[50];

    // Used for statisitcs
    unsigned int num_requests = 0;
    struct clock time_blocked[MAX_PROC_CNT+1];
    for (i = 1; i <= MAX_PROC_CNT; i++) {
        time_blocked[i].seconds = 0;
        time_blocked[i].nanoseconds = 0;
    }
    total_time_blocked = malloc(sizeof(struct clock));
    reset_clock(total_time_blocked);

    /*
     *  Main loop
     */
    while ( elapsed_seconds < TOTAL_RUNTIME ) {
        // Check if it is time to fork a new user process
        if (compare_clocks(*sysclock, time_to_fork) >= 0 && proc_cnt < MAX_PROC_CNT) {
            // Fork a new process
            pid = get_available_pid();
            rsc_tbl->max_claims[pid] = get_max_resource_claims();
            fork_child(execv_arr, pid);
            proc_cnt++;
            
            if (verbose) {
                sprintf(buffer, "Master: Generating P%d at time %ld:%'ld\n",
                    pid, sysclock->seconds, sysclock->nanoseconds);
                print_and_write(buffer, fp);
            
   	
	    }
            sprintf(buffer, "Master has detected Process P%d requesting R%d at time %ld:%'ld\n",
                        pid, resource+1, time_to_fork.seconds, time_to_fork.nanoseconds);
                print_and_write(buffer, fp);

            time_to_fork = get_time_to_fork_new_proc(*sysclock);
    
        }


        // Get number of messages
        msgctl(rsc_msg_box_id, IPC_STAT, &msgq_ds);
        num_messages = msgq_ds.msg_qnum;

        // Check for any messages
        if (num_messages > 0) {
            
            receive_msg(rsc_msg_box_id, &rsc_msg_box, 0);

            if (strlen(rsc_msg_box.mtext) < 5) {
                // Every once in awhile, the message text is too short to be a real message and will cause segmentation faults if we continue
                // So, just try again
                continue;
            }

            // We received a message from a user process
            msg = parse_msg(rsc_msg_box.mtext);
            resource = msg.resource;
            pid = msg.pid;

		//new add
            time_to_fork = get_time_to_fork_new_proc(*sysclock);
            *sysclock = time_to_fork;

            if (strcmp(msg.txt, "REQ") == 0) {
                // Process is requesting a resource
                if (verbose) {
                    sprintf(buffer, "Master has detected Process P%d requesting R%d at time %ld:%'ld\n",
                        pid, resource+1, time_to_fork.seconds, time_to_fork.nanoseconds);
                    print_and_write(buffer, fp);
                }

                num_requests++;

                rsc_granted = 0;
                rsc_is_available = resource_is_available(rsc_tbl, resource);
                // sprintf(reason, "resource is unavailable");
                
                if (rsc_is_available) {
                    // Resource is available so run bankers algorithm to check if we grant
                    // safely grant this request
                    rsc_granted = bankers_algorithm(rsc_tbl, pid, resource);
                    increment_clock(sysclock, get_work());
                    num_bankers_ran++;

                    // sprintf(reason, "granting this resource would lead to an unsafe state");
                }
                
                if (rsc_granted) {
                    // Resource granted
                    if (verbose) {
                        sprintf(buffer, "Master granting P%d request R%d at time %ld:%'ld\n",
                            pid, resource+1, sysclock->seconds, sysclock->nanoseconds);
                        print_and_write(buffer, fp);
                    }

                    // Update program state
                    rsc_tbl->rsc_descs[resource].allocated[pid]++;
                    num_resources_granted++;
                    
                    if (num_resources_granted % 20 == 0) {
                        // Print table of allocated resources
                        print_allocated_rsc_tbl(rsc_tbl, fp);
                    }

                    // Send message back to user program to let it know that it's request was granted
                    send_msg(rsc_msg_box_id, &rsc_msg_box, pid+MAX_PROC_CNT);
                }
                else {
                    // Resource was not granted
                    // sprintf(buffer, "Master: Blocking P%d for requesting R%d at time %ld:%'ld because %s\n",
                    //     pid, resource+1, sysclock->seconds, sysclock->nanoseconds, reason);
                    sprintf(buffer, "Master blocking P%d for requesting R%d at time %ld:%'ld\n",
                        pid, resource+1, sysclock->seconds, sysclock->nanoseconds);
                    print_and_write(buffer, fp);

                    // Add process to blocked queue
                    enqueue(&blocked[resource], pid);
                    
                    // Record the time the process was blocked
                    time_blocked[pid] = *sysclock;
                }

            }
            else if (strcmp(msg.txt, "RLS") == 0) {
                // Process is releasing a resource
                if (verbose) {
                    sprintf(buffer, "Master has acknowledged Process P%d releasing R%d at time %ld:%'ld\n",
                        pid, resource+1, sysclock->seconds, sysclock->nanoseconds);
                    print_and_write(buffer, fp);
                }

                // Update program state
                rsc_tbl->rsc_descs[resource].allocated[pid]--;

                // Send message back to user program to let it know that we updated the resource table
                send_msg(rsc_msg_box_id, &rsc_msg_box, pid+MAX_PROC_CNT);

                // Check if we can unblock any processes
                unblock_process_if_possible(resource, rsc_msg_box, time_blocked);

            }
            else {
                // Process terminated
                if (verbose) {
                    sprintf(buffer, "Master has acknowledged that P%d terminated at time %ld:%'ld\n",
                        pid, sysclock->seconds, sysclock->nanoseconds);
                    print_and_write(buffer, fp);
                }

                // Update program state
                childpids[pid] = 0;
                proc_cnt--;
                release_resources(rsc_tbl, pid); // Updates resource table
                
                for (i = 0; i < NUM_RSC_CLS; i++) {
                    int j;
                    // Check MAX_CLAIMS times for each resource in case 
                    // the process had it's MAX resources allocated to it
                    for (j = 0; j < MAX_CLAIMS; j++) {
                        // Check if we can unblock any processes
                        unblock_process_if_possible(i, rsc_msg_box, time_blocked);   
                    }
                }

            }
            if (verbose) {
                sprintf(buffer, "\n");
                print_and_write(buffer, fp);
            }

            // Increment clock slightly whenever a resource is granted or released
            increment_clock(sysclock, get_work());
        }

        increment_clock(sysclock, get_nanoseconds());

        // Calculate total elapsed real-time seconds
        gettimeofday(&tv_stop, NULL);
        elapsed_seconds = tv_stop.tv_sec - tv_start.tv_sec;
    }

    // Print information before exiting
    sprintf(buffer, "Master: Exiting at time %'ld:%'ld because %d seconds have been passed\n", 
        sysclock->seconds, sysclock->nanoseconds, TOTAL_RUNTIME);
    print_and_write(buffer, fp);

    print_allocated_rsc_tbl(rsc_tbl, fp);
    print_rsc_summary(rsc_tbl, fp);

    sprintf(buffer, "\n");
    print_and_write(buffer, fp);
    
    print_blocked_queue();

    print_statistics(num_requests);

    cleanup_and_exit();

    return 0;
}

void fork_child(char** execv_arr, unsigned int pid) {
    if ((childpids[pid] = fork()) == 0) {
        // Child so...
        char clock_id[10];
        char rtbl_id[10];
        char rmsgbox_id[10];
        char p_id[5];
        
        sprintf(clock_id, "%d", simulated_clock_id);
        sprintf(rtbl_id, "%d", rsc_tbl_id);
        sprintf(rmsgbox_id, "%d", rsc_msg_box_id);
        sprintf(p_id, "%d", pid);
        
        execv_arr[SYSCLOCK_ID_IDX] = clock_id;
        execv_arr[RSC_TBL_ID_IDX] = rtbl_id;
        execv_arr[RSC_MSGBX_ID_IDX] = rmsgbox_id;
        execv_arr[PID_IDX] = p_id;

// todo
//	printf("Master has detected Process P%d requesting R%d at time xxx:xxx\n", pid, rsc_tbl_id);
//todo
        execvp(execv_arr[0], execv_arr);

        perror("Child failed to execvp the command!");
        exit(1);
    }

    if (childpids[pid] == -1) {
        perror("Child failed to fork!\n");
        exit(1);
    }
}

void wait_for_all_children() {
    pid_t pid;
    printf("Master: Waiting for all children to exit\n");
    fprintf(fp, "Master: Waiting for all children to exit\n");
    
    while ((pid = wait(NULL))) {
        if (pid < 0) {
            if (errno == ECHILD) {
                perror("wait");
                break;
            }
        }
    }
}

void terminate_children() {
    printf("Master: Sending SIGTERM to all children\n");
    fprintf(fp, "Master: Sending SIGTERM to all children\n");
    int i;
    for (i = 1; i <= MAX_PROC_CNT; i++) {
        if (childpids[i] == 0) {
            continue;
        }
        if (kill(childpids[i], SIGTERM) < 0) {
            if (errno != ESRCH) {
                // Child process exists and kill failed
                perror("kill");
            }
        }
    }
    free(childpids);
}

void add_signal_handlers() {
    struct sigaction act;
    act.sa_handler = handle_sigint; // Signal handler
    sigemptyset(&act.sa_mask);      // No other signals should be blocked
    act.sa_flags = 0;               // 0 so do not modify behavior
    if (sigaction(SIGINT, &act, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    act.sa_handler = handle_sigalrm; // Signal handler
    sigemptyset(&act.sa_mask);       // No other signals should be blocked
    if (sigaction(SIGALRM, &act, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
}

void handle_sigint(int sig) {
    printf("\nMaster: Caught SIGINT signal %d\n", sig);
    fprintf(fp, "\nMaster: Caught SIGINT signal %d\n", sig);
    if (cleaning_up == 0) {
        cleaning_up = 1;
        cleanup_and_exit();
    }
}

void handle_sigalrm(int sig) {
    printf("\nMaster: Caught SIGALRM signal %d\n", sig);
    fprintf(fp, "\nMaster: Caught SIGALRM signal %d\n", sig);
    if (cleaning_up == 0) {
        cleaning_up = 1;
        cleanup_and_exit();
    }

}

void cleanup_and_exit() {
    terminate_children();
    printf("Master: Removing message queues and shared memory\n");
    fprintf(fp, "Master: Removing message queues and shared memory\n");
    remove_message_queue(rsc_msg_box_id);
    wait_for_all_children();
    cleanup_shared_memory(simulated_clock_id, sysclock);
    cleanup_shared_memory(rsc_tbl_id, rsc_tbl);
    free(blocked);
    free(total_time_blocked);
    fclose(fp);
    exit(0);
}

struct clock get_time_to_fork_new_proc(struct clock sysclock) {
    unsigned int ns_before_next_proc = rand() % MAX_NS_BEFORE_NEW_PROC; 
    increment_clock(&sysclock, ns_before_next_proc);
    return sysclock;
}

unsigned int get_nanoseconds() {
    return (rand() % 800000) + 10000; // 800,000 - 10,000 inclusive
}

unsigned int get_work() {
    return (rand() % 100000) + 10000; // 10,000 - 100,000 inclusive
}

unsigned int get_available_pid() {
    unsigned int pid, i;
    for (i = 1; i <= MAX_PROC_CNT; i++) {
        if (childpids[i] > 0) {
            continue;
        }
        pid = i;
        break;
    }
    return pid;
}

struct message parse_msg(char* mtext) {
    // Parse a message sent from a user process
    struct message msg;
    char ** msg_info = split_string(mtext, ",");
    
    msg.pid = atoi(msg_info[0]);
    strcpy(msg.txt, msg_info[1]);
    msg.resource = atoi(msg_info[2]);

    free(msg_info);

    return msg;
}

void unblock_process_if_possible(int resource, struct msgbuf rsc_msg_box, struct clock* time_blocked) {
    if (empty(&blocked[resource])) {
        // There are no processes blocked on this resource
        return;
    }

    bool rsc_is_available = resource_is_available(rsc_tbl, resource);
    if (!rsc_is_available) {
        // Resource is unavaible
        return;
    }

    // char reason[50];
    char buffer [100];

    int pid = peek(&blocked[resource]);

    // Resource is available so run bankers algorithm to check if we can
    // safely grant this request
    bool rsc_granted = bankers_algorithm(rsc_tbl, pid, resource);
    increment_clock(sysclock, get_work());
    num_bankers_ran++;

    // sprintf(reason, "granting this resource would lead to an unsafe state");
    
    if (rsc_granted) {
        // Resource granted
        sprintf(buffer, "Master Unblocking P%d and granting it R%d at time %ld:%'ld\n",
            pid, resource+1, sysclock->seconds, sysclock->nanoseconds);
        print_and_write(buffer, fp);

        // Update program state
        rsc_tbl->rsc_descs[resource].allocated[pid]++;
        num_resources_granted++;
        dequeue(&blocked[resource]);

        // Add wait time to total time blocked
        struct clock wait_time = subtract_clocks(*sysclock, time_blocked[pid]);
        *total_time_blocked = add_clocks(*total_time_blocked, wait_time);
        
        if (num_resources_granted % 20 == 0) {
            // Print table of allocated resources
            print_allocated_rsc_tbl(rsc_tbl, fp);
        }

        // Send message back to user program to let it know that it's request was granted
        send_msg(rsc_msg_box_id, &rsc_msg_box, pid+MAX_PROC_CNT);
    }
}

void print_blocked_queue() {
    int i;
    bool queue_is_empty = 1;
    char buffer[1000];
    char* queue;
    
    sprintf(buffer, "Blocked Processes\n");
    for (i = 0; i < NUM_RSC_CLS; i++) {
        if (empty(&blocked[i])) {
            // Queue empty
            continue;
        }
        
        // Resource label
        sprintf(buffer + strlen(buffer), "  R%2d:", i+1);
        
        // What processes are blocked on resource i
        queue = get_queue_string(&blocked[i]);
        sprintf(buffer + strlen(buffer), "%s", queue);
        free(queue);
        
        // Queue is not empty
        queue_is_empty = 0;
    }
    
    if (queue_is_empty) {
        sprintf(buffer + strlen(buffer), "  < no blocked processes >\n");
    }
    
    sprintf(buffer + strlen(buffer), "\n");

    print_and_write(buffer, fp);
}

void print_statistics(unsigned int num_requests) {
    char buffer[1000];

    sprintf(buffer, "Statistics\n");
    sprintf(buffer + strlen(buffer), "  %-23s: %'d\n", "Requests Granted", num_resources_granted);
    sprintf(buffer + strlen(buffer), "  %-23s: %'d\n", "Total Requests", num_requests);
    sprintf(buffer + strlen(buffer), "  %-23s: %.2f%%\n", "Pct Requests Granted", pct_requests_granted(num_requests));
    sprintf(buffer + strlen(buffer), "  %-23s: %'ld:%'ld\n", "Total Wait Time", total_time_blocked->seconds, total_time_blocked->nanoseconds);
    sprintf(buffer + strlen(buffer), "  %-23s: %'d\n", "Deadlock Avoidance Ran", num_bankers_ran);

    sprintf(buffer + strlen(buffer), "\n");
    
    print_and_write(buffer, fp);
}

float pct_requests_granted(unsigned int num_requests) {
    return  (num_resources_granted / (float) num_requests) * 100;
}
