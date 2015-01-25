/* Tests categorical mutual exclusion with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */
#include <stdio.h>
#include <stdlib.h>

#include "devices/timer.h"
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/interrupt.h"
#include "lib/random.h" //generate random numbers

#define VERBOSE 0

#define BUS_CAPACITY 3
#define SENDER 0
#define RECEIVER 1
#define NORMAL 0
#define HIGH 1

#define TRANSFER_TIME 5

/*
 *	initialize task with direction and priority
 * */
typedef struct 
{
	int direction;
	int priority;
} task_t;

void batchScheduler(unsigned int num_tasks_send, 
					unsigned int num_task_receive,
					unsigned int num_priority_send,
					unsigned int num_priority_receive);

void senderTask(void *);
void receiverTask(void *);
void senderPriorityTask(void *);
void receiverPriorityTask(void *);

void init_bus(void);

void oneTask(task_t task);/*Task requires to use the bus and executes methods below*/
void getSlot(task_t task); /* task tries to use slot on the bus */
void transferData(task_t task); /* task processes data on the bus either sending or receiving based on the direction*/
void leaveSlot(task_t task); /* task release the slot */

// The semaphore keeping track of the bus capacity
// Will be initialized to a value of 3
struct semaphore busCapacity;

// The semaphores keeping track of whether we have a high priority task to finish first
struct semaphore hiPriSendTask;
struct semaphore hiPriRecvTask;
// A lock (mutex) locking down the hiPriTask semaphore (only one thread should alter it)
struct lock mutex;

// Global variable to keep track of the current bus direction;
// the initial value makes no difference.
static int currentBusDirection = 1;

/* initializes semaphores */ 
void init_bus(void)
{
    random_init((unsigned int)123456789); 
    
    // The semaphores keeping track of whether we have a high-priority task to run (and direction), and it's lock
	sema_init(&hiPriSendTask, 0);
	sema_init(&hiPriRecvTask, 0);
	
	lock_init(&mutex);
	
	// The semaphore keeping track of the current bus capacity;  
	// we can't have more than 3 senders/receivers at any point in time
    sema_init(&busCapacity, BUS_CAPACITY);
    
    if(VERBOSE)
    	msg("Finished Initializing stuff...");
}

/*
 *  Creates a memory bus sub-system  with num_tasks_send + num_priority_send
 *  sending data to the accelerator and num_task_receive + num_priority_receive tasks
 *  reading data/results from the accelerator.
 *
 *  Every task is represented by its own thread. 
 *  Task requires and gets slot on bus system (1)
 *  process data and the bus (2)
 *  Leave the bus (3).
 */

// Keep track of nr of threads that has finished executing
int threadsFinished;
// Keep track of nr of threads that has been created
int threadsCreated;

void batchScheduler(unsigned int num_tasks_send, 
					unsigned int num_tasks_receive,
					unsigned int num_priority_send, 
					unsigned int num_priority_receive)
{	
	threadsFinished = 0;
	threadsCreated = 0;
	
	// Calculate the number of threads we need to create
	int numThreads = num_tasks_send + num_tasks_receive + num_priority_send + num_priority_receive;
	
	// An array of all thread IDs
	tid_t threads[numThreads];
	
	int counter;
	for(counter = 0; counter<numThreads; counter++)
	{		
		if(num_priority_send != 0)
		{
			num_priority_send--;
			
			threads[counter] = thread_create("hiSend", PRI_MAX, senderPriorityTask, NULL);
			if(VERBOSE)
				msg("Created a thread with thread id #%d", threads[counter]);
		}
		else if (num_priority_receive != 0)
		{
			num_priority_receive--;

			threads[counter] = thread_create("hiRecv", PRI_MAX, receiverPriorityTask, NULL);
			if(VERBOSE)
				msg("Created a thread with thread id #%d", threads[counter]);
		}
		else if(num_tasks_send != 0)
		{
			num_tasks_send--;
			
			threads[counter] = thread_create("send", PRI_MIN, senderTask, NULL);
			if(VERBOSE)
				msg("Created a thread with thread id #%d", threads[counter]);
		}
		else if(num_tasks_receive != 0)
		{
			num_tasks_receive--;
			
			threads[counter] = thread_create("recv", PRI_MIN, receiverTask, NULL);
			if(VERBOSE)
				msg("Created a thread with thread id #%d", threads[counter]);
		}
	}
	
	threadsCreated = sizeof(threads)/sizeof(threads[0]);
	
	// Give all the threads time to finish
	while(threadsFinished != threadsCreated)
	{
		if(VERBOSE)
			msg("%d threads created and %d threads finished, sleeping...", threadsCreated, threadsFinished);
		timer_sleep(20);
	}
}

/* Normal task,  sending data to the accelerator */
void senderTask(void *aux UNUSED)
{
	task_t newTask = {SENDER, NORMAL};
	oneTask(newTask);
}

/* High priority task, sending data to the accelerator */
void senderPriorityTask(void *aux UNUSED)
{
	task_t newTask = {SENDER, HIGH};
	oneTask(newTask);
}

/* Normal task, reading data from the accelerator */
void receiverTask(void *aux UNUSED)
{
	task_t newTask = {RECEIVER, NORMAL};
	oneTask(newTask);
}

/* High priority task, reading data from the accelerator */
void receiverPriorityTask(void *aux UNUSED)
{
	task_t newTask = {RECEIVER, HIGH};
	oneTask(newTask);
}

/* abstract task execution*/
void oneTask(task_t task) 
{
	getSlot(task);
	transferData(task);
	leaveSlot(task);
}

/* task tries to get slot on the bus subsystem */
void getSlot(task_t task) 
{	
	if(task.priority == NORMAL)
		{if(task.direction == SENDER)
			{if(VERBOSE)
				msg("Thread #%d ARRIVED for LOW-priority SEND", thread_current()->tid);}
		else
			{if(VERBOSE)
				msg("Thread #%d ARRIVED for LOW-priority RECEIVE", thread_current()->tid);}}
	else
		{if(task.direction == SENDER)
			{if(VERBOSE)
				msg("Thread #%d ARRIVED for HIGH-priority SEND", thread_current()->tid);
			sema_up(&hiPriSendTask);}
		else
			{if(VERBOSE)
				msg("Thread #%d ARRIVED for HIGH-priority RECEIVE", thread_current()->tid);
			sema_up(&hiPriRecvTask);}}
	
	//Save the number of high priority tasks waiting for the other direction on the bus
	struct semaphore *opposing_hiPriTask;
	struct semaphore *sameDir_hiPriTask;
	if(task.direction == SENDER)
	{
		opposing_hiPriTask = &hiPriRecvTask;
		sameDir_hiPriTask = &hiPriSendTask;
	}
	else
	{
		opposing_hiPriTask = &hiPriSendTask;
		sameDir_hiPriTask = &hiPriRecvTask;
	}
	
	// Loop (and yield) until we find a thread able to perform it's task (pass the if-statement)
	while(true)
	{
		// Aqcuire the MUTEX lock
		lock_acquire(&mutex);
		
		// Only take a slot on the bus iff (the bus is empty OR the current direction is equal to ours) AND 
		//	(we are a high priority task OR there is no high priority task currently waiting from the other direction or in our direction)
		if((busCapacity.value == BUS_CAPACITY || currentBusDirection == task.direction) && 
				(task.priority == HIGH || opposing_hiPriTask->value + sameDir_hiPriTask->value == 0))
		{
			if(task.priority == NORMAL)
				{if(task.direction == SENDER)
					{if(VERBOSE)
						msg("Thread #%d GETS A SLOT for LOW-priority SEND", thread_current()->tid);}
				else
					{if(VERBOSE)
						msg("Thread #%d GETS A SLOT for LOW-priority RECEIVE", thread_current()->tid);}}
			else
				{if(task.direction == SENDER)
					{if(VERBOSE)
						msg("Thread #%d GETS A SLOT for HIGH-priority SEND", thread_current()->tid);}
				else
					{if(VERBOSE)
						msg("Thread #%d GETS A SLOT for HIGH-priority RECEIVE", thread_current()->tid);}}
			
			// Change the current bus direction to our own
			currentBusDirection = task.direction;
			
			// Take one from the current bus capacity
			sema_down(&busCapacity);
			
			if(task.priority == HIGH)
			{
				if(task.direction == SENDER)
				{
					sema_down(&hiPriSendTask);
				}
				else
				{
					sema_down(&hiPriRecvTask);
				}
			}
			
			// Release the MUTEX lock
			lock_release(&mutex);
			
			// Break out of the while(true) loop when a thread has taken a slot
			break;
		}
		
		// Release the MUTEX lock
		lock_release(&mutex);
		
		// Let another thread try to get a slot
		thread_yield();
	}
}

/* task processes data on the bus send/receive */
void transferData(task_t task) 
{	
    if(task.direction == SENDER)
    {
    	if(task.priority == HIGH)
    		{if(VERBOSE)
				msg("THREAD #%d is now SENDING with HIGH priority", thread_tid());}
    	else
    		{if(VERBOSE)
				msg("THREAD #%d is now SENDING with LOW priority", thread_tid());}}
    else
    {
    	if(task.priority == HIGH)
			{if(VERBOSE)
				msg("THREAD #%d is now RECEIVING with HIGH priority", thread_tid());}
		else
			{if(VERBOSE)
				msg("THREAD #%d is now RECEIVING with LOW priority", thread_tid());}	
    }
    
    // Let the thread sleep for a random amount of ticks
	int64_t randomNumber = (int64_t) random_ulong() % TRANSFER_TIME;
	timer_sleep(randomNumber);
	
    if(task.direction == SENDER)
    {
    	if(task.priority == HIGH)
    		{if(VERBOSE)
				msg("THREAD #%d has now FINISHED SENDING with HIGH priority", thread_tid());}
    	else
    		{if(VERBOSE)
				msg("THREAD #%d has now FINISHED SENDING with LOW priority", thread_tid());}}
    else
    {
    	if(task.priority == HIGH)
			{if(VERBOSE)
				msg("THREAD #%d has now FINISHED RECEIVING with HIGH priority", thread_tid());}
		else
			{if(VERBOSE)
				msg("THREAD #%d has now FINISHED RECEIVING with LOW priority", thread_tid());}}
}

/* task releases the slot */
void leaveSlot(task_t task) 
{	
	sema_up(&busCapacity);
	
	if(task.direction == SENDER)
	{
		if(VERBOSE)
			msg("Sender leaving slot, value of busCapacity semaphore is now %d", busCapacity.value);
	}
	else
	{
		if(VERBOSE)
			msg("Receiver leaving slot, value of busCapacity semaphore is now %d", busCapacity.value);
	}

	threadsFinished++;
	if(VERBOSE)
		msg("Number of threads finished == %d", threadsFinished);
	
	//Remove the thread from the list and set it's status to DYING
	thread_exit();
}