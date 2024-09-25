How to run the programs using terminal?

Set the path to where the program file is 

To compile use the command :
g++ <filename>.cpp -lpthread -latomic

To run use the command :
./a.out
 

// Output log files will be generated for respective queue implementations. 
// Alongwith that the output on terminal, can be found statistics of computations like Throughput and Time taken for different operations which are printed by computeStats() function 

Note : 

1. The above command is for Kali-Linux, as that is the environment we have used for our experiments. The above commands can vary for other platforms. 

2. For Basket Queue implementation we need to use '-latomic' alongwith running the 'g++' command, irrespective of the OS/Platform as it requires 128-bit atomic operations. 

3. For starters, "inp-params.txt" contains number of threads as 10 and number of operations as 100000. This file can be used for running multiple cases and iterations for the queue implementations. 

4. We are taking seperate readings for :

    a. Circular Queue vs Basket queue,  where the number of threads are varying in range 10 to 140.
    b. Wait free queue vs Cicular queue vs Basket queue, where the number of threads are varying in range 2 to 18. 
