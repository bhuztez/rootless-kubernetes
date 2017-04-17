#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <getopt.h>

#define OPT_PIDFILE  0

static char *executable = NULL;
static char* opt_name = NULL;
static char *opt_pidfile = NULL;
static int opt_kill = 0;


static struct option options[] = {
  {"name",         required_argument, NULL, 'n'},
  {"pidfile",      required_argument, NULL, OPT_PIDFILE},
  {"kill",         required_argument, NULL, 'k'},

  {"help",         no_argument,       NULL, 'h'},
  {NULL,           no_argument,       NULL, 0}
};


void
show_usage() {
  printf("Usage: %s [options] [--] [command]\n", executable);
  printf("\n"
         "  -n, --name=NAME            name of the namespace\n"
         "      --pidfile=PIDFILE      path to pidfile\n"
         "  -k, --kill                 kill process\n"
         "\n"
         "  -h, --help                 print help message and exit\n"
         );
  exit(EXIT_SUCCESS);
}


void
cleanup_fd(int *fd) {
  if (*fd < 0)
    return;
  close(*fd);
}


int
main(int argc, char *const argv[]) {
  executable = argv[0];

  int opt, index;

  while((opt = getopt_long(argc, argv, "+n:kh", options, &index)) != -1) {
    switch(opt) {
    case '?':
      goto argument;

    case 'h':
      show_usage();
      break;

    case 'n':
      opt_name = optarg;
      break;

    case 'k':
      opt_kill = 1;
      break;

    case OPT_PIDFILE:
      opt_pidfile = optarg;
      break;

    default:
      break;
    }
  }

  char path[PATH_MAX] = {0};

  if (!opt_pidfile) {
    if (!opt_name) {
      fprintf(stderr, "error: missing name\n");
      goto argument;
    }

    char *rundir = getenv("XDG_RUNTIME_DIR");
    if (!rundir) {
      fprintf(stderr, "error: environment XDG_RUNTIME_DIR not set\n");
      return EXIT_FAILURE;
    }
    snprintf(path, PATH_MAX, "%s/userns/%s", rundir, opt_name);

    opt_pidfile = path;
  }


  {
    int fd __attribute__((cleanup(cleanup_fd))) = open(opt_pidfile, O_RDONLY|O_CLOEXEC);
    if (fd < 0) {
      fprintf(stderr, "error: open '%s', %m\n", opt_pidfile);
      return EXIT_FAILURE;
    }

    struct stat buf = {0};
    if (fstat(fd, &buf) != 0) {
      fprintf(stderr, "error: stat, %m\n");
      return EXIT_FAILURE;
    }

    off_t len = buf.st_size;

    char s[len+1];
    if (read(fd, s, len) != len) {
      fprintf(stderr, "error: read pidfile, %m\n");
      return EXIT_FAILURE;
    }

    s[len] = '\0';
    pid_t pid;
    if (sscanf(s, "%d", &pid) == EOF) {
      fprintf(stderr, "error: sscanf pid, %m\n");
      return EXIT_FAILURE;
    }

    struct flock lock = {
      .l_type = F_WRLCK,
      .l_whence = SEEK_SET,
      .l_start = 0,
      .l_len = len,
    };

    if (fcntl(fd, F_GETLK, &lock) != 0) {
      fprintf(stderr, "error: test lock pidfile, %m\n");
      return EXIT_FAILURE;
    }

    if (lock.l_type == F_UNLCK) {
      fprintf(stderr, "error: pidfile not locked\n");
      return EXIT_FAILURE;
    }

    if (opt_kill) {
      kill(pid, SIGKILL);
    }
  }

  return EXIT_SUCCESS;
argument:
  fprintf(stderr, "Try '%s --help'\n", executable);
  return EXIT_FAILURE;
}
