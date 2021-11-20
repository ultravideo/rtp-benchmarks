#include <kvazaar.h>

#include <iostream>
#include <string>
#include <cstdio>
#include <cstdlib>

#include <stdlib.h>


int kvazaar_encode(const std::string& input, const std::string& output, 
    int width, int height, int qp, int fps, int period, std::string& preset);

void cleanup(FILE* inputFile, FILE* outputFile);

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

    return kvazaar_encode(input_file, output_file, width, height, qp, fps, intra_period, preset);
}

int kvazaar_encode(const std::string& input, const std::string& output, 
    int width, int height, int qp, int fps, int period, std::string& preset)
{
    std::cout << "Opening files. Input: " << input << " output: " << output << std::endl;
    FILE* inputFile = fopen(input.c_str(), "r");
    FILE* outputFile = fopen(output.c_str(), "w");

    if (inputFile == NULL || outputFile == NULL)
    {
        std::cerr << "Failed to open input or output file!" << std::endl;
        cleanup(inputFile, outputFile);
        return EXIT_FAILURE;
    }

    kvz_encoder* enc = NULL;
    const kvz_api* const api = kvz_api_get(8);
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
    if (!enc) {
        std::cerr << "Failed to open kvazaar encoder!" << std::endl;
        cleanup(inputFile, outputFile);
        return EXIT_FAILURE;
    }

    kvz_picture* img_in[16];
    for (uint32_t i = 0; i < 16; ++i) {
        img_in[i] = api->picture_alloc_csp(KVZ_CSP_420, width, height);
    }

    uint8_t inputCounter = 0;
    uint8_t outputCounter = 0;
    bool done = false;
    /* int r = 0; */

    std::cout << "Start creating the HEVC benchmark file from " << input << std::endl;

    while (!done) {
        kvz_data_chunk* chunks_out = NULL;
        kvz_picture* img_rec = NULL;
        kvz_picture* img_src = NULL;
        uint32_t len_out = 0;
        kvz_frame_info info_out;

        if (!fread(img_in[inputCounter]->y, width * height, 1, inputFile)) {
            done = true;
            continue;
        }
        if (!fread(img_in[inputCounter]->u, width * height >> 2, 1, inputFile)) {
            done = true;
            continue;
        }
        if (!fread(img_in[inputCounter]->v, width * height >> 2, 1, inputFile)) {
            done = true;
            continue;
        }

        if (!api->encoder_encode(enc,
            img_in[inputCounter],
            &chunks_out, &len_out, &img_rec, &img_src, &info_out))
        {
            fprintf(stderr, "Failed to encode image.\n");
            for (uint32_t i = 0; i < 16; i++) {
                api->picture_free(img_in[i]);
            }
            cleanup(inputFile, outputFile);
            return EXIT_FAILURE;
        }
        inputCounter = (inputCounter + 1) % 16;


        if (chunks_out == NULL && img_in == NULL) {
            // We are done since there is no more input and output left.
            cleanup(inputFile, outputFile);
            std::cout << "No more input or output. Finished creating the HEVC benchmark file to " << output << std::endl;
            return EXIT_SUCCESS;
        }

        if (chunks_out != NULL) {
            uint64_t written = 0;

            // Write data into the output file.
            for (kvz_data_chunk* chunk = chunks_out; chunk != NULL; chunk = chunk->next) {
                written += chunk->len;
            }

            fprintf(stderr, "write chunk size: %lu\n", written);
            fwrite(&written, sizeof(uint64_t), 1, outputFile);
            for (kvz_data_chunk* chunk = chunks_out; chunk != NULL; chunk = chunk->next) {
                fwrite(chunk->data, chunk->len, 1, outputFile);
            }

            outputCounter = (outputCounter + 1) % 16;

            /* if (++r > 5) */
            /*     goto cleanup; */
        }
    }

    std::cout << "Finished creating the HEVC benchmark file to " << output << std::endl;

    cleanup(inputFile, outputFile);

    return EXIT_SUCCESS;
}

void cleanup(FILE* inputFile, FILE* outputFile)
{
    if (inputFile)
    {
        fclose(inputFile);
    }

    if (outputFile)
    {
        fclose(outputFile);
    }
}