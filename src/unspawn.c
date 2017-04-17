#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sched.h>
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/signal.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <linux/limits.h>
#include <getopt.h>

#ifndef CLONE_NEWCGROUP
#define CLONE_NEWCGROUP 0x02000000
#endif


#define OPT_USERNS   0
#define OPT_NETNS    1
#define OPT_PIDFILE  2
#define OPT_NOPID    3
#define OPT_NOCGROUP 4

static char *executable = NULL;
static char* opt_name = NULL;
static char* opt_domain = NULL;
static int opt_userns = 0;
static int opt_netns = 0;
static char *opt_netns_name = NULL;
static char *opt_pidfile = NULL;
static int opt_flags = 0;


static struct option options[] = {
  {"name",         required_argument, NULL, 'n'},
  {"domain",       optional_argument, NULL, 'd'},
  {"user",         no_argument,       NULL, OPT_USERNS},
  {"net",          optional_argument, NULL, OPT_NETNS},
  {"pidfile",      required_argument, NULL, OPT_PIDFILE},
  {"no-pid",       no_argument,       NULL, OPT_NOPID},
  {"no-cgroup",    no_argument,       NULL, OPT_NOCGROUP},
  {"help",         no_argument,       NULL, 'h'},

  {NULL,           no_argument,       NULL, 0}
};


void
show_usage() {
  printf("Usage: %s [options] [--] [command]\n", executable);
  printf("\n"
         "  -n, --name=NAME            name of the namespace\n"
         "  -d, --domain=DOMAIN        domain of the namespace\n"
         "      --user                 new USER namespace\n"
         "      --net[=NETNS]          new NET namespace, or use NETNS\n"
         "      --no-pid               do not create new PID namespace\n"
         "      --pidfile=PIDFILE      path to pidfile, default ${XDG_RUNTIME_DIR}/userns/${NAME}.pid\n"
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

void
cleanup_file(FILE **file) {
  if (*file == NULL)
    return;
  fclose(*file);
}


int
write_string(const char *data, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

int
write_string(const char *data, const char *fmt, ...) {
  char path[PATH_MAX] = {0};
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(path, PATH_MAX, fmt, ap);
  va_end(ap);

  int fd __attribute__((cleanup(cleanup_fd))) = open(path, O_RDWR|O_CLOEXEC);
  if (fd < 0) {
    fprintf(stderr, "error: open '%s', %m\n", path);
    return -1;
  }

  ssize_t len = strlen(data);

  if (write(fd, data, len) != len) {
    fprintf(stderr, "error: write '%s', %m\n", path);
    return -1;
  }

  return 0;
}


int
unshare_user() {
  uid_t uid = geteuid();
  gid_t gid = getegid();

  if (unshare(CLONE_NEWUSER) != 0) {
    fprintf(stderr, "error: unshare user namespace, %m\n");
    return -1;
  }

  static const char deny[] = "deny";
  if (write_string(deny, "/proc/self/setgroups") != 0) {
    return -1;
  }

  {
    char mapping[25] = {0};
    snprintf(mapping, sizeof(mapping), "0 %d 1", uid);
    if (write_string(mapping, "/proc/self/%s_map", "uid") != 0) {
      return -1;
    }
  }

  {
    char mapping[25] = {0};
    snprintf(mapping, sizeof(mapping), "0 %d 1", gid);
    if (write_string(mapping, "/proc/self/%s_map", "gid") != 0) {
      return -1;
    }
  }

  return 0;
}


pid_t
_fork(int flags) {
  return syscall(SYS_clone, (flags | SIGCHLD), NULL, NULL, NULL);
}


pid_t
spawn_process(char *const argv[], const sigset_t *oldset) {
  int flags = CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWPID | CLONE_NEWCGROUP;

  if (opt_netns && (!opt_netns_name)) {
    flags |= CLONE_NEWNET;
  }

  flags ^= opt_flags;

  pid_t pid = _fork(flags);

  if (pid < 0) {
    fprintf(stderr, "error: fork process, %m\n");
    return -1;
  }

  if (pid) {
    return pid;
  }

  if (kill(0, SIGCONT) != 0) {
    fprintf(stderr, "error: stop child process, %m\n");
    exit(EXIT_FAILURE);
  }

  if (sigprocmask(SIG_SETMASK, oldset, NULL) != 0) {
    fprintf(stderr, "error: set signal mask, %m\n");
    exit(EXIT_FAILURE);
  }

  if (setenv("USERNS_NAME", opt_name, 1) != 0) {
    fprintf(stderr, "error: set environment USERNS_NAME, %m\n");
    exit(EXIT_FAILURE);
  }

  if (sethostname(opt_name, strlen(opt_name)) != 0) {
    fprintf(stderr, "error: set hostname, %m\n");
    exit(EXIT_FAILURE);
  }

  if (setenv("USERNS_DOMAIN", opt_domain, 1) != 0) {
    fprintf(stderr, "error: set environment USERNS_DOMAIN, %m\n");
    exit(EXIT_FAILURE);
  }

  if (setdomainname(opt_domain, strlen(opt_domain)) != 0) {
    fprintf(stderr, "error: set domain name, %m\n");
    exit(EXIT_FAILURE);
  }

  execvp(argv[0], argv);
  fprintf(stderr, "error: exec, %m\n");
  exit(EXIT_FAILURE);
}



int
write_pid(int dirfd, const char *name, pid_t pid) {
  int fd = openat(dirfd, ".", O_TMPFILE|O_CLOEXEC|O_WRONLY, S_IRUSR);
  if (fd < 0) {
    fprintf(stderr, "error: open pidfile, %m\n");
    return -1;
  }

  long len;
  int fd2 __attribute__((cleanup(cleanup_fd))) = -1;

  {
    FILE *f __attribute__((cleanup(cleanup_file))) = fdopen(fd, "w");
    if (f == NULL) {
      fprintf(stderr, "error: fdopen pidfile, %m\n");
      close(fd);
      return -1;
    }

    if (fprintf(f, "%d", pid) < 0) {
      fprintf(stderr, "error: write pidfile, %m\n");
      return -1;
    }

    if (fflush(f) != 0) {
      fprintf(stderr, "error: fflush pidfile, %m\n");
      return -1;
    }

    if (fdatasync(fd) != 0) {
      fprintf(stderr, "error: fdatasync pidfile, %m\n");
      return -1;
    }

    len = ftell(f);
    if (len < 0) {
      fprintf(stderr, "error: ftell pidfile, %m\n");
      return -1;
    }

    fd2 = dup(fd);
    if (fd2 < 0) {
      fprintf(stderr, "error: dup pidfile, %m\n");
      return -1;
    };
  }

  struct flock lock = {
    .l_type = F_WRLCK,
    .l_whence = SEEK_SET,
    .l_start = 0,
    .l_len = len,
  };

  if (fcntl(fd2, F_SETLK, &lock) != 0) {
    fprintf(stderr, "error: lock pidfile, %m\n");
    return -1;
  }

  char path[PATH_MAX];
  snprintf(path, PATH_MAX, "/proc/self/fd/%d", fd2);


  for(;;) {
    if (linkat(AT_FDCWD, path, dirfd, name, AT_SYMLINK_FOLLOW) == 0) {
      int result = fd2;
      fd2 = -1;
      return result;
    }

    if (errno != EEXIST) {
      fprintf(stderr, "error: link pidfile, %m\n");
      return -1;
    }

    {
      int fd3 __attribute__((cleanup(cleanup_fd))) = openat(dirfd, name, O_RDONLY);
      if (fd3 < 0) {
        if (errno == ENOENT)
          continue;

        fprintf(stderr, "error: open pidfile, %m\n");
        return -1;
      }

      off_t len = lseek(fd3, 0, SEEK_END);
      if (len < 0) {
        fprintf(stderr, "error: lseek pidfile, %m\n");
        return -1;
      }

      struct flock lock = {
        .l_type = F_WRLCK,
        .l_whence = SEEK_SET,
        .l_start = 0,
        .l_len = len,
      };

      if (fcntl(fd3, F_GETLK, &lock) != 0) {
        fprintf(stderr, "error: lock pidfile, %m\n");
        return -1;
      }

      if (lock.l_type != F_UNLCK) {
        fprintf(stderr, "error: pidfile locked\n");
        return -1;
      }
    }

    if (unlinkat(dirfd, name, 0) != 0) {
      if (errno == ENOENT)
        continue;

      fprintf(stderr, "error: unlink pidfile, %m\n");
      return -1;
    }
  }
}

pid_t
spawn_and_wait(const char *path, const char *name, char *const argv[]) {
  int dirfd __attribute__((cleanup(cleanup_fd))) = open(path, O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);

  if ((dirfd < 0) && (errno == ENOENT)) {
    if ((mkdir(path, S_IRWXU) != 0) && (errno != EEXIST)) {
      fprintf(stderr, "error: mkdir '%s', %m\n", path);
      return -1;
    }

    dirfd = open(path, O_PATH|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
  }

  if (dirfd < 0) {
    fprintf(stderr, "error: open '%s', %m\n", path);
    return -1;
  }

  if (opt_netns_name) {
    char netns_fd_path[PATH_MAX] = {0};
    snprintf(netns_fd_path, PATH_MAX, "/var/run/netns/%s", opt_netns_name);
    int netns_fd __attribute__((cleanup(cleanup_fd))) = open(netns_fd_path, O_RDONLY);

    if (netns_fd < 0) {
      fprintf(stderr, "error: open '%s', %m\n", netns_fd_path);
      return -1;
    }

    if (setns(netns_fd, CLONE_NEWNET) != 0) {
      fprintf(stderr, "error: set netns, %m\n");
      return -1;
    }
  }

  if (opt_userns) {
    if (unshare_user() != 0) {
      return -1;
    }
  }

  sigset_t set, oldset;
  sigemptyset(&set);
  sigaddset(&set, SIGCHLD);

  if(sigprocmask(SIG_BLOCK, &set, &oldset) != 0) {
    fprintf(stderr, "set signal mask, %m\n");
    return -1;
  }

  {
    int sfd __attribute__((cleanup(cleanup_fd))) = signalfd(-1, &set, 0);
    if (sfd < 0) {
      fprintf(stderr, "error: create signalfd, %m\n");
      return -1;
    }

    pid_t pid = spawn_process(argv, &oldset);
    if (pid < 0) {
      return -1;
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);

    {
      int fd __attribute__((cleanup(cleanup_fd))) = write_pid(dirfd, name, pid);
      if (fd < 0) {
        kill(pid, SIGKILL);
        return -1;
      }

      if (kill(pid, SIGCONT) != 0) {
        fprintf(stderr, "error: continue child process, %m\n");
        kill(pid, SIGKILL);
        return -1;
      }

      struct signalfd_siginfo fdsi = {0};
      if (read(sfd, &fdsi, sizeof(struct signalfd_siginfo)) != sizeof(struct signalfd_siginfo)) {
        fprintf(stderr, "error: read signalfd %m\n");
        kill(pid, SIGKILL);
        return -1;
      }
    }

    unlinkat(dirfd, name, 0);
    return pid;
  }
}

int
main(int argc, char *const argv[]) {
  executable = argv[0];

  int opt, index;

  while((opt = getopt_long(argc, argv, "+n:d:h", options, &index)) != -1) {
    switch(opt) {
    case '?':
      goto argument;

    case 'h':
      show_usage();
      break;

    case 'n':
      opt_name = optarg;
      break;

    case 'd':
      opt_domain = optarg;
      break;

    case OPT_USERNS:
      opt_userns = 1;
      break;

    case OPT_NETNS:
      opt_netns = 1;
      opt_netns_name = optarg;
      break;

    case OPT_PIDFILE:
      opt_pidfile = optarg;
      break;

    case OPT_NOPID:
      opt_flags |= CLONE_NEWPID;
      break;

    case OPT_NOCGROUP:
      opt_flags |= CLONE_NEWCGROUP;
      break;

    default:
      break;
    }
  }

  if (!opt_name) {
    fprintf(stderr, "error: missing name\n");
    goto argument;
  }

  opt_domain = (opt_domain)?opt_domain:getenv("USERNS_DOMAIN");
  opt_domain = (opt_domain)?opt_domain:"localdomain";

  char path[PATH_MAX] = {0};
  char name[PATH_MAX] = {0};

  if (opt_pidfile) {
    char tmp[PATH_MAX] = {0};
    strncpy(tmp, opt_pidfile, PATH_MAX-1);
    strncpy(path, dirname(tmp), PATH_MAX-1);
    strncpy(tmp, opt_pidfile, PATH_MAX-1);
    strncpy(name, basename(tmp), PATH_MAX-1);
  } else {
    char *rundir = getenv("XDG_RUNTIME_DIR");
    if (!rundir) {
      fprintf(stderr, "error: environment XDG_RUNTIME_DIR not set\n");
      return EXIT_FAILURE;
    }
    snprintf(path, PATH_MAX, "%s/userns", rundir);
    strncpy(name, opt_name, PATH_MAX-1);
  }

  pid_t pid;
  if (optind < argc) {
    pid = spawn_and_wait(path, name, argv + optind);
  } else {
    char *shell = getenv("SHELL");
    char *argv[2] = {shell?shell:"/bin/sh", NULL};
    pid = spawn_and_wait(path, name, argv);
  }

  if (pid < 0) {
    return EXIT_FAILURE;
  }

  int status;
  pid_t p = waitpid(pid, &status, WNOHANG);
  if (p < 0) {
    fprintf(stderr, "error: waitpid, %m\n");
    return EXIT_FAILURE;
  }

  if (WIFSIGNALED(status)) {
    return WTERMSIG(status) + 128;
  } else {
    return WEXITSTATUS(status);
  }

argument:
  fprintf(stderr, "Try '%s --help'\n", executable);
  return EXIT_FAILURE;
}
