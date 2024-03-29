
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <asm/byteorder.h>

#include <talloc/talloc.h>
#include <list/list.h>
#include <log/log.h>

#include "pb-protocol.h"


/* Message format:
 *
 * 4-byte action, determines the remaining message content
 * 4-byte total payload len
 *   - not including action and payload len header
 *
 * action = 0x1: device add message
 *  payload:
 *   4-byte len, id
 *   4-byte len, name
 *   4-byte len, description
 *   4-byte len, icon_file
 *
 *   4-byte option count
 *   for each option:
 *    4-byte len, id
 *    4-byte len, name
 *    4-byte len, description
 *    4-byte len, icon_file
 *    4-byte len, boot_image_file
 *    4-byte len, initrd_file
 *    4-byte len, boot_args
 *
 * action = 0x2: device remove message
 *  payload:
 *   4-byte len, id
 *
 * action = 0x3: boot
 *  payload:
 *   4-byte len, boot option id
 *   4-byte len, boot_image_file
 *   4-byte len, initrd_file
 *   4-byte len, boot_args
 *
 */

void pb_protocol_dump_device(const struct device *dev, const char *text,
	FILE *stream)
{
	struct boot_option *opt;

	fprintf(stream, "%snew dev:\n", text);
	fprintf(stream, "%s\tid:   %s\n", text, dev->id);
	fprintf(stream, "%s\tname: %s\n", text, dev->name);
	fprintf(stream, "%s\tdesc: %s\n", text, dev->description);
	fprintf(stream, "%s\ticon: %s\n", text, dev->icon_file);
	fprintf(stream, "%s\tboot options:\n", text);
	list_for_each_entry(&dev->boot_options, opt, list) {
		fprintf(stream, "%s\t\tid:   %s\n", text, opt->id);
		fprintf(stream, "%s\t\tname: %s\n", text, opt->name);
		fprintf(stream, "%s\t\tdesc: %s\n", text, opt->description);
		fprintf(stream, "%s\t\ticon: %s\n", text, opt->icon_file);
		fprintf(stream, "%s\t\tboot: %s\n", text, opt->boot_image_file);
		fprintf(stream, "%s\t\tinit: %s\n", text, opt->initrd_file);
		fprintf(stream, "%s\t\targs: %s\n", text, opt->boot_args);
	}
}

int pb_protocol_device_cmp(const struct device *a, const struct device *b)
{
	return !strcmp(a->id, b->id);
}

int pb_protocol_boot_option_cmp(const struct boot_option *a,
	const struct boot_option *b)
{
	return !strcmp(a->id, b->id);
}

/* Write a string into the buffer, starting at pos.
 *
 * Returns the total length used for the write, including length header.
 */
int pb_protocol_serialise_string(char *pos, const char *str)
{
	int len = 0;

	if (str)
		len = strlen(str);

	*(uint32_t *)pos = __cpu_to_be32(len);
	pos += sizeof(uint32_t);

	memcpy(pos, str, len);

	return len + sizeof(uint32_t);
}

/* Read a string from a buffer, allocating the new string as necessary.
 *
 * @param[in] ctx	The talloc context to base the allocation on
 * @param[in,out] pos	Where to start reading
 * @param[in,out] len	The amount of data remaining in the buffer
 * @param[out] str	Pointer to resuling string
 * @return		zero on success, non-zero on failure
 */
static int read_string(void *ctx, const char **pos, unsigned int *len,
	char **str)
{
	uint32_t str_len, read_len;

	if (*len < sizeof(uint32_t))
		return -1;

	str_len = __be32_to_cpu(*(uint32_t *)(*pos));
	read_len = sizeof(uint32_t);

	if (read_len + str_len > *len)
		return -1;

	if (str_len == 0)
		*str = NULL;
	else
		*str = talloc_strndup(ctx, *pos + read_len, str_len);

	read_len += str_len;

	/* all ok, update the caller's pointers */
	*pos += read_len;
	*len -= read_len;

	return 0;
}

char *pb_protocol_deserialise_string(void *ctx,
		const struct pb_protocol_message *message)
{
	const char *buf;
	char *str;
	unsigned int len;

	len = message->payload_len;
	buf = message->payload;

	if (read_string(ctx, &buf, &len, &str))
		return NULL;

	return str;
}

static int optional_strlen(const char *str)
{
	if (!str)
		return 0;
	return strlen(str);
}

int pb_protocol_device_len(const struct device *dev)
{
	return	4 + optional_strlen(dev->id) +
		4 + optional_strlen(dev->name) +
		4 + optional_strlen(dev->description) +
		4 + optional_strlen(dev->icon_file);
}

int pb_protocol_boot_option_len(const struct boot_option *opt)
{

	return	4 + optional_strlen(opt->device_id) +
		4 + optional_strlen(opt->id) +
		4 + optional_strlen(opt->name) +
		4 + optional_strlen(opt->description) +
		4 + optional_strlen(opt->icon_file) +
		4 + optional_strlen(opt->boot_image_file) +
		4 + optional_strlen(opt->initrd_file) +
		4 + optional_strlen(opt->boot_args);
}

int pb_protocol_boot_len(const struct boot_command *boot)
{
	return  4 + optional_strlen(boot->option_id) +
		4 + optional_strlen(boot->boot_image_file) +
		4 + optional_strlen(boot->initrd_file) +
		4 + optional_strlen(boot->boot_args);
}

int pb_protocol_boot_status_len(const struct boot_status *status)
{
	return  4 +
		4 + optional_strlen(status->message) +
		4 + optional_strlen(status->detail) +
		4;
}

int pb_protocol_serialise_device(const struct device *dev,
		char *buf, int buf_len)
{
	char *pos = buf;

	pos += pb_protocol_serialise_string(pos, dev->id);
	pos += pb_protocol_serialise_string(pos, dev->name);
	pos += pb_protocol_serialise_string(pos, dev->description);
	pos += pb_protocol_serialise_string(pos, dev->icon_file);

	assert(pos <= buf + buf_len);
	(void)buf_len;

	return 0;
}

int pb_protocol_serialise_boot_option(const struct boot_option *opt,
		char *buf, int buf_len)
{
	char *pos = buf;

	pos += pb_protocol_serialise_string(pos, opt->device_id);
	pos += pb_protocol_serialise_string(pos, opt->id);
	pos += pb_protocol_serialise_string(pos, opt->name);
	pos += pb_protocol_serialise_string(pos, opt->description);
	pos += pb_protocol_serialise_string(pos, opt->icon_file);
	pos += pb_protocol_serialise_string(pos, opt->boot_image_file);
	pos += pb_protocol_serialise_string(pos, opt->initrd_file);
	pos += pb_protocol_serialise_string(pos, opt->boot_args);

	assert(pos <= buf + buf_len);
	(void)buf_len;

	return 0;
}

int pb_protocol_serialise_boot_command(const struct boot_command *boot,
		char *buf, int buf_len)
{
	char *pos = buf;

	pos += pb_protocol_serialise_string(pos, boot->option_id);
	pos += pb_protocol_serialise_string(pos, boot->boot_image_file);
	pos += pb_protocol_serialise_string(pos, boot->initrd_file);
	pos += pb_protocol_serialise_string(pos, boot->boot_args);

	assert(pos <= buf + buf_len);
	(void)buf_len;

	return 0;
}

int pb_protocol_serialise_boot_status(const struct boot_status *status,
		char *buf, int buf_len)
{
	char *pos = buf;

	*(uint32_t *)pos = __cpu_to_be32(status->type);
	pos += sizeof(uint32_t);

	pos += pb_protocol_serialise_string(pos, status->message);
	pos += pb_protocol_serialise_string(pos, status->detail);

	*(uint32_t *)pos = __cpu_to_be32(status->type);
	pos += sizeof(uint32_t);

	assert(pos <= buf + buf_len);
	(void)buf_len;

	return 0;
}

int pb_protocol_write_message(int fd, struct pb_protocol_message *message)
{
	int total_len, rc;
	char *pos;

	total_len = sizeof(*message) + message->payload_len;

	message->payload_len = __cpu_to_be32(message->payload_len);
	message->action = __cpu_to_be32(message->action);

	for (pos = (void *)message; total_len;) {
		rc = write(fd, pos, total_len);

		if (rc <= 0)
			break;

		total_len -= rc;
		pos += rc;
	}

	talloc_free(message);

	if (!total_len)
		return 0;

	pb_log("%s: failed: %s\n", __func__, strerror(errno));
	return -1;
}

struct pb_protocol_message *pb_protocol_create_message(void *ctx,
		enum pb_protocol_action action, int payload_len)
{
	struct pb_protocol_message *message;

	if (payload_len > PB_PROTOCOL_MAX_PAYLOAD_SIZE) {
		pb_log("%s: payload too big %u/%u\n", __func__, payload_len,
			PB_PROTOCOL_MAX_PAYLOAD_SIZE);
		return NULL;
	}

	message = talloc_size(ctx, sizeof(*message) + payload_len);

	/* we convert these to big-endian in write_message() */
	message->action = action;
	message->payload_len = payload_len;

	return message;

}

struct pb_protocol_message *pb_protocol_read_message(void *ctx, int fd)
{
	struct pb_protocol_message *message, m;
	int rc;
	unsigned int len;

	/* use the stack for the initial 8-byte read */

	rc = read(fd, &m, sizeof(m));
	if (rc != sizeof(m))
		return NULL;

	m.payload_len = __be32_to_cpu(m.payload_len);
	m.action = __be32_to_cpu(m.action);

	if (m.payload_len > PB_PROTOCOL_MAX_PAYLOAD_SIZE) {
		pb_log("%s: payload too big %u/%u\n", __func__, m.payload_len,
			PB_PROTOCOL_MAX_PAYLOAD_SIZE);
		return NULL;
	}

	message = talloc_size(ctx, sizeof(m) + m.payload_len);
	memcpy(message, &m, sizeof(m));

	for (len = 0; len < m.payload_len;) {
		rc = read(fd, message->payload + len, m.payload_len - len);

		if (rc <= 0) {
			talloc_free(message);
			pb_log("%s: failed (%u): %s\n", __func__, len,
				strerror(errno));
			return NULL;
		}

		len += rc;
	}

	return message;
}


int pb_protocol_deserialise_device(struct device *dev,
		const struct pb_protocol_message *message)
{
	unsigned int len;
	const char *pos;
	int rc = -1;

	len = message->payload_len;
	pos = message->payload;

	if (read_string(dev, &pos, &len, &dev->id))
		goto out;

	if (read_string(dev, &pos, &len, &dev->name))
		goto out;

	if (read_string(dev, &pos, &len, &dev->description))
		goto out;

	if (read_string(dev, &pos, &len, &dev->icon_file))
		goto out;

	rc = 0;

out:
	return rc;
}

int pb_protocol_deserialise_boot_option(struct boot_option *opt,
		const struct pb_protocol_message *message)
{
	unsigned int len;
	const char *pos;
	int rc = -1;

	len = message->payload_len;
	pos = message->payload;

	if (read_string(opt, &pos, &len, &opt->device_id))
		goto out;

	if (read_string(opt, &pos, &len, &opt->id))
		goto out;

	if (read_string(opt, &pos, &len, &opt->name))
		goto out;

	if (read_string(opt, &pos, &len, &opt->description))
		goto out;

	if (read_string(opt, &pos, &len, &opt->icon_file))
		goto out;

	if (read_string(opt, &pos, &len, &opt->boot_image_file))
		goto out;

	if (read_string(opt, &pos, &len, &opt->initrd_file))
		goto out;

	if (read_string(opt, &pos, &len, &opt->boot_args))
		goto out;

	rc = 0;

out:
	return rc;
}

int pb_protocol_deserialise_boot_command(struct boot_command *cmd,
		const struct pb_protocol_message *message)
{
	unsigned int len;
	const char *pos;
	int rc = -1;

	len = message->payload_len;
	pos = message->payload;

	if (read_string(cmd, &pos, &len, &cmd->option_id))
		goto out;

	if (read_string(cmd, &pos, &len, &cmd->boot_image_file))
		goto out;

	if (read_string(cmd, &pos, &len, &cmd->initrd_file))
		goto out;

	if (read_string(cmd, &pos, &len, &cmd->boot_args))
		goto out;

	rc = 0;

out:
	return rc;
}

int pb_protocol_deserialise_boot_status(struct boot_status *status,
		const struct pb_protocol_message *message)
{
	unsigned int len;
	const char *pos;
	int rc = -1;

	len = message->payload_len;
	pos = message->payload;

	/* first up, the type enum... */
	if (len < sizeof(uint32_t))
		goto out;

	status->type = __be32_to_cpu(*(uint32_t *)(pos));

	switch (status->type) {
	case BOOT_STATUS_ERROR:
	case BOOT_STATUS_INFO:
		break;
	default:
		goto out;
	}

	pos += sizeof(uint32_t);
	len -= sizeof(uint32_t);

	/* message and detail strings */
	if (read_string(status, &pos, &len, &status->message))
		goto out;

	if (read_string(status, &pos, &len, &status->detail))
		goto out;

	/* and finally, progress */
	if (len < sizeof(uint32_t))
		goto out;

	status->progress = __be32_to_cpu(*(uint32_t *)(pos));

	/* clamp to 100% */
	if (status->progress > 100)
		status->progress = 100;

	rc = 0;

out:
	return rc;
}
