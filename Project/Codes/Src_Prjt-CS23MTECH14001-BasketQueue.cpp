#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <mutex>
#include <ctime>
#include <math.h>
#include <unistd.h>
#include <random>
#include <vector>
#include <algorithm>
#include <atomic>
#include <map>
#include <sstream>

using namespace std;
using namespace std::chrono;

int n_enqueues = 1;
int n_dequeues = 1;
double total_time_for_enq_ops = 0.0, total_time_for_deq_ops = 0.0, exec_time_for_enqueue_ops = 0.0, exec_time_for_dequeue_ops = 0.0;
double thrTimes = 0.0;

mutex fileLock; // Required for output log file
mutex output_file_lock;

// numOfThreads denote number of threads
// numOfOperations denote number of enqueue and dequeue operations per thread
int numOfThreads, numOfOperations;

map<double, string> events;


struct node;
struct reference;

struct reference {
    
    node* ptr; // Refers to the node being pointed to

    bool marked;  // If marked == True, it means node is marked for logical deletion
    
    int tag;  // Helps us in solving the ABA problem

    reference(node* node_p, bool marked_n, int tag_n) noexcept: ptr(node_p), marked(marked_n), tag(tag_n) {}

    reference() noexcept: ptr(NULL) {}

    
    bool operator == (const reference& other) {
        return (this->ptr == other.ptr && this->marked == other.marked && this->tag == other.tag);
    }

    bool operator != (const reference& other) {
        return !(*this == other);
    }

};


struct node {

    int data;   // Contains data to be stored in the node

    atomic<reference> next;  // Store reference to next node

    node(int value, reference p) noexcept: data(value), next(p) {}

    node() noexcept {}

    node& operator = (const node& other) {
        data = other.data;
        next.store(other.next.load());
        return *this;
    }

    bool operator == (const node& other) {
        this->data == other.data && this->next.load() == other.next.load();
    }
};



class BasketQueue {
public:

    // Refers to the maximum number of logically deleted nodes allowed to be 
    // present in queue before they are physically deleted
    const int max_chain_length = 3; 

    atomic<reference> head;

    atomic<reference> tail;


    BasketQueue() {
        
        node* qnode = new node();

        qnode->next.store({NULL, false, 0});

        tail.store({qnode, false, 0});

        head.store({qnode, false, 0});
    }

    ~BasketQueue() {

        // deallocate memory for all the nodes present in queue 
        if (this->head.load() != this->tail.load())
            cleanup_nodes(this->head.load(), this->tail.load());
    }

    // deletes all nodes starting from head to qnode
    void cleanup_nodes(reference head, reference qnode) {
        if(this->head.compare_exchange_strong(head, reference(qnode.ptr, 0, head.tag+1))) {
            reference next;
            while(head.ptr != qnode.ptr) {
                next = head.ptr->next.load();
                delete head.ptr;
                head = next;
            }
        }
    }

    // Enqueue given item into queue
    bool enqueue(int item) {
        
        node* qnode = new node();  // This is the new node to be inserted
        
        qnode->data = item;
        
        while(true) {
            
            reference tail_ref = this->tail.load();  // Fetching tail's value locally
            
            reference next = tail_ref.ptr->next.load(); // Fetching next of tail

            // check if queue's tail and local tail are pointing to same data
            if(tail_ref == this->tail.load()) {
                if(next.ptr == NULL) {
                    qnode->next.store({NULL, 0, tail_ref.tag+2});

                    // first thread in the basket
                    if(tail_ref.ptr->next.compare_exchange_strong(next, reference(qnode, 0, tail_ref.tag+1))) {
                        this->tail.compare_exchange_strong(tail_ref, reference(qnode, 0, tail_ref.tag+1));
                        return true;
                    }
                    
                    // Remaining threads belonging to the same basket that failed the CAS operation
                    next = tail_ref.ptr->next.load();
                    while((next.tag == tail_ref.tag+1) and !(next.marked)) {
                        qnode->next = next;
                        if(tail_ref.ptr->next.compare_exchange_strong(next, reference(qnode, 0, tail_ref.tag+1))) {
                            return true;
                        }
                        next = tail_ref.ptr->next.load();
                    }
                } else {

                    // Updating tail to point to queue's last node
                    while((next.ptr->next.load().ptr != NULL) and (this->tail.load() == tail_ref)) {
                        next = next.ptr->next.load();
                    }
                    this->tail.compare_exchange_strong(tail_ref, reference(next.ptr, 0, tail_ref.tag+1));
                }
            }
        }
    }


    // Dequeue an Item from queue
    int dequeue() {

        while(true) {

            reference head_ref = this->head.load();  // Fetching head's value locally
            reference tail_ref = this->tail.load();  // Fetching tail's value locally
            reference next = head_ref.ptr->next.load();  // Fetching next of head

            if(head_ref == this->head.load()) {

                if(head_ref.ptr == tail_ref.ptr) {
                    if(next.ptr == NULL) 
                        return -1;  // Means Queue is Empty

                    // Updating tail to point to queue's last node
                    while((next.ptr->next.load().ptr != NULL) and (this->tail.load() == tail_ref)) {
                        next = next.ptr->next.load();
                    }
                    this->tail.compare_exchange_strong(tail_ref, reference(next.ptr, 0, tail_ref.tag+1));
                }

                else {

                    int num_of_chain_nodes = 0;

                    reference last_deleted = head_ref;  // points to last logically deleted node
    
                    // next points to first reachable(unmarked) node 

                    // We need to skip the marked nodes (i.e. which are logically deleted)
                    while((next.marked and last_deleted.ptr != tail_ref.ptr) and this->head.load() == head_ref) {
                        last_deleted = next;
                        next = last_deleted.ptr->next.load();
                        num_of_chain_nodes++;
                    }
                    
                    if(this->head.load() != head_ref) {

                        continue; // If head has any changes then continue

                    } else if(last_deleted.ptr == tail_ref.ptr) {

                        cleanup_nodes(head_ref, last_deleted);
                    } else {

                        int data = next.ptr->data;

                        // Delete the node pointed by next and change the next value of 'last_deleted'
                        if(last_deleted.ptr->next.compare_exchange_strong(next, reference(next.ptr, 1, next.tag+1))) {

                            // If num_of_chain_nodes more than max_chain_length, it means nodes are logically deleted
                            if(num_of_chain_nodes >= max_chain_length) {
                                cleanup_nodes(head_ref, next);  // Hence, physically delete the marked nodes now
                            }
                            return data;
                        } 
                    }
                }
            }
        }
    }

};

default_random_engine generator;

ofstream output_file;


void computeStats () {

double  avg_time_for_enq = exec_time_for_enqueue_ops / n_enqueues; 
double avg_time_for_deq = exec_time_for_dequeue_ops / n_dequeues;

double total_time = thrTimes;
double total_average_time = total_time / (numOfThreads*numOfOperations);

double throughput = (1/total_average_time) * 1000000;


cout << "Avg Enq time : " << avg_time_for_enq << endl;
cout << "Avg Deq time : " << avg_time_for_deq<< endl;
cout << "Total time : " << total_time << endl;
cout << "Total average time : " << total_average_time << endl;
cout << "Throughput : " << throughput  << endl;

}

// Testing threads and logging events to output file
void testCS(int thread_id, BasketQueue* basket_queue) {

    map<double, string> local_events; // Mapping time of event with output logs

    // choosing enqueue or dequeue operation
    uniform_int_distribution<int> distribution(0, 1);

    int dequeued_number;
    
    // Dividing operations as 50% enqueuers and 50% dequeuers 

    for(int i=0;i<numOfOperations;i++) {

        int p = distribution(generator);

        int rand_number = rand() % 100000000;

        // local log outputs string stream
        stringstream local_output_entry;
        stringstream local_output_exit;

        
        auto clock_curr = chrono::high_resolution_clock::now();
        time_t request_entry_time_t = time(0);
        tm* request_entry_time = localtime(&request_entry_time_t);
        
        
        if(p == 0) {
            fileLock.lock();
                output_file<<"Thread"<<thread_id<<"'s "<<n_enqueues<<"th enqueue request at "<<
                request_entry_time->tm_hour<<":"<<request_entry_time->tm_min<<":"<<request_entry_time->tm_sec<<"\n";
            fileLock.unlock();
            auto startTime = high_resolution_clock::now();  
            basket_queue->enqueue(rand_number);
            auto endTime = high_resolution_clock::now();
            auto execution_time = duration_cast<microseconds>(endTime - startTime) ;
        // Storing execution time of current operation
        thrTimes += execution_time.count();
        exec_time_for_enqueue_ops += execution_time.count();  // Saving execution times for all enqueue operations
        
        } else {
            fileLock.lock();
                output_file<<"Thread"<<thread_id<<"'s "<<n_dequeues<<"th dequeue request at "<<
                request_entry_time->tm_hour<<":"<<request_entry_time->tm_min<<":"<<request_entry_time->tm_sec<<"\n";
            fileLock.unlock();           
            auto startTime = high_resolution_clock::now();   
            dequeued_number = basket_queue->dequeue();

            auto endTime = high_resolution_clock::now();

            auto execution_time = duration_cast<microseconds>(endTime - startTime) ;
            // Storing execution time of current operation
            thrTimes += execution_time.count();
        

        exec_time_for_dequeue_ops += execution_time.count();  // Saving execution times for all dequeue operations
        
        }

        // recording time of completion of enqueue or dequeue operation
        auto clock_exit = chrono::high_resolution_clock::now();
        time_t exit_time_t = time(0);
        tm* exit_time = localtime(&exit_time_t);

        // storing log output in local buffer
        if(p == 0) {
            fileLock.lock();
                output_file<<"Thread"<<thread_id<<"'s "<<n_enqueues<<"th enqueue of "<<rand_number<<" completed at "<<
                exit_time->tm_hour<<":"<<exit_time->tm_min<<":"<<exit_time->tm_sec<<"\n";
            fileLock.unlock();
            n_enqueues++;
        } else {
            if(dequeued_number == -1){
                fileLock.lock();
                    output_file<<"Thread"<<thread_id<<"'s "<<n_dequeues<<"th dequeue reporting empty list completed at "<<
                    exit_time->tm_hour<<":"<<exit_time->tm_min<<":"<<exit_time->tm_sec<<"\n";
                fileLock.unlock();
            }
            else {
                fileLock.lock();
                    output_file<<"Thread"<<thread_id<<"'s "<<n_dequeues<<"th dequeue of "<<
                    dequeued_number<<" completed at "<<exit_time->tm_hour<<":"<<exit_time->tm_min<<":"<<exit_time->tm_sec<<"\n";
                fileLock.unlock();
            }
            n_dequeues++;
        }

    }    
}



int main() {

    // initialize random seed
    srand (time(NULL));

    ifstream input_file;
    input_file.open("inp-params.txt");

    output_file.open("output_logs_basket_queue.txt");

    // taking parameters from input file
    input_file>>numOfThreads>>numOfOperations;
    

    
    thread thread_arr[numOfThreads];

    // Initilaise Basket Queue Object
    BasketQueue* basket_queue = new BasketQueue();

    // start time required for sorting the write events for logfile
    chrono::high_resolution_clock::time_point start = chrono::high_resolution_clock::now();

    // Creating and Executing threads
    for(int i=0;i<numOfThreads;i++) {
        thread_arr[i] = thread(testCS, i, basket_queue);
    }

    // waiting for all threads to join
    for(int i=0;i<numOfThreads;i++)
        thread_arr[i].join();

    // Log entries are sorted by their time of entry

    basket_queue->cleanup_nodes(basket_queue->head.load(), basket_queue->tail.load());

    // closing all the files
    input_file.close();
    output_file.close();

    computeStats();

    return 0;
}