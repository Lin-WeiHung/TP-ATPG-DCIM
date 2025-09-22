#include "../include/Parser.hpp"
#include "../include/FaultSimulator.hpp"
#include <chrono>
#include <iostream>

int main(int argc, char* argv[])
{
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << 
        " <faults.json> <marchTest.json> <detection_report.txt>\n";
        return 1;
    }

    try {
        Parser parser;
        auto faults = parser.parseFaults(argv[1]);
        auto marchTest = parser.parseMarchTest(argv[2]);

        int rows = 4;
        int cols = 4;
        int seed = 12345; // Default seed value
        if (argc >= 5) {
            rows = std::stoi(argv[4]);
        }
        if (argc >= 6) {
            cols = std::stoi(argv[5]);
        }
        if (argc >= 7) {
            seed = std::stoi(argv[6]);
        }
        if (rows <= 0 || cols <= 0) {
            throw std::invalid_argument("Row and column dimensions must be positive integers.");
        }

        // 開始計時
        auto start = std::chrono::high_resolution_clock::now();

        OneByOneFaultSimulator faultSim(faults, marchTest, rows, cols, seed);
        faultSim.run();

        // 結束計時
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        std::cout << "Execution time: " << duration.count() << " ms\n";

        // Write detection report
        parser.writeDetectionReport(faults, faultSim.getDetectedRate(), argv[3]);

        

        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}