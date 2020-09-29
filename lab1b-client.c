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
#include <sys/wait.h>
#include <poll.h>
#include <stdbool.h>
#include <zlib.h>
#include <fcntl.h>


struct termios curmode;
int bufsize = 256;
char inputbuf[256];
char compressionbuf[256];
int socketfd, portnum, read_status, i;
char cr = '\r';
char lf = '\n';
struct pollfd polls[2];
bool c_kill_flag = false;
bool d_kill_flag = false;
bool readzero = false;
int logptr;
bool logopt = false;
bool compressopt = false;
// COMPRESSION STREAMS
z_stream server_input;
z_stream server_output;


void resetterminalmode() {
    if(tcsetattr(0, TCSAFLUSH, &curmode) < 0)
    {
        fprintf(stderr, "%s", strerror(errno));
        exit(1);
    }
}

void setterminalmode() {
    struct termios setmode;
    if(tcgetattr(0, &curmode) < 0)
    {
        fprintf(stderr, "%s", strerror(errno));
        _exit(1);
    }
    setmode = curmode;
    setmode.c_iflag = ISTRIP;
    setmode.c_oflag = 0;
    setmode.c_lflag = 0;	
    if(tcsetattr(0, TCSANOW, &setmode) < 0)
    {
        fprintf(stderr, "%s", strerror(errno));
        exit(1);
    }
}

void shut_down(int exit_code) {
    if(logopt)
        close(logptr);
    if(compressopt)
    {
        deflateEnd(&server_input);
        inflateEnd(&server_output);
    }
    close(socketfd);
    resetterminalmode();
    exit(exit_code);
}

void syscall_error() {
    fprintf(stderr, "%s", strerror(errno));
    shut_down(1);
}

int compression(int inputbytesize, int compbufsize, char* inputbuf, char* outputbuf) {
    int numcompressedbytes;
    server_input.zalloc = Z_NULL;
    server_input.zfree = Z_NULL;
    server_input.opaque = Z_NULL;
    if (deflateInit(&server_input, Z_DEFAULT_COMPRESSION) != Z_OK)
		syscall_error();  

    server_input.avail_in = (uInt) inputbytesize;
    server_input.next_in = (Bytef *) inputbuf;
    server_input.avail_out = compbufsize;
    server_input.next_out = (Bytef *) outputbuf;

    do {
        if(deflate(&server_input, Z_SYNC_FLUSH) == Z_STREAM_ERROR)
        {
            fprintf(stderr, "%s", server_input.msg);
            exit(1);
        }
    }while(server_input.avail_in > 0);
    numcompressedbytes = compbufsize - server_input.avail_out;
    return numcompressedbytes;
}

int decompression(int inputbytesize, int decompbufsize, char* inputbuf, char* outputbuf) {
    int numdecompressedbytes;
    server_output.zalloc = Z_NULL;
	server_output.zfree = Z_NULL;
	server_output.opaque = Z_NULL;
	if (inflateInit(&server_output) != Z_OK)
		syscall_error();

    server_output.avail_in = (uInt) inputbytesize;
    server_output.next_in = (Bytef *) inputbuf;
    server_output.avail_out = decompbufsize;
    server_output.next_out = (Bytef *) outputbuf;

    do {
        if(inflate(&server_output, Z_SYNC_FLUSH) == Z_STREAM_ERROR)
        {
            fprintf(stderr, "%s", server_output.msg);
            exit(1);
        }
    }while(server_output.avail_in > 0);
    numdecompressedbytes = decompbufsize - server_output.avail_out;
    return numdecompressedbytes; 
}

int main(int argc, char* argv[]) {
    
    memset(inputbuf, 0, bufsize);
    memset(compressionbuf, 0, bufsize);


    // OPTION PARSING
    static struct option long_opts[] = {
        {"port", required_argument, 0, 'p'},
        {"log", required_argument, 0, 'l'},
        {"compress", no_argument, 0, 'c'},
        {0,0,0,0}
    };
    while(1) {
        int opt = getopt_long(argc, argv, "p:l:c", long_opts, NULL);
        if(opt == -1)
            break;
        switch(opt)
        {
            case 'p':
                portnum = atoi(optarg);
                break;
            case 'l':
                logopt = true;
                if(access(optarg, F_OK) == 0 && access(optarg, W_OK) != 0)
                {
                    fprintf(stderr, "%s", strerror(errno));
                    exit(1);
                }
                logptr = creat(optarg, 0666);
                if(logptr < 0)
                {
                    fprintf(stderr, "%s", strerror(errno));
                    exit(1);
                }
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

    setterminalmode();

    struct sockaddr_in server_addr;
    struct hostent *server;
    
    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    if(socketfd < 0)
        syscall_error();

    server = gethostbyname("localhost");
    if (server == NULL) 
        syscall_error();

    bzero((char *) &server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&server_addr.sin_addr.s_addr, server->h_length);
    server_addr.sin_port = htons(portnum);

    if (connect(socketfd,(struct sockaddr *)&server_addr,sizeof(server_addr)) < 0)
        syscall_error();

    
    polls[0].fd = STDIN_FILENO;
    polls[0].events = POLLIN | POLLHUP | POLLERR;
    polls[0].revents = 0;
    polls[1].fd = socketfd;
    polls[1].events = POLLIN | POLLHUP | POLLERR;
    polls[1].revents = 0;

    // POLLING AND READING LOOP
    while(1)
    {   
        if(poll(polls, 2, -1) < 0)
            syscall_error();

    
        // TERMINAL INPUT TO SOCKET
        if(polls[0].revents & POLLIN)
        {
            read_status = read(STDIN_FILENO, &inputbuf, bufsize);
            if(read_status < 0)
                syscall_error();
            i = 0;
            if(logopt && !compressopt)
                dprintf(logptr, "SENT %d BYTES: ", read_status);
            while(i < read_status)
            {
                if(inputbuf[i] == '\r')
                {
                    if(write(STDOUT_FILENO, &cr, 1) < 0)
                        syscall_error();
                    if(write(STDOUT_FILENO, &lf, 1) < 0)
                        syscall_error();
                    if(compressopt)
                        compressionbuf[i] = lf;
                    else
                    {
                        if(logopt)
                            dprintf(logptr, "%s", &cr);
                        if(write(socketfd, &lf, 1) < 0)
                            syscall_error();
                    }    
                }
                else 
                {
                    if(write(STDOUT_FILENO, &inputbuf[i], 1) < 0)
                        syscall_error();
                    if(compressopt)
                        compressionbuf[i] = inputbuf[i];
                    else 
                    {
                        if(logopt)
                            dprintf(logptr, "%s", &inputbuf[i]);
                        if(write(socketfd, &inputbuf[i], 1) < 0)
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
                if(write(socketfd, compressionbuf, read_status) < 0)
                    syscall_error();
                if(logopt)
                {
                    dprintf(logptr, "SENT %d BYTES: ", read_status);
                    dprintf(logptr, "%s", compressionbuf);
                }
            }
            if(logopt)
                dprintf(logptr, "%s", &lf);
            memset(inputbuf, 0, bufsize);
        }
        if(polls[0].revents & (POLLHUP | POLLERR))
        {
            fprintf(stderr, "Poll error");
            break;
        }


        // SOCKET OUTPUT TO TERMINAL
        if(polls[1].revents & POLLIN)
        {
            read_status = read(socketfd, &inputbuf, bufsize);
            if(read_status < 0)
                syscall_error();
            if(read_status == 0)
                break;
            if(logopt)
            {
                dprintf(logptr, "RECEIVED %d BYTES: ", read_status);
                dprintf(logptr, "%s", inputbuf);
                dprintf(logptr, "%s", &lf);
            }
            if(compressopt)
            {
                memcpy(compressionbuf, inputbuf, read_status);
                read_status = decompression(read_status, bufsize, compressionbuf, inputbuf);
            }
            i = 0;
            while(i < read_status)
            {
                if(inputbuf[i] == '\n')
                {
                    if(write(STDOUT_FILENO, &cr, 1) < 0)
                        syscall_error();
                    if(write(STDOUT_FILENO, &lf, 1) < 0)
                        syscall_error();
                }
                else 
                {
                    if(write(STDOUT_FILENO, &inputbuf[i], 1) < 0)
                        syscall_error();
                }
                i++;
            }
            memset(inputbuf, 0, bufsize);
        }
        if(polls[1].revents & (POLLERR | POLLHUP))
        {
            fprintf(stderr, "Received network error\r\n");
            break;
        }
    }
    shut_down(0);
}