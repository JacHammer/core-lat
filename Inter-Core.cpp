// Inter-Core.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <atomic>
#include <future>
#include <mutex>
#include <vector>

// const int iterations = 100000000;  // old value
const int iterations = 100000000;
int volatile counter = -1;

#ifdef _WIN32 
#include "windows.h"
#include <intrin.h>
#elif __linux__ && __amd64__
#include <pthread.h>
#include <unistd.h>
#include <x86intrin.h>
#elif __linux__ && __arm__
#include <pthread.h>
#include <unistd.h>
#elif __APPLE__ && __aarch64__  // Apple M1
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <mach/thread_policy.h>
#include <mach/thread_info.h>
#endif

#define __use_std_timer

#pragma intrinsic(__rdtsc)

inline void set_affinity(unsigned int core) {
#ifdef _WIN32
    SetThreadIdealProcessor(GetCurrentThread(), core);
    DWORD_PTR mask = (DWORD_PTR)1 << core;
    SetThreadAffinityMask(GetCurrentThread(), mask);
    SetPriorityClass(GetCurrentProcess(), 0x00000080);
#elif __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#elif __APPLE_
#endif
}


void workThread(int core) {
    //Lock thread to specified core
    set_affinity(core);
    //Make sure core affinity gets set before continuing
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    //Loop bouncing data back and forth between cores
    while (counter != 0) {
        if (counter > 0) {
            counter = -counter + 1;
        }
    }
}

#ifdef _WIN32
// QPF and QPC works only under win32
double getTSCTicksPerNanosecond() {
    int sleep_time = 2000;
    //Calculate TSC frequency
    LARGE_INTEGER Frequency;
    QueryPerformanceFrequency(&Frequency);
    std::cout << "The native TSC frequency is: " << Frequency.QuadPart << "/s" << std::endl;
    LARGE_INTEGER tStart;
    LARGE_INTEGER tEnd;

    QueryPerformanceCounter(&tStart);
    unsigned long long start = __rdtsc();

    //Sleep for a bit
    //TODO: maybe poll freq of the core and get averaged freq during this time?
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
    //Sleep(2000);

    QueryPerformanceCounter(&tEnd);
    unsigned long long end = __rdtsc();

    LONGLONG deltaQPC = tEnd.QuadPart - tStart.QuadPart;

    unsigned long long deltaTSC = end - start;
    std::cout << "The QPC started at: " << tStart.QuadPart << " cycle" << std::endl;
    std::cout << "The QPC ended at: " << tEnd.QuadPart << " cycle" << std::endl;
    std::cout << "The delta QPC is: " << deltaQPC << " cycle" << std::endl;
    std::cout << "The delta TSC is: " << deltaTSC << " cycle" << std::endl;

    //Duration in nanoseconds
    double qpcDuration = (double)deltaQPC * 1000000000.0 / (double)Frequency.QuadPart;
    std::cout << "It took this amount of time in nanoseconds to complete " << sleep_time << "ms sleep: " << qpcDuration << std::endl;

    //Calculate TSC ticks per nanosecond
    auto ticks_per_ns = (double)deltaTSC / qpcDuration;
    std::cout << "TSC ticks per nanosecond is: " << ticks_per_ns << "/ns" << std::endl;

    return ticks_per_ns;
}


long long testSingleCore() {
    unsigned long long volatile start;
    unsigned long long volatile end;
    counter = -1;
    //Record start time
    start = __rdtsc();
    counter = counter - iterations;
    while (counter != 0) {
        if (counter < 0) {
            counter = -counter - 1;
        }
        else if (counter > 0) {
            counter = -counter + 1;
        }
    }
    //Record end time
    end = __rdtsc();
    return end - start;
}


long long measureLatency(int core) {
    unsigned long long volatile start;
    unsigned long long volatile end;
    //Enable counter
    counter = -1;
    //Start the far thread
    std::thread coreWorker(workThread, core);
    //Wait for it to start and lock affinity
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    //Record start time
    start = __rdtsc();
    //Loop bouncing data back and forth between cores
    counter = counter - iterations;
    while (counter != 0) {
        if (counter < 0) {
            counter = -counter - 1;
        }
    }
    //Record end time
    end = __rdtsc();
    //Make sure the thread exits before continuing
    coreWorker.join();
    //Return total time taken
    return end - start;
}


auto ticksPerNanosecond = getTSCTicksPerNanosecond();


void pinned_worker(int pinned_core, std::vector<std::vector<double>>& latency_matrix) {
    std::mutex stream_mutex;
    set_affinity(pinned_core);
    double time;
    time = testSingleCore();
    time = time / iterations / ticksPerNanosecond * 1.0;
    const std::lock_guard<std::mutex> lock(stream_mutex);
    std::cout << "RDTSC: from " << pinned_core << " to " << pinned_core << " : " << time << " ns" << std::endl;
    latency_matrix[pinned_core][pinned_core] = time;
}


void pinned_two_workers(int pinned_core, int that_core, std::vector<std::vector<double>>& latency_matrix) {
    std::mutex stream_mutex;
    set_affinity(pinned_core);
    double time;
    time = measureLatency(that_core);
    time = time / iterations / ticksPerNanosecond;
    const std::lock_guard<std::mutex> lock(stream_mutex);
    std::cout << "RDTSC: from " << pinned_core << " to " << that_core << " : " << time << " ns" << std::endl;
    latency_matrix[pinned_core][that_core] = time;
}

#endif


long long testSingleCoreStd() {
    counter = -1;
    //Record start time
    auto start = std::chrono::high_resolution_clock::now();

    counter = counter - iterations;
    while (counter != 0) {
        if (counter < 0) {
            counter = -counter - 1;
        }
        else if (counter > 0) {
            counter = -counter + 1;
        }
    }

    //Record end time
    auto end = std::chrono::high_resolution_clock::now();
    long long ns_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return ns_elapsed;
}


long long measureLatencyStd(int core) {
    //Enable counter
    counter = -1;
    //Start the far thread
    std::thread coreWorker(workThread, core);
    //Wait for it to start and lock affinity
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    //Record start time
    auto start = std::chrono::high_resolution_clock::now();
    //Loop bouncing data back and forth between cores
    counter = counter - iterations;
    while (counter != 0) {
        if (counter < 0) {
            counter = -counter - 1;
        }
    }
    //Record end time
    auto end = std::chrono::high_resolution_clock::now();
    //Make sure the thread exits before continuing
    coreWorker.join();
    //Return total time taken
    long long ns_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return ns_elapsed;
}


void pinned_worker_std(int pinned_core, std::vector<std::vector<double>>& latency_matrix) {
    std::mutex stream_mutex;
    set_affinity(pinned_core);
    double time;
    time = testSingleCoreStd();
    time /= iterations * 1.0;
    const std::lock_guard<std::mutex> lock(stream_mutex);
    std::cout << "STD: from " << pinned_core << " to " << pinned_core << " : " << time << " ns" << std::endl;
    latency_matrix[pinned_core][pinned_core] = time;
}


void pinned_two_workers_std(int pinned_core, int that_core, std::vector<std::vector<double>>& latency_matrix) {
    std::mutex stream_mutex;
    set_affinity(pinned_core);
    double time;
    time = measureLatencyStd(that_core);
    time /= iterations * 1.0;
    const std::lock_guard<std::mutex> lock(stream_mutex);
    std::cout << "STD: from " << pinned_core << " to " << that_core << " : " << time << " ns" << std::endl;
    latency_matrix[pinned_core][that_core] = time;
}


int main() {
    #if defined (__APPLE__) && defined (__aarch64__)
    std::cout << "Hello from Apple M1🤔" << std::endl;
    #endif
    const int  processor_count = std::thread::hardware_concurrency();
    std::vector<std::vector<double>> latency_matrix(processor_count, std::vector<double>(processor_count, 0.0));
    set_affinity(0);
    for (int this_core = 0; this_core < processor_count; this_core++) {
        for (int that_core = 0; that_core < processor_count; that_core++) {
            if (this_core != that_core) {
#if defined _WIN32 
                pinned_two_workers(this_core, that_core, latency_matrix);
#endif
#if defined(__linux__) || (defined( _WIN32) && defined(__use_std_timer))
                pinned_two_workers_std(this_core, that_core, latency_matrix);
#endif
#if defined(__APPLE__) && defined(__aarch64__)
                pinned_two_workers_std(this_core, that_core, latency_matrix);
#endif
            }
            else {
#if defined(_WIN32) 
                pinned_worker(this_core, latency_matrix);
#endif
#if defined(__linux__) || (defined( _WIN32) && defined(__use_std_timer))
                pinned_worker_std(this_core, latency_matrix);
#endif
#if defined(__APPLE__) && defined(__aarch64__)
                pinned_worker_std(this_core, latency_matrix);
#endif
            }
        }
    }

    std::cout << std::fixed;
    std::cout << std::setprecision(1);

    // print x axis
    std::cout << std::setfill(' ') << std::setw(8) << " ";
    for (int core_idx = 0; core_idx < processor_count; core_idx++) {
        std::cout << std::setfill(' ') << std::setw(10) << core_idx;
    }
    std::cout << std::endl;

    // print cross-core latency
    for (int this_core_idx = 0; this_core_idx < processor_count; this_core_idx++) {
        std::cout << std::setfill(' ') << std::setw(8) << this_core_idx;
        for (int that_core_idx = 0; that_core_idx < processor_count; that_core_idx++) {
            std::cout << std::setfill(' ') << std::setw(8) << latency_matrix[this_core_idx][that_core_idx] << "ns";
        }
        std::cout << std::endl;
    }

    return 0;
}
