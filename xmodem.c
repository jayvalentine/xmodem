#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <sys/stat.h>

#define ACK 0x06
#define NAK 0x15
#define SOH 0x01
#define EOT 0x04
#define SUB 0x1a

uint8_t data[128];

#define XMODEM_DELAY_US (1000)

unsigned int delay;

void xmodem_wait_nak(void)
{
    while (1)
    {
        uint8_t c = getc(stdin);
        if (c == NAK) return;
    }
}

void xmodem_wait_ack(void)
{
    while (1)
    {
        uint8_t c = getc(stdin);
        if (c == ACK) return;
    }
}

void xmodem_send(uint8_t c)
{
    fputc(c, stdout);
    fflush(stdout);
    usleep(delay * XMODEM_DELAY_US);
}

int main(int argc, char ** argv)
{
    struct termios tty_stdin_opts_backup, tty_stdout_opts_backup, tty_opts_raw;

    if (argc < 3)
    {
        fprintf(stderr, "Usage: xmodem <delay> <file>\n");
        return 1;
    }

    char * endptr;
    delay = strtoul(argv[1], &endptr, 10);
    const char * filename = argv[2];

    if (!isatty(STDIN_FILENO))
    {
        fprintf(stderr, "Error: stdin is not a TTY\n");
        return 1;
    }

    if (!isatty(STDOUT_FILENO))
    {
        fprintf(stderr, "Error: stdout is not a TTY\n");
        return 1;
    }

    fprintf(stderr, "stdin is %s\n", ttyname(STDIN_FILENO));
    fprintf(stderr, "stdout is %s\n", ttyname(STDOUT_FILENO));
    fprintf(stderr, "Opening '%s'...\n", filename);

    // Back up current TTY settings
    tcgetattr(STDIN_FILENO, &tty_stdin_opts_backup);
    tcgetattr(STDOUT_FILENO, &tty_stdout_opts_backup);

    // Change TTY settings to raw mode
    cfmakeraw(&tty_opts_raw);
    tcsetattr(STDIN_FILENO, TCSANOW, &tty_opts_raw);
    tcsetattr(STDOUT_FILENO, TCSANOW, &tty_opts_raw);

    struct stat st;
    stat(filename, &st);
    long filesize = st.st_size;
    
    /* Number of blocks, accounting for padding. */
    long num_blocks = filesize / 128 + ((filesize % 128) ? 1 : 0);

    FILE * f = fopen(filename, "rb");

    fprintf(stderr, "Initiate transfer on client side now.\r\n");

    /* Wait for initial NAK */
    xmodem_wait_nak();

    /* Transfer file in blocks of 128. */
    int current_block = 1;
    while (1)
    {
        size_t bytes_read = fread(data, 1, 128, f);

        fprintf(stderr, "Sending block %d of %ld...\r\n", current_block, num_blocks);
        current_block++;

        /* Fill rest of data with SUB (thanks CP/M...) */
        for (size_t i = bytes_read; i < 128; i++)
        {
            data[i] = SUB;
        }

        /* Send header. This is SOH followed by 2 bytes.
         * They're arbitrary for now... */
        xmodem_send(SOH);
        xmodem_send(0xa5);
        xmodem_send(0x5a);

        for (int i = 0; i < 64; i++)
        {
            fprintf(stderr, " ");
        }
        fprintf(stderr, "|\r\n");
        for (int i = 0; i < 64; i++)
        {
            fprintf(stderr, " ");
        }
        fprintf(stderr, "|\r\033[1A");
        fflush(stderr);

        /* Send data bytes. */
        for (int i = 0; i < 128; i++)
        {
            if (i == 64) fprintf(stderr, "\r\n");
            xmodem_send(data[i]);
            fprintf(stderr, "#");
            fflush(stderr);
        }

        /* Send checksum. Also arbitrary for now. */
        xmodem_send(0x03);

        /* Now wait for ack. */
        xmodem_wait_ack();

        /* Last block of file? */
        if (bytes_read < 128) break;

        fprintf(stderr, "\r\033[2A");
    }

    fprintf(stderr, "\r\nWaiting for final ACK...\r\n");

    /* End of file. Send end-of-transmission. */
    xmodem_send(EOT);
    xmodem_wait_ack();

    /* Close the file. */
    fclose(f);

    /* Restore previous TTY settings. */
    tcsetattr(STDIN_FILENO, TCSANOW, &tty_stdin_opts_backup);
    tcsetattr(STDOUT_FILENO, TCSANOW, &tty_stdout_opts_backup);
}
