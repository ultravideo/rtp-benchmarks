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

void get_chunk_sizes(std::string filename, std::vector<uint64_t>& chunk_sizes)
{
    std::ifstream inputFile(filename, std::ios::in | std::ios::binary);

    if (!inputFile.good())
    {
        if (inputFile.eof())
        {
            std::cerr << "Input eof before starting" << std::endl;
        }
        else if (inputFile.bad())
        {
            std::cerr << "Input bad before starting" << std::endl;
        }
        else if (inputFile.fail())
        {
            std::cerr << "Input fail before starting" << std::endl;
        }

        return;
    }

    while (!inputFile.eof())
    {
        uint64_t chunk_size = 0;
        if (!inputFile.read((char*)&chunk_size, sizeof(uint64_t)))
        {
            break;
        }
        else
        {
            chunk_sizes.push_back(chunk_size);
        }
    }

    inputFile.close();
}

std::string get_chunk_filename(std::string& encoded_filename)
{
    std::string mem_file = "";
    std::string ending = "";
    // remove any possible file extensions and add hevc
    size_t lastindex = encoded_filename.find_last_of(".");

    if (lastindex != std::string::npos)
    {
        ending = encoded_filename.substr(lastindex + 1);
        mem_file = encoded_filename.substr(0, lastindex);
    }

    return mem_file + ".m" + ending;
}

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

void write_receive_results_to_file(const std::string& filename,
    const size_t bytes, const size_t packets, const uint64_t diff_ms)
{
    std::cout << "Writing receive results into file: " << filename << std::endl;

    std::ofstream result_file;
    result_file.open(filename, std::ios::out | std::ios::app | std::ios::ate);
    result_file << bytes << " " << packets << " " << diff_ms << std::endl;
    result_file.close();
}

void write_latency_results_to_file(const std::string& filename,
    const size_t frames, const float intra, const float inter, const float avg)
{
    std::cout << "Writing latency results into file: " << filename << std::endl;
    std::ofstream result_file;
    result_file.open(filename, std::ios::out | std::ios::app | std::ios::ate);
    result_file << frames << ";" << intra << ";" << inter << ";" << avg << std::endl;
    result_file.close();
}

bool get_srtp_state(std::string srtp)
{
    if (srtp == "1" || srtp == "yes" || srtp == "y" || srtp == "srtp")
    {
        return true;
    }

    return false;
}

bool get_vvc_state(std::string format)
{
    if (format == "vvc" || format == "h266")
    {
        return true;
    }
    else if (format != "hevc" && format != "h265" && format != "atlas" && format != "vpcc")    {
        std::cerr << "Unsupported sender format: " << format << std::endl;
    }
    return false;
}

bool get_atlas_state(std::string format)
{
    if (format == "atlas")
    {
        return true;
    }
    return false;
}