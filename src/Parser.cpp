#include "../include/Parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <regex>


// ─────────────── helper ───────────────────────────────────────────────
int Parser::toInt(const std::string& raw) const
{
    if (raw == "-") return -1;
    if (raw == "0" || raw == "1") return raw[0] - '0';
    throw std::runtime_error("期望 0 / 1 / - ，卻讀到 " + raw);
}

SingleOp Parser::toSingleOp(char opKind, char value) const
{
    SingleOp op;
    op.type_  = (opKind == 'R') ? OpType::R : OpType::W;
    op.value_ = value - '0';
    return op;
}

std::vector<SingleOp> Parser::explodeOpToken(const std::string& tok) const
{
    std::vector<SingleOp> out;
    if (tok == "-" || tok.empty()) return out;

    // 允許任意長度：regex 逐段比對 (大小寫皆可)
    static const std::regex pat(R"(([A-Z]+)(\d+))", std::regex::icase);
    auto begin = std::sregex_iterator(tok.begin(), tok.end(), pat);
    auto end   = std::sregex_iterator();

    if (begin == end)
        throw std::runtime_error("無法解析操作串：" + tok);

    for (auto it = begin; it != end; ++it) {
        std::string opStr = it->str(1);
        int         val   = std::stoi(it->str(2));

        OpType type = OpType::UNKNOWN;
        std::string opStrLower = opStr;
        std::transform(opStrLower.begin(), opStrLower.end(), opStrLower.begin(), ::tolower);

        if      (opStrLower == "r")   type = OpType::R;
        else if (opStrLower == "w")   type = OpType::W;
        else if (opStrLower == "ci")  type = OpType::CI;
        else if (opStrLower == "co")  type = OpType::CO;

        if (type == OpType::UNKNOWN)
            throw std::runtime_error("不支援的操作碼: " + opStr);

        out.push_back({type, val});
    }
    return out;
}

// ─────────────── parseFaults ──────────────────────────────────────────
std::vector<FaultConfig> Parser::parseFaults(const std::string& filename) const
{
    std::ifstream ifs(filename);
    if (!ifs) throw std::runtime_error("無法開啟檔案: " + filename);

    json jRoot;  ifs >> jRoot;
    if (!jRoot.is_array())
        throw std::runtime_error("fault.json 根節點應為 array");  // :contentReference[oaicite:3]{index=3}

    std::vector<FaultConfig> out;

    for (const auto& jfault : jRoot) {
        const std::string name       = jfault.at("name").get<std::string>();
        const int         cellNum    = jfault.at("cell_number").get<int>();
        const auto&       conditions = jfault.at("conditions");              // array<string>

        for (std::size_t subIdx = 0; subIdx < conditions.size(); ++subIdx) {
            std::string raw = conditions.at(subIdx).get<std::string>();

            // 去除 '{' , '}', 空白
            raw.erase(std::remove_if(raw.begin(), raw.end(),
                       [](unsigned char c){ return std::isspace(c) || c=='{'||c=='}'; }),
                      raw.end());
            
            std::vector<std::string> parts;
            std::stringstream ss(raw);
            std::string item;
            while (std::getline(ss, item, ',')) {
                item.erase(std::remove_if(item.begin(), item.end(), ::isspace), item.end());
                parts.push_back(item);
            }

            FaultConfig cfg;
            cfg.id_.faultName_  = name;
            cfg.id_.subcaseIdx_ = static_cast<int>(subIdx);

            if (cellNum == 1) {                             // ── 1-cell fault
                if (parts.size() != 5)
                    throw std::runtime_error("1-cell 條目必須 5 欄；" + name);
                cfg.VI_             = toInt(parts[0]);
                cfg.trigger_        = explodeOpToken(parts[1]);
                cfg.faultValue_     = toInt(parts[3]);
                cfg.finalReadValue_ = toInt(parts[4]);
                cfg.is_twoCell_     = false;
            }
            else if (cellNum == 2) {                        // ── 2-cell fault
                if (parts.size() != 8)
                    throw std::runtime_error("2-cell 條目必須 8 欄；" + name);

                cfg.is_twoCell_       = true;
                cfg.is_A_less_than_V_ = (toInt(parts[0]) == 1);
                cfg.AI_               = toInt(parts[1]);
                cfg.VI_               = toInt(parts[2]);

                std::string leftT  = parts[3];
                std::string rightT = parts[4];
                bool useLeft       = (leftT != "-" && !leftT.empty());
                cfg.twoCellFaultType_ = useLeft ? TwoCellFaultType::Sa
                                                : TwoCellFaultType::Sv;
                cfg.trigger_          = explodeOpToken(useLeft ? leftT : rightT);

                cfg.faultValue_       = toInt(parts[6]);
                cfg.finalReadValue_   = toInt(parts[7]);
            } else {
                throw std::runtime_error("未知 cell_number = " + std::to_string(cellNum));
            }
            out.push_back(std::move(cfg));       // 保留 JSON 順序
        }
    }
    return out;
}

// ─────────────── parseMarchTest ───────────────────────────────────────
std::vector<MarchElement>
Parser::parseMarchTest_menu(const std::string& filename)
{
    std::ifstream ifs(filename);
    if (!ifs) throw std::runtime_error("無法開啟檔案: " + filename);

    json jf;  ifs >> jf;
    if (!jf.is_array()) throw std::runtime_error("marchTest.json 根節點必須是 array");

    /* ── ① 列出所有可用 pattern 名稱 ─────────────────── */
    std::vector<std::string> marchNames;
    for (const auto& j : jf)
        marchNames.push_back(j.at("name").get<std::string>());

    std::cout << "Available March patterns:\n";
    for (std::size_t i = 0; i < marchNames.size(); ++i)
        std::cout << i + 1 << ". " << marchNames[i] << "\n";

    std::cout << "Select a March pattern by number: ";
    std::size_t choice = 0;
    std::cin  >> choice;
    if (choice < 1 || choice > marchNames.size())
        throw std::runtime_error("Invalid selection");

    const json& jSel = jf[choice - 1];
    marchTestName_ = marchNames[choice - 1];
    std::string pattern = jSel.at("pattern").get<std::string>();
    /* ───────────────────────────────────────────────────── */

    /* ── ② 將 pattern 字串拆解成 MarchElement ─────────── */
    std::vector<MarchElement> result;
    std::stringstream segSS(pattern);
    std::string seg;

    int elemIdx = 0;
    int overallIdx = 0;                     // 全域遞增 op 編號

    while (std::getline(segSS, seg, ';')) {
        // 去空白
        seg.erase(std::remove_if(seg.begin(), seg.end(), ::isspace), seg.end());
        if (seg.empty()) continue;

        // 解析 direction 字元 ('b'/'a'/'d') 與括號
        char dirC = std::tolower(seg[0]);
        Direction dir;
        if      (dirC == 'a') dir = Direction::ASC;
        else if (dirC == 'd') dir = Direction::DESC;
        else if (dirC == 'b') dir = Direction::BOTH;
        else throw std::runtime_error("未知 direction: " + seg);

        auto l = seg.find('(');
        auto r = seg.find(')', l);
        if (l == std::string::npos || r == std::string::npos || r <= l + 1)
            throw std::runtime_error("pattern 格式錯誤：" + seg);

        std::string opList = seg.substr(l + 1, r - l - 1);

        // 分割成個別 token
        std::vector<std::string> tokens;
        std::stringstream tokSS(opList);
        std::string tok;
        while (std::getline(tokSS, tok, ',')) {
            tok.erase(std::remove_if(tok.begin(), tok.end(), ::isspace), tok.end());
            if (!tok.empty()) tokens.push_back(tok);
        }

        // 建立 MarchElement
        MarchElement elem;
        elem.elemIdx_   = elemIdx;
        elem.addrOrder_ = dir;

        int opLocalIdx = 0;
        for (const auto& tk : tokens) {
            for (SingleOp sop : explodeOpToken(tk)) {
                
                PositionedOp po{ sop,
                                 { elemIdx,
                                   opLocalIdx,
                                   overallIdx++ } };
                elem.ops_.push_back(std::move(po));
            }
            ++opLocalIdx;
        }

        result.push_back(std::move(elem));
        ++elemIdx;
    }
    return result;
}

std::vector<MarchElement>
Parser::parseMarchTest(const std::string& filename)
{
    std::ifstream ifs(filename);
    if (!ifs) throw std::runtime_error("無法開啟檔案: " + filename);

    json jf;  ifs >> jf;
    if (!jf.is_object()) throw std::runtime_error("marchTest.json 根節點必須是 object");
    marchTestName_ = jf.at("name").get<std::string>();
    std::string pattern = jf.at("pattern").get<std::string>();
    /* ───────────────────────────────────────────────────── */

    /* ── ② 將 pattern 字串拆解成 MarchElement ─────────── */
    std::vector<MarchElement> result;
    std::stringstream segSS(pattern);
    std::string seg;

    int elemIdx = 0;
    int overallIdx = 0;                     // 全域遞增 op 編號

    while (std::getline(segSS, seg, ';')) {
        // 去空白
        seg.erase(std::remove_if(seg.begin(), seg.end(), ::isspace), seg.end());
        if (seg.empty()) continue;

        // 解析 direction 字元 ('b'/'a'/'d') 與括號
        char dirC = std::tolower(seg[0]);
        Direction dir;
        if      (dirC == 'a') dir = Direction::ASC;
        else if (dirC == 'd') dir = Direction::DESC;
        else if (dirC == 'b') dir = Direction::BOTH;
        else throw std::runtime_error("未知 direction: " + seg);

        auto l = seg.find('(');
        auto r = seg.find(')', l);
        if (l == std::string::npos || r == std::string::npos || r <= l + 1)
            throw std::runtime_error("pattern 格式錯誤：" + seg);

        std::string opList = seg.substr(l + 1, r - l - 1);

        // 分割成個別 token
        std::vector<std::string> tokens;
        std::stringstream tokSS(opList);
        std::string tok;
        while (std::getline(tokSS, tok, ',')) {
            tok.erase(std::remove_if(tok.begin(), tok.end(), ::isspace), tok.end());
            if (!tok.empty()) tokens.push_back(tok);
        }

        // 建立 MarchElement
        MarchElement elem;
        elem.elemIdx_   = elemIdx;
        elem.addrOrder_ = dir;

        int opLocalIdx = 0;
        for (const auto& tk : tokens) {
            for (SingleOp sop : explodeOpToken(tk)) {
                
                PositionedOp po{ sop,
                                 { elemIdx,
                                   opLocalIdx,
                                   overallIdx++ } };
                elem.ops_.push_back(std::move(po));
            }
            ++opLocalIdx;
        }

        result.push_back(std::move(elem));
        ++elemIdx;
    }
    return result;
}

// ─────────────── writeDetectionReport ─────────────────────────────────
void Parser::writeDetectionReport(const std::vector<FaultConfig>& faults,
                                  double detectedRate,
                                  const std::string& filename) const {
    std::ofstream ofs(filename);
    if (!ofs) throw std::runtime_error("無法開啟輸出檔案: " + filename);
    ofs << "Detected Rate: " << detectedRate * 100 << "%\n\n";
    for (const auto& fault : faults) {
        ofs << fault.id_.faultName_ << "\nSubcase " << fault.id_.subcaseIdx_ << " ";
        ofs << processSFR(fault) << "\n";
        // init0 健康報告
        ofs << "Init 0: ";
        // Output syndrome
        if (!fault.init0_healthReport_.isDetected_) {
            ofs << "No detection\n";
        } else {
            std::string bits;
            for (const auto& it : fault.init0_healthReport_.detected_) {
                bits += (it.second == 1) ? '1' : '0';
            }
            ofs << bits << " (";
            // Convert bits string to hex
            if (!bits.empty()) {
                unsigned long long value = std::stoull(bits, nullptr, 2);
                ofs << "0x" << std::hex << value << std::dec;
            }
            ofs << ")\n";

            for (const auto& it : fault.init0_healthReport_.detected_) {
                if (it.second) {
                    ofs << "M" << it.first.marchIdx << "(" << it.first.opIdx << ") ";
                }
            }
            ofs << "\n";
        }
        // init1 健康報告
        ofs << "Init 1: ";
        // Output syndrome
        if (!fault.init1_healthReport_.isDetected_) {
            ofs << "No detection\n";
            continue;
        } else {
            std::string bits;
            for (const auto& it : fault.init1_healthReport_.detected_) {
                bits += (it.second == 1) ? '1' : '0';
            }
            ofs << bits << " (";
            // Convert bits string to hex
            if (!bits.empty()) {
                unsigned long long value = std::stoull(bits, nullptr, 2);
                ofs << "0x" << std::hex << value << std::dec;
            }
            ofs << ")\n";

            for (const auto& it : fault.init1_healthReport_.detected_) {
                if (it.second) {
                    ofs << "M" << it.first.marchIdx << "(" << it.first.opIdx << ") ";
                }
            }
            ofs << "\n\n";
        }
    }
}

// ─────────────── processSFR ───────────────────────────────────────────
std::string Parser::processSFR(const FaultConfig& fault) const {
    std::string out;
    if (fault.is_twoCell_) {
        
        out += "< " + std::to_string(fault.AI_);
        if (fault.twoCellFaultType_ == TwoCellFaultType::Sa) {
            for (const auto& sop : fault.trigger_) {
                out += sop.type_ == OpType::R ? 'R' : 'W';
                out += std::to_string(sop.value_);
            }
            out += "; " + std::to_string(fault.VI_);
        } else if (fault.twoCellFaultType_ == TwoCellFaultType::Sv) {
            out += "; " + std::to_string(fault.VI_);
            for (const auto& sop : fault.trigger_) {
                out += sop.type_ == OpType::R ? 'R' : 'W';
                out += std::to_string(sop.value_);
            }
        }
        
        out += " / " + std::to_string(fault.faultValue_);
        out += " / " + std::to_string(fault.finalReadValue_);
        out += " >";
        out += (fault.is_A_less_than_V_ ? " (A<V)" : " (A>V) ");
    } else {
        out += "< " + std::to_string(fault.VI_);
        for (const auto& sop : fault.trigger_) {
            out += sop.type_ == OpType::R ? 'R' : 'W';
            out += std::to_string(sop.value_);
        }
        out += " / " + std::to_string(fault.faultValue_);
        out += " / " + std::to_string(fault.finalReadValue_) + " >";
    }
    return out;
}