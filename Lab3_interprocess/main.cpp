#include <windows.h>
#include <iostream>
#include <vector>
#include <chrono>
#include <string>
#include <iomanip>
#include <sstream>

#pragma pack(push, 1)
// int VARIANT_ID = 18;

struct MessageHeader {
    uint32_t version;
    uint32_t messageType;
    uint32_t sourcePid;
    uint32_t requestId;
    uint32_t payloadSize;
    uint32_t data;
    uint8_t padding[40];
};

struct SharedMemory {
    int head;
    int tail;
    MessageHeader queue[123];
};
#pragma pack(pop)

enum MsgType {
    MSG_TASK = 1,
    MSG_STOP = 2,
    MSG_ERROR = 3
};

int STUDENT_ID = 74;
int VARIANT_ID = 18;
std::string RUN_ID = "0";
std::string ROLE = "PRODUCER";
DWORD PID = 0;

void PrintLog(const std::string& action, uint32_t reqId, const std::string& type, size_t size) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::ostringstream oss;
    oss << "[" << std::setfill('0') << std::setw(2) << st.wHour << ":"
        << std::setw(2) << st.wMinute << ":"
        << std::setw(2) << st.wSecond << "."
        << std::setw(3) << st.wMilliseconds << "]\n"
        << "PID=" << PID << "\n"
        << action << "\n"
        << "requestId=" << reqId << "\n"
        << "type=" << type << "\n"
        << "size=" << size << "\n";
    std::cout << oss.str() << std::flush;
}

void PrintError(const std::string& func) {
    DWORD err = GetLastError();
    LPVOID msgBuf;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&msgBuf, 0, NULL);
    std::cerr << "ERROR in " << func << ": " << err << " - " << (char*)msgBuf << "\n";
    LocalFree(msgBuf);
}

std::string GetTypeName(uint32_t t) {
    switch (t) {
        case MSG_TASK: return "TASK";
        case MSG_STOP: return "STOP";
        case MSG_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void RunConsumer() {
    HANDLE hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, "Local\\Lab3_Map_123");
    HANDLE hMutex = OpenMutexA(MUTEX_ALL_ACCESS, FALSE, "Local\\Lab3_Mutex_123");
    HANDLE hSemEmpty = OpenSemaphoreA(SEMAPHORE_ALL_ACCESS, FALSE, "Local\\Lab3_SemEmpty_123");
    HANDLE hSemFilled = OpenSemaphoreA(SEMAPHORE_ALL_ACCESS, FALSE, "Local\\Lab3_SemFilled_123");

    if (!hMap || !hMutex || !hSemEmpty || !hSemFilled) {
        PrintError("Open IPC Objects");
        return;
    }

    SharedMemory* shm = (SharedMemory*)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemory));
    if (!shm) {
        PrintError("MapViewOfFile");
        return;
    }

    int processedCount = 0;
    bool running = true;

    while (running) {
        if (WaitForSingleObject(hSemFilled, INFINITE) != WAIT_OBJECT_0) break;
        if (WaitForSingleObject(hMutex, INFINITE) != WAIT_OBJECT_0) break;

        MessageHeader msg = shm->queue[shm->head];
        shm->head = (shm->head + 1) % 123;

        ReleaseMutex(hMutex);
        ReleaseSemaphore(hSemEmpty, 1, NULL);

        PrintLog("RECEIVE", msg.requestId, GetTypeName(msg.messageType), sizeof(MessageHeader));

        if (msg.messageType == MSG_STOP) {
            running = false;
        } else if (msg.messageType == MSG_TASK) {
            processedCount++;
            Sleep(1);
        }
    }

    std::cout << ROLE << " finished. Processed tasks: " << processedCount << "\n";

    UnmapViewOfFile(shm);
    CloseHandle(hMap);
    CloseHandle(hMutex);
    CloseHandle(hSemEmpty);
    CloseHandle(hSemFilled);
}

void RunProducer() {
    HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(SharedMemory), "Local\\Lab3_Map_123");
    HANDLE hMutex = CreateMutexA(NULL, FALSE, "Local\\Lab3_Mutex_123");
    HANDLE hSemEmpty = CreateSemaphoreA(NULL, 123, 123, "Local\\Lab3_SemEmpty_123");
    HANDLE hSemFilled = CreateSemaphoreA(NULL, 0, 123, "Local\\Lab3_SemFilled_123");

    if (!hMap || !hMutex || !hSemEmpty || !hSemFilled) {
        PrintError("Create IPC Objects");
        return;
    }

    SharedMemory* shm = (SharedMemory*)MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedMemory));
    if (!shm) {
        PrintError("MapViewOfFile");
        return;
    }

    shm->head = 0;
    shm->tail = 0;

    int totalTasks = 1000 + STUDENT_ID;
    
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < totalTasks; ++i) {
        WaitForSingleObject(hSemEmpty, INFINITE);
        WaitForSingleObject(hMutex, INFINITE);

        shm->queue[shm->tail].version = 1;
        shm->queue[shm->tail].messageType = MSG_TASK;
        shm->queue[shm->tail].sourcePid = PID;
        shm->queue[shm->tail].requestId = i + 1;
        shm->queue[shm->tail].payloadSize = sizeof(MessageHeader);
        shm->queue[shm->tail].data = i * 10;
        
        shm->tail = (shm->tail + 1) % 123;

        ReleaseMutex(hMutex);
        ReleaseSemaphore(hSemFilled, 1, NULL);
        
        if (i % 100 == 0) {
            PrintLog("SEND", i + 1, "TASK", sizeof(MessageHeader));
        }
    }

    for (int i = 0; i < 3; ++i) {
        WaitForSingleObject(hSemEmpty, INFINITE);
        WaitForSingleObject(hMutex, INFINITE);

        shm->queue[shm->tail].version = 1;
        shm->queue[shm->tail].messageType = MSG_STOP;
        shm->queue[shm->tail].sourcePid = PID;
        shm->queue[shm->tail].requestId = totalTasks + i + 1;
        shm->queue[shm->tail].payloadSize = sizeof(MessageHeader);
        
        shm->tail = (shm->tail + 1) % 123;

        ReleaseMutex(hMutex);
        ReleaseSemaphore(hSemFilled, 1, NULL);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    double secs = std::chrono::duration<double>(end - start).count();
    std::cout << "--- BENCHMARK: Total messages=" << totalTasks << ", time=" << secs 
              << "s, msgs/sec=" << (totalTasks / secs) << " ---\n";

    UnmapViewOfFile(shm);
    CloseHandle(hMap);
    CloseHandle(hMutex);
    CloseHandle(hSemEmpty);
    CloseHandle(hSemFilled);
}

int main(int argc, char* argv[]) {
    PID = GetCurrentProcessId();
    
    if (argc == 1) {
        RUN_ID = std::to_string(GetTickCount64());
        std::cout << ROLE << " | PID=" << PID << " | VARIANT=" << VARIANT_ID << " | RUN_ID=" << RUN_ID << "\n";
        
        HANDLE hProcess[3];
        for (int i = 1; i <= 3; ++i) {
            STARTUPINFOA si = { sizeof(si) };
            PROCESS_INFORMATION pi;
            std::string cmd = std::string(argv[0]) + " consumer " + RUN_ID + " " + std::to_string(i);
            
            if (!CreateProcessA(NULL, &cmd[0], NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &si, &pi)) {
                PrintError("CreateProcess");
            } else {
                hProcess[i-1] = pi.hProcess;
                CloseHandle(pi.hThread);
            }
        }
        
        RunProducer();
        
        std::cout << "Waiting for consumers to finish...\n";
        WaitForMultipleObjects(3, hProcess, TRUE, INFINITE);
        for (int i = 0; i < 3; ++i) CloseHandle(hProcess[i]);
        
    } else if (argc >= 4) {
        ROLE = "CONSUMER #" + std::string(argv[3]);
        RUN_ID = argv[2];
        std::cout << ROLE << " | PID=" << PID << " | VARIANT=" << VARIANT_ID << " | RUN_ID=" << RUN_ID << "\n";
        RunConsumer();
        system("pause");
    }
    return 0;
}