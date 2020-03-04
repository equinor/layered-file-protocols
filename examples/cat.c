#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <lfp/lfp.h>

int main(int args, char** argv) {
    if (args < 2) {
        fputs("usage: cat FILE", stderr);
        exit(EXIT_FAILURE);
    }

    /*
     * Open the file the standard way
     */
    FILE* fp = fopen(argv[1], "rb");
    if (!fp) {
        perror("unable to open file");
        exit(EXIT_FAILURE);
    }

    /*
     * Give the file to the cfile protocol, which takes
     * ownership of the file handle
     */
    lfp_protocol* cfile = lfp_cfile(fp);
    if (!cfile) {
        // OS will clean up fp
        exit(EXIT_FAILURE);
    }

    /*
     * Read data in chunks of 1024 bytes at the time
     */
    unsigned char buf[1024];
    for (;;) {
        int64_t nread;
        int err = lfp_readinto(cfile, buf, sizeof(buf), &nread);
        switch (err) {
            case LFP_OK:
            case LFP_OKINCOMPLETE:
            case LFP_EOF:
                /*
                 * Reading was a success
                 */
                break;

            default:
                /*
                 * Reading failed, maybe something is wrong with
                 * the device. Just print some simple diagnostic
                 * from errno and abort.
                 */
                perror(lfp_errormsg(cfile));
                lfp_close(cfile);
                exit(EXIT_FAILURE);
        }

        /*
         * Output the freshly-read data to stdout
         */
        fwrite(buf, 1, nread, stdout);

        /*
         * Incomplete read - since this is a file, not a pipe, it is
         * end-of-file, so exit
         */
        if (err == LFP_EOF) {
            lfp_close(cfile);
            return EXIT_SUCCESS;
        }
    }
}
