#ifndef WORKLOAD_HPP
#define WORKLOAD_HPP

#include <stdexcept>
#include <memory>

struct Workload {
    Workload() : MAX_TIMESTEPS(1'000'000), 
                 MAX_CELL_UPATES(10'000'000), 
                 CELL_COUNT_THRESHOLD(1'000'000),
                 MEGABYTE_THRESHOLD(MEGABYTES * 4) {}

    Workload(std::size_t MAX_TIMESTEPS_,
             std::size_t MAX_CELL_UPATES_, 
             int CELL_COUNT_THRESHOLD_, 
             int MEGABYTE_THRESHOLD_) 
             : Workload() 
             {
                set_max_timesteps(MAX_TIMESTEPS_);
                set_max_cell_updates(MAX_CELL_UPATES_);
                set_max_cell_threshold(CELL_COUNT_THRESHOLD_);
                set_max_megabyte_threshold(MEGABYTE_THRESHOLD_);
             }

    void set_max_timesteps(std::size_t max) {
        if(max >= 1) MAX_TIMESTEPS = max;
        else {
            throw std::invalid_argument("Workload: max timesteps must be >= 1.");
        }
    }

    void set_max_cell_updates(std::size_t max) {
        if(max >= 1) MAX_CELL_UPATES = max;
        else {
            throw std::invalid_argument("Workload: max cell updates must be >= 1.");
        }
    }

    void set_max_cell_threshold(int max) {
        if(max >= 1) CELL_COUNT_THRESHOLD = max;
        else {
            throw std::invalid_argument("Workload: max cell count must be >= 1.");
        }
    }

    void set_max_megabyte_threshold(int max) {
        if(max >= 1) MEGABYTE_THRESHOLD = max * MEGABYTES;
        else {
            throw std::invalid_argument("Workload: max megabyte count must be >= 1.");
        }
    }

    std::size_t get_max_timesteps() const { return MAX_TIMESTEPS; }
    std::size_t get_max_cell_updates() const { return MAX_CELL_UPATES; }
    int get_cell_count_threshold() const { return CELL_COUNT_THRESHOLD; }
    int get_megabyte_threshold() const { return MEGABYTE_THRESHOLD; }

private:
    static constexpr int MEGABYTES = 1024 * 1024;
    std::size_t MAX_TIMESTEPS;
    std::size_t MAX_CELL_UPATES;
    int CELL_COUNT_THRESHOLD;
    int MEGABYTE_THRESHOLD;
};

#endif 