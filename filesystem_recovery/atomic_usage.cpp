/* mbed Microcontroller Library
 * Copyright (c) 2017-2017 ARM Limited
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


/**
 * # Defined behavior
 * - A file rename is atomic (Note - rename can be used to replace a file)
 * - A directory rename is atomic (Note - rename can be used to replace an empty directory)
 * - Directory create is atomic
 * - Directory delete is atomic
 * - File create is atomic
 * - File delete is atomic
 * - File contents are atomically written on close
 *
 * # Undefined behavior
 */

#include "mbed.h"
#include "greentea-client/test_env.h"
#include "unity.h"
#include "utest.h"
#include <stdlib.h>
#include <errno.h>

#include "ObservingBlockDevice.h"
#include "LittleFileSystem.h"
#include "ExhaustibleBlockDevice.h"

#define DEBUG                   printf
#define DEBUG_CHECK(...)

#define ARRAY_LENGTH(array)     (sizeof(array) / sizeof(array[0]))

using namespace utest::v1;

typedef void (*test_function_t)(LittleFileSystem *fs);
typedef bool (*test_function_bool_t)(LittleFileSystem *fs);

struct TestEntry {
    const char *name;
    test_function_t setup;
    test_function_bool_t perform;
    test_function_t check;
};


static uint8_t buffer[1024];
static uint8_t check_buffer[1024];


#define FILE_RENAME_A           "file_to_rename_a.txt"
#define FILE_RENAME_B           "file_to_rename_b.txt"
#define FILE_RENAME_CONTENTS    "Test contents for the file to be renamed"
#define FILE_RENAME_LEN         (sizeof(FILE_RENAME_CONTENTS) - 1)

/**
 * Setup for the file rename test
 *
 * Create file FILE_RENAME_A with contents FILE_RENAME_CONTENTS.
 */
void setup_file_rename(LittleFileSystem *fs)
{
    DEBUG("setup_file_rename()\r\n");

    File file;

    int res = file.open(fs, FILE_RENAME_A, O_WRONLY | O_CREAT);
    DEBUG("  open result %i\r\n", res);
    TEST_ASSERT_EQUAL(0, res);

    res = file.write(FILE_RENAME_CONTENTS, FILE_RENAME_LEN);
    DEBUG("  write result %i\r\n", res);
    TEST_ASSERT_EQUAL(FILE_RENAME_LEN, res);
}

/**
 * Change the file name to either FILE_RENAME_A or FILE_RENAME_B
 */
bool perform_file_rename(LittleFileSystem *fs)
{
    DEBUG("perform_file_rename()\r\n");

    struct stat st;
    int res = fs->stat(FILE_RENAME_A, &st);
    const char *src = (0 == res) ? FILE_RENAME_A : FILE_RENAME_B;
    const char *dst = (0 == res) ? FILE_RENAME_B : FILE_RENAME_A;

    DEBUG("  stat result  %i\r\n", res);
    TEST_ASSERT((res == -ENOENT) || (0 == res));

    DEBUG("  Renaming %s to %s\r\n", src, dst);
    res = fs->rename(src, dst);
    if (-ENOSPC  == res) {
        return true;
    }
    TEST_ASSERT_EQUAL(0, res);
    return false;
}

/**
 * Check that the file rename is in a good state
 *
 * Check that there is only one file and that file containts the correct
 * contents.
 */
void check_file_rename(LittleFileSystem *fs)
{

    int files = 0;
    int valids = 0;
    const char * const filenames[] = {FILE_RENAME_A, FILE_RENAME_B};

    for (int i = 0; i < 2; i++) {
        File file;
        if (0 == file.open(fs, filenames[i], O_RDONLY)) {
            files++;
            memset(check_buffer, 0, sizeof(check_buffer));
            int res = file.read(check_buffer, FILE_RENAME_LEN);
            if (res != FILE_RENAME_LEN) {
                break;
            }
            if (memcmp(check_buffer, FILE_RENAME_CONTENTS, FILE_RENAME_LEN) != 0) {
                break;
            }
            valids++;
        }
    }

    TEST_ASSERT_EQUAL(1, files);
    TEST_ASSERT_EQUAL(1, valids);
}



#define FILE_RENAME_REPLACE     "rename_replace_file.txt"
#define FILE_RENAME_REPLACE_NEW "new_rename_replace_file.txt"
#define FILE_RENAME_REPLACE_FMT "file replace count: %lu\r\n"

/**
 * Create the file FILE_RENAME_REPLACE with initial contents
 */
void setup_file_rename_replace(LittleFileSystem *fs)
{
    DEBUG("setup_file_rename_replace()\r\n");
    File file;

    // Write out initial count

    int res = file.open(fs, FILE_RENAME_REPLACE, O_WRONLY | O_CREAT);
    TEST_ASSERT_EQUAL(0, res);

    uint32_t count = 0;
    memset(buffer, 0, sizeof(buffer));
    const int length = sprintf((char*)buffer, FILE_RENAME_REPLACE_FMT, count);
    TEST_ASSERT(length > 0);

    res = file.write(buffer, length);
    DEBUG("  write result %i\r\n", res);
    TEST_ASSERT_EQUAL(length, res);
}

/**
 * Atomically increment the count in FILE_RENAME_REPLACE using a rename
 */
bool perform_file_rename_replace(LittleFileSystem *fs)
{
    DEBUG("perform_file_rename_replace()\r\n");
    File file;

    // Read in previous count

    int res = file.open(fs, FILE_RENAME_REPLACE, O_RDONLY);
    TEST_ASSERT_EQUAL(0, res);

    memset(buffer, 0, sizeof(buffer));
    res = file.read(buffer, sizeof(buffer));
    TEST_ASSERT(res > 0);

    uint32_t count;
    res = sscanf((char*)buffer, FILE_RENAME_REPLACE_FMT, &count);
    TEST_ASSERT_EQUAL(1, res);

    file.close();

    // Write out new count

    count++;
    memset(buffer, 0, sizeof(buffer));
    const int length = sprintf((char*)buffer, FILE_RENAME_REPLACE_FMT, count);
    TEST_ASSERT(length > 0);

    res = file.open(fs, FILE_RENAME_REPLACE_NEW, O_WRONLY | O_CREAT);
    if (-ENOSPC  == res) {
        return true;
    }
    TEST_ASSERT_EQUAL(0, res);

    res = file.write(buffer, length);
    if (-ENOSPC  == res) {
        return true;
    }
    TEST_ASSERT_EQUAL(length, res);

    file.close();

    // Rename file

    res = fs->rename(FILE_RENAME_REPLACE_NEW, FILE_RENAME_REPLACE);
    if (-ENOSPC  == res) {
        return true;
    }
    TEST_ASSERT_EQUAL(0, res);
    DEBUG("  count %lu -> %lu\r\n", count - 1, count);

    return false;
}

/**
 * Check that FILE_RENAME_REPLACE always has a valid count
 */
void check_file_rename_replace(LittleFileSystem *fs)
{
    DEBUG_CHECK("check_file_rename_replace()\r\n");
    File file;

    // Read in previous count

    int res = file.open(fs, FILE_RENAME_REPLACE, O_RDONLY);
    TEST_ASSERT_EQUAL(0, res);

    memset(check_buffer, 0, sizeof(check_buffer));
    res = file.read(check_buffer, sizeof(check_buffer));
    TEST_ASSERT(res > 0);

    uint32_t count;
    res = sscanf((char*)check_buffer, FILE_RENAME_REPLACE_FMT, &count);
    TEST_ASSERT_EQUAL(1, res);
    DEBUG_CHECK("  count %lu\r\n", count);
}


//void directory_rename()
//{
//
//}
//
//void directory_rename_replace()
//{
//
//}
//
//void file_append_and_rename()
//{
//
//}
//
//void file_change_contents()
//{
//
//}
//
//void file_create_delete()
//{
//
//}

const TestEntry atomic_test_entries[] = {
    {"File rename", setup_file_rename, perform_file_rename, check_file_rename},
    {"File rename replace", setup_file_rename_replace, perform_file_rename_replace, check_file_rename_replace}
};

void setup_atomic_operations(BlockDevice *bd, bool force_rebuild)
{
    LittleFileSystem fs("fs");

    if (force_rebuild || (fs.mount(bd) != 0)) {
        int res = fs.format(bd);
        TEST_ASSERT_EQUAL(0, res);
        res = fs.mount(bd);
        TEST_ASSERT_EQUAL(0, res);
    }

    for (size_t i = 0; i < ARRAY_LENGTH(atomic_test_entries); i++) {
        atomic_test_entries[i].setup(&fs);
    }

    fs.unmount();
}

bool perform_atomic_operations(BlockDevice *bd)
{
    LittleFileSystem fs("fs");
    bool out_of_space = false;

    fs.mount(bd);

    for (size_t i = 0; i < ARRAY_LENGTH(atomic_test_entries); i++) {
        out_of_space |= atomic_test_entries[i].perform(&fs);
    }

    fs.unmount();

    return out_of_space;
}

void check_atomic_operations(BlockDevice *bd)
{
    LittleFileSystem fs("fs");
    fs.mount(bd);

    for (size_t i = 0; i < ARRAY_LENGTH(atomic_test_entries); i++) {
        atomic_test_entries[i].check(&fs);
    }

    fs.unmount();
}

void change_callback(BlockDevice *bd)
{
    check_atomic_operations(bd);
}


int main()
{

//    printf("Program running\r\n");
//    HeapBlockDevice bd(16 * 1024);
//    bd.init();
//    setup_atomic_operations(&bd, true);
//    printf("setup_atomic_operations\r\n");
//
//    ObservingBlockDevice observer(&bd);
//    observer.attach(change_callback);
//
//    // Tests to run
//    // -With observer
//    // -Until flash fails
//    // -With host test and on real hardware
//    // -Over a long period of time
//    perform_atomic_operations(&observer);
//    perform_atomic_operations(&observer);
//    perform_atomic_operations(&observer);
//    perform_atomic_operations(&observer);
//
//
//    bd.deinit();


    uint8_t buf[512];

    ExhaustibleBlockDevice ebd(128 * 1024);
    ebd.set_erase_cycles(100);
    ebd.init();

    setup_atomic_operations(&ebd, true);
    printf("setup done\r\n");


    for (int i = 0; i < 100; i++) {
        bool out_of_space = perform_atomic_operations(&ebd);
        printf("Loop %i out of space %i\r\n", i, out_of_space);
        if (out_of_space) {
            break;
        }
    }

    check_atomic_operations(&ebd);
//    int i = 0;
//    while (true) {
//        printf("Loop %i\r\n", i);
//        ebd.erase(0, 4 * 1024);
//        printf("Erase\r\n");
//        memset(buf, i & 0xFF, sizeof(buf));
//        ebd.program((void*)buf, 512, sizeof(buf));
//        printf("program\r\n");
//        ebd.read((void*)buf, 512, sizeof(buf));
//        printf("read\r\n");
//        for (size_t j = 0; j < sizeof(buf); j++) {
//            if (buf[j] != (i & 0xFF)) {
//                printf("Mismatch on loop %i expected %i got %i\r\n", i, i & 0xFF, buf[j]);
//                goto end;
//            }
//        }
//        printf("End\r\n");
//        i++;
//    }
//    end:

    return 0;
}

