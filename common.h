#ifndef _COMMON_H
#define _COMMON_H

#define NUM_RSC_CLS 	20
#define MAX_PROC_CNT	18	
#define MAX_CLAIMS 	2 // Upper bound of max number of a resource class a process can claim 

#define bool	int

struct resource_descriptor {
    unsigned int total;
    unsigned int allocated[MAX_PROC_CNT+1];
};

struct resource_table {
    struct resource_descriptor rsc_descs[NUM_RSC_CLS];
    unsigned int max_claims[MAX_PROC_CNT+1];
};

void allocate_rsc_tbl(struct resource_table* rsc_tbl);
struct resource_descriptor get_rsc_desc();
void init_allocated(unsigned int* allocated);
unsigned int get_num_resources();
unsigned int get_max_resource_claims();
void release_resources(struct resource_table* rsc_tbl, int pid);
void print_available_rsc_tbl(struct resource_table* rsc_tbl, FILE* fp);
void print_allocated_rsc_tbl(struct resource_table* rsc_tbl, FILE* fp);
unsigned int* get_current_alloced_rscs(int pid, struct resource_table* rsc_tbl);
unsigned int get_number_of_allocated_rsc_classes(int pid, struct resource_table* rsc_tbl);
bool has_resource(int pid, struct resource_table* rsc_tbl);
bool resource_is_available(struct resource_table* rsc_tbl, int requested_resource);
unsigned int* get_allocated_resources(struct resource_table* rsc_tbl);
unsigned int* get_available_resources(struct resource_table* rsc_tbl);
unsigned int* get_total_resources(struct resource_table* rsc_tbl);
void print_resources(unsigned int* resources, char* title, FILE* fp);
void print_rsc_summary(struct resource_table* rsc_tbl, FILE* fp);



/* Clock defines */
struct clock {
    unsigned long seconds;
    unsigned long nanoseconds;
};

void increment_clock(struct clock* clock, int increment);
struct clock add_clocks(struct clock c1, struct clock c2);
int compare_clocks(struct clock c1, struct clock c2);
long double clock_to_seconds(struct clock c);
struct clock seconds_to_clock(long double seconds);
struct clock calculate_avg_time(struct clock clk, int divisor);
struct clock subtract_clocks(struct clock c1, struct clock c2);
void print_clock(char* name, struct clock clk);
struct clock nanoseconds_to_clock(int nanoseconds);
struct clock get_clock();
void reset_clock(struct clock* clk);



/* Message Queue defines */
#define MSGSZ 50

struct msgbuf {
    long mtype;
    char mtext[MSGSZ];
};

int get_message_queue();
void remove_message_queue(int msgqid);
void receive_msg(int msgqid, struct msgbuf* mbuf, int mtype);
void receive_msg_no_wait(int msgqid, struct msgbuf* mbuf, int mtype);
void send_msg(int msgqid, struct msgbuf* mbuf, int mtype);


/* Global Constants */
#define FIVE_HUNDRED_MILLION 500000000

#define EXECV_SIZE		6
#define SYSCLOCK_ID_IDX		1
#define RSC_TBL_ID_IDX		2
#define RSC_MSGBX_ID_IDX	3
#define PID_IDX			4
#define MAX_RUNTIME		20
#define MAX_NS_BEFORE_NEW_PROC	FIVE_HUNDRED_MILLION
#if 0
const unsigned int EXECV_SIZE = 6;
const unsigned int SYSCLOCK_ID_IDX = 1;
const unsigned int RSC_TBL_ID_IDX = 2;
const unsigned int RSC_MSGBX_ID_IDX = 3;
const unsigned int PID_IDX = 4;

const unsigned int MAX_RUNTIME = 20; // In seconds
//const unsigned int MAX_PROC_CNT = 18;
//const unsigned int NUM_RSC_CLS = 20;

const unsigned int MAX_NS_BEFORE_NEW_PROC = FIVE_HUNDRED_MILLION; // 500ms
#endif


char** split_string(char* str, char* delimeter);
char* get_timestamp();
void print_usage();
bool parse_cmd_line_args(int argc, char* argv[]);
void set_timer(int duration);
bool event_occured(unsigned int pct_chance);
unsigned int** create_array(int m, int n);
void destroy_array(unsigned int** arr);
void print_and_write(char* str, FILE* fp);

/* Shared mem */
int get_shared_memory();
void* attach_to_shared_memory(int shmemid, unsigned int readonly);
void cleanup_shared_memory(int shmemid, void* p);
void detach_from_shared_memory(void* p);
void deallocate_shared_memory(int shmemid);



#endif

