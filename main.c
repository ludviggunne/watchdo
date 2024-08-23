#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/wait.h>
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

int main(int argc, char **argv)
{

	if (argc < 4) {
		usage(stderr, argv[0]);
		exit(1);
	}
	// parse command line arguments
	char **cmd = NULL;
	char **files = NULL;
	size_t file_count = 0;
	char **argp = argv + 1;
	int mask = 0;
	files = malloc(argc * sizeof(*files));
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

		files[file_count++] = *argp;
	}
	if (!cmd || !mask || !file_count) {
		usage(stderr, argv[0]);
		exit(1);
	}
	// create copy of command, for substituting filenames
	size_t cmd_len = 0;
	char **cmd_cpy = NULL;
	for (argp = cmd; *argp; argp++) {
		cmd_len++;
	}
	cmd_cpy = malloc(cmd_len * sizeof(*cmd_cpy));
	if (!cmd_cpy) {
		perror("malloc");
		exit(1);
	}
	memcpy(cmd_cpy, cmd, cmd_len * sizeof(*cmd_cpy));

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

		if (event->mask & IN_IGNORED)
			continue;
		if ((event->mask & mask) == 0)
			continue;
		for (size_t i = 0; i < file_count; i++) {
			if (wds[i] == event->wd) {
				file_id = i;
				break;
			}
		}

		// substitute {} for filename in command
		for (size_t i = 0; i < cmd_len; i++) {
			if (strcmp(cmd[i], "{}") == 0) {
				cmd_cpy[i] = files[file_id];
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
			return 1;
		}

		if (pid == 0) {
			if (execvp(cmd_cpy[0], cmd_cpy) < 0) {
				perror(cmd_cpy[0]);
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
