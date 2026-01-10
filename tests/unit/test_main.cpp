#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "app_config.h"
#include "cli.h"
#include "decision.h"
#include "exclude.h"
#include "path_utils.h"

namespace {

struct TestCase {
    std::string name;
    std::function<void()> func;
};

std::vector<TestCase>& Registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> func) {
        Registry().push_back({name, std::move(func)});
    }
};

#define TEST_CASE(name) \
    void name(); \
    Registrar reg_##name(#name, name); \
    void name()

#define EXPECT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            throw std::runtime_error(std::string("EXPECT_TRUE failed: ") + #cond); \
        } \
    } while (0)

#define EXPECT_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            throw std::runtime_error(std::string("EXPECT_EQ failed: ") + #a + " vs " + #b); \
        } \
    } while (0)

}  // namespace

TEST_CASE(ParseArgsBasic) {
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "uploader_cli_test";
    std::filesystem::create_directories(temp_dir);

    AppConfig config;
    std::string error;
    bool ok = ParseArgs({"--source", temp_dir.string(),
                         "--email", "user@mail.ru",
                         "--dry-run"},
                        temp_dir, &config, &error);
    EXPECT_TRUE(ok);
    EXPECT_EQ(config.email, "user@mail.ru");
    EXPECT_TRUE(config.dry_run);
}

TEST_CASE(ParseArgsDefaultSource) {
    std::filesystem::path root_dir = std::filesystem::temp_directory_path() / "uploader_default_root_test";
    std::error_code ec;
    std::filesystem::remove_all(root_dir, ec);

    AppConfig config;
    std::string error;
    bool ok = ParseArgs({"--email", "user@mail.ru", "--dry-run"}, root_dir, &config, &error);
    EXPECT_TRUE(ok);
    std::filesystem::path expected = std::filesystem::absolute(root_dir / "p");
    EXPECT_EQ(config.source, expected);
    EXPECT_TRUE(std::filesystem::exists(expected));
}

TEST_CASE(NormalizeRemote) {
    EXPECT_EQ(NormalizeRemoteRoot("Folder"), "/Folder");
    EXPECT_EQ(NormalizeRemoteRoot("/Folder/"), "/Folder");
}

TEST_CASE(JoinRemotePathTest) {
    std::filesystem::path rel = std::filesystem::path("sub") / "file.txt";
    EXPECT_EQ(JoinRemotePath("/Root", rel), "/Root/sub/file.txt");
}

TEST_CASE(UrlEncoding) {
    EXPECT_EQ(UrlEncodePath("/A B"), "/A%20B");
}

TEST_CASE(DecisionJpg) {
    LocalFileInfo local;
    local.is_jpg = true;
    RemoteItemInfo remote;
    remote.exists = false;
    auto decision = DecideFileAction(local, remote, CompareMode::SizeMtime,
                                     std::chrono::system_clock::now());
    EXPECT_EQ(decision.action, FileActionType::UploadAndDelete);
}

TEST_CASE(DecisionNonJpgOld) {
    LocalFileInfo local;
    local.is_jpg = false;
    local.size = 10;
    local.last_modified = std::chrono::system_clock::now() - std::chrono::hours(48);
    RemoteItemInfo remote;
    remote.exists = false;
    auto decision = DecideFileAction(local, remote, CompareMode::SizeMtime,
                                     std::chrono::system_clock::now());
    EXPECT_EQ(decision.action, FileActionType::UploadAndDelete);
}

TEST_CASE(DecisionNonJpgSame) {
    LocalFileInfo local;
    local.is_jpg = false;
    local.size = 10;
    local.last_modified = std::chrono::system_clock::now();

    RemoteItemInfo remote;
    remote.exists = true;
    remote.has_size = true;
    remote.size = 10;
    remote.has_last_modified = true;
    remote.last_modified = local.last_modified + std::chrono::seconds(5);

    auto decision = DecideFileAction(local, remote, CompareMode::SizeMtime,
                                     std::chrono::system_clock::now());
    EXPECT_EQ(decision.action, FileActionType::Skip);
}

TEST_CASE(ExcludeRulesTest) {
    ExcludeRules rules = BuildDefaultExcludeRules();
    EXPECT_TRUE(ShouldExclude(std::filesystem::path(".git") / "config", rules));
    EXPECT_TRUE(ShouldExclude(std::filesystem::path("file.tmp"), rules));
    EXPECT_TRUE(!ShouldExclude(std::filesystem::path("keep.txt"), rules));

    rules.patterns.push_back("build/*");
    EXPECT_TRUE(ShouldExclude(std::filesystem::path("build") / "out.bin", rules));
}

int main() {
    int failed = 0;
    for (const auto& test : Registry()) {
        try {
            test.func();
            std::cout << "[PASS] " << test.name << "\n";
        } catch (const std::exception& ex) {
            std::cout << "[FAIL] " << test.name << ": " << ex.what() << "\n";
            failed++;
        }
    }
    return failed == 0 ? 0 : 1;
}
