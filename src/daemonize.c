// since alpine linux does not have daemonize packaged
// taken from http://software.clapper.org/daemonize/
// LICENSE of origin daemonize

// Copyright &copy; 2003-2011 Brian M. Clapper.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
//  this list of conditions and the following disclaimer in the documentation
//  and/or other materials provided with the distribution.
//
// * Neither the name "clapper.org" nor the names of its contributors may be
//  used to endorse or promote products derived from this software without
//  specific prior written permission.

// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <getopt.h>

static char *executable = NULL;
static char* opt_stdout = NULL;
static char* opt_stderr = NULL;


static struct option options[] = {
  {"stdout",       required_argument, NULL, 'o'},
  {"stderr",       required_argument, NULL, 'e'},
  {"help",         no_argument,       NULL, 'h'},
  {NULL,           no_argument,       NULL, 0}
};


void
show_usage() {
  printf("Usage: %s [options] [--] [command]\n", executable);
  printf("\n"
         "  -o, --stdout=STDOUT        standard output\n"
         "  -e, --stderr=STDERR        standard error\n"
         "\n"
         "  -h, --help                 print help message and exit\n"
         );
  exit(EXIT_SUCCESS);
}


int
main(int argc, char *const argv[]) {
  executable = argv[0];

  int opt, index;

  while((opt = getopt_long(argc, argv, "+e:o:h", options, &index)) != -1) {
    switch(opt) {
    case '?':
      goto argument;

    case 'h':
      show_usage();
      break;

    case 'o':
      opt_stdout = optarg;
      break;

    case 'e':
      opt_stderr = optarg;
      break;

    default:
      break;
    }
  }


  {
    pid_t pid = fork();
    if (pid < 0) {
      fprintf(stderr, "error: failed to fork, %m\n");
      return EXIT_FAILURE;
    }

    if (pid > 0) {
      _exit(0);
    }

  }

  if (setsid() < 0) {
    fprintf(stderr, "error: failed to setsid, %m\n");
    return EXIT_FAILURE;
  }

  {
    pid_t pid = fork();
    if (pid < 0) {
      fprintf(stderr, "error: failed to fork again, %m\n");
      return EXIT_FAILURE;
    }

    if (pid > 0) {
      _exit(0);
    }

  }

  umask(0);

  {
    int fd = open("/dev/null", O_RDONLY);
    if (fd < 0) {
      fprintf(stderr, "error: failed to open stdin, %m\n");
      return EXIT_FAILURE;
    }

    if (dup2(fd, 0) < 0) {
      fprintf(stderr, "error: failed to dup stdin, %m\n");
      close(fd);
      return EXIT_FAILURE;
    }

    close(fd);
  }


  {
    int fd = open(opt_stdout, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR);
    if (fd < 0) {
      fprintf(stderr, "error: failed to open stdout, %m\n");
      return EXIT_FAILURE;
    }

    if (dup2(fd, 1) < 0) {
      fprintf(stderr, "error: failed to dup stdout, %m\n");
      close(fd);
      return EXIT_FAILURE;
    }

    close(fd);
  }


  {
    int fd = open(opt_stderr, O_CREAT|O_WRONLY|O_TRUNC, S_IRUSR|S_IWUSR);
    if (fd < 0) {
      fprintf(stderr, "error: failed to open stderr, %m\n");
      return EXIT_FAILURE;
    }

    if (dup2(fd, 2) < 0) {
      fprintf(stderr, "error: failed to dup stderr, %m\n");
      close(fd);
      return EXIT_FAILURE;
    }

    close(fd);
  }


  if(optind < argc) {
    execvp(argv[optind], argv + optind);
    fprintf(stderr, "error: exec, %m\n");
    exit(EXIT_FAILURE);
  }

argument:
  fprintf(stderr, "Try '%s --help'\n", executable);
  return EXIT_FAILURE;
}
