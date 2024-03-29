#pragma once

#include <string>
#include <vector>

void get_chunk_sizes(std::string filename, std::vector<uint64_t>& chunk_sizes);

std::string get_chunk_filename(std::string& input_filename);

void* get_mem(std::string encoded_filename, size_t& len);

int get_next_frame_start(uint8_t* data, uint32_t offset, uint32_t data_len, uint8_t& start_len);

void write_send_results_to_file(const std::string& filename, 
    const size_t bytes, const uint64_t diff);

void write_receive_results_to_file(const std::string& filename,
    const size_t bytes, const size_t packets, const uint64_t diff_ms);

void write_latency_results_to_file(const std::string& filename,
    const size_t frames, const float intra, const float inter, const float avg);

bool get_srtp_state(std::string srtp);

bool get_vvc_state(std::string format);

bool get_atlas_state(std::string format);