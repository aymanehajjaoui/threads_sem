/*Common.hpp*/

#pragma once

// Enable CMSIS-NN and ARM DSP optimizations
#define WITH_CMSIS_NN 1
#define ARM_MATH_DSP 1
#define ARM_NN_TRUNCATE

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <deque>
#include <chrono>
#include <atomic>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <dirent.h>
#include <csignal>
#include <semaphore.h>

#include "rp.h"

// Include full_model.h after standard headers
#include "../model/include/model.h"

#define DATA_SIZE 16384
#define QUEUE_MAX_SIZE 1000000
#define DECIMATION (125000 / MODEL_INPUT_DIM_0)
#define DISK_SPACE_THRESHOLD 0.2 * 1024 * 1024 * 1024 // 200 MB disk space threshold
#define acq_priority 1
#define write__csv_priority 1
#define write_dac_priority 1
#define model_priority 20
#define log_csv_priority 1
#define log_dac_priority 1

// Global flags
extern bool save_data_csv;
extern bool save_data_dac;
extern bool save_output_csv;
extern bool save_output_dac;

extern volatile std::sig_atomic_t interrupted ;

// Structs for data management
struct data_part_t
{
    input_t data;
};

struct model_result_t
{
    output_t output;
    double computation_time;
};

struct Channel
{
    std::queue<std::shared_ptr<data_part_t>> data_queue_csv;
    std::queue<std::shared_ptr<data_part_t>> data_queue_dac;
    std::queue<std::shared_ptr<data_part_t>> model_queue;

    std::deque<model_result_t> result_buffer_csv;
    std::deque<model_result_t> result_buffer_dac;

    sem_t data_sem_csv;
    sem_t data_sem_dac;
    sem_t model_sem;
    sem_t result_sem_csv;
    sem_t result_sem_dac;

    rp_acq_trig_state_t state;
    std::chrono::steady_clock::time_point trigger_time_point;
    std::chrono::steady_clock::time_point end_time_point;

    bool acquisition_done = false;
    bool processing_done = false;
    bool channel_triggered = false;

    std::atomic<int> acquire_count{0};
    std::atomic<int> model_count{0};
    std::atomic<int> write_count_csv{0};
    std::atomic<int> write_count_dac{0};
    std::atomic<int> log_count_csv{0};
    std::atomic<int> log_count_dac{0};

    std::atomic<uint64_t> trigger_time_ns{0};
    std::atomic<uint64_t> end_time_ns{0};

    rp_channel_t channel_id;
};


extern std::atomic<bool> stop_acquisition; // Atomic flag to stop data acquisition
extern std::atomic<bool> stop_program;     // Atomic flag to stop the program

extern Channel channel1, channel2;

// Template function to convert data to input_t type
template<typename T>
inline void convert_raw_data(const int16_t* src, T dst[MODEL_INPUT_DIM_0][1], size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if constexpr (std::is_same<T, float>::value) {
            // Convert to float and normalize between [-1.0, 1.0]
            dst[i][0] = static_cast<float>(src[i]) / 8192.0f;
        } else if constexpr (std::is_same<T, int8_t>::value) {
            // Convert to int8: scale int16 [-8192, 8191] to int8 [-128, 127]
            dst[i][0] = static_cast<int8_t>(std::round(src[i] / 64.0f));
        } else if constexpr (std::is_same<T, int16_t>::value) {
            // Direct copy (no conversion)
            dst[i][0] = src[i];
        } else {
            static_assert(!sizeof(T*), "Unsupported data type in convert_raw_data.");
        }
    }
}