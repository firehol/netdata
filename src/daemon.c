#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <pwd.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include "common.h"
#include "appconfig.h"
#include "log.h"
#include "web_client.h"
#include "plugins_d.h"
#include "rrd.h"
#include "popen.h"
#include "main.h"
#include "daemon.h"

char pidfile[FILENAME_MAX + 1] = "";
int pidfd = -1;

void sig_handler(int signo)
{
	switch(signo) {
		case SIGILL:
		case SIGABRT:
		case SIGFPE:
		case SIGSEGV:
		case SIGBUS:
		case SIGSYS:
		case SIGTRAP:
		case SIGXCPU:
		case SIGXFSZ:
			infoerr("Death signaled exit (signal %d).", signo);
			signal(signo, SIG_DFL);
			break;

		case SIGKILL:
		case SIGTERM:
		case SIGQUIT:
		case SIGINT:
		case SIGHUP:
		case SIGUSR1:
		case SIGUSR2:
			infoerr("Signaled exit (signal %d).", signo);
			signal(SIGPIPE, SIG_IGN);
			signal(SIGTERM, SIG_IGN);
			signal(SIGQUIT, SIG_IGN);
			signal(SIGHUP,  SIG_IGN);
			signal(SIGINT,  SIG_IGN);
			signal(SIGCHLD, SIG_IGN);
			netdata_cleanup_and_exit(1);
			break;

		case SIGPIPE:
			infoerr("Signaled PIPE (signal %d).", signo);
			// this is received when web clients send a reset
			// no need to log it.
			// infoerr("Ignoring signal %d.", signo);
			break;

		default:
			info("Signal %d received. Falling back to default action for it.", signo);
			signal(signo, SIG_DFL);
			break;
	}
}

int become_user(const char *username)
{
	struct passwd *pw = getpwnam(username);
	if(!pw) {
		error("User %s is not present.", username);
		return -1;
	}

	if(pidfile[0] && getuid() != pw->pw_uid) {
		// we are dropping privileges
		if(chown(pidfile, pw->pw_uid, pw->pw_gid) != 0)
			error("Cannot chown pidfile '%s' to user '%s'", pidfile, username);

		else if(pidfd != -1) {
			// not need to keep it open
			close(pidfd);
			pidfd = -1;
		}
	}
	else if(pidfd != -1) {
		// not need to keep it open
		close(pidfd);
		pidfd = -1;
	}

	if(setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) != 0) {
		error("Cannot switch to user's %s group (gid: %d).", username, pw->pw_gid);
		return -1;
	}

	if(setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid) != 0) {
		error("Cannot switch to user %s (uid: %d).", username, pw->pw_uid);
		return -1;
	}

	if(setgid(pw->pw_gid) != 0) {
		error("Cannot switch to user's %s group (gid: %d).", username, pw->pw_gid);
		return -1;
	}
	if(setegid(pw->pw_gid) != 0) {
		error("Cannot effectively switch to user's %s group (gid: %d).", username, pw->pw_gid);
		return -1;
	}
	if(setuid(pw->pw_uid) != 0) {
		error("Cannot switch to user %s (uid: %d).", username, pw->pw_uid);
		return -1;
	}
	if(seteuid(pw->pw_uid) != 0) {
		error("Cannot effectively switch to user %s (uid: %d).", username, pw->pw_uid);
		return -1;
	}

	return(0);
}

int become_daemon(int dont_fork, int close_all_files, const char *user, const char *input, const char *output, const char *error, const char *access, int *access_fd, FILE **access_fp)
{
	fflush(NULL);

	// open the files before forking
	int input_fd = -1, output_fd = -1, error_fd = -1, dev_null;

	if(input && *input) {
		if((input_fd = open(input, O_RDONLY, 0666)) == -1) {
			error("Cannot open input file '%s'.", input);
			return -1;
		}
	}

	if(output && *output) {
		if((output_fd = open(output, O_RDWR | O_APPEND | O_CREAT, 0666)) == -1) {
			error("Cannot open output log file '%s'", output);
			if(input_fd != -1) close(input_fd);
			return -1;
		}
	}

	if(error && *error) {
		if((error_fd = open(error, O_RDWR | O_APPEND | O_CREAT, 0666)) == -1) {
			error("Cannot open error log file '%s'.", error);
			if(input_fd != -1) close(input_fd);
			if(output_fd != -1) close(output_fd);
			return -1;
		}
	}

	if(access && *access && access_fd) {
		if((*access_fd = open(access, O_RDWR | O_APPEND | O_CREAT, 0666)) == -1) {
			error("Cannot open access log file '%s'", access);
			if(input_fd != -1) close(input_fd);
			if(output_fd != -1) close(output_fd);
			if(error_fd != -1) close(error_fd);
			return -1;
		}

		if(access_fp) {
			*access_fp = fdopen(*access_fd, "w");
			if(!*access_fp) {
				error("Cannot migrate file's '%s' fd %d.", access, *access_fd);
				if(input_fd != -1) close(input_fd);
				if(output_fd != -1) close(output_fd);
				if(error_fd != -1) close(error_fd);
				close(*access_fd);
				*access_fd = -1;
				return -1;
			}
		}
	}

	if((dev_null = open("/dev/null", O_RDWR, 0666)) == -1) {
		perror("Cannot open /dev/null");
		if(input_fd != -1) close(input_fd);
		if(output_fd != -1) close(output_fd);
		if(error_fd != -1) close(error_fd);
		if(access && access_fd && *access_fd != -1) {
			close(*access_fd);
			*access_fd = -1;
			if(access_fp) {
				fclose(*access_fp);
				*access_fp = NULL;
			}
		}
		return -1;
	}

	// all files opened
	// lets do it

	if(!dont_fork) {
		int i = fork();
		if(i == -1) {
			perror("cannot fork");
			exit(1);
		}
		if(i != 0) {
			exit(0); // the parent
		}

		// become session leader
		if (setsid() < 0) {
			perror("Cannot become session leader.");
			exit(2);
		}
	}

	signal(SIGCHLD,  SIG_IGN);
	signal(SIGHUP,   SIG_IGN);
	signal(SIGWINCH, SIG_IGN);

	// fork() again
	if(!dont_fork) {
		int i = fork();
		if(i == -1) {
			perror("cannot fork");
			exit(1);
		}
		if(i != 0) {
			exit(0); // the parent
		}
	}

	// Set new file permissions
	umask(0);

	// close all files
	if(close_all_files) {
		int i;
		for(i = (int) (sysconf(_SC_OPEN_MAX) - 1); i > 0; i--)
			if(
				((access_fd && i != *access_fd) || !access_fd)
				&& i != dev_null
				&& i != input_fd
				&& i != output_fd
				&& i != error_fd
				&& fd_is_valid(i)
				) close(i);
	}
	else {
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
	}

	// put the opened files
	// to our standard file descriptors
	if(input_fd != -1) {
		if(input_fd != STDIN_FILENO) {
			dup2(input_fd, STDIN_FILENO);
			close(input_fd);
		}
		input_fd = -1;
	}
	else dup2(dev_null, STDIN_FILENO);

	if(output_fd != -1) {
		if(output_fd != STDOUT_FILENO) {
			dup2(output_fd, STDOUT_FILENO);
			close(output_fd);
		}
		output_fd = -1;
	}
	else dup2(dev_null, STDOUT_FILENO);

	if(error_fd != -1) {
		if(error_fd != STDERR_FILENO) {
			dup2(error_fd, STDERR_FILENO);
			close(error_fd);
		}
		error_fd = -1;
	}
	else dup2(dev_null, STDERR_FILENO);

	// close /dev/null
	if(dev_null != STDIN_FILENO && dev_null != STDOUT_FILENO && dev_null != STDERR_FILENO)
		close(dev_null);

	// generate our pid file
	if(pidfile[0]) {
		pidfd = open(pidfile, O_RDWR | O_CREAT, 0644);
		if(pidfd >= 0) {
			if(ftruncate(pidfd, 0) != 0)
				error("Cannot truncate pidfile '%s'.", pidfile);

			char b[100];
			sprintf(b, "%d\n", getpid());
			ssize_t i = write(pidfd, b, strlen(b));
			if(i <= 0)
				error("Cannot write pidfile '%s'.", pidfile);

			// don't close it, we might need it at exit
			// close(pidfd);
		}
	}

	if(user && *user) {
		if(become_user(user) != 0) {
			error("Cannot become user '%s'. Continuing as we are.", user);
		}
		else info("Successfully became user '%s'.", user);
	}
	else if(pidfd != -1)
		close(pidfd);

	return(0);
}
