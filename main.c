#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <glob.h>
#include <errno.h>
#include <limits.h>

// convenience macro for parsing flags
#define PF(f)\
	if (strcmp(flag, #f) == 0) {\
		mask |= IN_##f;\
		continue;\
	}

// inotify instance
int inotfd = -1;

void cleanup(void)
{
	if (inotfd >= 0)
		close(inotfd);
}

void usage(FILE *f, char *name)
{
	fprintf(f, "Usage: %s FLAG(S)... FILE(s)... -- COMMAND\n", name);
}

char *subst(char *str, char *with)
{
	char *ptr, *res, *resptr;
	int occurs = 0;
	int withlen = strlen(with);

	for (ptr = str; *ptr; ptr++) {
		if (*ptr == '{' && *(ptr + 1) == '}'
		    && !(ptr > str && *(ptr - 1) == '\\')) {
			occurs++;
			ptr++;
		}
	}

	res = malloc(strlen(str) + occurs * (withlen - 2) + 1);
	if (!res) {
		perror("malloc");
		exit(1);
	}

	resptr = res;
	for (ptr = str; *ptr; ptr++) {
		if (*ptr == '{' && *(ptr + 1) == '}') {
			if (ptr > str && *(ptr - 1) == '\\') {
				*(resptr - 1) = '{';
				continue;
			}
			ptr++;
			memcpy(resptr, with, withlen);
			resptr += withlen;
			continue;
		}

		*(resptr++) = *(ptr);
	}

	*resptr = '\0';
	return res;
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		usage(stderr, argv[0]);
		exit(1);
	}
	// parse command line arguments
	char **cmd = NULL;
	char **files = NULL;
	glob_t glob_result = { 0 };
	size_t file_count = 0;
	char **argp = argv + 1;
	int mask = 0;
	for (; *argp; argp++) {
		if (strcmp(*argp, "--") == 0) {
			*argp = NULL;
			argp++;
			if (!*argp) {
				usage(stderr, argv[0]);
				exit(1);
			}
			cmd = argp;
			break;
		}

		if (*argp[0] == '-') {
			char *flag = *argp + 1;
			PF(ACCESS);
			PF(MODIFY);
			PF(ATTRIB);
			PF(CLOSE_WRITE);
			PF(CLOSE_NOWRITE);
			PF(CLOSE);
			PF(OPEN);
			PF(MOVED_FROM);
			PF(MOVED_TO);
			PF(MOVE);
			PF(DELETE_SELF);
			PF(MOVE_SELF);
			usage(stderr, argv[0]);
			fprintf(stderr, "invalid flag: %s\n", *argp);
			exit(1);
		}
		// expand wildcard patterns
		int res;
		if (glob_result.gl_pathv) {
			res = glob(*argp, GLOB_APPEND, NULL, &glob_result);
		} else {
			res = glob(*argp, 0, NULL, &glob_result);
		}
		switch (res) {
		case GLOB_NOSPACE:
		case GLOB_ABORTED:
			perror("glob");
			exit(1);
		case GLOB_NOMATCH:
			fprintf(stderr, "no matches for pattern %s\n", *argp);
			exit(1);
		default:
			break;
		}
	}
	files = glob_result.gl_pathv;
	file_count = glob_result.gl_pathc;
	if (!mask || !files || !cmd) {
		usage(stderr, argv[0]);
		exit(1);
	}
	// avoid duplicate watch descriptors
	mask |= IN_MASK_CREATE;

	// initialize inotify
	inotfd = inotify_init();
	if (inotfd < 0) {
		perror("inotify_init");
		exit(1);
	}
	atexit(cleanup);

	// create watches
	int *wds = NULL;
	wds = malloc(file_count * sizeof(*wds));
	int ok = 1;
	for (size_t i = 0; i < file_count; i++) {
		wds[i] = inotify_add_watch(inotfd, files[i], mask);
		if (wds[i] == EEXIST) {
			wds[i] = -1;
			continue;
		}

		if (wds[i] < 0) {
			perror(files[i]);
			ok = 0;
		}
	}
	if (!ok)
		exit(1);

	char event_buf[sizeof(struct inotify_event)]
	    __attribute__((aligned(__alignof__(struct inotify_event))));
	char name_buf[PATH_MAX + 1];

	for (;;) {
		const struct inotify_event *event;
		size_t file_id = 0;
		size_t len = 0;

		// read inotify event
		while (len < sizeof(event_buf)) {
			len += read(inotfd, event_buf, sizeof(event_buf) - len);
		}
		event = (const struct inotify_event *)event_buf;

		// read name field
		len = 0;
		while (len < event->len) {
			len += read(inotfd, name_buf, sizeof(name_buf) - len);
		}

		if (event->mask & (IN_IGNORED | IN_Q_OVERFLOW))
			continue;
		if ((event->mask & mask) == 0)
			continue;
		for (size_t i = 0; i < file_count; i++) {
			if (wds[i] == event->wd) {
				file_id = i;
				break;
			}
		}

		// remove watch temporarily, in case command
		// triggers an event
		inotify_rm_watch(inotfd, wds[file_id]);
		wds[file_id] = -1;

		// run command
		pid_t pid = fork();
		if (pid < 0) {
			perror("fork");
			exit(1);
		}

		if (pid == 0) {
			// substitute filenames in child process so we don't
			// clobber parents argv
			for (argp = cmd; *argp; argp++) {
				*argp = subst(*argp, files[file_id]);
			}

			if (execvp(cmd[0], cmd) < 0) {
				perror(cmd[0]);
				exit(1);
			}
		}

		int status;
		wait(&status);
		(void)status;

		// re-add watch
		wds[file_id] = inotify_add_watch(inotfd, files[file_id], mask);
	}
}
