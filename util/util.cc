#include "util.hh"

#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>


void *get_mem(std::string filename, size_t& len)
{
    if (access(filename.c_str(), F_OK) == -1) {
        std::cerr << "Failed to access test file: " << filename << std::endl;
        return nullptr;
    }

    int fd = open(filename.c_str(), O_RDONLY, 0);

    if (fd < 0)
    {
        std::cerr << "Failed to open test file: " << filename << std::endl;
        return nullptr;
    }

    struct stat st;
    stat(filename.c_str(), &st);
    len = st.st_size;

    void *mem = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_POPULATE, fd, 0);

    if (mem)
    {
        madvise(mem, len, MADV_SEQUENTIAL | MADV_WILLNEED);
    }

    return mem;
}

int get_next_frame_start(uint8_t *data, uint32_t offset, uint32_t data_len, uint8_t& start_len)
{
    uint8_t zeros = 0;
    uint32_t pos  = 0;

    while (offset + pos < data_len) {
        if (zeros >= 2 && data[offset + pos] == 1) {
            start_len = zeros + 1;
            return offset + pos + 1;
        }

        if (data[offset + pos] == 0)
            zeros++;
        else
            zeros = 0;

        pos++;
    }

    return -1;
}


void write_send_results_to_file(const std::string& filename, 
    const size_t bytes, const uint64_t diff)
{
    std::cout << "Writing send results into file: " << filename << std::endl;

    std::ofstream result_file;
    result_file.open(filename, std::ios::out | std::ios::app | std::ios::ate);
    result_file << bytes << " bytes, " << bytes / 1000 << " kB, " << bytes / 1000000 << " MB took "
        << diff << " ms " << diff / 1000 << " s" << std::endl;
    result_file.close();
}