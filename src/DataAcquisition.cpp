/*DataAcquisition.cpp*/

#include "DataAcquisition.hpp"
#include "SystemUtils.hpp"
#include <iostream>
#include <thread>
#include <chrono>

#include "DataAcquisition.hpp"
#include "SystemUtils.hpp"
#include <iostream>
#include <thread>
#include <chrono>

void acquire_data(Channel &channel, rp_channel_t rp_channel)
{
    try
    {
        std::cout << "Waiting for trigger on channel " << rp_channel + 1 << "..." << std::endl;

        while (!channel.channel_triggered && !stop_acquisition.load())
        {
            if (rp_AcqGetTriggerStateCh(rp_channel, &channel.state) != RP_OK)
            {
                std::cerr << "rp_AcqGetTriggerStateCh failed on channel " << rp_channel + 1 << std::endl;
                exit(-1);
            }

            if (channel.state == RP_TRIG_STATE_TRIGGERED)
            {
                channel.channel_triggered = true;
                std::cout << "Trigger detected on channel " << rp_channel + 1 << "!" << std::endl;
                channel.trigger_time_point = std::chrono::steady_clock::now();
                channel.trigger_time_ns.store(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        channel.trigger_time_point.time_since_epoch())
                        .count());
            }
        }

        if (!channel.channel_triggered)
        {
            std::cerr << "INFO: Acquisition stopped before trigger detected on channel " << rp_channel + 1 << "." << std::endl;
            stop_acquisition.store(true);
            exit(-1);
        }

        std::cout << "Starting data acquisition on channel " << rp_channel + 1 << std::endl;

        uint32_t pw = 0;
        constexpr uint32_t samples_per_chunk = MODEL_INPUT_DIM_0;
        uint32_t chunk_size = samples_per_chunk;

        if (rp_AcqAxiGetWritePointerAtTrig(rp_channel, &pw) != RP_OK)
        {
            std::cerr << "Error getting write pointer at trigger for channel " << rp_channel + 1 << std::endl;
            exit(-1);
        }

        uint32_t pos = pw;

        while (!stop_acquisition.load())
        {
            if (is_disk_space_below_threshold("/", DISK_SPACE_THRESHOLD))
            {
                std::cerr << "ERR: Disk space below threshold. Stopping acquisition." << std::endl;
                stop_acquisition.store(true);
                break;
            }

            uint32_t pwrite = 0;
            if (rp_AcqAxiGetWritePointer(rp_channel, &pwrite) == RP_OK)
            {
                int64_t distance = (pwrite >= pos) ? (pwrite - pos) : (DATA_SIZE - pos + pwrite);

                if (distance < 0)
                {
                    std::cerr << "ERR: Negative distance calculated on channel " << rp_channel + 1 << std::endl;
                    continue;
                }

                if (distance >= DATA_SIZE)
                {
                    std::cerr << "ERR: Overrun detected on channel " << rp_channel + 1 << " at: " << channel.acquire_count.load() << std::endl;
                    stop_acquisition.store(true);
                    return;
                }

                if (distance >= samples_per_chunk)
                {
                    int16_t buffer_raw[samples_per_chunk];
                    if (rp_AcqAxiGetDataRaw(rp_channel, pos, &chunk_size, buffer_raw) != RP_OK)
                    {
                        std::cerr << "rp_AcqAxiGetDataRaw failed on channel " << rp_channel + 1 << std::endl;
                        continue;
                    }

                    auto part = std::make_shared<data_part_t>();
                    convert_raw_data(buffer_raw, part->data, samples_per_chunk);

                    pos += samples_per_chunk;
                    if (pos >= DATA_SIZE)
                        pos -= DATA_SIZE;

                    if (save_data_csv)
                    {
                        channel.data_queue_csv.push(part);
                        sem_post(&channel.data_sem_csv);
                    }

                    if (save_data_dac)
                    {
                        channel.data_queue_dac.push(part);
                        sem_post(&channel.data_sem_dac);
                    }

                    channel.model_queue.push(part);
                    sem_post(&channel.model_sem);

                    channel.acquire_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }

        channel.end_time_point = std::chrono::steady_clock::now();
        channel.end_time_ns.store(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                channel.end_time_point.time_since_epoch())
                .count());

        channel.acquisition_done = true;

        if (save_data_csv)
            sem_post(&channel.data_sem_csv);

        if (save_data_dac)
            sem_post(&channel.data_sem_dac);

        sem_post(&channel.model_sem); // Wake up model thread if waiting

        std::cout << "Acquisition thread on channel " << static_cast<int>(channel.channel_id) + 1 << " exiting..." << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception in acquire_data for channel " << static_cast<int>(channel.channel_id) + 1 << ": " << e.what() << std::endl;
    }
}
