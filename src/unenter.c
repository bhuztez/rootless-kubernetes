#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/limits.h>
#include <getopt.h>

#ifndef CLONE_NEWCGROUP
#define CLONE_NEWCGROUP 0x02000000
#endif

#define OPT_PIDFILE  0

static char *executable = NULL;
static char *opt_name = NULL;
static char *opt_pidfile = NULL;

static struct option options[] = {
  {"name",         required_argument, NULL, 'n'},
  {"pidfile",      required_argument, NULL, OPT_PIDFILE},

  {"help",         no_argument,       NULL, 'h'},
  {NULL,           no_argument,       NULL, 0}
};


void
show_usage() {
  printf("Usage: %s [options] [--] [command]\n", executable);
  printf("\n"
         "  -n, --name=NAME            name of the namespace\n"
         "      --pidfile=PIDFILE      path to pidfile\n"
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
open_file(int flags, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

int
open_file(int flags, const char *fmt, ...) {
  char path[PATH_MAX] = {0};
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(path, PATH_MAX, fmt, ap);
  va_end(ap);

  int fd = open(path, flags);
  if (fd < 0) {
    fprintf(stderr, "error: open '%s', %m\n", path);
  }

  return fd;
}


int
enter_ns(pid_t pid) {
  static const int mask[] = {
    CLONE_NEWUSER,
    CLONE_NEWUTS,
    CLONE_NEWIPC,
    CLONE_NEWNET,
    CLONE_NEWCGROUP,
    CLONE_NEWPID,
    CLONE_NEWNS,
  };

  static char const* filename[] = {
      "user",
      "uts",
      "ipc",
      "net",
      "cgroup",
      "pid",
      "mnt",
  };

  for(int i=0; i<7; i++) {
    struct stat buf = {0};

    char path[PATH_MAX] = {0};
    snprintf(path, PATH_MAX, "/proc/self/ns/%s", filename[i]);

    if (stat(path, &buf) != 0) {
      fprintf(stderr, "error: stat '%s', %m\n", path);
      return -1;
    }

    ino_t my_ino = buf.st_ino;

    int fd __attribute__((cleanup(cleanup_fd))) = open_file(O_RDONLY|O_CLOEXEC, "/proc/%d/ns/%s", pid, filename[i]);

    if (fd < 0) {
      fprintf(stderr, "error: open '%s' %m\n", path);
      return -1;
    }

    if (fstat(fd, &buf) != 0) {
      fprintf(stderr, "error: stat '%s', %m\n", path);
      return -1;
    }

    ino_t new_ino = buf.st_ino;

    if (my_ino != new_ino) {
      if (setns(fd, mask[i]) != 0) {
        fprintf(stderr, "error: setns '%s', %m\n", path);
        return -1;
      }
    }
  }

  return 0;
}


int
set_environ(pid_t pid) {
  int fd __attribute__((cleanup(cleanup_fd))) = open_file(O_RDONLY|O_CLOEXEC, "/proc/%d/environ", pid);
  if (fd < 0) {
    return -1;
  }

  clearenv();
  size_t size = 0;
  char *env = NULL;

  for(;;) {
    if (env == NULL) {
      env = mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    } else {
      env = mremap(env, size, size + 4096, MREMAP_MAYMOVE);
    }

    if (env == MAP_FAILED) {
      fprintf(stderr, "error: mmap, %m\n");
      return -1;
    }

    ssize_t len = read(fd, env+size, 4096);
    if (len < 0) {
      fprintf(stderr, "error: read environ, %m\n");
      return -1;
    }

    if (len < 4096) {
      size += len;
      break;
    }

    size+=4096;
  }

  for(size_t offset=0; offset<size; offset += strlen(env+offset)+1) {
    putenv(env+offset);
  }

  return 0;
}


int
enter(int fd, off_t len) {
  char buf[len+1];
  if (read(fd, buf, len) != len) {
    fprintf(stderr, "error: read pidfile, %m\n");
    return -1;
  }

  buf[len] = '\0';
  pid_t pid;
  if (sscanf(buf, "%d", &pid) == EOF) {
    fprintf(stderr, "error: sscanf pid, %m\n");
    return -1;
  }

  if(set_environ(pid) != 0) {
    return -1;
  }

  int wd __attribute__((cleanup(cleanup_fd))) = open_file(O_PATH|O_DIRECTORY|O_CLOEXEC, "/proc/%d/cwd", pid);
  if (wd < 0) {
    return -1;
  }

  {
    int root __attribute__((cleanup(cleanup_fd))) = open_file(O_PATH|O_DIRECTORY|O_CLOEXEC, "/proc/%d/root", pid);
    if (root < 0) {
      return -1;
    }

    if(enter_ns(pid) != 0) {
      return -1;
    }

    if (fchdir(root) != 0) {
      fprintf(stderr, "error: chdir, %m\n");
      return -1;
    }
  }

  if (chroot(".") != 0) {
    fprintf(stderr, "error: chroot, %m\n");
      return -1;
  }


  if (fchdir(wd) != 0) {
    fprintf(stderr, "error: chdir, %m\n");
    return -1;
  }

  return 0;
}


pid_t
spawn_process(char *const argv[]) {
  pid_t pid = fork();

  if (pid < 0) {
    fprintf(stderr, "error: fork, %m\n");
    return -1;
  }

  if (pid == 0) {
    execvp(argv[0], argv);
    fprintf(stderr, "error: exec, %m\n");
    exit(EXIT_FAILURE);
  }

  return pid;
}


int
main(int argc, char *const argv[]) {
  executable = argv[0];

  int opt, index;

  while((opt = getopt_long(argc, argv, "+n:h", options, &index)) != -1) {
    switch(opt) {
    case '?':
      goto argument;

    case 'h':
      show_usage();
      break;

    case 'n':
      opt_name = optarg;
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

    if (enter(fd, len) != 0) {
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
  }

  pid_t pid;
  if (optind < argc) {
    pid = spawn_process(argv + optind);
  } else {
    char *shell = getenv("SHELL");
    char *argv[2] = {shell?shell:"/bin/sh", NULL};
    pid = spawn_process(argv);
  }

  if (pid < 0) {
    return EXIT_FAILURE;
  }

  close(STDIN_FILENO);
  close(STDOUT_FILENO);

  for(;;) {
    int status;
    if (waitpid(pid, &status, 0) < 0) {
      fprintf(stderr, "error: waitpid, %m\n");
      return EXIT_FAILURE;
    }

    if (WIFSTOPPED(status)) {
      continue;
    }

    if (WIFSIGNALED(status)) {
      return WTERMSIG(status) + 128;
    } else {
      return WEXITSTATUS(status);
    }
  }

  return EXIT_FAILURE;

argument:
  fprintf(stderr, "Try '%s --help'\n", executable);
  return EXIT_FAILURE;
}
