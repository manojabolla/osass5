#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <unistd.h>
#include <sys/time.h>
#include <locale.h>
#include <signal.h>
#include <errno.h>

#include "common.h"

bool will_terminate();
unsigned int get_random_pct();
void add_signal_handlers();
void handle_sigterm(int sig);
struct clock get_time_to_request_release_rsc(struct clock sysclock);
unsigned int get_nanosecs_to_request_release();
void create_msg_that_contains_rsc(char* mtext, int pid, struct resource_table* rsc_tbl);
unsigned int get_random_resource();
bool will_release_resource();
unsigned int get_resource_to_release(int pid, struct resource_table* rsc_tbl);
void request_a_resource(int rsc_msg_box_id, int pid, struct resource_table* rsc_tbl);
void release_a_resource(int rsc_msg_box_id, int pid, struct resource_table* rsc_tbl);
void send_termination_notification(int rsc_msg_box_id, int pid);

#define ONE_BILLION 1000000000
#define BUF_SIZE 1800

bool event_occured(unsigned int pct_chance) {
    unsigned int percent = (rand() % 100) + 1;
    if (percent <= pct_chance) {
        return 1;
    }
    else {
        return 0;
    }
}

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

void print_and_write(char* str, FILE* fp) {
    fputs(str, stdout);
    fputs(str, fp);
}
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

#define ONE_HUNDRED_MILLION 100000000 // 100ms in nanoseconds
#define TEN_MILLION 10000000 // 10ms in nanoseconds
#define ONE_MILLION 10000000 // 1ms in nanoseconds

const unsigned int CHANCE_TERMINATE = 25;
const unsigned int CHANCE_RELEASE = 50;


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

int main (int argc, char *argv[]) {
    add_signal_handlers();
    srand(time(NULL) ^ getpid());
    setlocale(LC_NUMERIC, "");      // For comma separated integers in printf

    // Get shared memory IDs
    int sysclock_id = atoi(argv[SYSCLOCK_ID_IDX]);
    int rsc_tbl_id = atoi(argv[RSC_TBL_ID_IDX]);
    int rsc_msg_box_id = atoi(argv[RSC_MSGBX_ID_IDX]);
    int pid = atoi(argv[PID_IDX]);

    // Attach to shared memory
    struct clock* sysclock = attach_to_shared_memory(sysclock_id, 1);
    struct resource_table* rsc_tbl = attach_to_shared_memory(rsc_tbl_id, 0);

    struct clock time_to_request_release = get_time_to_request_release_rsc(*sysclock);

    while(1) {
        if (compare_clocks(*sysclock, time_to_request_release) < 0) {
            continue;
        }
        // Time to request/release a resource 
        if (!has_resource(pid, rsc_tbl)) {
            request_a_resource(rsc_msg_box_id, pid, rsc_tbl);
            if (will_terminate()) {
                send_termination_notification(rsc_msg_box_id, pid);
                break;
            }
        }
        else {
            // Determine if we are going to request or release a resource
            if (will_release_resource()) {
                release_a_resource(rsc_msg_box_id, pid, rsc_tbl);
            }
            else {
                request_a_resource(rsc_msg_box_id, pid, rsc_tbl);
                if (will_terminate()) {
                    send_termination_notification(rsc_msg_box_id, pid);
                    break;
                }
            }
        }
        // Get new time to request/release a resouce
        time_to_request_release = get_time_to_request_release_rsc(*sysclock);
    } 

    return 0;  
}

void send_termination_notification(int rsc_msg_box_id, int pid) {
    struct msgbuf rsc_msg_box;
    sprintf(rsc_msg_box.mtext, "%d,TERM,0", pid);
    send_msg(rsc_msg_box_id, &rsc_msg_box, pid); 
}

void release_a_resource(int rsc_msg_box_id, int pid, struct resource_table* rsc_tbl) {
    struct msgbuf rsc_msg_box;
    unsigned int resource_to_release = get_resource_to_release(pid, rsc_tbl);
    sprintf(rsc_msg_box.mtext, "%d,RLS,%d", pid, resource_to_release);
    send_msg(rsc_msg_box_id, &rsc_msg_box, pid);
    // Blocking receive: wait until OSS updates the program state 
    // so that we do not release the same resource many times
    receive_msg(rsc_msg_box_id, &rsc_msg_box, pid+MAX_PROC_CNT);
    return;
}

void request_a_resource(int rsc_msg_box_id, int pid, struct resource_table* rsc_tbl) {
    struct msgbuf rsc_msg_box;
    create_msg_that_contains_rsc(rsc_msg_box.mtext, pid, rsc_tbl);
    send_msg(rsc_msg_box_id, &rsc_msg_box, pid);
    // Blocking receive - wait until granted a resource
    receive_msg(rsc_msg_box_id, &rsc_msg_box, pid+MAX_PROC_CNT);
    // Granted a resource
    return;
}

unsigned int get_resource_to_release(int pid, struct resource_table* rsc_tbl) {
    unsigned int* allocated_resources = get_current_alloced_rscs(pid, rsc_tbl);
    unsigned int size = get_number_of_allocated_rsc_classes(pid, rsc_tbl);
    unsigned int random_idx = rand() % size;
    unsigned int resource_to_release = allocated_resources[random_idx];
    free(allocated_resources);
    return resource_to_release;
}

bool will_release_resource() {
    return event_occured(CHANCE_RELEASE);
}

bool will_terminate() {
    return event_occured(CHANCE_TERMINATE);
}

void create_msg_that_contains_rsc(char* mtext, int pid, struct resource_table* rsc_tbl) {
    unsigned int resource_to_request, num_currently_allocated, max_claims;
    do {
        resource_to_request = get_random_resource();
        num_currently_allocated = rsc_tbl->rsc_descs[resource_to_request].allocated[pid];
        max_claims = rsc_tbl->max_claims[pid];
    } while (num_currently_allocated == max_claims);
    // We currently do not have more of this resource allocated then our max claims limits
    sprintf(mtext, "%d,REQ,%d", pid, resource_to_request);
}

unsigned int get_random_resource() {
    return rand() % NUM_RSC_CLS;
}

unsigned int get_random_pct() {
    return (rand() % 99) + 1;
}

void add_signal_handlers() {
    struct sigaction act;
    act.sa_handler = handle_sigterm; // Signal handler
    sigemptyset(&act.sa_mask);      // No other signals should be blocked
    act.sa_flags = 0;               // 0 so do not modify behavior
    if (sigaction(SIGTERM, &act, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
}

void handle_sigterm(int sig) {
    //printf("USER %d: Caught SIGTERM %d\n", getpid(), sig);
    _exit(0);
}

struct clock get_time_to_request_release_rsc(struct clock sysclock) {
    unsigned int nanoseconds = get_nanosecs_to_request_release();
    struct clock time_to_request_release = sysclock;
    increment_clock(&time_to_request_release, nanoseconds);
    return time_to_request_release;
}

unsigned int get_nanosecs_to_request_release() {
    unsigned int lower_bound = 100000;
    return (rand() % (800000 - lower_bound)) + lower_bound;
}
