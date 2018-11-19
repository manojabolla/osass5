#ifndef _OSS_H
#define _OSS_H

/* Macros */
#define CLOCK_SEM 		"clocksem"
#define SHARED_MEM_KEY_SEC 	4020012
#define SHARED_MEM_KEY_NSEC	4020013
#define SHARED_MEM_KEY_STATE	4020014
#define SHARED_MEM_KEY_MSGQ	4020015
#define SHARED_BUF_SIZE		sizeof (unsigned int)
#define ONE_BILLION 		1000000000

/********************* functions ************************************/
static int set_periodic_intr(double); 		//periodic interrupt setup
static int configure_interrupt(); 		//periodic interrupt setup
static void intr_handler(int signo, siginfo_t *info, void *context); //handler
static void sigint_handler(int sig_num); 	//sigint handler

void user_kill(int);
void user_block(int);
void release_resource(int, int, int);
void terminate_children();
void msg_clear();
int get_bit_vector_pos(); //returns first open position in bitvector array
int can_spawn_process(); //returns 1 if it's time to spawn a new user process
void next_proc_time(); //schedule next user process spawn
void add_clock(unsigned int, unsigned int); //add time to sim clock
void print_alloc(); //print memory resource allocation table
void print_avail(); //print the system's available resources
void sem_shm_msgq_init(); //initialize IPC stuff. shared mem, semaphores, queues, etc
void sem_shm_msgq_cleanup(); //initialize IPC stuff. shared mem, semaphores, queues, etc
int safe_algo(int, int, int); //determine if current system is in a safe state
int get_user_proc_count(int[]); //returns current number of user processes


#endif
