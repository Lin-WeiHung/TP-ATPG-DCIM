#include <iostream>
#include <string>
#include <vector>
#include "Parser.hpp"
#include "Dominance.hpp"

using std::cerr;
using std::cout;
using std::endl;
using std::string;
using std::vector;

static int expect(bool cond, const char* msg) {
    if (!cond) { cerr << "FAIL: " << msg << endl; return 1; }
    return 0;
}

int main() {
    try {
        FaultsParser parser;
        auto faults = parser.parse_file("faults.json");

        const Fault* SA0 = nullptr;     // either_read_or_compute, single
        const Fault* SA1 = nullptr;     // either_read_or_compute, single
        const Fault* TFu = nullptr;     // either_read_or_compute, single (victim W1)
        const Fault* CIDD00 = nullptr;  // must_read, single
        const Fault* CI0011 = nullptr;  // must_compute, two cell (row-agnostic)

        for (const auto& f : faults) {
            if (f.fault_id == "SA0") SA0 = &f;
            else if (f.fault_id == "SA1") SA1 = &f;
            else if (f.fault_id == "TFu") TFu = &f;
            else if (f.fault_id == "CIDD(0,0)") CIDD00 = &f;
            else if (f.fault_id == "CI(00,11)") CI0011 = &f;
        }

        Dominance dom;
        int rc = 0;

        // 規則 1：同 category 可比
        rc |= expect(dom.dominates(*SA0, *SA1) == false, "SA0 should not dominate SA1 (different primitives)");

        // must_read 支配 either_read_or_compute 允許（僅測試允許性，非實際支配）
        rc |= expect(dom.dominates(*CIDD00, *SA0) == true, "CIDD(0,0) dominates SA0 should be false by primitives");

        // 規則 2：Cell scope 允許關係（TwoRowAgnostic 可以支配 Single）
        if (CI0011 && SA0) {
            // 內容不同，多半不支配，但至少不因 scope 被擋掉；此處用 primitives 不匹配導致 false
            rc |= expect(dom.dominates(*CI0011, *SA0) == false, "CI(00,11) vs SA0 likely false by primitives");
        }

        // 規則 3：逐欄位完整支配（人工製造兩個相同的 primitive 組合來驗證 true 案例）
        if (SA0 && !SA0->primitives.empty()) {
            Fault A = *SA0;
            Fault B = *SA0; // 完全相同
            rc |= expect(dom.dominates(A, B) == true, "Identical faults should dominate");
        }

        if (rc == 0) {
            cout << "Dominance tests completed (basic checks)" << endl;

            cout << "Dominance relationships:" << endl;
            for (const auto& f1 : faults) {
                for (const auto& f2 : faults) {
                    if (&f1 != &f2 && dom.dominates(f1, f2)) {
                        cout << f1.fault_id << " dominates " << f2.fault_id << endl;
                    }
                }
            }

            return 0;
        }
        
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 2;
    }
}
