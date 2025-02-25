// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Isolate.hxx"
#include "system/Mount.hxx"
#include "system/linux/pivot_root.h"
#include "io/FileDescriptor.hxx"
#include "io/linux/UserNamespace.hxx"

#include <sched.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>

#ifndef __linux
#error This library requires Linux
#endif

void
isolate_from_filesystem(bool allow_dbus,
			bool allow_prometheus_exporters)
{
	const int uid = geteuid(), gid = getegid();

	constexpr int flags = CLONE_NEWUSER|CLONE_NEWNS;
	if (unshare(flags) < 0) {
		fprintf(stderr, "unshare(0x%x) failed: %s\n", flags, strerror(errno));
		return;
	}

	/* since version 4.8, the Linux kernel requires a uid/gid mapping
	   or else the mkdir() calls below fail */
	/* for dbus "AUTH EXTERNAL", libdbus needs to obtain the "real"
	   uid from geteuid(), so set up the mapping */
	DenySetGroups(0);
	SetupGidMap(0, gid, false);
	SetupUidMap(0, uid, uid, 0, false);

	/* convert all "shared" mounts to "private" mounts */
	MountSetAttr(FileDescriptor::Undefined(), "/",
		     AT_RECURSIVE|AT_SYMLINK_NOFOLLOW|AT_NO_AUTOMOUNT,
		     0, 0, MS_PRIVATE);

	const char *const new_root = "/tmp";
	const char *const put_old = "old";

	/* create an empty tmpfs as the new filesystem root */
	MountOrThrow("none", new_root, "tmpfs", MS_NODEV|MS_NOEXEC|MS_NOSUID,
		     "size=16k,nr_inodes=16,mode=700");

	/* release a reference to the old root */
	if (chdir(new_root) < 0) {
		fprintf(stderr, "chdir('%s') failed: %s\n",
			new_root, strerror(errno));
		_exit(2);
	}

	/* bind-mount /run/systemd to be able to send messages to
	   /run/systemd/notify */
	mkdir("run", 0700);

	mkdir("run/systemd", 0);
	mount("/run/systemd", "run/systemd", nullptr, MS_BIND|MS_REC, nullptr);
	mount(nullptr, "run/systemd", nullptr,
	      MS_REMOUNT|MS_BIND|MS_NOEXEC|MS_NOSUID|MS_RDONLY, nullptr);

	if (allow_dbus) {
		mkdir("run/dbus", 0);
		mount("/run/dbus", "run/dbus", nullptr, MS_BIND|MS_REC, nullptr);
		mount(nullptr, "run/dbus", nullptr,
		      MS_REMOUNT|MS_BIND|MS_NOEXEC|MS_NOSUID|MS_RDONLY, nullptr);
	}

	if (allow_prometheus_exporters) {
		mkdir("run/cm4all", 0700);
		mkdir("run/cm4all/prometheus-exporters", 0700);

		mount("/run/cm4all/prometheus-exporters",
		      "run/cm4all/prometheus-exporters", nullptr,
		      MS_BIND|MS_REC, nullptr);
		mount(nullptr, "run/cm4all/prometheus-exporters", nullptr,
		      MS_REMOUNT|MS_BIND|MS_NOEXEC|MS_NOSUID|MS_RDONLY, nullptr);

		chmod("run/cm4all", 0111);
	}

	chmod("run", 0111);

	/* symlink /var/run to /run, because some libraries such as
	   libdbus use the old path */
	mkdir("var", 0700);
	(void)symlink("/run", "var/run");
	chmod("var", 0111);

	struct stat st;
	if (stat("/var/lib/cm4all/save-core/incoming", &st) == 0 && S_ISDIR(st.st_mode)) {
		/* bind-mount the cm4all-save-core "incoming" directory so
		   beng-lb can generate core dumps */

		mkdir("var/lib", 0700);
		mkdir("var/lib/cm4all", 0700);
		mkdir("var/lib/cm4all/save-core", 0700);
		mkdir("var/lib/cm4all/save-core/incoming", 0);

		mount("/var/lib/cm4all/save-core/incoming",
		      "var/lib/cm4all/save-core/incoming",
		      nullptr, MS_BIND|MS_REC, nullptr);
		mount(nullptr, "var/lib/cm4all/save-core/incoming", nullptr,
		      MS_REMOUNT|MS_BIND|MS_NOEXEC|MS_NOSUID, nullptr);

		chmod("var/lib", 0111);
		chmod("var/lib/cm4all", 0111);
		chmod("var/lib/cm4all/save-core", 0111);
	}

	/* enter the new root */
	mkdir(put_old, 0);
	if (my_pivot_root(new_root, put_old) < 0) {
		fprintf(stderr, "pivot_root('%s') failed: %s\n",
			new_root, strerror(errno));
		_exit(2);
	}

	/* get rid of the old root */
	Umount(put_old, MNT_DETACH);

	rmdir(put_old);

	chmod("/", 0111);
}
