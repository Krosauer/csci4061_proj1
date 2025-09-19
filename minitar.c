#include "minitar.h"

#include <fcntl.h>
#include <grp.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

#define NUM_TRAILING_BLOCKS 2
#define MAX_MSG_LEN 128
#define BLOCK_SIZE 512

// Constants for tar compatibility information
#define MAGIC "ustar"

// Constants to represent different file types
// We'll only use regular files in this project
#define REGTYPE '0'
#define DIRTYPE '5'

/*
 * Helper function to compute the checksum of a tar header block
 * Performs a simple sum over all bytes in the header in accordance with POSIX
 * standard for tar file structure.
 */
void compute_checksum(tar_header *header) {
    // Have to initially set header's checksum to "all blanks"
    memset(header->chksum, ' ', 8);
    unsigned sum = 0;
    char *bytes = (char *) header;
    for (int i = 0; i < sizeof(tar_header); i++) {
        sum += bytes[i];
    }
    snprintf(header->chksum, 8, "%07o", sum);
}

/*
 * Populates a tar header block pointed to by 'header' with metadata about
 * the file identified by 'file_name'.
 * Returns 0 on success or -1 if an error occurs
 */
int fill_tar_header(tar_header *header, const char *file_name) {
    memset(header, 0, sizeof(tar_header));
    char err_msg[MAX_MSG_LEN];
    struct stat stat_buf;
    // stat is a system call to inspect file metadata
    if (stat(file_name, &stat_buf) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to stat file %s", file_name);
        perror(err_msg);
        return -1;
    }

    strncpy(header->name, file_name, 100);    // Name of the file, null-terminated string
    snprintf(header->mode, 8, "%07o",
             stat_buf.st_mode & 07777);    // Permissions for file, 0-padded octal

    snprintf(header->uid, 8, "%07o", stat_buf.st_uid);    // Owner ID of the file, 0-padded octal
    struct passwd *pwd = getpwuid(stat_buf.st_uid);       // Look up name corresponding to owner ID
    if (pwd == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up owner name of file %s", file_name);
        perror(err_msg);
        return -1;
    }
    strncpy(header->uname, pwd->pw_name, 32);    // Owner name of the file, null-terminated string

    snprintf(header->gid, 8, "%07o", stat_buf.st_gid);    // Group ID of the file, 0-padded octal
    struct group *grp = getgrgid(stat_buf.st_gid);        // Look up name corresponding to group ID
    if (grp == NULL) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to look up group name of file %s", file_name);
        perror(err_msg);
        return -1;
    }
    strncpy(header->gname, grp->gr_name, 32);    // Group name of the file, null-terminated string

    snprintf(header->size, 12, "%011o",
             (unsigned) stat_buf.st_size);    // File size, 0-padded octal
    snprintf(header->mtime, 12, "%011o",
             (unsigned) stat_buf.st_mtime);    // Modification time, 0-padded octal
    header->typeflag = REGTYPE;                // File type, always regular file in this project
    strncpy(header->magic, MAGIC, 6);          // Special, standardized sequence of bytes
    memcpy(header->version, "00", 2);          // A bit weird, sidesteps null termination
    snprintf(header->devmajor, 8, "%07o",
             major(stat_buf.st_dev));    // Major device number, 0-padded octal
    snprintf(header->devminor, 8, "%07o",
             minor(stat_buf.st_dev));    // Minor device number, 0-padded octal

    compute_checksum(header);
    return 0;
}

/*
 * Removes 'nbytes' bytes from the file identified by 'file_name'
 * Returns 0 upon success, -1 upon error
 * Note: This function uses lower-level I/O syscalls (not stdio), which we'll learn about later
 */
int remove_trailing_bytes(const char *file_name, size_t nbytes) {
    char err_msg[MAX_MSG_LEN];

    struct stat stat_buf;
    if (stat(file_name, &stat_buf) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to stat file %s", file_name);
        perror(err_msg);
        return -1;
    }

    off_t file_size = stat_buf.st_size;
    if (nbytes > file_size) {
        file_size = 0;
    } else {
        file_size -= nbytes;
    }

    if (truncate(file_name, file_size) != 0) {
        snprintf(err_msg, MAX_MSG_LEN, "Failed to truncate file %s", file_name);
        perror(err_msg);
        return -1;
    }
    return 0;
}

// Helper to do the adding 2 blocks of 512
int write_end_blocks(FILE *archive_fp) {
    char zero_block[BLOCK_SIZE] = {0};

    int write_result = fwrite(zero_block, 1, BLOCK_SIZE, archive_fp);
    if (BLOCK_SIZE != write_result) {
        perror("Failure writing first zero block to archive file");
        return 1;
    }

    write_result = fwrite(zero_block, 1, BLOCK_SIZE, archive_fp);
    if (BLOCK_SIZE != write_result) {
        perror("Failure writing second zero block to archive file");
        return 1;
    }
    return 0;
}

int write_files(FILE *archive_fp, const file_list_t *files) {
    node_t *ptr = files->head;
    int archive_close_result = 0;
    int input_close_result = 0;
    // Traverse file list
    while (NULL != ptr) {
        tar_header header;
        const char *file_name = ptr->name;

        // Attempt to create header
        int header_result = fill_tar_header(&header, file_name);
        if (0 != header_result) {
            // Does this need to be error checked?
            archive_close_result = fclose(archive_fp);
            return 1;
        }

        // Attempt to write header to archive file
        int write_result = fwrite(&header, sizeof(tar_header), 1, archive_fp);
        if (1 != write_result) {
            perror("Failed to write header to archive file");
            archive_close_result = fclose(archive_fp);
            return 1;
        }

        // Attempt to open input file
        FILE *input_fp = fopen(file_name, "rb");
        if (NULL == input_fp) {
            perror("Failed to open input file for read");
            archive_close_result = fclose(archive_fp);
            return 1;
        }

        char buffer[BLOCK_SIZE];
        size_t bytes_read;

        // Read and write file contents in 512-byte blocks
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), input_fp)) > 0) {
            // Check if what we read in was less than 512 -> need to pad
            if (bytes_read < sizeof(buffer)) {
                memset(buffer + bytes_read, 0, sizeof(buffer) - bytes_read);
            }

            // Write the full 512-byte block
            size_t bytes_wrote = fwrite(buffer, 1, sizeof(buffer), archive_fp);
            if (bytes_wrote != sizeof(buffer)) {
                perror("Failure writing to archive file");
                fclose(input_fp);
                fclose(archive_fp);
                return 1;
            }
        }

        input_close_result = fclose(input_fp);
        if (0 != input_close_result) {
            perror("Failure closing input file");
            fclose(archive_fp);
            return 1;
        }

        ptr = ptr->next;
    }
    if (0 != archive_close_result) {
        perror("Failure closing archive file");
        return -1;
    }

    return 0;
}


int create_archive(const char *archive_name, const file_list_t *files) {
    FILE *archive_fp = fopen(archive_name, "wb");
    int archive_close_result = 0;

    if (NULL == archive_fp) {
        perror("Error opening archive file for write");
        return 1;
    }

    // Attempt to write the files
    int write_files_result = write_files(archive_fp, files);
    if (0 != write_files_result) {
        perror("Error writing files");
        return 1;
    }
    // Data should have been written, now we need to add the 2 blocks of padding
    int add_zero_block_result = write_end_blocks(archive_fp);
    if (0 != add_zero_block_result) {
        archive_close_result = fclose(archive_fp);
        return 1;
    }
    // Close archive fp
    archive_close_result = fclose(archive_fp);
    if (0 != archive_close_result) {
        perror("Failure closing archive file");
        return 1;
    }

    return 0;
}


int append_files_to_archive(const char *archive_name, const file_list_t *files) {
    // First check that archive exists
    FILE *check_archive_fp = fopen(archive_name, "rb");
    int archive_close_result = 0;
    if (check_archive_fp == NULL) {
        perror("Archive file does not exist");
        return 1;
    }
    archive_close_result = fclose(check_archive_fp);

    // Remove the footer (two 512-byte zero blocks)
    if (remove_trailing_bytes(archive_name, 1024) != 0) {
        perror("Error removing bytes");
        return 1;
    }

    // Atempt to open archive
    FILE *archive_fp = fopen(archive_name, "r+b");
    if (archive_fp == NULL) {
        perror("Failure opening archive file");
        return 1;
    }

    // We removed the footer but now we need to position
    // the fp at the end so that it is in position
    // to start writing the files
    int seek_result = fseek(archive_fp, 0, SEEK_END);
    if (0 != seek_result) {
        perror("Failure seeking archive file");
        return 1;
    }

    // Do the adding of files
    int write_files_result = write_files(archive_fp, files);
    if (0 != write_files_result) {
        perror("Error writing files");
        return 1;
    }

    // Now add new footer
    int add_zero_block_result = write_end_blocks(archive_fp);
    if (0 != add_zero_block_result) {
        fclose(archive_fp);
        return 1;
    }

    // Close archive fp
    archive_close_result = fclose(archive_fp);
    if (0 != archive_close_result) {
        perror("Failure closing archive file");
        return 1;
    }

    return 0;
}

int get_archive_file_list(const char *archive_name, file_list_t *files) {
    // TODO: Not yet implemented

    // Opens Archive and checks if it exists.
    FILE *fp = fopen(archive_name, "rb");
    if (fp == NULL) {
        perror("Archive file does not exist");
        return 1;
    }

    char buffer[BLOCK_SIZE];

    int read_result;
    int seek_result;

    //loops through archive and adds each file name to the files input value

    while(fp != NULL){
        //Is this the correct way to read in the blocks header and get each individual file name?
        read_result = fread(buffer, 1, sizeof(buffer), fp);
        if(read_result <= 0){
        perror("Error reading file");
        return 1;
        }

        file_list_add(files, buffer);

        //This while loop needs to be fixed, how do I determine when I'm at a header block again?
        while(fp){
            seek_result = fseek(fp, SEEK_CUR, BLOCK_SIZE);

            //breaks if fp reaches end of file
            if(fp == NULL){
                break;
            }
            if(seek_result == -1){
                perror("Error seeking on file");
                return 1;
            }
        }

    }




    return 0;
}

int extract_files_from_archive(const char *archive_name) {
    // TODO: Not yet implemented
    return 0;
}
