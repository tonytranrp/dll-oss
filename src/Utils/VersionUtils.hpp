#pragma once
#include <string>
#include <string_view>
#include <vector>

class VersionUtils {
public:
    struct VersionEntry {
        std::string_view version;
        void (*loadSignatures)();
        void (*loadOffsets)();
    };

    static std::string getFormattedVersion();

    static void initialize();
    static bool isSupported(const std::string& version);
    static void addData();

    static bool checkAboveOrEqual(int m, int b);
    static bool checkBelowOrEqual(int m, int b);
    static bool checkBetween(int m1, int b1, int m2, int b2);

    static std::vector<VersionEntry> versions;
};
