/*
 * Copyright 2007-2020 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Main.hxx"
#include "Progress.hxx"
#include "AcmeMain.hxx"
#include "Config.hxx"
#include "CertDatabase.hxx"
#include "WrapKey.hxx"
#include "Wildcard.hxx"
#include "ssl/Init.hxx"
#include "ssl/Buffer.hxx"
#include "ssl/Dummy.hxx"
#include "ssl/Key.hxx"
#include "ssl/LoadFile.hxx"
#include "ssl/Name.hxx"
#include "ssl/Unique.hxx"
#include "ssl/Error.hxx"
#include "lb/Config.hxx"
#include "system/Urandom.hxx"
#include "system/Error.hxx"
#include "io/FileDescriptor.hxx"
#include "io/StringFile.hxx"
#include "util/ConstBuffer.hxx"
#include "util/PrintException.hxx"
#include "util/Compiler.h"

#include <stdexcept>

#include <stdio.h>
#include <stdlib.h>

struct AutoUsage {};

WorkshopProgress root_progress;

static CertDatabaseConfig
LoadCertDatabaseConfig(const char *path)
{
	LbConfig lb_config;
	LoadConfigFile(lb_config, path);

	auto i = lb_config.cert_dbs.begin();
	if (i == lb_config.cert_dbs.end())
		throw "/etc/cm4all/beng/lb.conf contains no cert_db section";

	if (std::next(i) != lb_config.cert_dbs.end())
		fprintf(stderr, "Warning: %s contains multiple cert_db sections\n",
			path);

	return std::move(i->second);
}

static CertDatabaseConfig
LoadCertDatabaseConfig()
{
	return LoadCertDatabaseConfig("/etc/cm4all/beng/lb.conf");
}

CertDatabaseConfig
LoadPatchCertDatabaseConfig()
{
	CertDatabaseConfig config = LoadCertDatabaseConfig();

	try {
		config.connect = LoadStringFile("/etc/cm4all/beng/certdb.connect");
	} catch (const std::system_error &e) {
		/* ignore ENOENT */
		if (!IsFileNotFound(e))
			throw;
	}

	return config;
}

static void
LoadCertificate(const CertDatabaseConfig &db_config,
		const char *handle,
		const char *cert_path, const char *key_path)
{
	const ScopeSslGlobalInit ssl_init;

	const auto cert = LoadCertFile(cert_path);
	const auto common_name = GetCommonName(*cert);
	if (common_name == nullptr)
		throw "Certificate has no common name";

	const auto key = LoadKeyFile(key_path);
	if (!MatchModulus(*cert, *key))
		throw "Key and certificate do not match.";

	WrapKeyHelper wrap_key_helper;
	const auto wrap_key = wrap_key_helper.SetEncryptKey(db_config);

	CertDatabase db(db_config);

	bool inserted;

	db.DoSerializableRepeat(8, [&](){
		inserted = db.LoadServerCertificate(handle,
						    *cert, *key, wrap_key.first,
						    wrap_key.second);
	});

	printf("%s: %s\n", inserted ? "insert" : "update", common_name.c_str());
	db.NotifyModified();
}

static void
ReloadCertificate(const CertDatabaseConfig &db_config, const char *handle)
{
	const ScopeSslGlobalInit ssl_init;

	CertDatabase db(db_config);

	auto cert_key = db.GetServerCertificateKeyByHandle(handle);
	if (!cert_key.second)
		throw "Certificate not found";

	WrapKeyHelper wrap_key_helper;
	const auto wrap_key = wrap_key_helper.SetEncryptKey(db_config);

	db.LoadServerCertificate(handle,
				 *cert_key.first, *cert_key.second,
				 wrap_key.first, wrap_key.second);
}

static void
DeleteCertificate(const CertDatabaseConfig &db_config, const char *handle)
{
	CertDatabase db(db_config);

	const auto result = db.DeleteServerCertificateByHandle(handle);
	if (result.GetAffectedRows() == 0)
		throw "Certificate not found";

	db.NotifyModified();
}

static void
GetCertificate(const CertDatabaseConfig &db_config, const char *handle)
{
	const ScopeSslGlobalInit ssl_init;
	CertDatabase db(db_config);
	auto cert = db.GetServerCertificateByHandle(handle);
	if (!cert)
		throw "Certificate not found";

	X509_print_fp(stdout, cert.get());
	PEM_write_X509(stdout, cert.get());
}

/**
 * Load the private key for the given host name from the database.
 *
 * Returns the key or nullptr if no such certificate/key pair was
 * found.  Throws an exception on error.
 */
static UniqueEVP_PKEY
FindKeyByName(CertDatabase &db, const char *common_name)
{
	return db.GetServerCertificateKey(common_name).second;
}

static void
FindPrintCertificates(CertDatabase &db, const char *name)
{
	for (const auto &row : db.FindServerCertificatesByName(name))
		printf("%s\t%s\t%s\t%s\n",
		       row.GetValue(0), row.GetValue(1),
		       row.GetValue(2), row.GetValue(3));
}

static void
FindCertificate(const CertDatabaseConfig &db_config, const char *host, bool headers)
{
	if (headers)
		printf("id\thandle\tissuer\tnot_after\n");

	const ScopeSslGlobalInit ssl_init;
	CertDatabase db(db_config);

	FindPrintCertificates(db, host);

	const auto wildcard = MakeCommonNameWildcard(host);
	if (!wildcard.empty())
		FindPrintCertificates(db, wildcard.c_str());
}

static void
DumpKey(const CertDatabaseConfig &db_config, const char *host)
{
	const ScopeSslGlobalInit ssl_init;
	CertDatabase db(db_config);

	auto key = FindKeyByName(db, host);
	if (!key)
		throw "Key not found";

	if (PEM_write_PrivateKey(stdout, key.get(), nullptr, nullptr, 0,
				 nullptr, nullptr) <= 0)
		throw SslError("Failed to dump key");
}

gcc_noreturn
static void
Monitor(const CertDatabaseConfig &db_config)
{
	CertDatabase db(db_config);
	db.ListenModified();

	std::string last_modified = db.GetLastModified();
	if (last_modified.empty()) {
		last_modified = db.GetCurrentTimestamp();
		if (last_modified.empty())
			throw "CURRENT_TIMESTAMP failed";
	}

	const FileDescriptor fd(db.GetSocket());

	while (true) {
		if (fd.WaitReadable(-1) < 0)
			throw "poll() failed";

		db.ConsumeInput();
		while (db.GetNextNotify()) {}

		std::string new_last_modified = db.GetLastModified();
		if (new_last_modified.empty())
			throw "No MAX(modified) found";

		for (auto &row : db.GetModifiedServerCertificatesMeta(last_modified.c_str()))
			printf("%s %s %s\n",
			       row.GetValue(1),
			       *row.GetValue(0) == 't' ? "deleted" : "modified",
			       row.GetValue(2));

		last_modified = std::move(new_last_modified);
	}
}

static void
Tail(const CertDatabaseConfig &db_config)
{
	CertDatabase db(db_config);

	for (auto &row : db.TailModifiedServerCertificatesMeta())
		printf("%s %s %s\n",
		       row.GetValue(1),
		       *row.GetValue(0) == 't' ? "deleted" : "modified",
		       row.GetValue(2));
}

static void
Populate(CertDatabase &db, EVP_PKEY *key, ConstBuffer<void> key_der,
	 const char *common_name)
{
	(void)key;

	// TODO: fake time stamps
	const char *not_before = "1971-01-01";
	const char *not_after = "1971-01-01";

	auto cert = MakeSelfSignedDummyCert(*key, common_name);
	db.InsertServerCertificate(nullptr, common_name, common_name,
				   not_before, not_after,
				   *cert, key_der,
				   nullptr);
}

static void
Populate(const CertDatabaseConfig &db_config,
	 const char *key_path, const char *suffix, unsigned n)
{
	const ScopeSslGlobalInit ssl_init;

	const auto key = LoadKeyFile(key_path);

	const SslBuffer key_buffer(*key);

	CertDatabase db(db_config);

	if (n == 0) {
		Populate(db, key.get(), key_buffer.get(), suffix);
	} else {
		db.DoSerializableRepeat(2, [&](){
			for (unsigned i = 1; i <= n; ++i) {
				char buffer[256];
				snprintf(buffer, sizeof(buffer), "%u%s", i, suffix);
				Populate(db, key.get(), key_buffer.get(), buffer);
			}
		});
	}

	db.NotifyModified();
}

static void
HandleLoad(ConstBuffer<const char *> args)
{
	if (args.size != 3)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	LoadCertificate(db_config, args[0], args[1], args[2]);
}

static void
HandleReload(ConstBuffer<const char *> args)
{
	if (args.size != 1)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	ReloadCertificate(db_config, args[0]);
}

static void
HandleDelete(ConstBuffer<const char *> args)
{
	if (args.size != 1)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	DeleteCertificate(db_config, args[0]);
}

static void
PrintNames(const CertDatabaseConfig &db_config, const char *handle)
{
	CertDatabase db(db_config);
	for (const auto &name : db.GetNamesByHandle(handle))
		printf("%s\n", name.c_str());
}

static void
HandleNames(ConstBuffer<const char *> args)
{
	if (args.size != 1)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	PrintNames(db_config, args[0]);
}

static void
HandleGet(ConstBuffer<const char *> args)
{
	if (args.size != 1)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	GetCertificate(db_config, args[0]);
}

static void
HandleFind(ConstBuffer<const char *> args)
{
	bool headers = false;

	while (!args.empty() && args.front()[0] == '-') {
		const char *arg = args.front();

		if (strcmp(arg, "--headers") == 0) {
			args.shift();
			headers = true;
		} else
			break;
	}

	if (args.size != 1)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	FindCertificate(db_config, args[0], headers);
}

static void
SetHandle(const CertDatabaseConfig &db_config,
	  Pg::Serial id, const char *handle)
{
	CertDatabase db(db_config);
	db.SetHandle(id, handle);
}

static void
HandleSetHandle(ConstBuffer<const char *> args)
{
	if (args.size != 2)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	SetHandle(db_config, Pg::Serial::Parse(args[0]), args[1]);
}

static void
HandleDumpKey(ConstBuffer<const char *> args)
{
	if (args.size != 1)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	DumpKey(db_config, args[0]);
}

gcc_noreturn
static void
HandleMonitor(ConstBuffer<const char *> args)
{
	if (args.size != 0)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	Monitor(db_config);
}

static void
HandleTail(ConstBuffer<const char *> args)
{
	if (args.size != 0)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	Tail(db_config);
}

static void
HandleAcme(ConstBuffer<const char *> args)
{
	Acme(args);
}

static void
HandleGenwrap(ConstBuffer<const char *> args)
{
	if (args.size != 0)
		throw AutoUsage();

	CertDatabaseConfig::AES256 key;
	UrandomFill(&key, sizeof(key));

	for (auto b : key)
		printf("%02x", b);
	printf("\n");
}

static void
HandlePopulate(ConstBuffer<const char *> args)
{
	if (args.size < 2 || args.size > 3)
		throw AutoUsage();

	const char *key = args[0];
	const char *suffix = args[1];
	unsigned count = 0;

	if (args.size == 3) {
		count = strtoul(args[2], nullptr, 10);
		if (count == 0)
			throw std::runtime_error("Invalid COUNT parameter");
	}

	const auto db_config = LoadPatchCertDatabaseConfig();
	Populate(db_config, key, suffix, count);
}

static void
HandleMigrate(ConstBuffer<const char *> args)
{
	if (args.size != 0)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	CertDatabase db(db_config);
	db.Migrate();
}

static constexpr struct Command {
	const char *name, *usage;
	void (*function)(ConstBuffer<const char *> args);
	bool undocumented = false;

	constexpr Command(const char *_name, const char *_usage,
			  void (*_function)(ConstBuffer<const char *> args),
			  bool _undocumented = false)
		:name(_name), usage(_usage),
		 function(_function), undocumented(_undocumented) {}

} commands[] = {
	{ "load", "HANDLE CERT KEY", HandleLoad },
	{ "reload", "HANDLE", HandleReload, true },
	{ "delete", "HANDLE", HandleDelete },
	{ "names", "HANDLE", HandleNames },
	{ "get", "HANDLE", HandleGet },
	{ "find", "[--headers] HOST", HandleFind },
	{ "set-handle", "ID HANDLE", HandleSetHandle },
	{ "dumpkey", "HOST", HandleDumpKey, true },
	{ "monitor", nullptr, HandleMonitor },
	{ "tail", nullptr, HandleTail },
	{ "acme", "[OPTIONS] COMMAND ...", HandleAcme },
	{ "genwrap", "", HandleGenwrap },
	{ "populate", "KEY SUFFIX COUNT", HandlePopulate, true },
	{ "migrate", nullptr, HandleMigrate },
};

static const Command *
FindCommand(const char *name)
{
	for (const auto &i : commands)
		if (strcmp(i.name, name) == 0)
			return &i;

	return nullptr;
}

int
main(int argc, char **argv)
try {
	ConstBuffer<const char *> args(argv + 1, argc - 1);

	while (!args.empty() && *args.front() == '-') {
		if (strcmp(args.front(), "--progress") == 0) {
			args.shift();
			root_progress.Enable(0, 100);
		} else if (strncmp(args.front(), "--progress=", 11) == 0) {
			const char *range = args.front() + 11;
			args.shift();

			char *endptr;
			unsigned min = strtoul(range, &endptr, 10);
			if (endptr == range || *endptr != '-' || min > 100)
				throw "Failed to parse progress range";

			range = endptr + 1;
			unsigned max = strtoul(range, &endptr, 10);
			if (endptr == range || *endptr != 0 || max < min || max > 100)
				throw "Failed to parse progress range";

			root_progress = WorkshopProgress(min, max);
		} else if (strcmp(args.front(), "--workshop-control") == 0) {
			args.shift();
			root_progress.UseControlChannel();
		} else {
			fprintf(stderr, "Unknown option: %s\n\n", args.front());
			/* clear the list to trigger printing the usage */
			args.size = 0;
		}
	}

	if (args.empty()) {
		fprintf(stderr, "Usage: %s [OPTIONS] COMMAND ...\n"
			"\n"
			"Commands:\n", argv[0]);

		for (const auto &i : commands) {
			if (i.undocumented)
				continue;

			if (i.usage != nullptr)
				fprintf(stderr, "  %s %s\n", i.name, i.usage);
			else
				fprintf(stderr, "  %s\n", i.name);
		}

		fprintf(stderr, "\n"
			"Global options:\n"
			"  --progress[=MIN,MAX]  print Workshop job progress\n"
			"  --workshop-control    use the Workshop contrl channel for progress\n");

		return EXIT_FAILURE;
	}

	/* force line buffering, because this program may be used
	   non-interactively, and mixing stdout/stderr is confusing in
	   block-buffered mode */
	setvbuf(stdout, nullptr, _IOLBF, 0);
	setvbuf(stderr, nullptr, _IOLBF, 0);

	const auto cmd = args.shift();

	const auto *cmd2 = FindCommand(cmd);
	if (cmd2 == nullptr) {
		fprintf(stderr, "Unknown command: %s\n", cmd);
		return EXIT_FAILURE;
	}

	try {
		cmd2->function(args);
	} catch (AutoUsage) {
		if (cmd2->usage != nullptr)
			fprintf(stderr, "Usage: %s %s %s\n", argv[0],
				cmd2->name, cmd2->usage);
		else
			fprintf(stderr, "Usage: %s %s\n", argv[0],
				cmd2->name);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
} catch (const std::exception &e) {
	PrintException(e);
	return EXIT_FAILURE;
} catch (Usage u) {
	fprintf(stderr, "Usage: %s %s\n", argv[0], u.text);
	return EXIT_FAILURE;
} catch (const char *msg) {
	fprintf(stderr, "%s\n", msg);
	return EXIT_FAILURE;
}
