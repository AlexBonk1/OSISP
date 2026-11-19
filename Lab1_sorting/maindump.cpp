#include "utils.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdlib>

const int MAXTHREADS = 16;
const int TSTEP = 1;
const int MAXARRSIZE = 50000;
const int STEP = 1000;
const int REPEATS = 10;

int main() {
    std::ofstream filedump("dump.txt");
    if (!filedump.is_open()) {
        std::cerr << "Error: Could not open dump.txt for writing\n";
        return 1;
    }

    filedump << "threads array_size time_ms\n";

    for (int threads_count = 1; threads_count <= MAXTHREADS; threads_count += TSTEP) {
        std::cout << "Testing with threads: " << threads_count << "...\n";

        for (int n = STEP; n <= MAXARRSIZE; n += STEP) {
            SortResult resfin;
            resfin.arraySize = n;
            resfin.thread_count =threads_count;
            resfin.ms = 0.0f;
            for(int r =0; r< REPEATS; r++){
            std::vector<int> data(n);
            for (int i = 0; i < n; i++) {
                data[i] = rand() % 1000;
            }

            SortResult res = parallelSortWithMerge(data, threads_count, false);
            resfin.ms += res.ms/(float)REPEATS;
            }
            filedump << resfin.thread_count << " " << resfin.arraySize << " " << resfin.ms << "\n";
        }
    }

    filedump.close();
    std::cout << "Benchmarking finished successfully!\n";
    return 0;
}