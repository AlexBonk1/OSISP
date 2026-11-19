#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <vector>
#include <cstdint>
#include <chrono>
#include <iomanip>
#include <string>

uint64_t ComputeChecksum(const uint8_t* data, size_t size, uint64_t hash = 14695981039346656037ULL) {
    constexpr uint64_t FNV_PRIME = 1099511628211ULL;
    for (size_t i = 0; i < size; ++i) {
        hash ^= static_cast<uint64_t>(data[i]);
        hash *= FNV_PRIME;
    }
    return hash;
}

bool EnsureTestFileExists(const wchar_t* filePath, uint64_t fileSize) {
    HANDLE hFile = CreateFileW(filePath, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER existingSize{};
        if (GetFileSizeEx(hFile, &existingSize) && static_cast<uint64_t>(existingSize.QuadPart) == fileSize) {
            CloseHandle(hFile);
            return true;
        }
        CloseHandle(hFile);
    }

    std::cout << "[*] Генерация тестового файла (" << (fileSize / (1024 * 1024)) << " МБ)...\n\n";
    hFile = CreateFileW(filePath, GENERIC_WRITE, 0, nullptr,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    const size_t chunkSize = 4 * 1024 * 1024;
    std::vector<uint8_t> buffer(chunkSize);
    for (size_t i = 0; i < chunkSize; ++i) {
        buffer[i] = static_cast<uint8_t>((i * 31 + 17) & 0xFF);
    }

    uint64_t remaining = fileSize;
    while (remaining > 0) {
        DWORD toWrite = static_cast<DWORD>(remaining < chunkSize ? remaining : chunkSize);
        DWORD written = 0;
        if (!WriteFile(hFile, buffer.data(), toWrite, &written, nullptr) || written != toWrite) {
            CloseHandle(hFile);
            return false;
        }
        remaining -= written;
    }

    CloseHandle(hFile);
    return true;
}

struct BenchmarkResult {
    size_t windowSizeMB;
    size_t windowsCount;
    uint64_t totalChecksum;
    double elapsedMs;
};

bool ProcessFileWithWindow(const wchar_t* filePath, size_t requestedWindowSize, BenchmarkResult& outResult, bool printSampleWindows = false) {
    SYSTEM_INFO sysInfo{};
    GetSystemInfo(&sysInfo);
    const uint64_t granularity = sysInfo.dwAllocationGranularity;

    HANDLE hFile = CreateFileW(
        filePath,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        std::cout << "[!] Ошибка открытия файла. Код: " << GetLastError() << "\n";
        return false;
    }

    LARGE_INTEGER liFileSize{};
    if (!GetFileSizeEx(hFile, &liFileSize) || liFileSize.QuadPart == 0) {
        std::cout << "[!] Не удалось получить размер файла или файл пуст.\n";
        CloseHandle(hFile);
        return false;
    }
    const uint64_t fileSize = static_cast<uint64_t>(liFileSize.QuadPart);

    HANDLE hMapping = CreateFileMappingW(
        hFile,
        nullptr,
        PAGE_READONLY,
        0, 0,
        nullptr
    );

    if (hMapping == nullptr) {
        std::cout << "[!] Ошибка CreateFileMappingW. Код: " << GetLastError() << "\n";
        CloseHandle(hFile);
        return false;
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    uint64_t currentOffset = 0;
    uint64_t rollingFileChecksum = 14695981039346656037ULL;
    size_t windowIndex = 0;

    while (currentOffset < fileSize) {
        uint64_t alignedOffset = (currentOffset / granularity) * granularity;
        uint64_t alignmentDelta = currentOffset - alignedOffset;

        uint64_t bytesToProcess = requestedWindowSize;
        if (currentOffset + bytesToProcess > fileSize) {
            bytesToProcess = fileSize - currentOffset;
        }

        SIZE_T bytesToMap = static_cast<SIZE_T>(bytesToProcess + alignmentDelta);

        DWORD offsetHigh = static_cast<DWORD>((alignedOffset >> 32) & 0xFFFFFFFFULL);
        DWORD offsetLow  = static_cast<DWORD>(alignedOffset & 0xFFFFFFFFULL);

        void* mappedBase = MapViewOfFile(
            hMapping,
            FILE_MAP_READ,
            offsetHigh,
            offsetLow,
            bytesToMap
        );

        if (mappedBase == nullptr) {
            std::cout << "[!] Ошибка MapViewOfFile на смещении " << alignedOffset
                      << ". Код: " << GetLastError() << "\n";
            CloseHandle(hMapping);
            CloseHandle(hFile);
            return false;
        }

        const uint8_t* windowData = static_cast<const uint8_t*>(mappedBase) + alignmentDelta;

        uint64_t windowChecksum = ComputeChecksum(windowData, static_cast<size_t>(bytesToProcess));
        rollingFileChecksum = ComputeChecksum(windowData, static_cast<size_t>(bytesToProcess), rollingFileChecksum);

        if (printSampleWindows && windowIndex < 3) {
            std::cout << "Окно #" << windowIndex
                      << " Смещение: " << currentOffset
                      << "Размер: " << (bytesToProcess / (1024 * 1024)) << " МБ"
                      << "Hash: 0x" << std::hex << windowChecksum << std::dec << "\n";
        }

        UnmapViewOfFile(mappedBase);

        currentOffset += bytesToProcess;
        ++windowIndex;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    double elapsedMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();

    CloseHandle(hMapping);
    CloseHandle(hFile);

    outResult = {
        requestedWindowSize / (1024 * 1024),
        windowIndex,
        rollingFileChecksum,
        elapsedMs
    };

    return true;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    // SetConsoleCP(CP_UTF8);

    const wchar_t* filePath = L"large_test_data.bin";
    const uint64_t testFileSize = 512ULL * 1024 * 1024;

    SYSTEM_INFO sysInfo{};
    GetSystemInfo(&sysInfo);
    std::cout << "Гранулярность виртуальной памяти Windows: "
              << (sysInfo.dwAllocationGranularity / 1024) << " КБ\n\n";

    if (!EnsureTestFileExists(filePath, testFileSize)) {
        std::cout << "[!] Не удалось подготовить тестовый файл.\n";
        return 1;
    }

    const std::vector<size_t> windowSizes = {
        1ULL  * 1024 * 1024,
        16ULL * 1024 * 1024,
        64ULL * 1024 * 1024
    };

    std::vector<BenchmarkResult> results;

    for (size_t winSize : windowSizes) {
        std::cout << "=== Исследование окна: " << (winSize / (1024 * 1024)) << " МБ ===\n";
        BenchmarkResult res{};
        if (ProcessFileWithWindow(filePath, winSize, res, true)) {
            results.push_back(res);
        }
        std::cout << "\n";
    }

    std::cout << "=== Итоговая таблица результатов ===\n\n";
    std::cout << "Окно (МБ)\tКол-во окон\tИтоговый Hash\t\tВремя (мс)\n";
    std::cout << "------------------------------------------------------------------\n";

    for (const auto& r : results) {
        std::cout << r.windowSizeMB << "\t\t"
                  << r.windowsCount << "\t\t"
                  << "0x" << std::hex << r.totalChecksum << std::dec << "\t"
                  << std::fixed << std::setprecision(2) << r.elapsedMs << "\n\n";
    }

    return 0;
}