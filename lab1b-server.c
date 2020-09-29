//NAME: Ryan Lam
//EMAIL: ryan.lam53@gmail.com
//ID: 705124474

#include <stdio.h>
#include <termios.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <poll.h>
#include <zlib.h>


int bufsize = 256;
char inputbuf[256];
char compressionbuf[256];
int portnum, socketfd, newsocketfd, shell_status, wait_result, read_status, i;
socklen_t clientlen;
pid_t childpid;
int input_to_shell[2];
int output_from_shell[2];
char* execargs[2] = {"execshell", NULL};
struct pollfd polls[2];
bool c_kill_flag = false;
bool d_kill_flag = false;
struct sockaddr_in server_addr, client_addr;
char cr = '\r';
char lf = '\n';
bool compressopt = false;
// COMPRESSION STREAMS
z_stream shell_input;
z_stream shell_output;

void shut_down(int exit_code) {
    if(compressopt)
    {
        deflateEnd(&shell_input);
        inflateEnd(&shell_output);
    }
    close(newsocketfd);
    close(socketfd);
    exit(exit_code);
}

void syscall_error() {
    fprintf(stderr, "%s", strerror(errno));
    shut_down(1);
}

void sighandler(int signum) {
    if(signum == SIGPIPE)
    {
        int exit_status;
        wait(&exit_status);
        fprintf(stderr,"SHELL EXIT SIGNAL=%d STATUS=%d",exit_status & 0xff, exit_status/256);
        shut_down(1);
    }
}

int compression(int inputbytesize, int compbufsize, char* inputbuf, char* outputbuf) {
    int numcompressedbytes;
    shell_input.zalloc = Z_NULL;
    shell_input.zfree = Z_NULL;
    shell_input.opaque = Z_NULL;
    if (deflateInit(&shell_input, Z_DEFAULT_COMPRESSION) != Z_OK)
		syscall_error();  

    shell_input.avail_in = (uInt) inputbytesize;
    shell_input.next_in = (Bytef *) inputbuf;
    shell_input.avail_out = compbufsize;
    shell_input.next_out = (Bytef *) outputbuf;

    do {
        if(deflate(&shell_input, Z_SYNC_FLUSH) == Z_STREAM_ERROR)
        {
            fprintf(stderr, "%s", shell_input.msg);
            exit(1);
        }
    }while(shell_input.avail_in > 0);
    numcompressedbytes = compbufsize - shell_input.avail_out;
    return numcompressedbytes;
}

int decompression(int inputbytesize, int decompbufsize, char* inputbuf, char* outputbuf) {
    int numdecompressedbytes;
    shell_output.zalloc = Z_NULL;
	shell_output.zfree = Z_NULL;
	shell_output.opaque = Z_NULL;
	if (inflateInit(&shell_output) != Z_OK)
		syscall_error();

    shell_output.avail_in = (uInt) inputbytesize;
    shell_output.next_in = (Bytef *) inputbuf;
    shell_output.avail_out = decompbufsize;
    shell_output.next_out = (Bytef *) outputbuf;

    do {
        if(inflate(&shell_output, Z_SYNC_FLUSH) == Z_STREAM_ERROR)
        {
            fprintf(stderr, "%s", shell_output.msg);
            exit(1);
        }
    }while(shell_output.avail_in > 0);
    numdecompressedbytes = decompbufsize - shell_output.avail_out;
    return numdecompressedbytes; 
}

int main(int argc, char* argv[])
{
    memset(inputbuf, 0, bufsize);
    memset(compressionbuf, 0, bufsize);

    // OPTION PARSING
    static struct option long_opts[] = 
    {
        {"port", required_argument, 0, 'p'},
        {"compress", no_argument, 0, 'c'},
        {0,0,0,0}
    };
    while(1)
    {
        int opt = getopt_long(argc, argv, "p:c", long_opts, NULL);
        if(opt == -1)
            break;
        switch(opt)
        {
            case 'p':
                portnum = atoi(optarg);
                break;
            case 'c':
                compressopt = true;
                break;
            case '?':
                fprintf(stderr, "Error: Unrecognized option, Correct usage: --port=, --log=\n");
                shut_down(1);
                break;
            default:
                break;
        }
    }

    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(socketfd < 0)
        syscall_error();

    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(portnum);

    if (bind(socketfd, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0)
        syscall_error();

    listen(socketfd, 5);

    clientlen = sizeof(client_addr);
    newsocketfd = accept(socketfd, (struct sockaddr *) &client_addr, &clientlen);
    if(newsocketfd < 0)
        syscall_error();

    if(pipe(input_to_shell) < 0) // CREATE PIPES
        syscall_error();
    if(pipe(output_from_shell) < 0)
        syscall_error();

    signal(SIGPIPE, sighandler);

    childpid = fork(); // FORK
    if(childpid < 0)
        syscall_error();

    if(childpid == 0) // CHILD PROCESS
    {
        close(input_to_shell[1]);
        close(output_from_shell[0]);
        close(0);
        dup(input_to_shell[0]);
        close(input_to_shell[0]);
        close(1);
        dup(output_from_shell[1]);
        close(2);
        dup(output_from_shell[1]);
        close(output_from_shell[1]);

        if(execvp("/bin/bash", execargs) < 0)
            syscall_error();
    }
    else // PARENT PROCESS
    {
        close(input_to_shell[0]);
        close(output_from_shell[1]);

        // SETTING UP POLLS
        polls[0].fd = newsocketfd;
        polls[0].events = POLLIN | POLLHUP | POLLERR;
        polls[0].revents = 0;
        polls[1].fd = output_from_shell[0];
        polls[1].events = POLLIN | POLLHUP | POLLERR;
        polls[1].revents = 0;

        // POLLING AND READING LOOP
        while(1)
        {   
            if(poll(polls, 2, -1) < 0)
                syscall_error();
            // SOCKET INPUT TO SHELL
            if(polls[0].revents & POLLIN)
            {
                read_status = read(newsocketfd, &inputbuf, bufsize);
                if(read_status < 0)
                {
                    close(input_to_shell[1]);
                }
                if(compressopt)
                {
                    memcpy(compressionbuf, inputbuf, read_status);
                    read_status = decompression(read_status, bufsize, compressionbuf, inputbuf);
                }
                i = 0;
                while(i < read_status)
                {
                    if(inputbuf[i] == 0x03)
                    {
                        printf("^C\r\n");
                        if(kill(childpid, SIGINT) < 0)
                            syscall_error();
                        c_kill_flag = true;
                    }
                    else if(inputbuf[i] == 0x04)
                    {
                        printf("^D\r\n");
                        close(input_to_shell[1]);
                        d_kill_flag = true;
                    }
                    else 
                    {
                        if(write(input_to_shell[1], &inputbuf[i], 1) < 0)
                            syscall_error();
                    }
                    i++;
                }
                memset(inputbuf, 0, bufsize);
            }
            if(polls[0].revents & (POLLHUP | POLLERR))
            {
                fprintf(stderr, "Poll error");
                break;
            }
            // SHELL OUTPUT TO SOCKET
            if(polls[1].revents & POLLIN)
            {
                read_status = read(output_from_shell[0], &inputbuf, bufsize);
                if(read_status < 0)
                    syscall_error();
                i = 0;
                while(i < read_status)
                {
                    if(inputbuf[i] == 0x04) // SERVER RECEIVES EOF FROM SHELL
                    {
                        printf("^D\r\n");
                        d_kill_flag = true;
                    }
                    if(inputbuf[i] == '\n')
                    {
                        if(compressopt)
                            compressionbuf[i] = lf;
                        else
                        {
                            if(write(newsocketfd, &cr, 1) < 0)
                                syscall_error();
                            if(write(newsocketfd, &lf, 1) < 0)
                                syscall_error();
                        } 
                    }
                    else 
                    {
                        if(compressopt)
                            compressionbuf[i] = inputbuf[i];
                        else
                        {
                            if(write(newsocketfd, &inputbuf[i], 1) < 0)
                                syscall_error();
                        }
                    }
                    i++;
                }
                if(compressopt)
                {
                    char compressiontemp[256];
                    memcpy(compressiontemp, compressionbuf, read_status);
                    read_status = compression(read_status, bufsize, compressiontemp, compressionbuf);
                    if(write(newsocketfd, compressionbuf, read_status) < 0)
                        syscall_error();
                }
                memset(inputbuf, 0, bufsize);
            }
            if(polls[1].revents & (POLLERR | POLLHUP))
            {
                fprintf(stderr, "Shell was terminated\r\n");
                break;
            }
            if(c_kill_flag)
            {
                break;
            }
            else if(d_kill_flag)
            {
                close(output_from_shell[0]);
                break;
            }
        }
        // RETRIEVE EXIT STATUS
        wait_result = waitpid(childpid, &shell_status, 0);
        if(wait_result < 0)
            fprintf(stderr, "%s", strerror(errno));
        fprintf(stderr, "SHELL EXIT SIGNAL=%d STATUS=%d\r\n", shell_status & 0xff, shell_status/256);
    }
    shut_down(0);
}