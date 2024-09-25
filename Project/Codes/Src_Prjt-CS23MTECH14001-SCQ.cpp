#include<bits/stdc++.h>
#include <sys/time.h>
#include <unistd.h>
#include <chrono>

#define ff first
#define ss second


using namespace std;
using namespace std::chrono;

mutex fileLock; 
ofstream output;

int numOfThreads, numOfOperations;

int no_of_enq_ops = 0 , no_of_deq_ops = 0;
double total_time_for_enq_ops = 0.0, total_time_for_deq_ops = 0.0, exec_time_for_enqueue_ops = 0.0, exec_time_for_dequeue_ops = 0.0;
double thrTimes = 0.0;


struct item{ 

	int cycle,index;
};

 // atomicItem is an atomic wrapper of item so that we can use item atomically
class atomicItem{
    public:
        atomic < item > c;
    atomicItem(){
        c.store({0,0});
    }
    atomicItem(int cycle,int index){
        c.store({cycle,index});
    }
    atomicItem (const atomicItem &other){
        c.store(other.c.load()); // store to write to a register , load to read the register
    }
    atomicItem &operator=(const  atomicItem &other){
        c.store(other.c.load());
    }   
};


class Queue{
	private:
	int capacity;
	atomic<int> tail, head;
	atomicItem * dataItems ;  // array to store data items of queue
	public: Queue(int flag,int capacity){
		this->capacity = capacity;
		dataItems = new atomicItem[capacity];

        // For an empty queue, tail = head = n
		tail.store(capacity,memory_order_relaxed); 
		head.store(capacity,memory_order_relaxed);

		if(flag == 1){ 

			for(int i=0;i<capacity;i++){
				this->enqueue(i);			
			}
		}
	}

	timeval enqueue(int index){

		item data,newData;

		int j,t;

		do{
			rerun:
			t = tail.load(); 
			j = (t) % capacity;
			data = dataItems[j].c.load(); 

			// Check if cycle of tail is same as cycle of the item
			if(data.cycle == (t/capacity)){ 
				tail.compare_exchange_strong(t,t+1);
				goto rerun;
			}

			if((data.cycle + 1) != (t/capacity))
				goto rerun;
			
			newData = {t/capacity , index}; // cycle of item is same as cycle of tail
		} while(!dataItems[j].c.compare_exchange_strong(data,newData)); // Update dataItems[j] with new value if condition true
		struct timeval LP;

		// Time Point where CAS is linearized
		gettimeofday(&LP,NULL); 
		tail.compare_exchange_strong(t,t+1); 
		return LP;
	}


	pair<int,timeval> dequeue(){

		item data;
		
		int h,j;
		
		// If cycle of head and item are same, then dequeue possible
		do{
			rerun:
			h = head.load();

			j = h % capacity;

			data = dataItems[j].c.load();

			if((data.cycle) != (h/capacity)){

				if ((data.cycle + 1) == (h/capacity) )

					return {-1,timeval()};

				goto rerun;
			}

		}  while(!head.compare_exchange_strong(h,h+1));

		struct timeval LP;

		// Time Point where CAS is linearized
		gettimeofday(&LP,NULL);

		return {data.index,LP}; // Return data value and linearization time point
	}
};


// This queue uses indirection, ie, indices are not stored directly into the array entries
class CircularQueue {

    int* data;

    // fq - It keeps the indices to unallocated dataItems of the data array.
    // aq - This queue keeps allocated indices which are to be consumed. 
    Queue *aq, *fq; 

	int capacity;

    public:
    CircularQueue(int capacity){
		this->capacity = capacity;
		data = new int[capacity];
		aq = new Queue(0,capacity);
		fq = new Queue(1,capacity);
    }

	pair<int,timeval> enqueue_data(int val){
		pair<int,timeval> enq = fq->dequeue(); // first dequeue from fq
		if(enq.first == -1) return {0,timeval()}; 
		data[enq.first] = val ; 
		timeval time_for_enq = aq->enqueue(enq.first);  // push the index to aq for consumer
		return {1,time_for_enq};
	}

	pair<int,timeval> dequeue_data(){
		pair<int,timeval> deq = aq->dequeue(); // first dequeue from aq
		if(deq.first == -1) return {-1,timeval()}; 
		int val = data[deq.first];  
		timeval time_for_enq= fq->enqueue(deq.first); // push the index to fq for producer
		return {val,time_for_enq};
	}
};

struct threadData{ 
	CircularQueue* circularQueue;
    int id;
};	

class Formatter { 
    public:
    static string get_formatted_time(time_t t1)
    {
        struct tm* t2=localtime(&t1);
        char buffer[20];
        sprintf(buffer,"%d : %d : %d",t2->tm_hour,t2->tm_min,t2->tm_sec);
        return buffer;
    }
};


void computeStats () {

double  avg_time_for_enq = exec_time_for_enqueue_ops / no_of_enq_ops; 
double avg_time_for_deq = exec_time_for_dequeue_ops / no_of_deq_ops;

double total_time = thrTimes;
double total_average_time = total_time / (numOfThreads*numOfOperations);

double throughput = (1/total_average_time) * 1000000;


cout << "Avg Enq time : " << avg_time_for_enq << endl;
cout << "Avg Deq time : " << avg_time_for_deq<< endl;
cout << "Total time : " << total_time << endl;
cout << "Total average time : " << total_average_time << endl;
cout << "Throughput : " << throughput  << endl;

}

std::default_random_engine eng;
std::default_random_engine generator;

void* testCS(void* args){
	uniform_int_distribution<int> distribution(0, 1);
	struct threadData* tdata=(struct threadData*)(args);
    int id = tdata->id; // extract data from passed arguments
	CircularQueue* circularQueue = tdata->circularQueue;
	std::exponential_distribution<double>exponential2(1.0);
	for(int i=0;i<numOfOperations;i++)
	{
		int choice = distribution(generator);
		struct timeval t_req,t_enter,t_exit,t_exitReq;
		int val = rand() % 100000000 ;	
		
	
		time_t Request_time=time(NULL);
		string formatted_time=Formatter::get_formatted_time(Request_time);       
		if(choice ==0 ){
			string FinalString1 = "Thread"+to_string(id+1) + " reqested enqueue of"+ to_string(val) + " for the "+ to_string(i) +  "th time on "+ to_string(t_req.tv_sec) + " "+ to_string(t_req.tv_usec) + "\n";
            fileLock.lock();
			output<<FinalString1;
            fileLock.unlock();
            auto startTime = high_resolution_clock::now();
			pair<int,timeval> successfull = circularQueue->enqueue_data(val);
			auto endTime = high_resolution_clock::now();
			auto execution_time = duration_cast<microseconds>(endTime - startTime) ;
		// Storing execution time of current operation
		thrTimes += execution_time.count();
		exec_time_for_enqueue_ops += execution_time.count();  // Saving execution times for all enqueue operations
		no_of_enq_ops++;
			string FinalString2 = "Thread"+to_string(id+1) + " successfully enqueued "+ to_string(val) + " for the "+ to_string(i) +  "th time on "+ to_string(successfull.second.tv_sec) + " "+ to_string(successfull.second.tv_usec) + "\n";
			if(successfull.first == 1){
				fileLock.lock();
				output<<FinalString2;
                fileLock.unlock();
			}
		}

		else{
			string FinalString1 = "Thread"+to_string(id+1) + " reqested dequeue for the "+ to_string(i) +  "th time on "+ to_string(t_req.tv_sec) + " "+ to_string(t_req.tv_usec) + "\n";
			fileLock.lock();
            output<<FinalString1;   
            fileLock.unlock();
            auto startTime = high_resolution_clock::now();

			pair<int,timeval> val = circularQueue->dequeue_data();

			auto endTime = high_resolution_clock::now();

					auto execution_time = duration_cast<microseconds>(endTime - startTime) ;
		// Storing execution time of current operation
		thrTimes += execution_time.count();

		exec_time_for_dequeue_ops += execution_time.count();  // Saving execution times for all dequeue operations

		no_of_deq_ops++;

			string FinalString = "Thread"+to_string(id+1) + " successfully dequeued "+ to_string(val.first) + " for the "+ to_string(i) + "th time on "+ to_string(val.second.tv_sec) + " "+ to_string(val.second.tv_usec) + "\n";
			if(val.first != -1){
				fileLock.lock();
				output<<FinalString;
                fileLock.unlock();
			}			
		}
	}

	return 0;
}


int main(){
	srand(time(NULL));
 
    ifstream input("inp-params.txt"); // take input from inp-params.txt
    output.open("output_logs_for_SCQ.txt");
    input>>numOfThreads>>numOfOperations;
	pthread_t tid[numOfThreads+1];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
	CircularQueue* circularQueue = new CircularQueue(10000000);
    for(int i=0;i<numOfThreads;i++) // create n threads
    {
        struct threadData* tdata = new threadData{circularQueue,i};            
      	pthread_create(&tid[i],&attr,testCS,(void*)(tdata));		
    }

    for(int i=0;i<numOfThreads;i++) // join threads
    {
        pthread_join(tid[i],NULL);
    }

    computeStats();

	return 0;   
    
}
