#ifndef _USER_H
#define _USER_H

/* Macros */
#define CLOCK_SEM 		"clocksem"
#define SHARED_MEM_KEY_SEC 	4020012
#define SHARED_MEM_KEY_NSEC	4020013
#define SHARED_MEM_KEY_STATE	4020014
#define SHARED_MEM_KEY_MSGQ	4020015
#define SHARED_BUF_SIZE		sizeof (unsigned int)
#define ONE_BILLION 		1000000000

/* Functions */
void sem_shm_msgq_init(); //initialize IPC stuff. shared mem, semaphores, queues, etc
void set_max_claims(int, int); //decide this user's max claim for each resource
void add_clock(unsigned int, unsigned int); //add time to sim clock

/* Count of Resource types and number of user processes */
int R = 20, P = 18, seed;
int shmid_sim_secs, shmid_sim_ns, shmid_state, shmid_qid;
sem_t *sem; //sim clock mutual exclusion
unsigned int *SC_secs; //pointer to shm sim clock (seconds)
unsigned int *SC_ns; //pointer to shm sim clock (nanoseconds)


#endif
