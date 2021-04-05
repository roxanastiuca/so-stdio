#include "utils.h"
#include "so_stdio.h"

/*
 * Strcture for a FILE stream.
 */
typedef struct _so_file {
	int fd; /* file descriptor */
	int flags; /* openning flags */

	int pid; /* the process ID, in case of opening through popen */

	char rbuffer[SO_BUFSIZE]; /* read buffer */
	int roffset; /* offset in read buffer */
	int rsize; /* number of bytes read in rbuffer */
	int rerror; /* 0 if last read succeeded / SO_EOF if not */

	char wbuffer[SO_BUFSIZE]; /* write buffer */
	int woffset; /* offset in write buffer */
	int werror; /* 0 if last write succeeded / SO_EOF if not */
} SO_FILE;

/**
 * Description: opens a file in a given mode.
 * Return: stream/NULL if anything fails (memory allocation, file open).
 */
// SO_FILE *so_fopen(const char *pathname, const char *mode) {
// 	SO_FILE *stream = (SO_FILE *) calloc(1, sizeof(SO_FILE));
// 	if (stream == NULL)	return NULL;

// 	if (strcmp(mode, "r") == 0) {
// 		stream->flags = O_RDONLY;
// 	} else if (strcmp(mode, "r+") == 0) {
// 		stream->flags = O_RDWR | O_CREAT;
// 	} else if (strcmp(mode, "w") == 0) {
// 		stream->flags = O_WRONLY | O_CREAT | O_TRUNC;
// 	} else if (strcmp(mode, "w+") == 0) {
// 		stream->flags = O_RDWR | O_CREAT | O_TRUNC;
// 	} else if (strcmp(mode, "a") == 0) {
// 		stream->flags = O_WRONLY | O_APPEND | O_CREAT;
// 	} else if (strcmp(mode, "a+") == 0) {
// 		stream->flags = O_RDWR | O_APPEND | O_CREAT;
// 	} else {
// 		/* Unknown mode */
// 		free(stream);
// 		return NULL;
// 	}

// 	stream->fd = open(pathname, stream->flags);
// 	if (stream->fd < 0) {
// 		free(stream);
// 		return NULL;
// 	}

// 	return stream;
// }

SO_FILE *so_fopen(const char *pathname, const char *mode)
{
	/* File descriptor for the SO_FILE structure. */
	int fd;
	SO_FILE *f;

	if (strcmp(mode, "r") == 0)
		fd = open(pathname, O_RDONLY);
	else if (strcmp(mode, "r+") == 0)
		fd = open(pathname, O_RDWR | O_CREAT);
	else if (strcmp(mode, "w") == 0)
		fd = open(pathname, O_WRONLY | O_CREAT | O_TRUNC);
	else if (strcmp(mode, "w+") == 0)
		fd = open(pathname, O_RDWR | O_CREAT | O_TRUNC);
	else if (strcmp(mode, "a") == 0)
		fd = open(pathname, O_WRONLY | O_APPEND | O_CREAT);
	else if (strcmp(mode, "a+") == 0)
		fd = open(pathname, O_RDWR | O_APPEND | O_CREAT);
	else
		return NULL;

	/* If open call failed. */
	if (fd < 0)
		return NULL;

	f = (SO_FILE *)calloc(1, sizeof(SO_FILE));

	if (f == NULL)
		return NULL;

	// f->fd = fd;
	// f->read_cnt = SEEK_SET;
	// f->write_cnt = 0;
	// f->last_read_cnt = 0;
	// f->ferror = 0;
	// f->feof = 0;

	return f;
}

/*
 * Description: loads read buffer with data from file.
 * Return: number of bytes read/negative number if read fails.
 */
int load_rbuffer(SO_FILE *stream) {
	int bytes_read = read(stream->fd, stream->rbuffer, SO_BUFSIZE);
	if (bytes_read <= 0) {
		stream->rerror = SO_EOF;
		return bytes_read;
	}

	stream->rsize = bytes_read;
	stream->roffset = 0;
	stream->rerror = 0;

	return bytes_read;
}

/*
 * Description: unloads data from write buffer to file.
 * Return: number of bytes wrote/0 or negative number if write fails.
 */
int unload_wbuffer(SO_FILE *stream) {
	int bytes_wrote = xwrite(stream->fd, stream->wbuffer, stream->woffset);
	stream->woffset = 0;

	if (bytes_wrote <= 0) {
		stream->werror = SO_EOF;
		return bytes_wrote;
	}

	return bytes_wrote;
}

/*
 * Description: unloads buffers, closes file and frees memory for a stream.
 * Return: 0 for no error/SO_EOF.
 */
int so_fclose(SO_FILE *stream) {
	int rc;

	if (stream->woffset != 0) {
		rc = unload_wbuffer(stream);
		if (rc <= 0) {
			free(stream);
			return rc;
		}
	}
	
	rc = close(stream->fd);
	free(stream);

	return (rc < 0) ? SO_EOF : 0;
}

/*
 * Description: reads one character from stream. Tries first to read it from
 buffer, but if buffer is not loaded or it was already read, reloads the
 read buffer.
 * Return: the character read/SO_EOF.
 */
int so_fgetc(SO_FILE *stream) {
	char c;

	if (stream->roffset == stream->rsize) {
		int rc = load_rbuffer(stream);
		if (rc <= 0)	return SO_EOF;
	}

	c = stream->rbuffer[stream->roffset];
	(stream->roffset)++;

	return c;
}

/*
 * Description: writes one character to stream. Tries first to put it into
 the buffer, but if write buffer is full, it must unload it first.
 * Return: the character wrote/SO_EOF.
 */
int so_fputc(int c, SO_FILE *stream) {
	if (stream->woffset == SO_BUFSIZE) {
		int rc = unload_wbuffer(stream);
		if (rc <= 0)	return SO_EOF;
	}

	stream->wbuffer[stream->woffset] = (char) c;
	(stream->woffset)++;

	return c;
}

/*
 * Description: reads nmemb elements of given size from a stream and puts
 read bytes to ptr.
 * Return: number of elements read/SO_EOF.
 */
size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream) {
	int i;
	int offset = 0; /* offset in ptr */
	size_t to_read; /* number of bytes to read */
	size_t mem_read = 0; /* number of elements read succesfully */

	for (i = 0; i < nmemb; i++) {
		size_t bytes_read_now = 0; /* number of bytes read this time */

		while (bytes_read_now < size) {
			if (stream->roffset == stream->rsize) {
				/* Read buffer must be reloaded first: */
				int bytes_loaded = load_rbuffer(stream);
				if (bytes_loaded < 0)	return 0;
				if (bytes_loaded == 0)	return mem_read;
			}

			/* Read either size bytes or all buffer: */
			to_read = size;
			if (stream->rsize - stream->roffset < to_read)
				to_read = stream->rsize - stream->roffset;

			/* Copy from buffer to ptr: */
			memcpy(ptr + offset, stream->rbuffer + stream->roffset,
				to_read);
			stream->roffset += to_read;
			bytes_read_now += to_read;
			offset += to_read;
		}

		if (bytes_read_now > 0)
			mem_read++;
	}

	return mem_read;
}

/*
 * Description: writes nmemb elements of given size from ptr to stream.
 * Return: number of elements succesfully wrote/SO_EOF.
 */
size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream) {
	int i;
	int offset = 0; /* offset in ptr */
	size_t to_write; /* number of bytes to write */
	size_t mem_wrote = 0; /* number of elements wrote succesfully */

	for (i = 0; i < nmemb; i++) {
		size_t bytes_wrote_now = 0;

		while (bytes_wrote_now < size) {
			if (stream->woffset == SO_BUFSIZE) {
				/* Write buffer is full. Unload it first: */
				int bytes_unloaded = unload_wbuffer(stream);
				if (bytes_unloaded <= 0)	return 0;
			}

			/* Write either size bytes or as much as write buffer
			has space for: */
			to_write = size;
			if (SO_BUFSIZE - stream->woffset < to_write)
				to_write = SO_BUFSIZE - stream->woffset;

			/* Copy from ptr into write buffer: */
			memcpy(stream->wbuffer + stream->woffset, ptr + offset,
				to_write);
			stream->woffset += to_write;
			bytes_wrote_now += to_write;
			offset += to_write;
		}

		if (bytes_wrote_now > 0)
			mem_wrote++;
	}

	return mem_wrote;
}

/*
 * Description: move file cursor position.
 * Return: 0 if succes/-1 fail.
 */
int so_fseek(SO_FILE *stream, long offset, int whence) {
	// If anything is in write buffer, unload it:
	if (stream->woffset != 0) {
		int rc = unload_wbuffer(stream);
		if (rc <= 0)	return -1;
		stream->woffset = 0;
	}

	// Disregard bytes read in advance in read buffer:
	if (whence == SEEK_CUR)
		offset -= (stream->rsize - stream->roffset);
	stream->roffset = 0;
	stream->rsize = 0;

	int off = lseek(stream->fd, offset, whence);
	return (off == -1) ? -1 : 0;
}

/*
 * Description: get file cursor position.
 * Return: position/-1 if fail.
 */
long so_ftell(SO_FILE *stream) {
	/* Do a lseek from current position: */
	int off = lseek(stream->fd, 0, SEEK_CUR);
	if (off == -1)	return -1;

	/* If anything was read in advance, disregard it: */
	if (stream->rsize != 0) {
		off = off - (stream->rsize - stream->roffset);
	}

	/* If anything is in write buffer, add those bytes: */
	if (stream->woffset != 0) {
		off = off + stream->woffset;
	}

	return off;
}

/*
 * Description: flush the contents of write buffer.
 * Return: 0/SO_EOF.
 */
int so_fflush(SO_FILE *stream) {
	if (stream->woffset != 0) {
		int bytes_unloaded = unload_wbuffer(stream);
		if (bytes_unloaded <= 0)	return SO_EOF;
	}

	return 0;
}

/*
 * Description: get file descriptor.
 */
int so_fileno(SO_FILE *stream) {
	return stream->fd;
}

/*
 * Description: check if EOF.
 * Return: 0 if not EOF/!=0 if EOF.
 */
int so_feof(SO_FILE *stream) {
	return stream->rerror;
}

/*
 * Description: check if last operation resulted in an error.
 * Return: 0 if no error/!=0 if error.
 */
int so_ferror(SO_FILE *stream) {
	return (stream->rerror | stream->werror);
}

/*
 * Description: launch new process, creating a pipe, forking and
 executing the given command.
 * Return: stream that is either read-only or write-only/NULL if error.
 */
SO_FILE *so_popen(const char *command, const char *type) {
	int rc;

	SO_FILE *stream = (SO_FILE *) calloc(1, sizeof(SO_FILE));
	if (stream == NULL)	return NULL;

	if (strcmp(type, "r") == 0) {
		stream->flags = O_RDONLY;
	} else if (strcmp(type, "w") == 0) {
		stream->flags = O_WRONLY;
	} else {
		/* Unknown type */
		free(stream);
		return NULL;
	}
	
	/* Create pipe: */
	int fds[2];
	rc = pipe(fds);
	if (rc != 0) {
		free(stream);
		return NULL;
	}

	if (stream->flags == O_RDONLY) {
		stream->fd = fds[PIPE_READ];
	} else {
		stream->fd = fds[PIPE_WRITE];
	}

	/* Create process: */
	int pid = fork();
	switch (pid) {
		case -1:
			/* Fork failed. */
			close(fds[PIPE_READ]);
			close(fds[PIPE_WRITE]);
			free(stream);

			return NULL;
		case 0:
			/* Child process */
			if (stream->flags == O_RDONLY) {
				close(fds[PIPE_READ]);
				dup2(fds[PIPE_WRITE], STDOUT_FILENO);
			} else {
				close(fds[PIPE_WRITE]);
				dup2(fds[PIPE_READ], STDIN_FILENO);
			}

			/* Launch command */
			rc = execl("/bin/sh", "sh", "-c", command, (char*)0);
			if (rc)	return NULL;

			break;
		default:
			/* Parent process */
			stream->pid = pid;

			if (stream->flags == O_RDONLY) {
				close(fds[PIPE_WRITE]);
			} else {
				close(fds[PIPE_READ]);
			}

			break;
	}

	return stream;
}

/*
 * Description: waits for child process, closes files and frees
 memory for stream opened through popen.
 * Return: 0 for no error/-1 error.
 */
int so_pclose(SO_FILE *stream) {
	int status, rc;
	int pid = stream->pid;
	int fd = stream->fd;

	/* Flush anything in write buffer: */
	if (stream->woffset != 0) {
		rc = unload_wbuffer(stream);
		if (rc <= 0) {
			free(stream);
			return rc;
		}
	}

	free(stream);
	close(fd);

	rc = waitpid(pid, &status, 0);
	if (rc < 0)	return -1;

	return 0;
}