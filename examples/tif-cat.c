#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <lfp/lfp.h>
#include <lfp/tapeimage.h>

int main(int args, char** argv) {
    if (args < 2) {
        fputs("usage: tif-cat FILE", stderr);
        exit(EXIT_FAILURE);
    }

    FILE* fp = fopen(argv[1], "rb");
    if (!fp) {
        perror("unable to open file");
        exit(EXIT_FAILURE);
    }

    lfp_protocol* cfile = lfp_cfile(fp);
    if (!cfile) exit(EXIT_FAILURE);

    lfp_protocol* tfile = lfp_tapeimage_open(cfile);
    if (!tfile) {
        lfp_close(cfile);
        exit(EXIT_FAILURE);
    }

    /*
     * This is identical to cat.c, but with tfile instead of cfile
     */
    unsigned char buf[1024];
    for (;;) {
        int64_t nread;
        int err = lfp_readinto(tfile, buf, sizeof(buf), &nread);
        switch (err) {
            case LFP_OK:
            case LFP_OKINCOMPLETE:
            case LFP_EOF:
                break;

            default:
                perror(lfp_errormsg(tfile));
                lfp_close(tfile);
                exit(EXIT_FAILURE);
        }

        fwrite(buf, 1, nread, stdout);

        if (err == LFP_EOF) {
            lfp_close(tfile);
            return EXIT_SUCCESS;
        }
    }
}
