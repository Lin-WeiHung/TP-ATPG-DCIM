#pragma once

/**
 * @file SynthConfigCLI.hpp
 * @brief Minimal CLI glue to configure SynthConfig (non-intrusive).
 */

#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <algorithm>

#include "MarchSynth.hpp"

namespace synthcli {

inline bool starts_with(const std::string& s, const char* pfx){ return s.rfind(pfx,0) == 0; }
inline bool parse_bool(const std::string& v){
    if (v=="1"||v=="true"||v=="True"||v=="TRUE"||v=="yes") return true;
    if (v=="0"||v=="false"||v=="False"||v=="FALSE"||v=="no") return false;
    return std::atoi(v.c_str())!=0;
}

/**
 * @brief Apply a single --flag value to SynthConfig, or k/target if name matches.
 * Recognized names:
 *  alpha, beta, gamma, lambda, mu, max_ops, defer-detect-only, k, target
 */
inline void apply_flag(const std::string& name, const std::string& val,
                       SynthConfig& cfg, int& k, double& target){
    if (name=="alpha") cfg.alpha_state = std::atof(val.c_str());
    else if (name=="beta") cfg.beta_sens = std::atof(val.c_str());
    else if (name=="gamma") cfg.gamma_detect = std::atof(val.c_str());
    else if (name=="lambda") cfg.lambda_mask = std::atof(val.c_str());
    else if (name=="mu") cfg.mu_cost = std::atof(val.c_str());
    else if (name=="max_ops") cfg.max_ops = std::max(1, std::atoi(val.c_str()));
    else if (name=="defer-detect-only") cfg.defer_detect_only = parse_bool(val);
    else if (name=="k") k = std::max(1, std::atoi(val.c_str()));
    else if (name=="target") {
        double t = std::atof(val.c_str());
        target = std::max(0.0, std::min(1.0, t));
    }
}

/**
 * @brief Parse flags of the form --name=value or --name value from argv[start..).
 * Unknown flags are ignored to remain non-intrusive.
 */
inline void parse_cli_flags(int argc, char** argv, int start,
                            SynthConfig& cfg, int& k, double& target){
    for (int i=start;i<argc;++i){
        std::string tok = argv[i];
        if (!starts_with(tok, "--")) continue;
        std::string name, val;
        auto pos = tok.find('=');
        if (pos != std::string::npos){
            name = tok.substr(2, pos-2);
            val = tok.substr(pos+1);
        } else {
            name = tok.substr(2);
            if (i+1 < argc && std::string(argv[i+1]).rfind("--",0)!=0){
                val = argv[i+1];
                ++i;
            } else {
                // flags without value → treat as boolean true
                val = "1";
            }
        }
        apply_flag(name, val, cfg, k, target);
    }
}

} // namespace synthcli
