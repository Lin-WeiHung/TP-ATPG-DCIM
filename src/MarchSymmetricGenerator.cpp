// g++ -std=c++20 -O2 -Wall -Wextra src/MarchSymmetricGenerator.cpp -o build/march_sym_gen
// ./build/march_sym_gen <N> <output.json>

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>

using std::string;
using std::vector;
using std::cout;
using std::cerr;
using std::endl;

// 12 operations
enum class Op {
    W0, W1, R0, R1,
    C_0_0_0, C_0_0_1, C_0_1_0, C_0_1_1,
    C_1_0_0, C_1_0_1, C_1_1_0, C_1_1_1
};

// Convert operation to string
static string op_to_string(Op op) {
    switch(op) {
        case Op::W0: return "W0";
        case Op::W1: return "W1";
        case Op::R0: return "R0";
        case Op::R1: return "R1";
        case Op::C_0_0_0: return "C(0)(0)(0)";
        case Op::C_0_0_1: return "C(0)(0)(1)";
        case Op::C_0_1_0: return "C(0)(1)(0)";
        case Op::C_0_1_1: return "C(0)(1)(1)";
        case Op::C_1_0_0: return "C(1)(0)(0)";
        case Op::C_1_0_1: return "C(1)(0)(1)";
        case Op::C_1_1_0: return "C(1)(1)(0)";
        case Op::C_1_1_1: return "C(1)(1)(1)";
    }
    return "?";
}

// Get symmetric operation
static Op get_symmetric(Op op) {
    switch(op) {
        case Op::W0: return Op::W1;
        case Op::W1: return Op::W0;
        case Op::R0: return Op::R1;
        case Op::R1: return Op::R0;
        case Op::C_0_0_0: return Op::C_1_1_1;
        case Op::C_0_0_1: return Op::C_1_1_0;
        case Op::C_0_1_0: return Op::C_1_0_1;
        case Op::C_0_1_1: return Op::C_1_0_0;
        case Op::C_1_0_0: return Op::C_0_1_1;
        case Op::C_1_0_1: return Op::C_0_1_0;
        case Op::C_1_1_0: return Op::C_0_0_1;
        case Op::C_1_1_1: return Op::C_0_0_0;
    }
    return Op::W0;
}

// All 12 operations
static const vector<Op>& all_ops() {
    static const vector<Op> ops = {
        Op::W0, Op::W1, Op::R0, Op::R1,
        Op::C_0_0_0, Op::C_0_0_1, Op::C_0_1_0, Op::C_0_1_1,
        Op::C_1_0_0, Op::C_1_0_1, Op::C_1_1_0, Op::C_1_1_1
    };
    return ops;
}

// Generate a march test pattern with insertions
// Base: b(W0); a(R0,W1); a(R1,W0); d(R0,W1); d(R1,W0); b(R0);
// Insert operations between R0 and W1 in elements 1 and 3 (ascending)
// Insert symmetric operations in elements 2 and 4 (descending)
static string generate_pattern(const vector<Op>& insertions) {
    std::ostringstream oss;
    
    // Element 0: b(W0)
    oss << "b(W0); ";
    
    // Element 1: a(R0, [insertions], W1)
    oss << "a(R0";
    for (const auto& op : insertions) {
        oss << ", " << op_to_string(op);
    }
    oss << ", W1); ";
    
    // Element 2: a(R1, [symmetric], W0)
    oss << "a(R1";
    for (const auto& op : insertions) {
        oss << ", " << op_to_string(get_symmetric(op));
    }
    oss << ", W0); ";
    
    // Element 3: d(R0, [insertions], W1)
    oss << "d(R0";
    for (const auto& op : insertions) {
        oss << ", " << op_to_string(op);
    }
    oss << ", W1); ";
    
    // Element 4: d(R1, [symmetric], W0)
    oss << "d(R1";
    for (const auto& op : insertions) {
        oss << ", " << op_to_string(get_symmetric(op));
    }
    oss << ", W0); ";
    
    // Element 5: b(R0)
    oss << "b(R0)";
    
    return oss.str();
}

// Recursive function to generate all combinations of N operations
static void generate_combinations(int n, int current_depth, vector<Op>& current,
                                   vector<vector<Op>>& all_combinations) {
    if (current_depth == n) {
        all_combinations.push_back(current);
        return;
    }
    
    for (const auto& op : all_ops()) {
        current.push_back(op);
        generate_combinations(n, current_depth + 1, current, all_combinations);
        current.pop_back();
    }
}

// Generate all march tests for N operations (0 to max_n)
static void generate_all_march_tests(int max_n, std::ostream& out) {
    out << "[\n";
    
    bool first_entry = true;
    
    for (int n = 0; n <= max_n; ++n) {
        cout << "Generating combinations for N=" << n << "...\n";
        
        vector<vector<Op>> all_combinations;
        
        if (n == 0) {
            // Base case: no insertions
            all_combinations.push_back(vector<Op>());
        } else {
            // Generate all combinations of length n (12^n total)
            vector<Op> current;
            generate_combinations(n, 0, current, all_combinations);
        }
        
        cout << "  Total combinations: " << all_combinations.size() << "\n";
        
        for (size_t idx = 0; idx < all_combinations.size(); ++idx) {
            if (!first_entry) {
                out << ",\n";
            }
            first_entry = false;
            
            const auto& combo = all_combinations[idx];
            string pattern = generate_pattern(combo);
            string name = std::to_string(n) + "_" + std::to_string(idx + 1);
            
            out << "  {\n";
            out << "    \"March_test\": \"" << name << "\",\n";
            out << "    \"Pattern\": \"" << pattern << "\"\n";
            out << "  }";
        }
    }
    
    out << "\n]\n";
}

int main(int argc, char** argv) {
    if (argc < 3) {
        cerr << "Usage: " << argv[0] << " <max_N> <output.json>\n";
        cerr << "  max_N: Maximum number of operations to insert (0 to N)\n";
        cerr << "  output.json: Output JSON file path\n";
        cerr << "\nExample: " << argv[0] << " 2 output/march_test.json\n";
        cerr << "\nNote: This generates all combinations from N=0 to N=max_N\n";
        cerr << "  N=0: 1 combination (base template)\n";
        cerr << "  N=1: 12 combinations\n";
        cerr << "  N=2: 144 combinations\n";
        cerr << "  N=3: 1,728 combinations\n";
        cerr << "  N=4: 20,736 combinations\n";
        return 1;
    }
    
    try {
        int max_n = std::stoi(argv[1]);
        string output_path = argv[2];
        
        if (max_n < 0) {
            cerr << "Error: max_N must be non-negative\n";
            return 1;
        }
        
        if (max_n > 4) {
            cerr << "Warning: N > 4 will generate a very large number of combinations (12^N)\n";
            cerr << "  N=0: 1 combination\n";
            cerr << "  N=1: 12 combinations\n";
            cerr << "  N=2: 144 combinations\n";
            cerr << "  N=3: 1,728 combinations\n";
            cerr << "  N=4: 20,736 combinations\n";
            cerr << "  N=5: 248,832 combinations\n";
            cerr << "Continue? (y/n): ";
            string response;
            std::getline(std::cin, response);
            if (response != "y" && response != "Y") {
                cout << "Cancelled.\n";
                return 0;
            }
        }
        
        cout << "Generating march tests for N=0 to N=" << max_n << "...\n";
        
        std::ofstream out(output_path);
        if (!out) {
            cerr << "Error: Cannot open output file: " << output_path << "\n";
            return 1;
        }
        
        generate_all_march_tests(max_n, out);
        out.close();
        
        cout << "Successfully written to: " << output_path << "\n";
        
    } catch (const std::exception& e) {
        cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
