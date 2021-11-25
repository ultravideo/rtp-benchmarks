#include <kvazaar.h>

#include "util.hh"

#include <iostream>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include <stdlib.h>


int kvazaar_encode(const std::string& input, const std::string& output, const std::string& memory_filename,
    int width, int height, int qp, int fps, int period, std::string& preset);

bool encode_frame(kvz_picture* input, int& rvalue, std::ofstream& outputFile, std::ofstream& memoryFile,
    const kvz_api* api, kvz_encoder* enc);
void cleanup_kvazaar(kvz_picture* input, const kvz_api* api, kvz_encoder* enc, kvz_config* config);

int main(int argc, char** argv)
{
    if (argc != 8) {
        fprintf(stderr, "usage: ./%s <filename> <width> <height> <qp> <fps> <period> <preset>\n", __FILE__);
        return EXIT_FAILURE;
    }

    std::string input_file = argv[1];
    std::string output_file = argv[1];
    
    // remove any possible file extensions and add hevc
    size_t lastindex = input_file.find_last_of(".");
    if (lastindex != std::string::npos)
    {
        output_file = input_file.substr(0, lastindex);
    }

    std::string mem_file = get_chunk_filename(input_file);

    // add hevc file ending
    output_file = output_file + ".hevc";

    if (input_file == output_file)
    {
        output_file = "out_" + output_file;
    }

    int width = atoi(argv[2]);
    int height = atoi(argv[3]);

    int qp = atoi(argv[4]);
    int fps = atoi(argv[5]);
    int intra_period = atoi(argv[6]);

    std::string preset = argv[7];

    if (!width || !height || !qp || !fps || !intra_period || preset == "")
    {
        std::cerr << "Invalid command line arguments" << std::endl;
        return EXIT_FAILURE;
    }

    return kvazaar_encode(input_file, output_file, mem_file, width, height, qp, fps, intra_period, preset);
}

int kvazaar_encode(const std::string& input, const std::string& output, const std::string& memory_filename,
    int width, int height, int qp, int fps, int period, std::string& preset)
{
    std::cout << "Opening files. Input: " << input << " Output: " << output << std::endl;
    std::cout << "Parameters. Res: " << width << "x" << height << " fps: " << fps << " qp: " << qp << std::endl;

    std::ifstream inputFile (input,  std::ios::in  | std::ios::binary);
    std::ofstream outputFile(output, std::ios::out | std::ios::binary);
    std::ofstream memoryFile(memory_filename, std::ios::out | std::ios::binary);

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

        return EXIT_FAILURE;
    }

    if (!outputFile.good() || !memoryFile.good())
    {
        if (outputFile.eof() || memoryFile.eof())
        {
            std::cerr << "Output eof before starting" << std::endl;
        }
        else if (outputFile.bad() || memoryFile.bad())
        {
            std::cerr << "Output bad before starting" << std::endl;
        }
        else if (outputFile.fail() || memoryFile.fail())
        {
            std::cerr << "Output fail before starting" << std::endl;
        }

        return EXIT_FAILURE;
    }

    kvz_encoder* enc = NULL;
    const kvz_api* api = kvz_api_get(8);
    kvz_config* config = api->config_alloc();
    api->config_init(config);
    api->config_parse(config, "preset", preset.c_str());
    config->width = width;
    config->height = height;
    config->hash = kvz_hash::KVZ_HASH_NONE;
    config->intra_period = period;
    config->qp = qp;
    config->framerate_num = fps;
    config->framerate_denom = 1;

    enc = api->encoder_open(config);
    kvz_picture* img_in = api->picture_alloc(width, height);

    if (!enc || !img_in) {
        std::cerr << "Failed to open kvazaar encoder!" << std::endl;
        inputFile.close();
        outputFile.close();
        memoryFile.close();
        cleanup_kvazaar(img_in, api, enc, config);
        return EXIT_FAILURE;
    }

    int frame_count = 1;
    bool input_has_been_read = false;

    while (!input_has_been_read) {

        if (!inputFile.read((char*)img_in->y, width * height) ||
            !inputFile.read((char*)img_in->u, width * height/4) ||
            !inputFile.read((char*)img_in->v, width * height/4)) {

            if (inputFile.eof())
            {
                std::cout << "End of input file reached" << std::endl;
            }
            else if (inputFile.bad())
            {
                std::cerr << "Input bad" << std::endl;
            }
            else if (inputFile.fail())
            {
                std::cerr << "Input fails" << std::endl;
            }
            else
            {
                std::cerr << "Unknown Input error" << std::endl;
            }

            input_has_been_read = true;
            inputFile.close();
            continue;
        }

        std::cout << "Start encoding frame " << frame_count << std::endl;
        ++frame_count;

        // feed input to kvazaar and write output to file
        int rvalue = EXIT_FAILURE;
        if (!encode_frame(img_in, rvalue, outputFile, memoryFile, api, enc))
        {
            // if encoding fails
            outputFile.close();
            memoryFile.close();
            cleanup_kvazaar(img_in, api, enc, config);
            return rvalue;
        }
    }

    // write the rest of the frames that are being encoded to file
    int rvalue = EXIT_FAILURE;
    while (encode_frame(nullptr, rvalue, outputFile, memoryFile, api, enc));

    memoryFile.close();
    outputFile.close();
    cleanup_kvazaar(img_in, api, enc, config);
    return rvalue;
}

void cleanup_kvazaar(kvz_picture* input, const kvz_api* api, kvz_encoder* enc, kvz_config* config)
{
    if (api)
    {
        if (enc)
        {
            api->encoder_close(enc);
        }

        if (config)
        {
            api->config_destroy(config);
        }

        if (input)
        {
            api->picture_free(input);
        }
    }
}

bool encode_frame(kvz_picture* input, int& rvalue, std::ofstream& outputFile, std::ofstream& memoryFile, 
    const kvz_api* api, kvz_encoder* enc)
{
    kvz_picture* img_rec = nullptr;
    kvz_picture* img_src = nullptr;
    uint32_t len_out = 0;
    kvz_frame_info info_out;
    kvz_data_chunk* chunks_out = nullptr;

    if (!api->encoder_encode(enc, input, &chunks_out, &len_out, &img_rec, &img_src, &info_out))
    {
        fprintf(stderr, "Failed to encode image.\n");
        for (uint32_t i = 0; i < 16; i++) {
            api->picture_free(input);
        }
        rvalue = EXIT_FAILURE;
        return false;
    }

    if (chunks_out == nullptr && input == nullptr) {
        // We are done since there is no more input or output left.
        rvalue = EXIT_SUCCESS;
        return false;
    }

    if (chunks_out != NULL) {
        uint64_t written = 0;

        // Calculate the total size of the chunks
        for (kvz_data_chunk* chunk = chunks_out; chunk != nullptr; chunk = chunk->next) {
            written += chunk->len;
        }

        std::cout << "Write the size of the chunk: " << written << std::endl;
        memoryFile.write((char*)(&written), sizeof(uint64_t));

        // write the chunks into the file
        for (kvz_data_chunk* chunk = chunks_out; chunk != nullptr; chunk = chunk->next) {

            outputFile.write((char*)(chunk->data), chunk->len);
        }
    }

    return true;
}