// main.cpp - CIM ATPG Tool Entry Point
// Unified interface for Generate mode (GreedySweep) and Simulator mode (FaultSimulation)
// 
// Usage:
//   cim-atpg --mode generate [options]     # Generate March Test patterns
//   cim-atpg --mode simulator [options]    # Simulate existing March Test patterns
//   cim-atpg --help                        # Show help

#include <iostream>
#include <string>
#include <vector>
#include <cstring>

// Forward declarations for mode entry points
int run_greedy_sweep(int argc, char** argv);
int run_simulator(int argc, char** argv);

static void print_usage(const char* prog) {
    std::cerr << "CIM Memory ATPG Tool - Dual Mode Interface\n\n"
              << "Usage: " << prog << " --mode <mode> [options]\n\n"
              << "Modes:\n"
              << "  generate   Generate optimal March Test patterns using Greedy search\n"
              << "  simulator  Simulate and analyze existing March Test patterns\n\n"
              << "Examples:\n"
              << "  " << prog << " --mode generate faults.json out.json out.html\n"
              << "  " << prog << " --mode simulator faults.json marchtests.json report.html\n\n"
              << "For mode-specific help:\n"
              << "  " << prog << " --mode generate --help\n"
              << "  " << prog << " --mode simulator --help\n";
}

int main(int argc, char** argv) {
    // Enable unbuffered output for real-time progress in containers
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    // Check for --help at top level
    if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        print_usage(argv[0]);
        return 0;
    }

    // Look for --mode argument
    std::string mode;
    int mode_arg_index = -1;
    
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--mode" && i + 1 < argc) {
            mode = argv[i + 1];
            mode_arg_index = i;
            break;
        }
    }

    if (mode.empty()) {
        std::cerr << "Error: --mode argument is required\n\n";
        print_usage(argv[0]);
        return 1;
    }

    // Build new argv without --mode and its value
    std::vector<char*> new_argv;
    new_argv.push_back(argv[0]);
    
    for (int i = 1; i < argc; ++i) {
        if (i == mode_arg_index || i == mode_arg_index + 1) {
            continue; // Skip --mode and its value
        }
        new_argv.push_back(argv[i]);
    }
    
    int new_argc = static_cast<int>(new_argv.size());

    // Dispatch to appropriate mode
    if (mode == "generate") {
        return run_greedy_sweep(new_argc, new_argv.data());
    } else if (mode == "simulator" || mode == "simulate" || mode == "sim") {
        return run_simulator(new_argc, new_argv.data());
    } else {
        std::cerr << "Error: Unknown mode '" << mode << "'\n"
                  << "Valid modes: generate, simulator\n";
        return 1;
    }
}
