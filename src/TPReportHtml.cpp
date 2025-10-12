#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <sstream>
#include <algorithm>

#include "FpParserAndTpGen.hpp"

using std::string; using std::vector; using std::cout; using std::endl;

static const char* v2s(Val v){
    switch(v){ case Val::Zero: return "0"; case Val::One: return "1"; case Val::X: return "-"; }
    return "?";
}

static const char* pos2s(PositionMark p){
    switch(p){ case PositionMark::Adjacent: return "#"; case PositionMark::SameElementHead: return "^"; case PositionMark::NextElementHead: return ";"; }
    return "?";
}

static const char* order2s(Detector::AddrOrder o){
    switch(o){
        case Detector::AddrOrder::Assending: return "Assending"; // 對應原先的 aggressor-first 概念
        case Detector::AddrOrder::Decending: return "Decending"; // 對應 victim-first 概念
        case Detector::AddrOrder::None: return "-";
    }
    return "?";
}

static const char* group2short(OrientationGroup g){
    switch(g){
        case OrientationGroup::Single: return "single";
        case OrientationGroup::A_LT_V: return "a&lt;v"; // HTML escape
        case OrientationGroup::A_GT_V: return "a&gt;v";
    }
    return "?";
}

static string detect_repr(const Detector& d){
    auto bit = [](Val v){ return v==Val::Zero? '0' : v==Val::One? '1' : '-'; };
    if (d.detectOp.kind == OpKind::Read){
        if (d.detectOp.value == Val::Zero) return "R0";
        if (d.detectOp.value == Val::One)  return "R1";
        return string("R-");
    } else if (d.detectOp.kind == OpKind::ComputeAnd){
        string s = "C("; s.push_back(bit(d.detectOp.C_T)); s += ")("; s.push_back(bit(d.detectOp.C_M)); s += ")("; s.push_back(bit(d.detectOp.C_B)); s += ")"; return s;
    }
    return string("?");
}

// 與 detect 相同風格的 Op 表示：Write => W0/W1, Read => R0/R1, ComputeAnd => C(T)(M)(B)
static string op_repr(const Op& op){
    auto bit = [](Val v){ return v==Val::Zero? '0' : v==Val::One? '1' : '-'; };
    if (op.kind == OpKind::Write){
        if (op.value == Val::Zero) return string("W0");
        if (op.value == Val::One)  return string("W1");
        return string("W-");
    } else if (op.kind == OpKind::Read){
        if (op.value == Val::Zero) return string("R0");
        if (op.value == Val::One)  return string("R1");
        return string("R-");
    } else { // ComputeAnd
        std::string s = "C("; s.push_back(bit(op.C_T)); s += ")("; s.push_back(bit(op.C_M)); s += ")("; s.push_back(bit(op.C_B)); s += ")"; return s;
    }
}

static string html_escape(const string& in){
    string out; out.reserve(in.size());
    for(char c: in){
        switch(c){
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c; break;
        }
    }
    return out;
}

static string optv(const std::optional<Val>& ov){ return ov.has_value()? v2s(ov.value()):"-"; }

static void write_fp_brief(std::ostream& os, const FPExpr& fp){
    os << "<ul class=\"fp\">";
    if (fp.Sa.has_value())
        os << "<li><b>Sa:</b> pre_D="<< optv(fp.Sa->pre_D) << ", Ci=" << optv(fp.Sa->Ci) << "</li>";
    else
        os << "<li><b>Sa:</b> (none)</li>";
    os << "<li><b>Sv:</b> pre_D="<< optv(fp.Sv.pre_D) << ", Ci=" << optv(fp.Sv.Ci) << "</li>";
    os << "<li><b>F:</b> " << optv(fp.F.FD) << ", <b>R:</b> " << optv(fp.R.RD) << ", <b>C:</b> " << optv(fp.C.Co) << "</li>";
    os << "</ul>";
}

int main(){
    try{
        FaultsJsonParser parser; FaultNormalizer norm; TPGenerator gen;
        auto raws = parser.parse_file("0916Cross_shape/faults.json");

        // Normalize and precompute counts
        vector<Fault> faults; faults.reserve(raws.size());
        vector<size_t> tpCounts; tpCounts.reserve(raws.size());
        std::set<string> catSet, scopeSet;
        for (const auto& rf : raws){
            catSet.insert(rf.category);
            scopeSet.insert(rf.cell_scope);
            faults.push_back(norm.normalize(rf));
        }
        for (const auto& f : faults){
            auto tps = gen.generate(f);
            tpCounts.push_back(tps.size());
        }

    std::ofstream ofs("output/TP_Report.html");
    ofs << "<!DOCTYPE html><html><head><meta charset=\"utf-8\">";
        ofs << "<title>TP Report</title>";
    ofs << "<style>body{font-family:sans-serif;line-height:1.4} details{margin:12px 0} summary{cursor:pointer;font-weight:600} table{border-collapse:collapse;margin:6px 0} th,td{border:1px solid #ccc;padding:4px 8px;text-align:center} .dc td{min-width:60px} .filters{position:sticky;top:0;background:#fff;padding:8px;border-bottom:1px solid #ddd} .muted{color:#666} .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(240px,1fr));gap:8px} .overview table{width:100%} .badge{display:inline-block;background:#eef;border:1px solid #99c;border-radius:10px;padding:2px 8px;margin-left:6px;font-size:12px} .detector{margin:4px 0 10px 18px} .fp{margin:4px 0 10px 18px} .tpTable{width:100%;font-size:13px} .tpTable th{background:#eef2f7} .tpTable tbody tr:nth-child(even){background:#f6f8fb} .tpTable tbody tr:hover{background:#e8f0ff} .tpTable td.ops{text-align:left;white-space:nowrap} .tpTable td.det{text-align:left} .tpTable td.state{font-family:monospace}</style>";
        ofs << "</head><body>";

        // Filters
        ofs << "<div class=\"filters\">";
        ofs << "<label>Category: <select id=\"fCat\"><option value=\"\">(All)</option>";
        for (const auto& c : catSet) ofs << "<option>"<<html_escape(c)<<"</option>";
        ofs << "</select></label>\n";
        ofs << "<label style=\"margin-left:12px\">Scope: <select id=\"fScope\"><option value=\"\">(All)</option>";
        for (const auto& s : scopeSet) ofs << "<option>"<<html_escape(s)<<"</option>";
        ofs << "</select></label>\n";
        ofs << "<label style=\"margin-left:12px\">Fault ID: <input id=\"fId\" placeholder=\"contains...\"/></label>";
        ofs << "</div>";

        // Overview
        ofs << "<section class=\"overview\">";
        ofs << "<h1>TP Report</h1>";
        ofs << "<p class=\"muted\">Total faults: "<<faults.size()<<"</p>";
        ofs << "<div class=\"grid\"><table><thead><tr><th>Fault</th><th>Category</th><th>Scope</th><th>TPs</th></tr></thead><tbody>";
        for (size_t i=0;i<faults.size();++i){
            ofs << "<tr><td>"<<html_escape(faults[i].fault_id)<<"</td><td>"<<html_escape(raws[i].category)<<"</td><td>"<<html_escape(raws[i].cell_scope)<<"</td><td>"<<tpCounts[i]<<"</td></tr>";
        }
        ofs << "</tbody></table></div>";
        ofs << "</section>";

        // Details per fault
        for (size_t fi=0; fi<faults.size(); ++fi){
            const auto& rf = raws[fi];
            const auto& fault = faults[fi];
            auto tps = gen.generate(fault);

            // group by FP index
            vector<vector<const TestPrimitive*>> by_fp(fault.primitives.size());
            for (const auto& tp : tps){ if (tp.parent_fp_index < by_fp.size()) by_fp[tp.parent_fp_index].push_back(&tp); }

            ofs << "<details class=\"fault\" data-category=\""<<html_escape(rf.category)<<"\" data-scope=\""<<html_escape(rf.cell_scope)<<"\" data-id=\""<<html_escape(fault.fault_id)<<"\">";
            ofs << "<summary>Fault: "<<html_escape(fault.fault_id)<<" <span class=\"badge\">"<<html_escape(rf.category)<<"</span> <span class=\"badge\">"<<html_escape(rf.cell_scope)<<"</span> <span class=\"badge\">TPs: "<<tps.size()<<"</span></summary>";

            ofs << "<div class=\"fps\">";
            for (size_t fpi=0; fpi<fault.primitives.size(); ++fpi){
                ofs << "<h3>FP["<<fpi<<"]</h3>";
                write_fp_brief(ofs, fault.primitives[fpi]);
                const auto& list = by_fp[fpi];
                ofs << "<p class=\"muted\">TP count: "<<list.size()<<"</p>";
                ofs << "<table class=\"tpTable\">";
                ofs << "<thead><tr><th>#</th><th>Group</th><th>A0(D,C)</th><th>A1(D,C)</th><th>CAS(D,C)</th><th>A3(D,C)</th><th>A4(D,C)</th><th>Ops(before detect)</th><th>Detector</th></tr></thead><tbody>";
                for (size_t idx=0; idx<list.size(); ++idx){
                    const auto* tp = list[idx];
                    auto cell = [&](Val d, Val c){ std::ostringstream cs; cs<<v2s(d)<<","<<v2s(c); return cs.str(); };
                    std::ostringstream opss;
                    if (tp->ops_before_detect.empty()) opss << "-"; else {
                        for (size_t oi=0; oi<tp->ops_before_detect.size(); ++oi){ if (oi) opss<<", "; opss<<op_repr(tp->ops_before_detect[oi]); }
                    }
                    std::ostringstream dets;
                    if (!tp->R_has_value) dets << "(none)"; else {
                        dets << detect_repr(tp->detector);
                        dets << " [" << pos2s(tp->detector.pos);
                        if (tp->detector.pos == PositionMark::SameElementHead || tp->detector.pos == PositionMark::NextElementHead)
                            dets << "/" << order2s(tp->detector.order);
                        dets << "]";
                    }
                    ofs << "<tr><td>"<<idx<<"</td><td>"<<group2short(tp->group)<<"</td>";
                    ofs << "<td class=\"state\">"<<html_escape(cell(tp->state.A0.D, tp->state.A0.C))<<"</td>";
                    ofs << "<td class=\"state\">"<<html_escape(cell(tp->state.A1.D, tp->state.A1.C))<<"</td>";
                    ofs << "<td class=\"state\">"<<html_escape(cell(tp->state.A2_CAS.D, tp->state.A2_CAS.C))<<"</td>";
                    ofs << "<td class=\"state\">"<<html_escape(cell(tp->state.A3.D, tp->state.A3.C))<<"</td>";
                    ofs << "<td class=\"state\">"<<html_escape(cell(tp->state.A4.D, tp->state.A4.C))<<"</td>";
                    ofs << "<td class=\"ops\">"<<html_escape(opss.str())<<"</td><td class=\"det\">"<<html_escape(dets.str())<<"</td></tr>";
                }
                ofs << "</tbody></table>";
            }
            ofs << "</div>";
            ofs << "</details>";
        }

        // Filtering script
        ofs << "<script>\n";
        ofs << "const fCat=document.getElementById('fCat'), fScope=document.getElementById('fScope'), fId=document.getElementById('fId');\n";
        ofs << "function applyFilter(){\n  const c=fCat.value.trim().toLowerCase();\n  const s=fScope.value.trim().toLowerCase();\n  const id=fId.value.trim().toLowerCase();\n  document.querySelectorAll('details.fault').forEach(el=>{\n    const ec=(el.dataset.category||'').toLowerCase();\n    const es=(el.dataset.scope||'').toLowerCase();\n    const ei=(el.dataset.id||'').toLowerCase();\n    const okc=!c||ec===c;\n    const oks=!s||es===s;\n    const oki=!id||ei.includes(id);\n    el.style.display=(okc&&oks&&oki)?'':'none';\n  });\n}\n";
        ofs << "[fCat,fScope,fId].forEach(x=>x.addEventListener('input',applyFilter));\n";
        ofs << "</script>";

        ofs << "</body></html>";
        ofs.close();
        cout << "HTML report written to: output/TP_Report.html" << endl;
    } catch (const std::exception& ex){
        cout << "Exception: " << ex.what() << endl; return 1;
    }
    return 0;
}
