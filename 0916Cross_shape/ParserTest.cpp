#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cassert>
#include "Parser.hpp"

// 簡易測試巨集
#define EXPECT_TRUE(cond) do { if(!(cond)) { \
	std::cerr << "[FAIL] " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
	failures++; \
} else { passed++; }} while(0)

static void runBasicTests(const std::vector<FaultEntry> &faults) {
	int passed = 0, failures = 0;
	EXPECT_TRUE(!faults.empty());
	if (!faults.empty()) {
		EXPECT_TRUE(!faults.front().fault_id.empty());
		EXPECT_TRUE(!faults.front().category.empty());
		EXPECT_TRUE(!faults.front().cell_scope.empty());
		EXPECT_TRUE(!faults.front().fault_primitives.empty());
	}
	std::cout << "Test summary: " << passed << " passed, " << failures << " failed." << std::endl;
}

static void printFault(const FaultEntry &f) {
	std::cout << "========================================\n";
	std::cout << "Fault ID     : " << f.fault_id << "\n";
	std::cout << "Category     : " << f.category << "\n";
	std::cout << "Cell Scope   : " << f.cell_scope << "\n";
	std::cout << "Primitives   :\n";
	for (const auto &p : f.fault_primitives) {
		std::cout << "  - " << p << "\n";
	}
	std::cout << "========================================\n";
}

static void interactiveMenu(const std::vector<FaultEntry> &all) {
	if (all.empty()) {
		std::cout << "No fault entries loaded.\n";
		return;
	}
	while (true) {
		std::cout << "\nFault Menu (total " << all.size() << ")\n";
		std::cout << "----------------------------------------\n";
		// 列出前 30 筆或全部
		size_t limit = std::min<size_t>(all.size(), 30);
		for (size_t i = 0; i < limit; ++i) {
			std::cout << std::setw(2) << i << ") " << all[i].fault_id << " - " << all[i].category << '\n';
		}
		if (all.size() > limit) {
			std::cout << "... (use id search for remaining)\n";
		}
		std::cout << "Commands: index | id:<fault_id> | list | exit\n> ";
		std::string cmd;
		if (!std::getline(std::cin, cmd)) break;
		if (cmd == "exit" || cmd == "quit") break;
		if (cmd == "list") {
			for (const auto &f : all) {
				std::cout << f.fault_id << '\n';
			}
			continue;
		}
		if (cmd.rfind("id:", 0) == 0) {
			std::string id = cmd.substr(3);
			auto it = std::find_if(all.begin(), all.end(), [&](const FaultEntry& f){ return f.fault_id == id; });
			if (it != all.end()) printFault(*it); else std::cout << "Fault id not found: " << id << '\n';
			continue; }
		// 嘗試視為 index
		try {
			size_t idx = static_cast<size_t>(std::stoul(cmd));
			if (idx < all.size()) {
				printFault(all[idx]);
			} else {
				std::cout << "Index out of range.\n";
			}
		} catch (...) {
			std::cout << "Unknown command.\n";
		}
	}
}

int main(int argc, char **argv) {
	std::string path = "faults.json"; // default assume run inside directory containing file
	if (argc > 1) path = argv[1];

	std::vector<FaultEntry> faults;
	try {
		faults = parse_file(path);
	} catch (const std::exception& ex) {
		std::cerr << "Failed to parse '" << path << "': " << ex.what() << std::endl;
		return 1;
	}

	runBasicTests(faults);
	interactiveMenu(faults);
	return 0;
}

