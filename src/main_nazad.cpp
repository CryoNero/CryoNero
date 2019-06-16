// Copyright (c) 2012-2018, The CryptoNote developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

// Copyright (c) 2019, The Cryonero developers.
// Licensed under the GNU Lesser General Public License. See LICENSE for details.

#include <boost/algorithm/string.hpp>
#include "Core/Config.hpp"
#include "Core/Node.hpp"
#include "common/CommandLine.hpp"
#include "common/ConsoleTools.hpp"
#include "logging/ConsoleLogger.hpp"
#include "logging/LoggerManager.hpp"
#include "platform/ExclusiveLock.hpp"
#include "platform/Network.hpp"
#include "platform/PathTools.hpp"
#include "version.hpp"

using namespace cryonerocoin;

static const char USAGE[] =
R"(cryonerod )" cryonerocoin_VERSION_STRING R"(.

Usage:
  cryonerod [options]
  cryonerod --help | -h
  cryonerod --version | -v

Options:
  --p2p-bind-address=<ip:port>         Interface and port for P2P network protocol [default: 0.0.0.0:18640].
  --p2p-external-port=<port>           External port for P2P network protocol, if port forwarding used with NAT [default: 18640].
  --daemon-rpc-bind-address=<ip:port>  Interface and port for cryonerod RPC [default: 127.0.0.1:18641].
 
 --seed-node-address=<ip:port>        Specify list (one or more) of nodes to start connecting to.
  --priority-node-address=<ip:port>    Specify list (one or more) of nodes to connect to and attempt to keep the connection open.
  --exclusive-node-address=<ip:port>   Specify list (one or more) of nodes to connect to only. All other nodes including seed nodes will be ignored.
  --export-blocks=<folder-path>        Perform hot export of blockchain into specified folder as blocks.bin and blockindexes.bin, then exit. This overwrites existing files.
  --backup-blockchain=<folder-path>         Perform hot backup of blockchain into specified backup data folder, then exit.
  --data-folder=<full-path>            Folder for blockchain, logs and peer DB [default: )" platform_DEFAULT_DATA_FOLDER_PATH_PREFIX
	R"(cryonero].
  --rpc-authorization=<usr:pass> HTTP authorization for RPC.
)"
#if platform_USE_SSL
R"(  --ssl-certificate-pem-file=<file-path>    Full path to file containing both server SSL certificate and private key in PEM format.
  --ssl-certificate-password=<pass>    DEPRECATED. Will read password from stdin if not specified.
)"
#endif
;

int main(int argc, const char *argv[]) try {
	common::console::UnicodeConsoleSetup console_setup;
	auto idea_start = std::chrono::high_resolution_clock::now();
	common::CommandLine cmd(argc, argv);

	const bool import_blocks = cmd.get_bool("--import-blocks"); 
	std::string export_blocks;
	if (const char *pa = cmd.get("--export-blocks"))
		export_blocks = pa;
	std::string backup_blockchain;
	if (const char *pa = cmd.get("--backup-blockchain"))
		backup_blockchain = pa;
	cryonerocoin::Config config(cmd);
	cryonerocoin::Currency currency(config.is_testnet);

	Height print_structure = Height(-1);
	if (const char *pa = cmd.get("--print-structure"))
		print_structure = std::stoi(pa);
	const bool print_outputs = cmd.get_bool("--print-outputs");
	if (cmd.should_quit(USAGE, cryonerocoin::app_version()))
		return 0;

	const std::string coin_folder = config.get_data_folder();
	if (!export_blocks.empty() && !backup_blockchain.empty()) {
		std::cout << "You can either export blocks or backup blockchain on one run of cryonerod" << std::endl;
		return api::CRYONEROD_WRONG_ARGS;
	}
	if (!backup_blockchain.empty()) {
		std::cout << "Backing up " << (coin_folder + "/blockchain") << " to " << (backup_blockchain + "/blockchain")
			<< std::endl;
		if (!platform::create_folder_if_necessary(backup_blockchain + "/blockchain")) {
			std::cout << "Could not create folder for backup " << (backup_blockchain + "/blockchain") << std::endl;
			return 1;
		}
		common::console::set_text_color(common::console::BrightRed);
		std::cout << "There will be no progress printed for 4-20 minutes, depending on your computer speed."
			<< std::endl;
		common::console::set_text_color(common::console::Default);
		std::cout << "Starting blockchain backup..." << std::endl;
		platform::DB::backup_db(coin_folder + "/blockchain", backup_blockchain + "/blockchain");
		std::cout << "Finished blockchain backup." << std::endl;
		return 0;
	}
	if (!export_blocks.empty() || print_structure != Height(-1) || print_outputs) {
		logging::ConsoleLogger log_console;
		BlockChainState block_chain_read_only(log_console, config, currency, true);

		if (!export_blocks.empty()) {
			if (!LegacyBlockChainWriter::export_blockchain2(export_blocks, block_chain_read_only))
				return 1;
			return 0;
		}
		if (print_structure != Height(-1))
			block_chain_read_only.test_print_structure(print_structure);
		if (print_outputs)
			block_chain_read_only.test_print_outputs();
		return 0;
	}

	if (!config.ssl_certificate_pem_file.empty() && !config.ssl_certificate_password) {
		std::string ssl_certificate_password;
		std::cout << "Enter ssl certificate password: " << std::flush;
		std::getline(std::cin, ssl_certificate_password);
		boost::algorithm::trim(ssl_certificate_password);

		config.ssl_certificate_password = ssl_certificate_password;
	}

	platform::ExclusiveLock coin_lock(coin_folder, "cryonerod.lock");

	logging::LoggerManager log_manager;
	log_manager.configure_default(config.get_data_folder("logs"), "cryonerod-");

	BlockChainState block_chain(log_manager, config, currency, false);
	if (import_blocks) 
	{
		LegacyBlockChainReader::import_blockchain2(coin_folder, &block_chain, 300000);
		return 0;
	}

	boost::asio::io_service io;
	platform::EventLoop run_loop(io);

	Node node(log_manager, config, block_chain);

	auto idea_ms =
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - idea_start);
	std::cout << "cryonerod started seconds=" << double(idea_ms.count()) / 1000 << std::endl;
	while (!io.stopped())
	{
		if (node.on_idle())
			io.poll();
		else
			io.run_one();
	}
	return 0;
}
catch (const platform::ExclusiveLock::FailedToLock &ex) 
{
	std::cout << "cryonerod already running - " << ex.what() << std::endl;
	return api::CRYONEROD_ALREADY_RUNNING;
}
catch (const std::exception &ex)
{ 
	std::cout << "Exception in main() - " << ex.what() << std::endl;
	throw;
}
