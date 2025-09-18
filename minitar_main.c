#include <stdio.h>
#include <string.h>

#include "file_list.h"
#include "minitar.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: %s -c|a|t|u|x -f ARCHIVE [FILE...]\n", argv[0]);
        return 0;
    }

    file_list_t files;
    file_list_init(&files);

    // TODO: Parse command-line arguments and invoke functions from 'minitar.h'
    // to execute archive operations

    char *operation = argv[1];
    if (strcmp(argv[2], "-f") != 0) {
        fprintf(stderr, "Expected -f flag\n");
        return 1;
    }
    char *archive_name = argv[3];

    for (int i = 4; i < argc; i++) {
        file_list_add(&files, argv[i]);
    }

    if (strcmp(operation, "-c") == 0) {
        int create_archive_result = create_archive(archive_name, &files);
        if (0 != create_archive_result) {
            fprintf(stderr, "Failed to create archive\n");
            file_list_clear(&files);
            return 1;
        }
    } else if (strcmp(operation, "-a") == 0) {
        FILE *archive_fp = fopen(archive_name, "r");
        if (NULL == archive_fp) {
            fprintf(stderr, "Failed to open archive\n");
            return 1;
        } else {
            append_files_to_archive(archive_name, &files);
            if (0 != fclose(archive_fp)) {
                fprintf(stderr, "Failed to close archive");
                return 1;
            }
        }

    } else if (strcmp(operation, "-t") == 0) {
        // call get_archive_file_list then print the list out here
    } else if (strcmp(operation, "-u") == 0) {
        // check if file is contained in archive file, then call
        // append_files_to_archive
    } else if (strcmp(operation, "-x") == 0) {
        extract_files_from_archive(archive_name);
    } else {
        printf("Usage: %s -c|a|t|u|x -f ARCHIVE [FILE...]\n", argv[0]);
        return 0;
    }

    file_list_clear(&files);
    return 0;
}
