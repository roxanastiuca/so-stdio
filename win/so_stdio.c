#include "utils.h"
#include "so_stdio.h"

BOOL xwrite(HANDLE handle, const void *buf, size_t count, int *bytes_wrote);

typedef struct _so_file {
	HANDLE handle; /* file handler */

	int pid; /* the process ID, in case of opening through popen */

	char rbuffer[SO_BUFSIZE]; /* read buffer */
	int roffset; /* offset in read buffer */
	int rsize; /* number of bytes read in rbuffer */
	int rerror; /* 0 if last read succeeded / SO_EOF if not */

	char wbuffer[SO_BUFSIZE]; /* write buffer */
	int woffset; /* offset in write buffer */
	int werror; /* 0 if last write succeeded / SO_EOF if not */
} SO_FILE;

SO_FILE *so_fopen(const char *pathname, const char *mode)
{
	SO_FILE *stream = (SO_FILE *) calloc(1, sizeof(SO_FILE));

	if (stream == NULL)
		return NULL;

	if (strcmp(mode, "r") == 0) {
		stream->handle = CreateFile(pathname, GENERIC_READ,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	} else if (strcmp(mode, "r+") == 0) {
		stream->handle = CreateFile(pathname, GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, NULL);
	} else if (strcmp(mode, "w") == 0) {
		stream->handle = CreateFile(pathname, GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	} else if (strcmp(mode, "w+") == 0) {
		stream->handle = CreateFile(pathname, GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, NULL);
	} else if (strcmp(mode, "a") == 0) {
		stream->handle = CreateFile(pathname, GENERIC_READ | FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, NULL);
	} else if (strcmp(mode, "a+") == 0) {
		stream->handle = CreateFile(pathname, GENERIC_READ | FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, NULL);
	} else {
		/* Unknown mode */
		free(stream);
		return NULL;
	}

	if (stream->handle == INVALID_HANDLE_VALUE) {
		free(stream);
		return NULL;
	}

	return stream;
}

int load_rbuffer(SO_FILE *stream)
{
	BOOL ret;
	int bytes_read;

	ret = ReadFile(stream->handle, stream->rbuffer, SO_BUFSIZE,
		&bytes_read, NULL);

	if (ret == FALSE) {
		stream->rerror = SO_EOF;
		return SO_EOF;
	}

	stream->rsize = bytes_read;
	stream->roffset = 0;
	stream->rerror = (bytes_read > 0) ? 0 : SO_EOF;

	return bytes_read;
}

int unload_wbuffer(SO_FILE *stream)
{
	BOOL ret;
	int bytes_wrote;

	ret = xwrite(stream->handle, stream->wbuffer,
		stream->woffset, &bytes_wrote);
	stream->woffset = 0;

	if (ret == FALSE) {
		stream->werror = SO_EOF;
		return SO_EOF;
	}

	return bytes_wrote;
}

int so_fclose(SO_FILE *stream) 
{
	int rc;

	if (stream->woffset != 0) {
		rc = unload_wbuffer(stream);
		if (rc <= 0) {
			CloseHandle(stream->handle);
			free(stream);
			return rc;
		}
	}

	rc = CloseHandle(stream->handle);
	free(stream);

	return (rc != 0) ? 0 : SO_EOF;
}

HANDLE so_fileno(SO_FILE *stream)
{
	return stream->handle;
}

int so_fflush(SO_FILE *stream)
{
	int bytes_unloaded;

	if (stream->woffset != 0) {
		bytes_unloaded = unload_wbuffer(stream);
		if (bytes_unloaded <= 0)
			return SO_EOF;
	}

	return 0;
}

int so_fseek(SO_FILE *stream, long offset, int whence)
{
	int rc;
	int off;

	/* If anything is in write buffer, unload it: */
	if (stream->woffset != 0) {
		rc = unload_wbuffer(stream);
		if (rc <= 0)
			return -1;
		stream->woffset = 0;
	}

	/* Disregard bytes read in advance in read buffer: */
	if (whence == SEEK_CUR)
		offset -= (stream->rsize - stream->roffset);
	stream->roffset = 0;
	stream->rsize = 0;

	off = SetFilePointer(stream->handle, offset, NULL, whence);
	return (off == INVALID_SET_FILE_POINTER) ? -1 : 0;
}

long so_ftell(SO_FILE *stream)
{
	long off;

	/* Do a lseek from current position: */
	off = SetFilePointer(stream->handle, 0, NULL, SEEK_CUR);
	if (off == INVALID_SET_FILE_POINTER)
		return -1;

	/* If anything was read in advance, disregard it: */
	if (stream->rsize != 0)
		off = off - (stream->rsize - stream->roffset);

	/* If anything is in write buffer, add those bytes: */
	if (stream->woffset != 0)
		off = off + stream->woffset;

	return off;
}

size_t so_fread(void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int i;
	int offset = 0; /* offset in ptr */
	int bytes_loaded;
	size_t to_read; /* number of bytes to read */
	size_t mem_read = 0; /* number of elements read succesfully */

	for (i = 0; i < nmemb; i++) {
		size_t bytes_read_now = 0; /* number of bytes read this time */

		while (bytes_read_now < size) {
			if (stream->roffset == stream->rsize) {
				/* Read buffer must be reloaded first: */
				bytes_loaded = load_rbuffer(stream);
				if (bytes_loaded < 0)
					return 0;
				if (bytes_loaded == 0)
					return mem_read;
			}

			/* Read either size bytes or all buffer: */
			to_read = size;
			if (stream->rsize - stream->roffset < to_read)
				to_read = stream->rsize - stream->roffset;

			/* Copy from buffer to ptr: */
			memcpy((char *)ptr + offset, stream->rbuffer + stream->roffset,
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

size_t so_fwrite(const void *ptr, size_t size, size_t nmemb, SO_FILE *stream)
{
	int i;
	int offset = 0; /* offset in ptr */
	int bytes_unloaded;
	size_t to_write; /* number of bytes to write */
	size_t mem_wrote = 0; /* number of elements wrote succesfully */

	for (i = 0; i < nmemb; i++) {
		size_t bytes_wrote_now = 0;

		while (bytes_wrote_now < size) {
			if (stream->woffset == SO_BUFSIZE) {
				/* Write buffer is full. Unload it first: */
				bytes_unloaded = unload_wbuffer(stream);
				if (bytes_unloaded <= 0)
					return 0;
			}

			/* Write either size bytes or as much as write buffer
			 * has space for:
			 */
			to_write = size;
			if (SO_BUFSIZE - stream->woffset < to_write)
				to_write = SO_BUFSIZE - stream->woffset;

			/* Copy from ptr into write buffer: */
			memcpy(stream->wbuffer + stream->woffset, (char *)ptr + offset,
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

int so_fgetc(SO_FILE *stream)
{
	int rc;
	char c;

	if (stream->roffset == stream->rsize) {
		rc = load_rbuffer(stream);
		if (rc <= 0)
			return SO_EOF;
	}

	c = stream->rbuffer[stream->roffset];
	(stream->roffset)++;

	return c;
}

int so_fputc(int c, SO_FILE *stream)
{
	int rc;

	if (stream->woffset == SO_BUFSIZE) {
		rc = unload_wbuffer(stream);
		if (rc <= 0)
			return SO_EOF;
	}

	stream->wbuffer[stream->woffset] = (char) c;
	(stream->woffset)++;

	return c;
}

int so_feof(SO_FILE *stream)
{
	return stream->rerror;
}

int so_ferror(SO_FILE *stream)
{
	return (stream->rerror | stream->werror);
}

SO_FILE *so_popen(const char *command, const char *type)
{
	return 0;
}

int so_pclose(SO_FILE *stream)
{
	return 0;
}

/*
 * Description: Implementation for write. Makes sure exactly count bytes
 are written (except for I/O error).
 */
BOOL xwrite(HANDLE handle, const void *buf, size_t count, int *bytes_wrote)
{
	size_t bytes_written = 0;
	BOOL ret;

	while (bytes_written < count) {
		size_t bytes_written_now;

		ret = WriteFile(handle, (char *)buf + bytes_written, count - bytes_written,
			&bytes_written_now, NULL);

		if (ret == FALSE) /* I/O error */
			return FALSE;

		bytes_written += bytes_written_now;
	}

	*bytes_wrote = bytes_written;
	return TRUE;
}
