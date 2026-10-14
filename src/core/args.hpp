#pragma once
// Tiny command-line helper: --name value and --flag.

#include <cstdlib>
#include <string>
#include <string_view>

namespace ledang {

class Args {
public:
    Args(int argc, char** argv) : argc_(argc), argv_(argv) {}

    bool flag(std::string_view name) const {
        for (int i = 1; i < argc_; ++i) {
            if (name == argv_[i]) return true;
        }
        return false;
    }
    std::string str(std::string_view name, std::string fallback) const {
        for (int i = 1; i + 1 < argc_; ++i) {
            if (name == argv_[i]) return argv_[i + 1];
        }
        return fallback;
    }
    double num(std::string_view name, double fallback) const {
        const std::string s = str(name, "");
        return s.empty() ? fallback : std::atof(s.c_str());
    }

private:
    int argc_;
    char** argv_;
};

}  // namespace ledang
