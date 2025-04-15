/* DataWriter.cpp */

#include "DataWriterCSV.hpp"
#include <iostream>
#include <type_traits>

template <typename T>
void write_scalar(FILE *file, const T &val)
{
    if constexpr (std::is_same_v<T, float>)
    {
        fprintf(file, "%.6f", val);
    }
    else if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, int16_t> || std::is_integral_v<T>)
    {
        fprintf(file, "%d", static_cast<int>(val));
    }
    else
    {
        fprintf(stderr, "Unsupported input type for writing!\n");
        fprintf(file, "ERR");
    }
}

void write_data_csv(Channel &channel, const std::string &filename)
{
    try
    {
        FILE *buffer_output_file = fopen(filename.c_str(), "w");
        if (!buffer_output_file)
        {
            std::cerr << "Error opening buffer output file.\n";
            return;
        }

        while (true)
        {
            if (sem_wait(&channel.data_sem_csv) != 0)
            {
                if (errno == EINTR && stop_program.load())
                    break;
                continue;
            }

            while (!channel.data_queue_csv.empty())
            {
                std::shared_ptr<data_part_t> part = channel.data_queue_csv.front();
                channel.data_queue_csv.pop();

                for (size_t k = 0; k < MODEL_INPUT_DIM_0; k++)
                {
                    write_scalar(buffer_output_file, part->data[k][0]);
                    if (k < MODEL_INPUT_DIM_0 - 1)
                        fprintf(buffer_output_file, ",");
                }

                fprintf(buffer_output_file, "\n");
                fflush(buffer_output_file);

                channel.write_count_csv.fetch_add(1, std::memory_order_relaxed);
            }

            if (channel.acquisition_done && channel.data_queue_csv.empty())
                break;
        }

        fclose(buffer_output_file);
        std::cout << "Data writing on CSV thread on channel " << static_cast<int>(channel.channel_id) + 1 << " exiting..." << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception in write_data_csv for channel " << static_cast<int>(channel.channel_id) + 1 << ": " << e.what() << std::endl;
    }
}

