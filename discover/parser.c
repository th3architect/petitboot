
#include <fcntl.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "types/types.h"
#include <log/log.h>
#include <talloc/talloc.h>

#include "device-handler.h"
#include "parser.h"
#include "parser-utils.h"
#include "paths.h"

struct p_item {
	struct list_item list;
	struct parser *parser;
};

STATIC_LIST(parsers);

static const int max_file_size = 1024 * 1024;

static int read_file(struct discover_context *ctx,
		const char *filename, char **bufp, int *lenp)
{
	struct stat statbuf;
	int rc, fd, i, len;
	char *buf;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
		return -1;

	rc = fstat(fd, &statbuf);
	if (rc < 0)
		goto err_close;

	len = statbuf.st_size;
	if (len > max_file_size)
		goto err_close;

	buf = talloc_array(ctx, char, len + 1);
	if (!buf)
		goto err_close;

	for (i = 0; i < len; i += rc) {
		rc = read(fd, buf + i, len - i);

		/* unexpected EOF: trim and return */
		if (rc == 0) {
			len = i;
			break;
		}

		if (rc < 0)
			goto err_free;

	}

	buf[len] = '\0';

	close(fd);
	*bufp = buf;
	*lenp = len;
	return 0;

err_free:
	talloc_free(buf);
err_close:
	close(fd);
	return -1;
}

static char *local_path(struct discover_context *ctx,
		const char *filename)
{
	return join_paths(ctx, ctx->device->mount_path, filename);
}

static int download_config(struct discover_context *ctx, char **buf, int *len)
{
	unsigned tempfile;
	const char *file;
	int rc;

	file = load_url(ctx, ctx->conf_url, &tempfile);
	if (!file)
		return -1;

	rc = read_file(ctx, file, buf, len);
	if (rc)
		goto out_clean;

	return 0;

out_clean:
	if (tempfile)
		unlink(file);
	return -1;
}

static void iterate_parser_files(struct discover_context *ctx,
		const struct parser *parser)
{
	const char * const *filename;
	const char *path;

	if (!parser->filenames)
		return;

	for (filename = parser->filenames; *filename; filename++) {
		int rc, len;
		char *buf;

		path = local_path(ctx, *filename);
		if (!path)
			continue;

		rc = read_file(ctx, path, &buf, &len);
		if (!rc) {
			pb_log("Running parser %s on file %s\n",
					parser->name, *filename);
			parser->parse(ctx, buf, len);
			talloc_free(buf);
		}
	}
}

void iterate_parsers(struct discover_context *ctx, enum conf_method method)
{
	struct p_item* i;
	int rc, len;
	char *buf;

	pb_log("trying parsers for %s\n", ctx->device->device->id);

	switch (method) {
	case CONF_METHOD_LOCAL_FILE:
		list_for_each_entry(&parsers, i, list) {
			if (i->parser->method != CONF_METHOD_LOCAL_FILE)
				continue;

			pb_log("\ttrying parser '%s'\n", i->parser->name);
			ctx->parser = i->parser;
			iterate_parser_files(ctx, ctx->parser);
		}
		ctx->parser = NULL;
		break;

	case CONF_METHOD_DHCP:
		rc = download_config(ctx, &buf, &len);
		if (rc) {
			pb_log("\tdownload failed, aborting\n");
			return;
		}

		list_for_each_entry(&parsers, i, list) {
			if (i->parser->method != method)
				continue;

			pb_log("\ttrying parser '%s'\n", i->parser->name);
			ctx->parser = i->parser;
			i->parser->parse(ctx, buf, len);
		}

		break;

	case CONF_METHOD_UNKNOWN:
		break;

	}
}

void __register_parser(struct parser *parser)
{
	struct p_item* i = talloc(NULL, struct p_item);

	i->parser = parser;
	list_add(&parsers, &i->list);
}

void parser_init(void)
{
}
