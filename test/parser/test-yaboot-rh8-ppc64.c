
#include "parser-test.h"

void run_test(struct parser_test *test)
{
	struct discover_boot_option *opt;
	struct discover_context *ctx;
	struct discover_device *dev;

	test_read_conf_file(test, "yaboot-rh8-ppc64.conf");
	test_run_parser(test, "yaboot");

	ctx = test->ctx;

	check_boot_option_count(ctx, 1);

	opt = get_boot_option(ctx, 0);

	check_unresolved_resource(opt->boot_image);
	check_unresolved_resource(opt->initrd);

	dev = test_create_device(ctx, "sdb1");
	test_hotplug_device(test, dev);

	check_resolved_local_resource(opt->boot_image, dev,
			"/boot/vmlinuz-1.0-20121219-1");
	check_resolved_local_resource(opt->initrd, dev,
			"/boot/initrd-1.0-20121219-1.img");

	check_args(opt, "root=/dev/sdb2 root=/dev/sdb2 ro crashkernel=auto "
			"rd_NO_LUKS rd_NO_MD console=hvc0 KEYTABLE=us quiet "
			"SYSFONT=latarcyrheb-sun16 LANG=en_US.utf8 rd_NO_LVM "
			"rd_NO_DM selinux=0 rootfsmode=ro");
}
