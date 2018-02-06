#include <unistd.h>     // fork(), execvp()
#include <pthread.h>    // pthread library
#include <stdio.h>      // printf(), scanf(), setbuf(), perror()
#include <stdlib.h>     // malloc()
#include <errno.h>      // errno
#include <string.h>     // strtok()
#include <ctype.h>      // isdigit
#include <sched.h>      // set pthread priority
#include <time.h>

#define QUEUES 4


/*---- structs & typedefs -----*/

//customer information
typedef struct Customer{
    int id;
    int service_time;
    int arrival_time;
}Customer;


/*---- global variables -----*/
// use this variable to record the simulation start time;
// No need to use mutex_lock when reading this variable since the value would not be changed by thread once the initial time was set.
// Initilize it in main thread
struct timeval init_time;
double init_secs;
//A global variable to add up the overall waiting time for all customers,
//every customer add their own waiting time to this variable, mutex_lock is necessary.
double overall_waiting_time;
// variable stores the real-time queue length information; mutex_lock needed
int queue_length[QUEUES];
//A clerk set it to its ID, when it signalling a customer. The customer called set it to -1, allowing other clerk and customers to set it.
int clerk_flag;

//mutex and convar for each queue and clerk
pthread_mutex_t queue_mutex[QUEUES];
pthread_cond_t queue_cond_var[QUEUES];
pthread_mutex_t clerk_mutex[2];
pthread_cond_t clerk_cond_var[2];
//a mutex for operations on length of queues
pthread_mutex_t qlength_mutex;
//a mutex for setting clerk_flag
//pthread_mutex_t c_flag_mutex;



/* ---- helper functions ----*/

//get the time difference of current time and initial time
double get_time_difference() {
    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    double cur_secs = (current_time.tv_sec + (double) current_time.tv_usec / 1000000);
    return (cur_secs - init_secs);

}

/* ---- thread routines ----*/

/* customer thread routine:
   Pick the shortest queue to enter, then increment the queue length by 1.
   Wait until a clerk signals it.
   Sleep for the service time it needs.
   Signal the clerk when it finishes its service.
*/
void* customer_entry(void* cusInfo) {

    struct Customer * cus_info = (struct Customer *)cusInfo;


    usleep(cus_info->arrival_time*100000);  //sleep until arrival time

    printf("A customer arrives: customer ID %2d.\n", cus_info->id);

    //pick the shortest queue to enter. Pick a random queue if same length
    int i;
    int shortest_index = 0;
    for (i = 1; i < QUEUES; ++i)
    {
        if (queue_length[i] < queue_length[i-1]){
            shortest_index = i;
        }
    }
    int q = shortest_index;
    //entering the queue
    pthread_mutex_lock(&qlength_mutex);
    queue_length[q]++;  //update queue length
    printf("A customer enters a queue: customer ID: %d the queue ID %1d, and length of the queue %2d.\n", cus_info->id, shortest_index+1, queue_length[q]);
    pthread_mutex_unlock(&qlength_mutex);


    pthread_mutex_lock(&queue_mutex[q]);


    double begin = get_time_difference();

    pthread_cond_wait(&queue_cond_var[q], &queue_mutex[q]);

    int clerkID = clerk_flag; //find which clerk awoke me

    clerk_flag = -1;


    double end = get_time_difference();
    double waiting_time = end-begin;

    overall_waiting_time += waiting_time;

    pthread_mutex_unlock(&queue_mutex[q]);

    printf("A clerk starts serving a customer: start time %.2f, the customer ID %2d, the clerk ID %1d. \n", get_time_difference(), cus_info->id, clerkID);

    usleep(cus_info->service_time*100000); //sleep when being served

    printf("A clerk finishes serving a customer: end time %.2f, the customer ID %2d, the clerk ID %1d.\n", get_time_difference(), cus_info->id, clerkID);

    pthread_mutex_lock(&clerk_mutex[clerkID]);

    pthread_cond_signal(&clerk_cond_var[clerkID]);  // tell the clerk that service is finished, it can serve another customer

    pthread_mutex_unlock(&clerk_mutex[clerkID]);

    pthread_exit(NULL);

    return NULL;
}

/* the clerk thread routine:
   Stays idle until any customer enters a queue.
   Pick the queue of the longest length, and signal the first customer in the queue.
   Wait until the customer finishes service.
*/

void* clerk_entry(void *clerk_id) {

    int clerkID = *(int *)clerk_id;

    while(1) {

        int longest_index = 0;
        int i;
        int flag = 0;

        int q = 0;

        //checking queue length and pick a queue
        pthread_mutex_lock(&qlength_mutex);

        for (i = 1; i < QUEUES; ++i) {
            if (queue_length[i] > queue_length[i-1]) {
                longest_index = i;
            }
        }

        q = longest_index;

        if (queue_length[q] != 0)
        {

            queue_length[q]--;
            //pick a queue, set flag, start serving
            flag = 1;
        }


        pthread_mutex_unlock(&qlength_mutex);

        if (flag == 0)
        {
            usleep(100);
            continue;
        }

        //check if another clerk is signaling by checking if the clerk_flag is -1 (not signalling).
        while(clerk_flag != -1) {
            usleep(100);
        }


        //signal the first customer in the selected queue
        pthread_mutex_lock(&queue_mutex[q]);
        clerk_flag = clerkID;  //set flag to my ID
        pthread_cond_signal(&queue_cond_var[q]);

        pthread_mutex_unlock(&queue_mutex[q]);

        //wait for the customer to finish service
        pthread_mutex_lock(&clerk_mutex[clerkID]);
        pthread_cond_wait(&clerk_cond_var[clerkID],&clerk_mutex[clerkID]);
        pthread_mutex_unlock(&clerk_mutex[clerkID]);
    }

    return NULL;
}

int main(int args, char* argv[]) {
    //Open file
    FILE * fp;
    if(args <= 1)
    {
        printf("Usage: ACS [filename]\n");
        return -1;
    }

    fp = fopen(argv[1], "r");

    if (fp == NULL)
    {
        printf("Failed to open file\n");
        return -1;
    }

    //read in file
    int total_customers;
    int id, arr_time, serv_time;
    fscanf(fp, "%d\n", &total_customers);
    int actual_customers;
    Customer* customers = malloc(sizeof(Customer)*total_customers);
    int i,j;
    for (i = 0; i < total_customers; ++i) //format: 1:2, 60
    {
        fscanf(fp, "%d:%d,%d\n", &id, &arr_time, &serv_time);
        if (id <= 0 || arr_time <= 0 ||serv_time <= 0)
        {
            printf("Negative value. One Customer negalected: Customer ID: %d.\n",id);

            continue;
        }
        customers[j].id = id;
        customers[j].arrival_time = arr_time;
        customers[j].service_time = serv_time;


        j++;
    }

    actual_customers = j;
    //printf("Total customers read in: %d\n", actual_customers);
    //initialize shared data
    for (i = 0; i < QUEUES; ++i){
        queue_length[i] = 0;
    }

    clerk_flag = -1;
    pthread_mutex_init(&qlength_mutex,NULL);


    gettimeofday(&init_time, NULL);
    init_secs = (init_time.tv_sec + (double) init_time.tv_usec / 1000000);


    //create customer threads
    pthread_t *customer_threads = (pthread_t *)malloc(sizeof(pthread_t)*actual_customers);
    for (i = 0; i < actual_customers; ++i)
    {
        //init mutex and convar for each customer
        pthread_mutex_init(&queue_mutex[i], NULL);
        pthread_cond_init(&queue_cond_var[i], NULL);

        if (pthread_create(&customer_threads[i], NULL, customer_entry, (void*)&customers[i])!= 0)
        {
            printf("Unable to create thread.\n");
            return -1;
        }
    }

    //create 2 clerk threads
    pthread_t *clerk_threads = (pthread_t *)malloc(sizeof(pthread_t)*2);
    for (i = 0; i < 2; ++i)
    {
        pthread_mutex_init(&clerk_mutex[i], NULL);
        pthread_cond_init(&clerk_cond_var[i], NULL);

        if (pthread_create(&clerk_threads[i], NULL, clerk_entry, (void*)&i) != 0)
        {
            printf("Unable to create thread.\n");
            return -1;
        }
    }

    //wait for customer threads to join
    for (i = 0; i < actual_customers; ++i)
    {
        pthread_join(customer_threads[i],NULL);
    }
    printf("The average waiting time for all customers in the system is: %.2f seconds.\n", overall_waiting_time/actual_customers);

    return 0;
}
