// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2019, The Cryonero developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

#include <boost/algorithm/string.hpp>
#include <future>
#include <random>
#include "Core/Config.hpp"
#include "Core/Node.hpp"
#include "Core/WalletNode.hpp"
#include "common/Base64.hpp"
#include "common/CommandLine.hpp"
#include "common/ConsoleTools.hpp"
#include "logging/LoggerManager.hpp"
#include "platform/ExclusiveLock.hpp"
#include "platform/Network.hpp"
#include "platform/PathTools.hpp"
#include "version.hpp"

using namespace cryonerocoin;

static const char USAGE[] =
R"(wallet-rpc )" cryonerocoin_VERSION_STRING R"(.

Usage:
  wallet-rpc [options] --wallet-file=<file>
  wallet-rpc --help | -h
  wallet-rpc --version | -v

Options:
  --wallet-file=<file>                 Path to wallet file to open.
  --wallet-password=<pass>         DEPRECATED AND NOT RECOMMENDED (as entailing security risk). Use given string as password and not read it from stdin.
  --create-wallet                      Create wallet file with new random keys. Must be used with --wallet-file option.
  --import-keys                        Create wallet file with imported keys read as a line from stdin. Must be used with --create-wallet.
  --set-password                       Read new password as a line from stdin (twice) and reencrypt wallet file.
  --export-view-only=<file-path>            Export view-only version of wallet file with the same password, then exit.
  --export-keys                        Export wallet keys to stdout, then exit.

  --wallet-rpc-bind-address=<ip:port>  Interface and port for wallet-rpc [default: 127.0.0.1:18642].
  --data-folder=<full-path>            Folder for wallet cache, blockchain, logs and peer DB [default: )" platform_DEFAULT_DATA_FOLDER_PATH_PREFIX
	R"(cryonero].
  --daemon-remote-address=<ip:port> Connect to remote cryonerod and suppress running built-in cryonerod.
  --rpc-authorization=<usr:pass> HTTP authorization for RCP.
  --backup-wallet-data=<folder-path>           Perform hot backup of wallet file and wallet cache into specified backup data folder, then exit.

Options for built-in cryonerod (run when no --daemon-remote-address specified):
  --p2p-bind-address=<ip:port>         Interface and port for P2P network protocol [default: 0.0.0.0:18640].
  --p2p-external-port=<port>           External port for P2P network protocol, if port forwarding used with NAT [default: 18640].
  --daemon-rpc-bind-address=<ip:port>  Interface and port for cryonerod RPC [default: 127.0.0.1:18641].
  --seed-node-address=<ip:port>        Specify list (one or more) of nodes to start connecting to.
  --priority-node-address=<ip:port>    Specify list (one or more) of nodes to connect to and attempt to keep the connection open.
  --exclusive-node-address=<ip:port>   Specify list (one or more) of nodes to connect to only. All other nodes including seed nodes will be ignored.)";

static const bool separate_thread_for_cryonerod = true;

int main(int argc, const char *argv[]) try {
	common::console::UnicodeConsoleSetup console_setup;
	auto idea_start = std::chrono::high_resolution_clock::now();
	common::CommandLine cmd(argc, argv);
	std::string wallet_file, password, new_password, export_view_only, import_keys_value, backup_wallet;
	const bool set_password = cmd.get_bool("--set-password");
	bool ask_password = true;
	const bool export_keys = cmd.get_bool("--export-keys");
	const bool create_wallet = cmd.get_bool("--create-wallet");
	const bool import_keys = cmd.get_bool("--import-keys");
	if (import_keys && !create_wallet) {
		std::cout << "When importing keys, you should use --create-wallet. You cannot import into existing wallet."
			<< std::endl;
		return api::WALLETD_WRONG_ARGS;
	}
	if (const char *pa = cmd.get("--wallet-file"))
		wallet_file = pa;
	if (const char *pa = cmd.get("--export-view-only")) {
		if (import_keys || create_wallet || export_keys) {
			std::cout
				<< "When exporting view-only version of wallet you cannot import keys, export keys, create wallet."
				<< std::endl;
			return api::WALLETD_WRONG_ARGS;
		}
		export_view_only = pa;
	}
	if (const char *pa = cmd.get("--backup-wallet", "Deprecated, use --backup-wallet-data"))
		backup_wallet = pa;
	if (const char *pa = cmd.get("--backup-wallet-data"))
		backup_wallet = pa;
	if (const char *pa = cmd.get("--wallet-password")) {
		password = pa;
		ask_password = false;
	}
	if (!ask_password && create_wallet) {
		std::cout << "When generating wallet, you cannot use --wallet-password. Wallet password will be read from stdin"
			<< std::endl;
		return api::WALLETD_WRONG_ARGS;
	}

	cryonerocoin::Config config(cmd);

	cryonerocoin::Currency currency(config.is_testnet);

	if (cmd.should_quit(USAGE, cryonerocoin::app_version()))
		return api::WALLETD_WRONG_ARGS;
	logging::LoggerManager logManagerNode;
	logManagerNode.configure_default(config.get_data_folder("logs"), "cryonerod-");

	if (wallet_file.empty()) {
		std::cout << "--wallet-file=<file> argument is mandatory" << std::endl;
		return api::WALLETD_WRONG_ARGS;
	}
	if (create_wallet && import_keys && import_keys_value.empty()) {  // TODO import_keys_value always empty
		std::cout << "Enter imported keys as hex bytes (05AB6F... etc.): " << std::flush;
		if (!console_setup.getline(import_keys_value)) {
			std::cout << "Unexpected end of stdin" << std::endl;
			return api::WALLETD_WRONG_ARGS;
		}
		boost::algorithm::trim(import_keys_value);
		if (import_keys_value.empty()) {
			std::cout << "Imported keys should not be empty" << std::endl;
			return api::WALLETD_WRONG_ARGS;
		}
	}
	if (!create_wallet && ask_password) {
		std::cout << "Enter current wallet password: " << std::flush;
		if (!console_setup.getline(password, true)) {
			std::cout << "Unexpected end of stdin" << std::endl;
			return api::WALLETD_WRONG_ARGS;
		}
		boost::algorithm::trim(password);
	}
	if (create_wallet || set_password) {
		std::cout << "Enter new wallet password: " << std::flush;
		if (!console_setup.getline(new_password, true)) {
			std::cout << "Unexpected end of stdin" << std::endl;
			return api::WALLETD_WRONG_ARGS;
		}
		boost::algorithm::trim(new_password);
		std::cout << "Repeat new wallet password:" << std::flush;
		std::string new_password2;
		if (!console_setup.getline(new_password2, true)) {
			std::cout << "Unexpected end of stdin" << std::endl;
			return api::WALLETD_WRONG_ARGS;
		}
		boost::algorithm::trim(new_password2);
		if (new_password != new_password2) {
			std::cout << "New passwords do not match" << std::endl;
			return api::WALLETD_WRONG_ARGS;
		}
	}
	const std::string coin_folder = config.get_data_folder();
	std::unique_ptr<Wallet> wallet;
	try {
		wallet = std::make_unique<Wallet>(
			wallet_file, create_wallet ? new_password : password, create_wallet, import_keys_value);
	}
	catch (const common::StreamError &ex) {
		std::cout << ex.what() << std::endl;
		return api::WALLET_FILE_READ_ERROR;
	}
	catch (const Wallet::Exception &ex) {
		std::cout << ex.what() << std::endl;
		return ex.return_code;
	}
	try {
		if (!backup_wallet.empty()) {
			if (import_keys || create_wallet || export_keys) {
				std::cout << "When doing wallet backup you cannot import keys, export keys, create wallet."
					<< std::endl;
				return api::WALLETD_WRONG_ARGS;
			}
			const std::string name = platform::get_filename_without_folder(wallet_file);
			const std::string dst_name = backup_wallet + "/" + name;
			const std::string dst_history_name = dst_name + ".history";
			const std::string dst_payments_name = dst_name + ".payments";
			const std::string dst_cache = backup_wallet + "/wallet_cache/" + wallet->get_cache_name();
			if (!platform::create_folder_if_necessary(backup_wallet + "/wallet_cache")) {
				std::cout << "Could not create folder for backup " << (backup_wallet + "/wallet_cache") << std::endl;
				return 1;
			}
			if (!platform::create_folder_if_necessary(dst_cache)) {
				std::cout << "Could not create folder for backup " << dst_cache << std::endl;
				return 1;
			}
			if (!platform::create_folder_if_necessary(dst_payments_name)) {
				std::cout << "Could not create folder for backup " << dst_payments_name << std::endl;
				return 1;
			}
			if (!platform::create_folder_if_necessary(dst_history_name)) {
				std::cout << "Could not create folder for backup " << dst_history_name << std::endl;
				return 1;
			}
			common::console::set_text_color(common::console::BrightRed);
			std::cout
				<< "There will be no progress printed for 1-20 minutes, depending on wallet size and computer speed."
				<< std::endl;
			common::console::set_text_color(common::console::Default);

			std::cout << "Starting wallet cache backup..." << std::endl;
			platform::DB::backup_db(coin_folder + "/wallet_cache/" + wallet->get_cache_name(), dst_cache);
			std::cout << "Finished wallet cache backup." << std::endl;

			for (const auto &file : platform::get_filenames_in_folder(wallet->get_payment_queue_folder())) {
				platform::copy_file(wallet->get_payment_queue_folder() + "/" + file, dst_payments_name + "/" + file);
			}
			for (const auto &file : platform::get_filenames_in_folder(wallet->get_history_folder())) {
				platform::copy_file(wallet->get_history_folder() + "/" + file, dst_history_name + "/" + file);
			}
			std::cout << "Backing up wallet file to " << (backup_wallet + "/" + name) << std::endl;
			wallet->export_wallet(backup_wallet + "/" + name, false); 
			std::cout << "Backing up wallet cache to " << dst_cache << std::endl;
			return 0;
		}
		if (!export_view_only.empty()) {
			if (wallet->is_view_only()) {
				std::cout << "Cannot export as view-only, wallet file is already view-only" << std::endl;
				return api::WALLETD_WRONG_ARGS;
			}
			wallet->export_wallet(export_view_only, true); 
			return 0;
		}
		if (export_keys) {
			std::cout << common::to_hex(wallet->export_keys()) << std::endl;
			return 0;
		}
		if (set_password) {
			wallet->set_password(new_password);
		}
	}
	catch (const common::StreamError &ex) {
		std::cout << ex.what() << std::endl;
		return api::WALLET_FILE_WRITE_ERROR;
	}
	catch (const Wallet::Exception &ex) {
		std::cout << ex.what() << std::endl;
		return ex.return_code;
	}
	std::unique_ptr<platform::ExclusiveLock> blockchain_lock;
	try {
		if (!config.cryonerod_remote_port)
			blockchain_lock = std::make_unique<platform::ExclusiveLock>(coin_folder, "cryonerod.lock");
	}
	catch (const platform::ExclusiveLock::FailedToLock &ex) {
		std::cout << "Cryonerod already running - " << ex.what() << std::endl;
		return api::CRYONEROD_ALREADY_RUNNING;
	}
	std::unique_ptr<platform::ExclusiveLock> walletcache_lock;
	try {
		std::cout << "Using wallet cache folder " << config.get_data_folder("wallet_cache") << "/"
			<< wallet->get_cache_name() << std::endl;
		walletcache_lock = std::make_unique<platform::ExclusiveLock>(
			config.get_data_folder("wallet_cache"), wallet->get_cache_name() + ".lock");
	}
	catch (const platform::ExclusiveLock::FailedToLock &ex) {
		std::cout << "Wallet with the same viewkey is in use - " << ex.what() << std::endl;
		return api::WALLET_WITH_SAME_KEYS_IN_USE;
	}
	if (!ask_password) {
		common::console::set_text_color(common::console::BrightRed);
		std::cout << "Password on command line is a security risk. Use 'echo <pwd> | ./walletd' or 'cat secrets.txt | "
			"./walletd'"
			<< std::endl;
		common::console::set_text_color(common::console::Default);
	}
	std::cout << "Enter HTTP authorization <user>:<password> for walletd RPC: " << std::flush;
	std::string auth;
	if (!console_setup.getline(auth, true)) {
		std::cout << "Unexpected end of stdin" << std::endl;
		return api::WALLETD_WRONG_ARGS;
	}
	boost::algorithm::trim(auth);
	config.walletd_authorization = common::base64::encode(BinaryArray(auth.data(), auth.data() + auth.size()));
	if (config.walletd_authorization.empty()) {
		common::console::set_text_color(common::console::BrightRed);
		std::cout << "No authorization for RPC is a security risk. Use username with a strong password" << std::endl;
		common::console::set_text_color(common::console::Default);
	}
	else {
		if (auth.find(":") == std::string::npos) {
			std::cout << "HTTP authorization must be in the format <user>:<password>" << std::endl;
			return api::WALLETD_WRONG_ARGS;
		}
	}
	logging::LoggerManager logManagerWalletNode;
	logManagerWalletNode.configure_default(config.get_data_folder("logs"), "wallet-rpc-");

	WalletState wallet_state(*wallet, logManagerWalletNode, config, currency);
	boost::asio::io_service io;
	platform::EventLoop run_loop(io);

	std::unique_ptr<BlockChainState> block_chain;
	std::unique_ptr<Node> node;

	std::promise<void> prm;
	std::thread cryonerod_thread;
	if (!config.cryonerod_remote_port) {
		try {
			if (separate_thread_for_cryonerod) {
				cryonerod_thread = std::thread([&prm, &logManagerNode, &config, &currency] {
					boost::asio::io_service io;
					platform::EventLoop separate_run_loop(io);

					std::unique_ptr<BlockChainState> separate_block_chain;
					std::unique_ptr<Node> separate_node;
					try {
						separate_block_chain = std::make_unique<BlockChainState>(logManagerNode, config, currency, false);
						separate_node = std::make_unique<Node>(logManagerNode, config, *separate_block_chain);
						prm.set_value();
					}
					catch (...) {
						prm.set_exception(std::current_exception());
						return;
					}
					while (!io.stopped()) {
						if (separate_node->on_idle())  
							io.poll();
						else
							io.run_one();
					}
				});
				std::future<void> fut = prm.get_future();
				fut.get();  
			}
			else {
				block_chain = std::make_unique<BlockChainState>(logManagerNode, config, currency, false);
				node = std::make_unique<Node>(logManagerNode, config, *block_chain);
			}
		}
		catch (const boost::system::system_error &ex) {
			std::cout << ex.what() << std::endl;
			if (cryonerod_thread.joinable())
				cryonerod_thread.join(); 
			return api::CRYONEROD_BIND_PORT_IN_USE;
		}
		catch (const std::exception &ex) {  
			std::cout << "Exception in main() - " << ex.what() << std::endl;
			throw;
		}
	}
	std::unique_ptr<WalletNode> wallet_node;
	try {
		wallet_node = std::make_unique<WalletNode>(nullptr, logManagerWalletNode, config, wallet_state);
	}
	catch (const boost::system::system_error &ex) {
		std::cout << ex.what() << std::endl;
		return api::WALLETD_BIND_PORT_IN_USE;
	}

	auto idea_ms =
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - idea_start);
	std::cout << "wallet-rpc started seconds=" << double(idea_ms.count()) / 1000 << std::endl;

	while (!io.stopped()) {
		if (node && node->on_idle())
			io.poll();
		else
			io.run_one();
	}
	return 0;
}
catch (const std::exception &ex) {  
	std::cout << "Exception in main() - " << ex.what() << std::endl;
	throw;
}
