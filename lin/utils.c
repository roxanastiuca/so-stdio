#include "utils.h"

/*
 * Description: Implementation for xread. Makes sure exactly count bytes
 were read (except in case of I/O or EOF).
 */
ssize_t xread(int fd, void *buf, size_t count)
{
	size_t bytes_read = 0;

	while (bytes_read < count) {
		ssize_t bytes_read_now = read(fd, buf + bytes_read,
									  count - bytes_read);

		if (bytes_read_now == 0) /* EOF */
			return bytes_read;

		if (bytes_read_now < 0) /* I/O error */
			return -1;

		bytes_read += bytes_read_now;
	}

	return bytes_read;
}

/*
 * Description: Implementation for write. Makes sure exactly count bytes
 are written (except for I/O error).
 */
ssize_t xwrite(int fd, const void *buf, size_t count)
{
	size_t bytes_written = 0;

	while (bytes_written < count) {
		ssize_t bytes_written_now = write(fd, buf + bytes_written,
										  count - bytes_written);

		if (bytes_written_now <= 0) /* I/O error */
			return -1;

		bytes_written += bytes_written_now;
	}

	return bytes_written;
}
