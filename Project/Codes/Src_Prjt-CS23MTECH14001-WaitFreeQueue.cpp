#include <iostream>

#include <atomic>

#include <thread>

#include <cstring>

#include <cstdlib>

#include <cstdio>

#include <fstream>

#include <sched.h>


#include <chrono>

#include <mutex>

#include <ctime>

#include <math.h>

#include <unistd.h>

#include <random>

#include <vector>

#include <algorithm>

#include <map>

#include <sstream>

#include <algorithm>



using namespace std;



using namespace std::chrono;







int n_enqueues = 0;

int n_dequeues = 0;

double total_time_for_enq_ops = 0.0, total_time_for_deq_ops = 0.0, exec_time_for_enqueue_ops = 0.0, exec_time_for_dequeue_ops = 0.0;

double thrTimes = 0.0;



// numOfThreads denote number of threads

// numOfOperations denote number of enqueue and dequeue operations per thread

int numOfThreads, numOfOperations;





#define WFQUEUE_NODE_SIZE 1022

#define CACHE_LINE_SIZE 64

#define CACHE_ALIGNED alignas(CACHE_LINE_SIZE)

#define DOUBLE_CACHE_ALIGNED alignas(2 * CACHE_LINE_SIZE)

#define PAUSE() asm volatile("nop" ::: "memory") sched_yield()

#define BOT nullptr

#define TOP reinterpret_cast<EnqT*>(1)

#define MAX_PATIENCE 10

using namespace std;



template <typename T>



bool CAS(std::atomic<T>* addr, T* expected, T desired) {

 return std::atomic_compare_exchange_weak(addr, expected, desired);



}





template <typename T>



T FAA(std::atomic<T>* addr, T value) {

 return std::atomic_fetch_add(addr, value);



}



template <typename T>



T ACQUIRE(T* addr) {

  return std::atomic_load(addr);

}



template <typename T>



void RELEASE(std::atomic<T>* addr, T value) {

 std::atomic_store(addr, value);



}



template <typename T, typename U>



bool CAScs(std::atomic<T>* addr, T* expected, U desired) {



    T old_val = ACQUIRE(addr);



//cout<<"Address acquired : "<<old_val<<endl;



//cout<<"before loop"<<endl;







    if(old_val == *expected || !CAS(addr, &old_val, reinterpret_cast<T>(desired))) {



        //cout<<"inside while"<<endl;







        *expected = old_val;







        old_val = ACQUIRE(addr);







    }







//cout<<"Here..."<<old_val<<endl;







    return old_val;







}































template <typename T, typename U>







bool CASra(std::atomic<T>* addr, T* expected, U desired) {







    T old_val = ACQUIRE(addr);







    while (old_val == *expected || !CAS(addr, &old_val, static_cast<T>(desired))) {







        *expected = old_val;







        old_val = ACQUIRE(addr);







    }







    return old_val;







}























class Align {







public:







    static void* align_malloc(size_t align, size_t size) {







        void* ptr;







        int ret = posix_memalign(&ptr, align, size);







        if (ret != 0) {







            fprintf(stderr, "%s\n", strerror(ret));















            abort();







        }







        return ptr;







    }







};















class EnqT {







public:







    std::atomic<long> id;







    std::atomic<void*> val;







};















class DeqT {







public:







    std::atomic<long> id;







    std::atomic<long> idx;







};















class CellT {







public:







    std::atomic<void*> val;







    std::atomic<EnqT*> enq;







    std::atomic<DeqT*> deq;







    //void* pad[5];







};















class NodeT {







public:







    std::atomic<NodeT*> next;







    std::atomic<long> id;







    CellT cells[WFQUEUE_NODE_SIZE];







};















class QueueT {







public:







    std::atomic<long> Ei; //Index of the next position for enqueue.







    std::atomic<long> Di; //Index of the next position for dequeue.







    std::atomic<long> Hi; //Index of the head of the queue.







    std::atomic<NodeT*> Hp; //Pointer to the head node of the queue.







    long nprocs;







};















class HandleT {







public:







    HandleT* next;







    std::atomic<long unsigned int> deq_node_id;







    std::atomic<NodeT*> Ep;







    unsigned long enq_node_id;







    std::atomic<NodeT*> Dp;







    //unsigned long deq_node_id;







    EnqT Er;







    DeqT Dr;







    HandleT* Eh;







    long Ei;







    HandleT* Dh;







    NodeT* spare;



    







};















void* spin(std::atomic<void*>* p) {







    int patience = 100;







    void* v = p->load();















    while (!v && patience-- > 0) {







        v = p->load();







        PAUSE();







    }















    return v;







}















NodeT* new_node() {







    NodeT* n = static_cast<NodeT*>(Align::align_malloc(4096, sizeof(NodeT)));







    std::memset(n, 0, sizeof(NodeT));







    return n;







}































CellT* find_cell(std::atomic<NodeT*> *ptr, long i, HandleT* th) {



    //cout<<"inside find cell"<<endl;



    NodeT* curr ;



    curr= *ptr;



    //cout<<"value of curr"<<curr<<endl;



    //cout<<"value of ptr"<<ptr<<endl;



    //cout<<"value of th->ep inside findcell"<<th<<endl;



    //cout<<"curr.id  value"<<curr->id<<endl;



    



    long j;



    for (j = curr->id; j < i /WFQUEUE_NODE_SIZE; ++j) {



    //cout<<"inside loop "<<j<<endl;



        NodeT* next = curr->next;



    //cout<<"next is "<<next<<endl;











        if (next == nullptr || next==0) {



        //cout<<"inside if (next == nullptr || next==0) line"<<endl;



            NodeT* temp = th->spare;

            //cout<<" value of temp "<<temp<<endl;



            if (!temp) {







                temp = new_node();







                th->spare = temp;







            }

            //cout<<" value of temp->id is "<<temp->id<<endl;



            temp->id= j+ 1;

        //cout<<" value of temp->id after changing is "<<temp->id<<endl;

            if (CASra(&curr->next, &next, temp)) {



        //cout<<"inside the next if........... "<<endl;



                next = temp;



                th->spare = nullptr;



            }



        }



        curr = next;

        //cout<<"Curr value now : "<<curr<<endl;



    }



    ptr->store(curr);



    //cout<<"outside find cell"<<endl;



    return &curr->cells[i % WFQUEUE_NODE_SIZE];







}







int enq_fast(QueueT* q, HandleT* th, void* v, long* id) {



    



    //cout<<"inside eq fast"<<endl;



    //cout<<"value of q inside enq_fast "<<q<<endl;



    //cout<<"value of th inside enq_fast "<<th<<endl;



    //cout<<"value of v inside enq_fast "<<v<<endl;



    //cout<<"value of id inside enq_fast "<<id<<endl;



    long i = FAA(&q->Ei, static_cast<long>(1));



    //cout<<"value of pointer "<<&th->Ep<<endl;



    



    //cout<<"value of th->ep inside enq_fast "<<&th->Ep<<endl;



    CellT* c = find_cell(&th->Ep, i, th);



    void* cv = BOT;



    if (CAS(&c->val, reinterpret_cast<void**>(&cv), v)) {







        return 1;







    } else {







        *id = i;







        return 0;







    }







}











void enq_slow(QueueT* q, HandleT* th, void* v, long id) {







    EnqT* enq = &th->Er;







    enq->val = v;







    RELEASE(&enq->id, id);



    NodeT* tail = th->Ep.load();  // Load the value from the atomic pointer







    long i;



    CellT* c;







    do {







        i = FAA(&q->Ei, static_cast<long>(1));



        c = find_cell(reinterpret_cast<std::atomic<NodeT*>*>(&tail), i, th);







        EnqT* ce = BOT;







        if (CAScs(&c->enq, &ce, &enq) && c->val != TOP) {







            if (CAS(&enq->id, &id, -i)) id = -i;







            break;







        }







    } while (enq->id > 0);







    id = -enq->id;







    c = find_cell(&th->Ep, id, th);







    if (id > i) {







        long Ei = q->Ei;







        while (Ei <= id && !CAS(&q->Ei, &Ei, id + 1))







            ;







    }







    c->val = v;







}







void enqueue(QueueT* q, HandleT* th, void* v) 



{







    long id;



    int p = 10;



    //cout<<"value of v in enqueue "<<v<<endl;



    //cout<<"value of q before enq_fast "<<q<<endl;



    //cout<<"value of th before enq_fast "<<th<<endl;



    //cout<<"value of v before enq_fast "<<v<<endl;



    //cout<<"value of address id before enq_fast "<<id<<endl;



    //cout<<"value of th->ep->id"<<th->Ep<<endl;



    while (!enq_fast(q, th, v, &id) && p-- > 0);







    if (p < 0) 



        enq_slow(q, th, v, id);    



    th->enq_node_id = th->Ep.load()->id.load(); 



     //cout<<"enqueued"<<endl;







}







//void help_deq(QueueT* q, HandleT* th, HandleT* ph);







//void help_enq(QueueT* q, HandleT* th, CellT* c, long i);







void* help_enq(QueueT* q, HandleT* th, CellT* c, long i) {



    //cout<<"inside help_enq"<<endl;







    void* v = spin(&c->val);



    //cout<<"after spin"<<endl;



    //cout<<"v value "<<v<<endl;



    



    // Changes in the following condition







    if ((v != TOP && v != BOT) || (v == BOT && !CAScs(&c->val, &v, TOP) && v != TOP)) {



    //cout<<"value inside if v"<<v<<endl;



        return v;







    }







    // Changes: Directly using load on std::atomic objects



    //cout<<"came"<<endl;



    EnqT* e = c->enq.load();



    //cout<<"value of e "<<e<<endl;



    if (e == BOT) {







        HandleT* ph;







        EnqT* pe;







        long id;







        ph = th->Eh, pe = &ph->Er, id = pe->id;







        if (th->Ei != 0 && th->Ei != id) {







            th->Ei = 0;







            th->Eh = ph->next;







            ph = th->Eh, pe = &ph->Er, id = pe->id;







        }







        if (id > 0 && id <= i && !CAS(&c->enq, &e, pe) && e != pe)







            th->Ei = id;







        else {







            th->Ei = 0;







            th->Eh = ph->next;







        }







        // Changes: Directly using load on std::atomic object







        if (e == BOT && CAS(reinterpret_cast<std::atomic<EnqT*>*>(&c->enq), reinterpret_cast<EnqT**>(&e), reinterpret_cast<EnqT*>(TOP))) e = TOP;







    }







    if (e == TOP) return (q->Ei <= i ? BOT : TOP);







    long ei = ACQUIRE(&e->id);







    void* ev = ACQUIRE(&e->val);















    if (ei > i) {







        if (c->val == TOP && q->Ei <= i) return BOT;







    } else {







        if ((ei > 0 && CAS(&e->id, &ei, -i)) || (ei == -i && c->val == TOP)) {







            long Ei = q->Ei;







            while (Ei <= i && !CAS(&q->Ei, &Ei, i + 1))







                ;







            c->val = ev;







        }







    }







    return c->val.load();  // Changes: Directly using load on std::atomic object







}







static void help_deq(QueueT* q, HandleT* th, HandleT* ph) {







    DeqT* deq = &ph->Dr;







    long idx = deq->idx.load();







    long id = deq->id.load();







    if (idx < id)







        return;







    NodeT** Dp = reinterpret_cast<NodeT**>(ph->Dp.load()); // Corrected this line







    std::atomic_thread_fence(std::memory_order_acquire);







    idx = deq->idx.load();







    long i = id + 1, old = id, new_val = 0;







    while (true) {







        NodeT* h = *Dp; // Updated this line







        for (; idx == old && new_val == 0; ++i) {







            CellT* c = find_cell(reinterpret_cast<std::atomic<NodeT*>*>(Dp), i, th);







                long Di = q->Di.load();







                while (Di <= i && !q->Di.compare_exchange_weak(Di, i + 1));















                void* v = help_enq(q, th, c, i);







                if (v == BOT || (v != TOP && c->deq.load() == BOT))







                    new_val = i;







                else







                    idx = deq->idx.load();







        }















        if (new_val != 0) {







            if (deq->idx.compare_exchange_weak(idx, new_val))







                idx = new_val;







            if (idx >= new_val) new_val = 0;







        }















        if (idx < 0 || deq->id.load() != id) break;















        CellT* c = find_cell(reinterpret_cast<std::atomic<NodeT*>*>(Dp), idx, th);







        DeqT* cd = BOT;







        if (c->val.load() == TOP || c->deq.compare_exchange_weak(cd, deq) || cd == deq) {







            deq->idx.store(-idx);







            break;







        }















        old = idx;







        if (idx >= i) i = idx + 1;







    }







}











void* deq_fast(QueueT* q, HandleT* th, long* id) {



    long i = FAA(&q->Di, static_cast<long>(1));



    CellT* c = find_cell(&th->Dp, i, th);



    //cout<<"value of c "<<c<<endl;



    void* v = help_enq(q, th, c, i);



    //cout<<"out of help enq"<<endl;



    DeqT* cd = BOT;







    if (v == BOT)



        return BOT;



    if (v != TOP && CAS(&c->deq, &cd, reinterpret_cast<DeqT*>(TOP)))



        return v;











    *id = i;



    return TOP;



}















static void* deq_slow(QueueT* q, HandleT* th, long id) {







    DeqT* deq = &th->Dr;







    deq->id.store(id);







    deq->idx.store(id);







    help_deq(q, th, th);







    long i = -deq->idx.load();







    CellT* c = find_cell(&th->Dp, i, th);







    void* val = c->val.load();















    return val == TOP ? BOT : val;







}







void* dequeue(QueueT* q, HandleT* th) {







    void* v;







    long id = 0;







    int p = MAX_PATIENCE;



    do



        v = deq_fast(q, th, &id);







    while (v == TOP && p-- > 0);



    if (v == TOP)







        v = deq_slow(q, th, id);







    else {







        // Fast dequeue succeeded







    }



    if (v != BOT) {







        help_deq(q, th, th->Dh);







        th->Dh = th->Dh->next;







    }











    th->deq_node_id.store(th->Dp.load()->id.load(), std::memory_order_relaxed);







    if (th->spare == nullptr) {



        th->spare = new_node();







    }



    return v;



}







void workerEnq(QueueT* q, HandleT* th, int numEnq) {







    for (int i = 0; i < numEnq; ++i) {







        int value = i + 1;



    //std::cout<<"before"<<endl;



    //std::cout<<q->Ei<<endl;



    //cout<<value<<endl;



    //cout<<&value<<endl;



    //cout<<"value of q before passing"<<q<<endl;



    //cout<<"value of th before"<<th<<endl;



    //cout<<"value of th->ep before"<<th->Ep<<endl;



    //cout<<"value of value beofer"<<value<<endl;



        enqueue(q, th, &value);



    



    //std::cout<<"after"<<endl;



    //std::cout<<q->Ei<<endl;



        



        std::this_thread::sleep_for(std::chrono::milliseconds(10));  // Simulate some work







    }







}







void workerDeq(QueueT* q, HandleT* th, int id) {



    void* result = dequeue(q, th);



    //cout<<"deque"<<result;



    if (result != nullptr) {



        intptr_t value = reinterpret_cast<intptr_t>(result);



        //std::cout << "Dequeued value: " << value << " in thread " << id << std::endl;



    } else {



        //std::cout << "Dequeue failed in thread " << id << std::endl;



    }



}











#include <iostream>



#include <atomic>







static std::atomic<int> barrier{0};







void queue_init(QueueT* q, int nprocs) {



    q->Hi.store(0);



    q->Hp.store(new_node());







    q->Ei.store(1);



    q->Di.store(1);







    q->nprocs = nprocs;







#ifdef RECORD



    q->fastenq = 0;



    q->slowenq = 0;



    q->fastdeq = 0;



    q->slowdeq = 0;



    q->empty = 0;



#endif







    barrier.store(0);



}







void queue_free(QueueT* q, HandleT* h) {



#ifdef RECORD



    static std::atomic<int> lock{0};







    q->fastenq.fetch_add(h->fastenq.load());



    q->slowenq.fetch_add(h->slowenq.load());



    q->fastdeq.fetch_add(h->fastdeq.load());



    q->slowdeq.fetch_add(h->slowdeq.load());



    q->empty.fetch_add(h->empty.load());







    barrier.fetch_add(1);







    // Only one thread prints the result



    if (barrier.load() == q->nprocs - 1 && lock.fetch_add(1) == 0) {



        std::cout << "Enq: " << q->slowenq * 100.0 / (q->fastenq + q->slowenq)



                  << " Deq: " << q->slowdeq * 100.0 / (q->fastdeq + q->slowdeq)



                  << " Empty: " << q->empty * 100.0 / (q->fastdeq + q->slowdeq)



                  << std::endl;



    }



#endif



}



















void queue_register(QueueT* q, HandleT* th, int id) {



    th->next = nullptr;



    //cout<<"yaha phat raha hai kya"<<endl;



    // Use load() to obtain the value from the atomic pointer



    th->Ep = q->Hp.load();







    // Use load() to obtain the value from the atomic pointer



    th->enq_node_id = th->Ep.load()->id.load();



//cout<<"yaha "<<endl;



    // Use load() to obtain the value from the atomic pointer



    th->Dp = q->Hp.load();







    // Use load() to obtain the value from the atomic pointer



    th->deq_node_id = th->Dp.load()->id.load();







    th->Er.id.store(0);



    th->Er.val.store(BOT);



    th->Dr.id.store(0);



    th->Dr.idx.store(-1);



    th->Ei = 0;



    th->spare = new_node();



#ifdef RECORD



    th->slowenq = 0;



    th->slowdeq = 0;



    th->fastenq = 0;



    th->fastdeq = 0;



    th->empty = 0;



#endif







    static std::atomic<HandleT*> _tail;







    HandleT* tail = _tail.load();



    if (tail == nullptr) {



        th->next = th;







        // Use load() to obtain the value from the atomic pointer



        if (_tail.compare_exchange_strong(tail, th)) {



            th->Eh = th->next;



            th->Dh = th->next;



            return;



        }



    }







    HandleT* next = tail->next;







    do



        th->next = next;



    while (!_tail.compare_exchange_strong(tail, th));







    th->Eh = th->next;



    th->Dh = th->next;



}





default_random_engine generator;



ofstream output_file;



// start time used to sort the write events

chrono::high_resolution_clock::time_point start;

mutex output_file_lock;

// map containing writer thread events 

// key is time and value is long entry of type string

map<double, string> log_events;



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

void testCS(int thread_id, QueueT* myQueue, HandleT* th1, HandleT* th2) {



    map<double, string> local_events; // Mapping time of event with output logs



    // choosing enqueue or dequeue operation

    uniform_int_distribution<int> distribution(0, 1);

    

    // Dividing operations as 50% enqueuers and 50% dequeuers 



    for(int i=0;i<numOfOperations;i++) {



        int p = distribution(generator);



        // local log outputs string stream

        stringstream local_output_entry;

        stringstream local_output_exit;



        auto clock_curr = chrono::high_resolution_clock::now();

        time_t request_entry_time_t = time(0);

        tm* request_entry_time = localtime(&request_entry_time_t);

        

        if(p == 0) {

            output_file_lock.lock();

                output_file<<"Thread"<<thread_id<<"'s "<<n_enqueues<<"th enqueue request at "<<

                request_entry_time->tm_hour<<":"<<request_entry_time->tm_min<<":"<<request_entry_time->tm_sec<<"\n";

            output_file_lock.unlock();

            auto startTime = high_resolution_clock::now();  

            workerEnq(myQueue, th1, 1);

            auto endTime = high_resolution_clock::now();

            auto execution_time = duration_cast<microseconds>(endTime - startTime) ;

        // Storing execution time of current operation

        thrTimes += execution_time.count();

        exec_time_for_enqueue_ops += execution_time.count();  // Saving execution times for all enqueue operations

        n_enqueues++;

        

        } else {

            output_file_lock.lock();

            

                output_file<<"Thread"<<thread_id<<"'s "<<n_dequeues<<"th dequeue request at "<<

                request_entry_time->tm_hour<<":"<<request_entry_time->tm_min<<":"<<request_entry_time->tm_sec<<"\n";

                     output_file_lock.unlock();

            auto startTime = high_resolution_clock::now();   

            workerDeq(myQueue, th2, thread_id);



            auto endTime = high_resolution_clock::now();



            auto execution_time = duration_cast<microseconds>(endTime - startTime) ;

            // Storing execution time of current operation

            thrTimes += execution_time.count();

        

        exec_time_for_dequeue_ops += execution_time.count();  // Saving execution times for all dequeue operations

        n_dequeues++;

        }

    }    

}









int main() {





    srand (time(NULL));

    QueueT myQueue;

    //std::cout<<"created instancenof queue"<<std::endl;



    HandleT handle1, handle2;

    ifstream input_file;

    input_file.open("inp-params.txt");



    



    // taking parameters from input file

    // n_threads denote number of threads

    // n_entries denote number of times each thread perform

    // enqueue or dequeue operation

    input_file>>numOfThreads>>numOfOperations;

    cout<<numOfThreads<<" "<<numOfOperations<<endl;



    queue_init(&myQueue, numOfOperations);



     queue_register(&myQueue, &handle1, 1);



     queue_register(&myQueue, &handle2, 1);





    

    // seed for default random engine generator

    // generator.seed(4);



    // input file stream

    



    // declaring threads

    thread worker_threads[numOfThreads];



    // storing start time before all threads start

    // used to sort log events for printing

    start = chrono::high_resolution_clock::now();



    // assigning threads their task

    for(int i=0;i<numOfThreads;i++) {

        worker_threads[i] = thread(testCS, i, &myQueue, &handle1, &handle2);

    }



    // waiting for all threads to join

    for(int i=0;i<numOfThreads;i++)

        worker_threads[i].join();



    // cleanup i.e. closing all the files

    input_file.close();





    queue_free(&myQueue, &handle1);



    computeStats();



    return 0;







}