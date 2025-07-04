// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Main.hxx"
#include "Progress.hxx"
#include "AcmeMain.hxx"
#include "Puki.hxx"
#include "Config.hxx"
#include "CertDatabase.hxx"
#include "WrapKey.hxx"
#include "Wildcard.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/fmt/ToBuffer.hxx"
#include "lib/openssl/Buffer.hxx"
#include "lib/openssl/Dummy.hxx"
#include "lib/openssl/Key.hxx"
#include "lib/openssl/LoadFile.hxx"
#include "lib/openssl/Name.hxx"
#include "lib/openssl/UniqueEVP.hxx"
#include "lib/openssl/UniqueCertKey.hxx"
#include "lib/openssl/Error.hxx"
#include "lb/Config.hxx"
#include "system/Urandom.hxx"
#include "system/Error.hxx"
#include "io/FileDescriptor.hxx"
#include "io/StringFile.hxx"
#include "util/AllocatedString.hxx"
#include <span>
#include "util/PrintException.hxx"
#include "util/StringCompare.hxx"

#include <stdexcept>

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

using std::string_view_literals::operator""sv;

struct AutoUsage {};

WorkshopProgress root_progress;

static bool
MaybeExists(const char *path) noexcept
{
	struct stat st;
	return lstat(path, &st) == 0 || errno != ENOENT;
}

static CertDatabaseConfig
LoadCertDatabaseConfig(const char *path)
{
	LbConfig lb_config;
	LoadConfigFile(lb_config, path);

	auto i = lb_config.cert_dbs.begin();
	if (i == lb_config.cert_dbs.end())
		throw FmtRuntimeError("No cert_db section found in {}", path);

	if (std::next(i) != lb_config.cert_dbs.end())
		fmt::print(stderr, "Warning: {} contains multiple cert_db sections\n",
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
	if (const char *path = "/etc/cm4all/beng/certdb.conf";
	    MaybeExists(path))
		return LoadStandaloneCertDatabaseConfig(path);

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
	const auto cert = LoadCertFile(cert_path);
	const auto common_name = GetCommonName(*cert);
	if (common_name == nullptr)
		throw "Certificate has no common name";

	const auto key = LoadKeyFile(key_path);
	if (!MatchModulus(*cert, *key))
		throw "Key and certificate do not match.";

	const auto [wrap_key_name, wrap_key] = db_config.GetDefaultWrapKey();

	CertDatabase db(db_config);

	bool inserted;

	db.DoSerializableRepeat(8, [&](){
		inserted = db.LoadServerCertificate(handle, nullptr,
						    *cert, *key, wrap_key_name,
						    wrap_key);
	});

	fmt::print("{}: {}\n", inserted ? "insert"sv : "update"sv,
		   common_name.c_str());
	db.NotifyModified();
}

static void
ReloadCertificate(const CertDatabaseConfig &db_config, const char *handle)
{
	CertDatabase db(db_config);

	auto cert_key = db.GetServerCertificateKeyByHandle(handle);
	if (!cert_key)
		throw "Certificate not found";

	const auto [wrap_key_name, wrap_key] = db_config.GetDefaultWrapKey();

	db.LoadServerCertificate(handle, nullptr,
				 *cert_key.cert, *cert_key.key,
				 wrap_key_name, wrap_key);
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
	return db.GetServerCertificateKey(common_name, nullptr).key;
}

static void
FindPrintCertificates(CertDatabase &db, const char *name)
{
	for (const auto &row : db.FindServerCertificatesByName(name))
		fmt::print("{}\t{}\t{}\t{}\n",
			   row.GetValueView(0), row.GetValueView(1),
			   row.GetValueView(2), row.GetValueView(3));
}

static void
FindCertificate(const CertDatabaseConfig &db_config, const char *host, bool headers)
{
	if (headers)
		fmt::print("id\thandle\tissuer\tnot_after\n");

	CertDatabase db(db_config);

	FindPrintCertificates(db, host);

	const auto wildcard = MakeCommonNameWildcard(host);
	if (!wildcard.empty())
		FindPrintCertificates(db, wildcard.c_str());
}

static void
DumpKey(const CertDatabaseConfig &db_config, const char *host)
{
	CertDatabase db(db_config);

	auto key = FindKeyByName(db, host);
	if (!key)
		throw "Key not found";

	if (PEM_write_PrivateKey(stdout, key.get(), nullptr, nullptr, 0,
				 nullptr, nullptr) <= 0)
		throw SslError("Failed to dump key");
}

[[noreturn]]
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

		for (const auto &row : db.GetModifiedServerCertificatesMeta(last_modified.c_str()))
			fmt::print("{} {} {}\n",
				   row.GetValueView(1),
				   row.GetValueView(0) == "t"sv ? "deleted"sv : "modified"sv,
				   row.GetValueView(2));

		last_modified = std::move(new_last_modified);
	}
}

static void
Tail(const CertDatabaseConfig &db_config)
{
	CertDatabase db(db_config);

	for (const auto &row : db.TailModifiedServerCertificatesMeta())
		fmt::print("{} {} {}\n",
			   row.GetValueView(1),
			   row.GetValueView(0) == "t"sv ? "deleted"sv : "modified"sv,
			   row.GetValueView(2));
}

static void
Populate(CertDatabase &db, EVP_PKEY *key, std::span<const std::byte> key_der,
	 const char *common_name)
{
	(void)key;

	// TODO: fake time stamps
	const char *not_before = "1971-01-01";
	const char *not_after = "1971-01-01";

	auto cert = MakeSelfSignedDummyCert(*key, common_name);
	db.InsertServerCertificate(nullptr,  nullptr,
				   common_name, common_name,
				   not_before, not_after,
				   *cert, key_der,
				   nullptr);
}

static void
Populate(const CertDatabaseConfig &db_config,
	 const char *key_path, const char *suffix, unsigned n)
{
	const auto key = LoadKeyFile(key_path);

	const SslBuffer key_buffer(*key);

	CertDatabase db(db_config);

	if (n == 0) {
		Populate(db, key.get(), key_buffer.get(), suffix);
	} else {
		db.DoSerializableRepeat(2, [&](){
			for (unsigned i = 1; i <= n; ++i) {
				const auto buffer = FmtBuffer<256>("{}{}", i, suffix);
				Populate(db, key.get(), key_buffer.get(), buffer);
			}
		});
	}

	db.NotifyModified();
}

static void
HandleLoad(std::span<const char *const> args)
{
	if (args.size() != 3)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	LoadCertificate(db_config, args[0], args[1], args[2]);
}

static void
HandleReload(std::span<const char *const> args)
{
	if (args.size() != 1)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	ReloadCertificate(db_config, args[0]);
}

static void
HandleDelete(std::span<const char *const> args)
{
	if (args.size() != 1)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	DeleteCertificate(db_config, args[0]);
}

static void
PrintNames(const CertDatabaseConfig &db_config, const char *handle)
{
	CertDatabase db(db_config);
	for (const auto &name : db.GetNamesByHandle(handle))
		fmt::print("{}\n", name);
}

static void
HandleNames(std::span<const char *const> args)
{
	if (args.size() != 1)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	PrintNames(db_config, args[0]);
}

static void
HandleGet(std::span<const char *const> args)
{
	if (args.size() != 1)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	GetCertificate(db_config, args[0]);
}

static void
HandleFind(std::span<const char *const> args)
{
	bool headers = false;

	while (!args.empty() && args.front()[0] == '-') {
		const char *arg = args.front();

		if (StringIsEqual(arg, "--headers")) {
			args = args.subspan(1);
			headers = true;
		} else
			break;
	}

	if (args.size() != 1)
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
HandleSetHandle(std::span<const char *const> args)
{
	if (args.size() != 2)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	SetHandle(db_config, Pg::Serial::Parse(args[0]), args[1]);
}

static void
HandleDumpKey(std::span<const char *const> args)
{
	if (args.size() != 1)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	DumpKey(db_config, args[0]);
}

[[noreturn]]
static void
HandleMonitor(std::span<const char *const> args)
{
	if (args.size() != 0)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	Monitor(db_config);
}

static void
HandleTail(std::span<const char *const> args)
{
	if (args.size() != 0)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	Tail(db_config);
}

static void
HandleAcme(std::span<const char *const> args)
{
	Acme(args);
}

static void
HandleGenwrap(std::span<const char *const> args)
{
	if (args.size() != 0)
		throw AutoUsage();

	WrapKeyBuffer key;
	UrandomFill(std::as_writable_bytes(std::span{key}));

	for (auto b : key)
		fmt::print("{:02x}", b);
	fmt::print("\n");
}

static void
HandlePopulate(std::span<const char *const> args)
{
	if (args.size() < 2 || args.size() > 3)
		throw AutoUsage();

	const char *key = args[0];
	const char *suffix = args[1];
	unsigned count = 0;

	if (args.size() == 3) {
		count = strtoul(args[2], nullptr, 10);
		if (count == 0)
			throw std::runtime_error("Invalid COUNT parameter");
	}

	const auto db_config = LoadPatchCertDatabaseConfig();
	Populate(db_config, key, suffix, count);
}

static void
HandleMigrate(std::span<const char *const> args)
{
	if (args.size() != 0)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	CertDatabase db(db_config);
	db.Migrate();
}

static constexpr struct Command {
	const char *name, *usage;
	void (*function)(std::span<const char *const> args);
	bool undocumented = false;

	constexpr Command(const char *_name, const char *_usage,
			  void (*_function)(std::span<const char *const> args),
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
	{ "puki", "[OPTIONS] COMMAND ...", HandlePuki },
	{ "genwrap", "", HandleGenwrap },
	{ "populate", "KEY SUFFIX COUNT", HandlePopulate, true },
	{ "migrate", nullptr, HandleMigrate },
};

static const Command *
FindCommand(const char *name)
{
	for (const auto &i : commands)
		if (StringIsEqual(i.name, name))
			return &i;

	return nullptr;
}

int
main(int argc, char **argv)
try {
	std::span<const char *const> args{argv + 1, static_cast<std::size_t>(argc - 1)};

	while (!args.empty() && *args.front() == '-') {
		if (StringIsEqual(args.front(), "--progress")) {
			args = args.subspan(1);
			root_progress.Enable(0, 100);
		} else if (auto range = StringAfterPrefix(args.front(), "--progress=")) {
			args = args.subspan(1);

			char *endptr;
			unsigned min = strtoul(range, &endptr, 10);
			if (endptr == range || *endptr != '-' || min > 100)
				throw "Failed to parse progress range";

			range = endptr + 1;
			unsigned max = strtoul(range, &endptr, 10);
			if (endptr == range || *endptr != 0 || max < min || max > 100)
				throw "Failed to parse progress range";

			root_progress = WorkshopProgress(min, max);
		} else if (StringIsEqual(args.front(), "--workshop-control")) {
			args = args.subspan(1);
			root_progress.UseControlChannel();
		} else {
			fmt::print(stderr, "Unknown option: {}\n\n", args.front());
			/* clear the list to trigger printing the usage */
			args = args.subspan(args.size());
		}
	}

	if (args.empty()) {
		fmt::print(stderr, "Usage: {} [OPTIONS] COMMAND ...\n"
			   "\n"
			   "Commands:\n", argv[0]);

		for (const auto &i : commands) {
			if (i.undocumented)
				continue;

			if (i.usage != nullptr)
				fmt::print(stderr, "  {} {}\n", i.name, i.usage);
			else
				fmt::print(stderr, "  {}\n", i.name);
		}

		fmt::print(stderr, "\n"
			   "Global options:\n"
			   "  --progress[=MIN,MAX]  print Workshop job progress\n"
			   "  --workshop-control    use the Workshop control channel for progress\n");

		return EXIT_FAILURE;
	}

	/* force line buffering, because this program may be used
	   non-interactively, and mixing stdout/stderr is confusing in
	   block-buffered mode */
	setvbuf(stdout, nullptr, _IOLBF, 0);
	setvbuf(stderr, nullptr, _IOLBF, 0);

	const auto cmd = args.front();
	args = args.subspan(1);

	const auto *cmd2 = FindCommand(cmd);
	if (cmd2 == nullptr) {
		fmt::print(stderr, "Unknown command: {}\n", cmd);
		return EXIT_FAILURE;
	}

	try {
		cmd2->function(args);
	} catch (AutoUsage) {
		if (cmd2->usage != nullptr)
			fmt::print(stderr, "Usage: {} {} {}\n", argv[0],
				   cmd2->name, cmd2->usage);
		else
			fmt::print(stderr, "Usage: {} {}\n", argv[0],
				   cmd2->name);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
} catch (const std::exception &e) {
	PrintException(e);
	return EXIT_FAILURE;
} catch (Usage u) {
	fmt::print(stderr, "Usage: {} {}\n", argv[0], u.text);
	return EXIT_FAILURE;
} catch (const char *msg) {
	fmt::print(stderr, "{}\n", msg);
	return EXIT_FAILURE;
}
