#include<iostream>
#include<vector>
#include<unordered_map>
#include <bits/stdc++.h>
#include <windows.h>
#include <iostream>
#include <vector>
#include <queue>
#include <chrono>
#include <utility>
#include<string>

class Printer{
    public:
    static void print(const std::vector<int>& vector,int count){
        for(int i = 0; i< vector.size();i++){
            std::cout<<vector[i]<<" ";
        }
        std::cout<<'\n';
    }
    static void print(int*arr,int count){
        for(int i = 0;i<count;i++){
            std::cout<<arr[i]<<" ";
        }
        std::cout<<'\n';
    }
    Printer(const Printer&) = delete;
    Printer& operator=(const Printer&) = delete;
};

std::vector<std::string> split(const std::string& string,char delimiter = ' '){
    std::vector<std::string> values;
    values.reserve(2);
    std::stringstream ss(string);
    std::string token;
    while (getline(ss, token, delimiter)) {
        values.push_back(token);
    }
    return values;
}

int partition(std::vector<int>& vec, int low, int high) {
    int pivot = vec[high];
    int i = low - 1;
    for (int j = low; j < high; j++) {
        if (vec[j] <= pivot) {
            i++;
            std::swap(vec[i], vec[j]);
        }
    }
    std::swap(vec[i + 1], vec[high]);
    return i + 1;
}

void quickSort(std::vector<int>& vec, int low, int high) {
    if (low < high) {
        int pi = partition(vec, low, high);
        quickSort(vec, low, pi - 1);
        quickSort(vec, pi + 1, high);
    }
}

struct SortStruct {
    std::vector<int>* vec;
    int low;
    int high;
    int threadId;
    double elapsedMs;
};

DWORD WINAPI part_quick_with_bench(LPVOID lpParam) {
    SortStruct* task = static_cast<SortStruct*>(lpParam);

    auto start = std::chrono::high_resolution_clock::now();

    if (task->low < task->high) {
        quickSort(*(task->vec), task->low, task->high);
    }

    auto end = std::chrono::high_resolution_clock::now();
    task->elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();

    return 0;
}

struct HeapNode {
    int value;
    int chunkIdx;
    int currentIdx;

    bool operator>(const HeapNode& other) const {
        return value > other.value;
    }
};

struct SortResult{
    int arraySize;
    double ms;
    int thread_count;
};

SortResult parallelSortWithMerge(std::vector<int>& vec, int numThreads,bool isDebug) {
    SortResult res;
    res.arraySize = vec.size();
    res.thread_count = numThreads;
    res.ms =0;
    int n = static_cast<int>(vec.size());
    if (n <= 1) return res;

    if (numThreads > n/4) numThreads = n/4;

    if (numThreads > MAXIMUM_WAIT_OBJECTS) numThreads = MAXIMUM_WAIT_OBJECTS;

    std::vector<SortStruct> tasks(numThreads);
    std::vector<HANDLE> handles(numThreads);

    int chunkSize = n / numThreads;
    int remainder = n % numThreads;
    int currentLow = 0;

    for (int i = 0; i < numThreads; i++) {
        int rangeSize = chunkSize + (i < remainder ? 1 : 0);
        
        tasks[i].vec = &vec;
        tasks[i].low = currentLow;
        tasks[i].high = currentLow + rangeSize - 1;
        tasks[i].threadId = i;
        tasks[i].elapsedMs = 0.0;

        handles[i] = CreateThread(
            nullptr,                
            0,                      
            part_quick_with_bench,  
            &tasks[i],              
            0,                      
            nullptr                 
        );

        currentLow += rangeSize;
    }

    WaitForMultipleObjects(numThreads, handles.data(), TRUE, INFINITE);

    for (int i = 0; i < numThreads; i++) {
        CloseHandle(handles[i]);
        if(isDebug){
            std::cout << "[Thread " << tasks[i].threadId << "] Range [" 
                    << tasks[i].low << ", " << tasks[i].high 
                    << "] sorted in " << tasks[i].elapsedMs << " ms\n";
            res.ms += tasks[i].elapsedMs; 
        }
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    std::priority_queue<HeapNode, std::vector<HeapNode>, std::greater<HeapNode>> minHeap;
    
    for (int i = 0; i < numThreads; i++) {
        if (tasks[i].low <= tasks[i].high) {
            minHeap.push({ vec[tasks[i].low], i, tasks[i].low });
        }
    }

    std::vector<int> mergedResult;
    mergedResult.reserve(n);

    while (!minHeap.empty()) {
        HeapNode top = minHeap.top();
        minHeap.pop();

        mergedResult.push_back(top.value);

        int nextIdx = top.currentIdx + 1;
        if (nextIdx <= tasks[top.chunkIdx].high) {
            minHeap.push({ vec[nextIdx], top.chunkIdx, nextIdx });
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
    res.ms+= elapsedMs;

    vec = std::move(mergedResult);
    return res;
}

