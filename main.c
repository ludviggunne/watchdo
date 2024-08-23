#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/wait.h>

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
			if (strcmp(flag, "ACCESS") == 0) {
				mask |= IN_ACCESS;
				continue;
			}
			if (strcmp(flag, "MODIFY") == 0) {
				mask |= IN_MODIFY;
				continue;
			}
			if (strcmp(flag, "ATTRIB") == 0) {
				mask |= IN_ATTRIB;
				continue;
			}
			if (strcmp(flag, "CLOSE_WRITE") == 0) {
				mask |= IN_CLOSE_WRITE;
				continue;
			}
			if (strcmp(flag, "CLOSE_NOWRITE") == 0) {
				mask |= IN_CLOSE_NOWRITE;
				continue;
			}
			if (strcmp(flag, "CLOSE") == 0) {
				mask |= IN_CLOSE;
				continue;
			}
			if (strcmp(flag, "OPEN") == 0) {
				mask |= IN_OPEN;
				continue;
			}
			if (strcmp(flag, "MOVED_FROM") == 0) {
				mask |= IN_MOVED_FROM;
				continue;
			}
			if (strcmp(flag, "MOVED_TO") == 0) {
				mask |= IN_MOVED_TO;
				continue;
			}
			if (strcmp(flag, "MOVE") == 0) {
				mask |= IN_MOVE;
				continue;
			}
			if (strcmp(flag, "DELETE_SELF") == 0) {
				mask |= IN_DELETE_SELF;
				continue;
			}
			if (strcmp(flag, "MOVE_SELF") == 0) {
				mask |= IN_MOVE_SELF;
				continue;
			}
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
	int inotfd = inotify_init();
	if (inotfd < 0) {
		perror("inotify_init");
		exit(1);
	}
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

	for (;;) {
		const struct inotify_event *event;
		size_t file_id = 0;
		size_t len = 0;

		// read inotify event and get corresponding filename
		while (len < sizeof(struct inotify_event)) {
			len += read(inotfd, event_buf, sizeof(event_buf) - len);
		}
		event = (const struct inotify_event *)event_buf;
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
		int wd = wds[file_id];
		wds[file_id] = -1;
		inotify_rm_watch(inotfd, wd);

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

		// re-add watch, ignore errors
		wds[file_id] = inotify_add_watch(inotfd, files[file_id], mask);
	}
}
