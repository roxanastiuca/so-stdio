#include "utils.h"
#include "so_stdio.h"

BOOL xwrite(HANDLE handle, const void *buf, size_t count, int *bytes_wrote);

/*
 * Structure for a FILE stream.
 */
typedef struct _so_file {
	HANDLE handle; /* file handler */

	PROCESS_INFORMATION process; /* in case of opening through popen */

	char rbuffer[SO_BUFSIZE]; /* read buffer */
	int roffset; /* offset in read buffer */
	int rsize; /* number of bytes read in rbuffer */
	int rerror; /* 0 if last read succeeded / SO_EOF if not */

	char wbuffer[SO_BUFSIZE]; /* write buffer */
	int woffset; /* offset in write buffer */
	int werror; /* 0 if last write succeeded / SO_EOF if not */
} SO_FILE;

/*
 * Description: opens a file in a given mode.
 * Return: stream/NULL if anything fails (memory allocation, file open).
 */
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
		stream->handle = CreateFile(pathname,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL, NULL);
	} else if (strcmp(mode, "w") == 0) {
		stream->handle = CreateFile(pathname, GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	} else if (strcmp(mode, "w+") == 0) {
		stream->handle = CreateFile(pathname,
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, CREATE_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, NULL);
	} else if (strcmp(mode, "a") == 0) {
		stream->handle = CreateFile(pathname,
			FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, NULL);
	} else if (strcmp(mode, "a+") == 0) {
		stream->handle = CreateFile(pathname,
			GENERIC_READ | FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			NULL, OPEN_ALWAYS,
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

/*
 * Description: loads read buffer with data from file.
 * Return: number of bytes read/negative number if read fails.
 */
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

/*
 * Description: unloads data from write buffer to file.
 * Return: number of bytes wrote/0 or negative number if write fails.
 */
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

/*
 * Description: unloads buffers, closes file and frees memory for a stream.
 * Return: 0 for no error/SO_EOF.
 */
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

/*
 * Description: get file handle.
 */
HANDLE so_fileno(SO_FILE *stream)
{
	return stream->handle;
}

/*
 * Description: flush the contents of write buffer.
 * Return: 0/SO_EOF.
 */
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

/*
 * Description: move file cursor position.
 * Return: 0 if succes/-1 fail.
 */
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

/*
 * Description: get file cursor position.
 * Return: position/-1 if fail.
 */
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

/*
 * Description: reads nmemb elements of given size from a stream and puts
 read bytes to ptr.
 * Return: number of elements read/SO_EOF.
 */
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
			memcpy((char *)ptr + offset,
				stream->rbuffer + stream->roffset,
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
			memcpy(stream->wbuffer + stream->woffset,
				(char *)ptr + offset,
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
 * Description: reads one character from stream. Tries first to read it from
 buffer, but if buffer is not loaded or it was already read, reloads the
 read buffer.
 * Return: the character read/SO_EOF.
 */
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

/*
 * Description: writes one character to stream. Tries first to put it into
 the buffer, but if write buffer is full, it must unload it first.
 * Return: the character wrote/SO_EOF.
 */
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

/*
 * Description: check if EOF.
 * Return: 0 if not EOF/!=0 if EOF.
 */
int so_feof(SO_FILE *stream)
{
	return stream->rerror;
}

/*
 * Description: check if last operation resulted in an error.
 * Return: 0 if no error/!=0 if error.
 */
int so_ferror(SO_FILE *stream)
{
	return (stream->rerror | stream->werror);
}

/*
 * Description: redirect file handle to. Taken from Lab03 Sol.
 * @psi		- STATRTUPINFO of the child process
 * @hFile	- file handle for redirect
 * @opt		- redirect option is one of the following
 *		 STD_INPUT_HANDLE,STD_OUTPUT_HANDLE, STD_ERROR_HANDLE
 */
static VOID RedirectHandle(STARTUPINFO *psi, HANDLE hFile, INT opt)
{
	if (hFile == INVALID_HANDLE_VALUE)
		return;

	/* TODO - Redirect */
	ZeroMemory(psi, sizeof(*psi));
	psi->cb = sizeof(*psi);

	psi->hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	psi->hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	psi->hStdError = GetStdHandle(STD_ERROR_HANDLE);

	psi->dwFlags |= STARTF_USESTDHANDLES;

	switch (opt) {
	case STD_INPUT_HANDLE:
		psi->hStdInput = hFile;
		break;
	case STD_OUTPUT_HANDLE:
		psi->hStdOutput = hFile;
		break;
	case STD_ERROR_HANDLE:
		psi->hStdError = hFile;
		break;
	}
}

/*
 * Description: launch new process, creating a pipe, forking and
 executing the given command.
 * Return: stream that is either read-only or write-only/NULL if error.
 */
SO_FILE *so_popen(const char *command, const char *type)
{
	SO_FILE *stream;
	HANDLE rhandle, whandle;
	SECURITY_ATTRIBUTES sa;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	BOOL ret;
	char commArgs[SO_BUFSIZE];

	strcpy(commArgs, "C:\\windows\\system32\\cmd.exe /c ");
	strcat(commArgs, command);

	stream = (SO_FILE *) calloc(1, sizeof(SO_FILE));
	if (stream == NULL)
		return NULL;

	if (strcmp(type, "r") != 0 && strcmp(type, "w") != 0) {
		/* Unknown type */
		free(stream);
		return NULL;
	}

	/* Set security attributes: */
	ZeroMemory(&sa, sizeof(sa));
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle = TRUE;

	/* Init process info: */
	ZeroMemory(&pi, sizeof(pi));
	
	/* Init startup info: */
	GetStartupInfo(&si);

	/* Create pipe: */
	ret = CreatePipe(&rhandle, &whandle, &sa, 0);
	if (ret == FALSE) {
		free(stream);
		return NULL;
	}

	if (strcmp(type, "r") == 0) {
		stream->handle = rhandle;
		RedirectHandle(&si, whandle, STD_OUTPUT_HANDLE);
		ret = SetHandleInformation(rhandle, HANDLE_FLAG_INHERIT, 0);
	} else if (strcmp(type, "w") == 0) {
		stream->handle = whandle;
		RedirectHandle(&si, rhandle, STD_INPUT_HANDLE);
		si.hStdInput = rhandle;
		ret = SetHandleInformation(whandle, HANDLE_FLAG_INHERIT, 0);
	}

	/* Create process: */
	ret = CreateProcess(
		NULL,
		command,
		NULL,
		NULL,
		TRUE,
		0,
		NULL,
		NULL,
		&si,
		&pi
		);

	if (strcmp(type, "r") == 0)
		CloseHandle(whandle);
	else
		CloseHandle(rhandle);

	if (ret == 0) {
		free(stream);
		return NULL;
	}

	stream->process = pi;

	return stream;
}

/*
 * Description: waits for child process, closes files and frees
 memory for stream opened through popen.
 * Return: 0 for no error/-1 error.
 */
int so_pclose(SO_FILE *stream)
{
	int rc;
	BOOL ret;
	PROCESS_INFORMATION pi = stream->process;
	HANDLE handle = stream->handle;

	/* Flush anything in write buffer: */
	if (stream->woffset != 0) {
		rc = unload_wbuffer(stream);
		if (rc <= 0) {
			free(stream);
			return rc;
		}
	}

	free(stream);

	rc = CloseHandle(handle);

	ret = WaitForSingleObject(pi.hProcess, INFINITE);
	if (ret == WAIT_FAILED) {
		printf("last error %d\n", GetLastError());
		return -1;
	}

	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

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

		ret = WriteFile(handle, (char *)buf + bytes_written,
			count - bytes_written,
			&bytes_written_now, NULL);

		if (ret == FALSE) /* I/O error */
			return FALSE;

		bytes_written += bytes_written_now;
	}

	*bytes_wrote = bytes_written;
	return TRUE;
}
