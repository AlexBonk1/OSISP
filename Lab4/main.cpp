#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <queue>
#include <string>
#include <random>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <sstream>

struct Task {
    int id;
    int stage1_ms;
    int stage2_ms;
    int stage3_ms;
    LONGLONG created_tick;
    LONGLONG stage1_wait_start;
    LONGLONG stage2_wait_start;
    LONGLONG stage3_wait_start;
    double wait1_ms;
    double wait2_ms;
    double wait3_ms;
    double total_ms;
    bool completed;
};

struct Config {
    int student_id = 0;
    int base_operations = 30;
    int operations = 30;
    unsigned int seed = 1000;
    int cap1 = 1;
    int cap2 = 1;
    int cap3 = 1;
    int work1_ms = 25;
    int work2_ms = 45;
    int work3_ms = 20;
    int arrival_interval_ms = 5;
    int queue_limit = 64;
    DWORD wait_timeout_ms = 5000;
    DWORD max_experiment_ms = 60000;
    bool verbose = false;
    bool benchmark_mode = true;
};

struct StageStats {
    LONG active_now = 0;
    LONG max_active = 0;
    LONG processed_count = 0;
    LONG timeout_count = 0;
    double total_wait_ms = 0.0;
    double total_service_ms = 0.0;
    size_t max_queue_len = 0;
};

struct ExperimentResult {
    std::string config_name;
    int run_index;
    double elapsed_ms;
    int total_tasks;
    int completed_tasks;
    int rejected_tasks;
    int timeout_tasks;
    int invariant_violations;
    StageStats s1;
    StageStats s2;
    StageStats s3;
};

class Timer {
public:
    Timer() {
        QueryPerformanceFrequency(&freq_);
        QueryPerformanceCounter(&start_);
    }

    void reset() {
        QueryPerformanceCounter(&start_);
    }

    LONGLONG now_ticks() const {
        LARGE_INTEGER cur;
        QueryPerformanceCounter(&cur);
        return cur.QuadPart;
    }

    double elapsed_ms() const {
        return ticks_to_ms(now_ticks() - start_.QuadPart);
    }

    double ticks_to_ms(LONGLONG ticks) const {
        return (static_cast<double>(ticks) * 1000.0) / static_cast<double>(freq_.QuadPart);
    }

private:
    LARGE_INTEGER freq_;
    LARGE_INTEGER start_;
};

struct SimulationContext {
    Config cfg;
    Timer timer;

    CRITICAL_SECTION log_cs;
    CRITICAL_SECTION q1_cs;
    CRITICAL_SECTION q2_cs;
    CRITICAL_SECTION q3_cs;
    CRITICAL_SECTION stats_cs;

    HANDLE sem1 = NULL;
    HANDLE sem2 = NULL;
    HANDLE sem3 = NULL;
    HANDLE stop_event = NULL;

    std::queue<Task*> q1;
    std::queue<Task*> q2;
    std::queue<Task*> q3;

    bool gen_done = false;
    bool s1_done = false;
    bool s2_done = false;

    std::vector<Task> all_tasks;
    StageStats st1;
    StageStats st2;
    StageStats st3;

    LONG rejected_count = 0;
    LONG invariant_violations = 0;
};

void record_violation(SimulationContext* ctx, const char* msg) {
    InterlockedIncrement(&ctx->invariant_violations);
    EnterCriticalSection(&ctx->log_cs);
    std::printf("[%08.2f ms] INVARIANT VIOLATION: %s\n", ctx->timer.elapsed_ms(), msg);
    LeaveCriticalSection(&ctx->log_cs);
}

void log_event(SimulationContext* ctx, const char* actor, int actor_num, DWORD tid, const char* state, int task_id, int stage) {
    if (!ctx->cfg.verbose) {
        return;
    }
    double t = ctx->timer.elapsed_ms();
    EnterCriticalSection(&ctx->log_cs);
    if (task_id >= 0) {
        std::printf("[%08.2f ms] %s %d | TID=%lu | STATE=%s | STAGE=%d | TASK=%d\n",
                    t, actor, actor_num, static_cast<unsigned long>(tid), state, stage, task_id);
    } else {
        std::printf("[%08.2f ms] %s %d | TID=%lu | STATE=%s | STAGE=%d\n",
                    t, actor, actor_num, static_cast<unsigned long>(tid), state, stage);
    }
    LeaveCriticalSection(&ctx->log_cs);
}

bool is_expired(SimulationContext* ctx) {
    if (WaitForSingleObject(ctx->stop_event, 0) == WAIT_OBJECT_0) {
        return true;
    }
    if (ctx->timer.elapsed_ms() > static_cast<double>(ctx->cfg.max_experiment_ms)) {
        SetEvent(ctx->stop_event);
        return true;
    }
    return false;
}

DWORD WINAPI GeneratorThread(LPVOID param) {
    SimulationContext* ctx = static_cast<SimulationContext*>(param);
    DWORD tid = GetCurrentThreadId();
    std::mt19937 rng(ctx->cfg.seed);

    std::uniform_int_distribution<int> d1(std::max(1, ctx->cfg.work1_ms - 5), ctx->cfg.work1_ms + 5);
    std::uniform_int_distribution<int> d2(std::max(1, ctx->cfg.work2_ms - 8), ctx->cfg.work2_ms + 8);
    std::uniform_int_distribution<int> d3(std::max(1, ctx->cfg.work3_ms - 5), ctx->cfg.work3_ms + 5);
    std::uniform_int_distribution<int> d_arr(0, std::max(0, ctx->cfg.arrival_interval_ms * 2));

    for (int i = 0; i < ctx->cfg.operations; ++i) {
        if (is_expired(ctx)) {
            break;
        }

        Task& t = ctx->all_tasks[i];
        t.id = i + 1;
        t.stage1_ms = d1(rng);
        t.stage2_ms = d2(rng);
        t.stage3_ms = d3(rng);
        t.created_tick = ctx->timer.now_ticks();
        t.stage1_wait_start = t.created_tick;
        t.wait1_ms = 0.0;
        t.wait2_ms = 0.0;
        t.wait3_ms = 0.0;
        t.total_ms = 0.0;
        t.completed = false;

        EnterCriticalSection(&ctx->q1_cs);
        if (static_cast<int>(ctx->q1.size()) >= ctx->cfg.queue_limit) {
            LeaveCriticalSection(&ctx->q1_cs);
            InterlockedIncrement(&ctx->rejected_count);
            log_event(ctx, "GENERATOR", 0, tid, "REJECTED_FULL_Q1", t.id, 1);
        } else {
            ctx->q1.push(&t);
            size_t sz = ctx->q1.size();
            if (sz > static_cast<size_t>(ctx->cfg.queue_limit)) {
                record_violation(ctx, "Queue 1 exceeded capacity limit");
            }
            LeaveCriticalSection(&ctx->q1_cs);

            EnterCriticalSection(&ctx->stats_cs);
            if (sz > ctx->st1.max_queue_len) {
                ctx->st1.max_queue_len = sz;
            }
            LeaveCriticalSection(&ctx->stats_cs);

            log_event(ctx, "GENERATOR", 0, tid, "ENQUEUED", t.id, 1);
        }

        int delay = d_arr(rng);
        if (delay > 0) {
            Sleep(static_cast<DWORD>(delay));
        }
    }

    EnterCriticalSection(&ctx->q1_cs);
    ctx->gen_done = true;
    LeaveCriticalSection(&ctx->q1_cs);
    log_event(ctx, "GENERATOR", 0, tid, "FINISHED", -1, 0);
    return 0;
}

struct StageWorkerParam {
    SimulationContext* ctx;
    int stage_id;
    int worker_id;
};

DWORD WINAPI StageWorkerThread(LPVOID param) {
    StageWorkerParam* wp = static_cast<StageWorkerParam*>(param);
    SimulationContext* ctx = wp->ctx;
    int stage = wp->stage_id;
    int wid = wp->worker_id;
    DWORD tid = GetCurrentThreadId();

    CRITICAL_SECTION* in_cs = nullptr;
    std::queue<Task*>* in_q = nullptr;
    CRITICAL_SECTION* out_cs = nullptr;
    std::queue<Task*>* out_q = nullptr;
    HANDLE sem = NULL;
    int max_cap = 1;
    StageStats* st = nullptr;

    if (stage == 1) {
        in_cs = &ctx->q1_cs;
        in_q = &ctx->q1;
        out_cs = &ctx->q2_cs;
        out_q = &ctx->q2;
        sem = ctx->sem1;
        max_cap = ctx->cfg.cap1;
        st = &ctx->st1;
    } else if (stage == 2) {
        in_cs = &ctx->q2_cs;
        in_q = &ctx->q2;
        out_cs = &ctx->q3_cs;
        out_q = &ctx->q3;
        sem = ctx->sem2;
        max_cap = ctx->cfg.cap2;
        st = &ctx->st2;
    } else {
        in_cs = &ctx->q3_cs;
        in_q = &ctx->q3;
        sem = ctx->sem3;
        max_cap = ctx->cfg.cap3;
        st = &ctx->st3;
    }

    while (!is_expired(ctx)) {
        Task* task = nullptr;
        bool upstream_done = false;

        EnterCriticalSection(in_cs);
        if (!in_q->empty()) {
            task = in_q->front();
            in_q->pop();
        } else {
            if (stage == 1) {
                upstream_done = ctx->gen_done;
            } else if (stage == 2) {
                upstream_done = ctx->s1_done;
            } else {
                upstream_done = ctx->s2_done;
            }
        }
        LeaveCriticalSection(in_cs);

        if (!task) {
            if (upstream_done) {
                break;
            }
            Sleep(1);
            continue;
        }

        log_event(ctx, "WORKER", wid, tid, "WAITING_SEM", task->id, stage);
        DWORD wait_res = WaitForSingleObject(sem, ctx->cfg.wait_timeout_ms);
        if (wait_res == WAIT_TIMEOUT) {
            EnterCriticalSection(&ctx->stats_cs);
            st->timeout_count++;
            LeaveCriticalSection(&ctx->stats_cs);
            log_event(ctx, "WORKER", wid, tid, "TIMEOUT", task->id, stage);
            continue;
        }
        if (wait_res != WAIT_OBJECT_0) {
            record_violation(ctx, "WaitForSingleObject returned unexpected status");
            break;
        }

        LONGLONG acquired_tick = ctx->timer.now_ticks();
        LONG cur_active = InterlockedIncrement(&st->active_now);
        if (cur_active > max_cap) {
            record_violation(ctx, "Semaphore capacity exceeded on stage");
        }

        int service_ms = 0;
        double wait_duration_ms = 0.0;
        if (stage == 1) {
            wait_duration_ms = ctx->timer.ticks_to_ms(acquired_tick - task->stage1_wait_start);
            task->wait1_ms = wait_duration_ms;
            service_ms = task->stage1_ms;
        } else if (stage == 2) {
            wait_duration_ms = ctx->timer.ticks_to_ms(acquired_tick - task->stage2_wait_start);
            task->wait2_ms = wait_duration_ms;
            service_ms = task->stage2_ms;
        } else {
            wait_duration_ms = ctx->timer.ticks_to_ms(acquired_tick - task->stage3_wait_start);
            task->wait3_ms = wait_duration_ms;
            service_ms = task->stage3_ms;
        }

        EnterCriticalSection(&ctx->stats_cs);
        if (cur_active > st->max_active) {
            st->max_active = cur_active;
        }
        st->total_wait_ms += wait_duration_ms;
        LeaveCriticalSection(&ctx->stats_cs);

        log_event(ctx, "WORKER", wid, tid, "WORKING", task->id, stage);
        Sleep(static_cast<DWORD>(service_ms));

        InterlockedDecrement(&st->active_now);
        if (!ReleaseSemaphore(sem, 1, NULL)) {
            record_violation(ctx, "ReleaseSemaphore failed");
        }

        EnterCriticalSection(&ctx->stats_cs);
        st->processed_count++;
        st->total_service_ms += static_cast<double>(service_ms);
        LeaveCriticalSection(&ctx->stats_cs);

        log_event(ctx, "WORKER", wid, tid, "RELEASED", task->id, stage);

        if (stage < 3) {
            if (stage == 1) {
                task->stage2_wait_start = ctx->timer.now_ticks();
            } else {
                task->stage3_wait_start = ctx->timer.now_ticks();
            }
            EnterCriticalSection(out_cs);
            out_q->push(task);
            size_t out_sz = out_q->size();
            LeaveCriticalSection(out_cs);

            EnterCriticalSection(&ctx->stats_cs);
            StageStats* next_st = (stage == 1) ? &ctx->st2 : &ctx->st3;
            if (out_sz > next_st->max_queue_len) {
                next_st->max_queue_len = out_sz;
            }
            LeaveCriticalSection(&ctx->stats_cs);
        } else {
            task->total_ms = ctx->timer.ticks_to_ms(ctx->timer.now_ticks() - task->created_tick);
            task->completed = true;
            log_event(ctx, "WORKER", wid, tid, "COMPLETED_TASK", task->id, stage);
        }
    }

    log_event(ctx, "WORKER", wid, tid, "FINISHED", -1, stage);
    return 0;
}

ExperimentResult run_single_experiment(const Config& cfg, const std::string& cfg_name, int run_idx) {
    SimulationContext ctx;
    ctx.cfg = cfg;
    ctx.all_tasks.resize(cfg.operations);

    InitializeCriticalSection(&ctx.log_cs);
    InitializeCriticalSection(&ctx.q1_cs);
    InitializeCriticalSection(&ctx.q2_cs);
    InitializeCriticalSection(&ctx.q3_cs);
    InitializeCriticalSection(&ctx.stats_cs);

    ctx.sem1 = CreateSemaphoreA(NULL, cfg.cap1, cfg.cap1, NULL);
    ctx.sem2 = CreateSemaphoreA(NULL, cfg.cap2, cfg.cap2, NULL);
    ctx.sem3 = CreateSemaphoreA(NULL, cfg.cap3, cfg.cap3, NULL);
    ctx.stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);

    if (!ctx.sem1 || !ctx.sem2 || !ctx.sem3 || !ctx.stop_event) {
        std::fprintf(stderr, "Failed to create synchronization objects. Error=%lu\n", GetLastError());
        std::exit(1);
    }

    int threads_per_stage = std::max({cfg.cap1, cfg.cap2, cfg.cap3, 3});
    std::vector<StageWorkerParam> params;
    params.reserve(threads_per_stage * 3);

    std::vector<HANDLE> s1_threads;
    std::vector<HANDLE> s2_threads;
    std::vector<HANDLE> s3_threads;

    ctx.timer.reset();

    HANDLE gen_thread = CreateThread(NULL, 0, GeneratorThread, &ctx, 0, NULL);
    if (!gen_thread) {
        std::fprintf(stderr, "CreateThread failed for generator\n");
        std::exit(1);
    }

    for (int i = 0; i < threads_per_stage; ++i) {
        params.push_back({&ctx, 1, i + 1});
        HANDLE h = CreateThread(NULL, 0, StageWorkerThread, &params.back(), 0, NULL);
        if (h) s1_threads.push_back(h);
    }
    for (int i = 0; i < threads_per_stage; ++i) {
        params.push_back({&ctx, 2, i + 1});
        HANDLE h = CreateThread(NULL, 0, StageWorkerThread, &params.back(), 0, NULL);
        if (h) s2_threads.push_back(h);
    }
    for (int i = 0; i < threads_per_stage; ++i) {
        params.push_back({&ctx, 3, i + 1});
        HANDLE h = CreateThread(NULL, 0, StageWorkerThread, &params.back(), 0, NULL);
        if (h) s3_threads.push_back(h);
    }

    WaitForSingleObject(gen_thread, cfg.max_experiment_ms);
    CloseHandle(gen_thread);

    if (!s1_threads.empty()) {
        WaitForMultipleObjects(static_cast<DWORD>(s1_threads.size()), s1_threads.data(), TRUE, cfg.max_experiment_ms);
        for (HANDLE h : s1_threads) CloseHandle(h);
    }
    EnterCriticalSection(&ctx.q2_cs);
    ctx.s1_done = true;
    LeaveCriticalSection(&ctx.q2_cs);

    if (!s2_threads.empty()) {
        WaitForMultipleObjects(static_cast<DWORD>(s2_threads.size()), s2_threads.data(), TRUE, cfg.max_experiment_ms);
        for (HANDLE h : s2_threads) CloseHandle(h);
    }
    EnterCriticalSection(&ctx.q3_cs);
    ctx.s2_done = true;
    LeaveCriticalSection(&ctx.q3_cs);

    if (!s3_threads.empty()) {
        WaitForMultipleObjects(static_cast<DWORD>(s3_threads.size()), s3_threads.data(), TRUE, cfg.max_experiment_ms);
        for (HANDLE h : s3_threads) CloseHandle(h);
    }

    double total_elapsed = ctx.timer.elapsed_ms();

    if (ctx.st1.active_now != 0 || ctx.st2.active_now != 0 || ctx.st3.active_now != 0) {
        record_violation(&ctx, "Resources still held after threads finished");
    }
    if (!ctx.q1.empty() || !ctx.q2.empty() || !ctx.q3.empty()) {
        record_violation(&ctx, "Unprocessed items remained in queues");
    }

    int completed = 0;
    for (const auto& t : ctx.all_tasks) {
        if (t.completed) {
            completed++;
        }
    }

    int timeouts = ctx.st1.timeout_count + ctx.st2.timeout_count + ctx.st3.timeout_count;
    if (completed + ctx.rejected_count + timeouts != cfg.operations) {
        record_violation(&ctx, "Conservation of tasks invariant violated");
    }

    CloseHandle(ctx.sem1);
    CloseHandle(ctx.sem2);
    CloseHandle(ctx.sem3);
    CloseHandle(ctx.stop_event);

    DeleteCriticalSection(&ctx.log_cs);
    DeleteCriticalSection(&ctx.q1_cs);
    DeleteCriticalSection(&ctx.q2_cs);
    DeleteCriticalSection(&ctx.q3_cs);
    DeleteCriticalSection(&ctx.stats_cs);

    ExperimentResult res;
    res.config_name = cfg_name;
    res.run_index = run_idx;
    res.elapsed_ms = total_elapsed;
    res.total_tasks = cfg.operations;
    res.completed_tasks = completed;
    res.rejected_tasks = ctx.rejected_count;
    res.timeout_tasks = timeouts;
    res.invariant_violations = ctx.invariant_violations;
    res.s1 = ctx.st1;
    res.s2 = ctx.st2;
    res.s3 = ctx.st3;
    return res;
}

std::string detect_bottleneck(const ExperimentResult& r) {
    double w1 = r.s1.processed_count > 0 ? r.s1.total_wait_ms / r.s1.processed_count : 0.0;
    double w2 = r.s2.processed_count > 0 ? r.s2.total_wait_ms / r.s2.processed_count : 0.0;
    double w3 = r.s3.processed_count > 0 ? r.s3.total_wait_ms / r.s3.processed_count : 0.0;

    if (w1 >= w2 && w1 >= w3) return "Stage 1";
    if (w2 >= w1 && w2 >= w3) return "Stage 2";
    return "Stage 3";
}

void print_usage() {
    std::printf("Usage: lab04_sync.exe [options]\n");
    std::printf("  --student-id <N>     Student ID last 3 digits (default: 0)\n");
    std::printf("  --base-ops <N>       Base operations count (default: 30)\n");
    std::printf("  --caps <c1,c2,c3>    Single configuration channels (switches off benchmark mode)\n");
    std::printf("  --work <w1,w2,w3>    Mean service times in ms for stages 1,2,3 (default: 25,45,20)\n");
    std::printf("  --arrival <ms>       Mean arrival interval in ms (default: 5)\n");
    std::printf("  --queue-limit <N>    Max queue capacity (default: 64)\n");
    std::printf("  --timeout <ms>       Semaphore wait timeout in ms (default: 5000)\n");
    std::printf("  --verbose            Enable detailed thread event logging\n");
    std::printf("  --csv <path>         Save benchmark results to CSV file\n");
}

int main(int argc, char* argv[]) {
    Config base_cfg;
    std::string csv_path = "results.csv";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--student-id" && i + 1 < argc) {
            base_cfg.student_id = std::atoi(argv[++i]);
        } else if (arg == "--base-ops" && i + 1 < argc) {
            base_cfg.base_operations = std::atoi(argv[++i]);
        } else if (arg == "--caps" && i + 1 < argc) {
            int c1 = 1, c2 = 1, c3 = 1;
            if (std::sscanf(argv[++i], "%d,%d,%d", &c1, &c2, &c3) == 3) {
                base_cfg.cap1 = std::max(1, c1);
                base_cfg.cap2 = std::max(1, c2);
                base_cfg.cap3 = std::max(1, c3);
                base_cfg.benchmark_mode = false;
            }
        } else if (arg == "--work" && i + 1 < argc) {
            int w1 = 25, w2 = 45, w3 = 20;
            if (std::sscanf(argv[++i], "%d,%d,%d", &w1, &w2, &w3) == 3) {
                base_cfg.work1_ms = std::max(1, w1);
                base_cfg.work2_ms = std::max(1, w2);
                base_cfg.work3_ms = std::max(1, w3);
            }
        } else if (arg == "--arrival" && i + 1 < argc) {
            base_cfg.arrival_interval_ms = std::max(0, std::atoi(argv[++i]));
        } else if (arg == "--queue-limit" && i + 1 < argc) {
            base_cfg.queue_limit = std::max(1, std::atoi(argv[++i]));
        } else if (arg == "--timeout" && i + 1 < argc) {
            base_cfg.wait_timeout_ms = static_cast<DWORD>(std::max(10, std::atoi(argv[++i])));
        } else if (arg == "--verbose") {
            base_cfg.verbose = true;
        } else if (arg == "--csv" && i + 1 < argc) {
            csv_path = argv[++i];
        } else if (arg == "--help") {
            print_usage();
            return 0;
        }
    }

    base_cfg.seed = static_cast<unsigned int>(1000 + base_cfg.student_id * 97);
    base_cfg.operations = base_cfg.base_operations + base_cfg.student_id * 5;

    std::printf("=== Variant 18: Pipeline Bottleneck Study (Semaphore + CriticalSection) ===\n");
    std::printf("STUDENT_ID=%d | SEED=%u | OPERATIONS=%d | WORK_MS=(%d, %d, %d)\n\n",
                base_cfg.student_id, base_cfg.seed, base_cfg.operations,
                base_cfg.work1_ms, base_cfg.work2_ms, base_cfg.work3_ms);

    std::vector<ExperimentResult> all_results;

    if (!base_cfg.benchmark_mode) {
        std::string name = std::to_string(base_cfg.cap1) + "-" +
                           std::to_string(base_cfg.cap2) + "-" +
                           std::to_string(base_cfg.cap3);
        ExperimentResult r = run_single_experiment(base_cfg, name, 1);
        all_results.push_back(r);
    } else {
        struct Preset {
            const char* name;
            int c1, c2, c3;
        };
        Preset presets[] = {
            {"1-1-1", 1, 1, 1},
            {"1-2-3", 1, 2, 3},
            {"3-2-1", 3, 2, 1}
        };

        for (const auto& p : presets) {
            for (int run = 1; run <= 3; ++run) {
                Config run_cfg = base_cfg;
                run_cfg.cap1 = p.c1;
                run_cfg.cap2 = p.c2;
                run_cfg.cap3 = p.c3;
                ExperimentResult r = run_single_experiment(run_cfg, p.name, run);
                all_results.push_back(r);
            }
        }
    }

    std::printf("%-8s | %-3s | %-10s | %-9s | %-9s | %-9s | %-9s | %-10s | %-8s\n",
                "Config", "Run", "Time(ms)", "Completed", "AvgW1(ms)", "AvgW2(ms)", "AvgW3(ms)", "Bottleneck", "Viol.");
    std::printf("-------------------------------------------------------------------------------------------------\n");

    for (const auto& r : all_results) {
        double w1 = r.s1.processed_count > 0 ? r.s1.total_wait_ms / r.s1.processed_count : 0.0;
        double w2 = r.s2.processed_count > 0 ? r.s2.total_wait_ms / r.s2.processed_count : 0.0;
        double w3 = r.s3.processed_count > 0 ? r.s3.total_wait_ms / r.s3.processed_count : 0.0;
        std::string bn = detect_bottleneck(r);

        std::printf("%-8s | %-3d | %-10.2f | %4d/%-4d | %-9.2f | %-9.2f | %-9.2f | %-10s | %-8d\n",
                    r.config_name.c_str(), r.run_index, r.elapsed_ms,
                    r.completed_tasks, r.total_tasks,
                    w1, w2, w3, bn.c_str(), r.invariant_violations);
    }

    std::ofstream csv(csv_path);
    if (csv.is_open()) {
        csv << "config,run,elapsed_ms,total_tasks,completed_tasks,rejected_tasks,timeout_tasks,"
            << "avg_wait1_ms,avg_wait2_ms,avg_wait3_ms,max_q1,max_q2,max_q3,bottleneck,violations\n";
        for (const auto& r : all_results) {
            double w1 = r.s1.processed_count > 0 ? r.s1.total_wait_ms / r.s1.processed_count : 0.0;
            double w2 = r.s2.processed_count > 0 ? r.s2.total_wait_ms / r.s2.processed_count : 0.0;
            double w3 = r.s3.processed_count > 0 ? r.s3.total_wait_ms / r.s3.processed_count : 0.0;
            csv << r.config_name << ","
                << r.run_index << ","
                << std::fixed << std::setprecision(2) << r.elapsed_ms << ","
                << r.total_tasks << ","
                << r.completed_tasks << ","
                << r.rejected_tasks << ","
                << r.timeout_tasks << ","
                << w1 << "," << w2 << "," << w3 << ","
                << r.s1.max_queue_len << "," << r.s2.max_queue_len << "," << r.s3.max_queue_len << ","
                << detect_bottleneck(r) << ","
                << r.invariant_violations << "\n";
        }
    }

    return 0;
}