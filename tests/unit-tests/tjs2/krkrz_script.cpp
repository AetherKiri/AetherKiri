#include <catch2/catch_test_macros.hpp>

#include "tjs.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef AETHER_KRKRZ_TJS2_TEST_ROOT
#define AETHER_KRKRZ_TJS2_TEST_ROOT ""
#endif

namespace {

class ScriptEngineOwner {
public:
    ScriptEngineOwner() : engine_(new tTJS()) {}
    ~ScriptEngineOwner() { engine_->Release(); }

    tTJS *operator->() const { return engine_; }

private:
    tTJS *engine_;
};

std::string readUtf8(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

// test.tjs normally dispatches the six sibling files through the engine's
// storage service.  The unit target intentionally links only tjs2, so we
// execute those same source blocks in order and remove only the storage
// dispatch statements.  The source itself still comes directly from the
// nested upstream submodule.
std::string withoutStorageDispatches(const std::string &source) {
    std::istringstream lines(source);
    std::string line;
    std::string result;
    while(std::getline(lines, line)) {
        const auto first = line.find_first_not_of(" \t");
        const bool isDispatch =
            first != std::string::npos &&
            line.compare(first, std::string("Scripts.execStorage(").size(),
                         "Scripts.execStorage(") == 0;
        if(!isDispatch) {
            result += line;
            result.push_back('\n');
        }
    }
    return result;
}

void installDebugSink(tTJS *engine) {
    REQUIRE_NOTHROW(engine->ExecScript(
        TJS_W("var __krkrzFailureCount = 0;\n"
              "var Debug = %[];\n"
              "Debug.message = function(message) {\n"
              "  var text = \"\" + message;\n"
              "  if(text.indexOf(\"faild\") >= 0 || text.indexOf(\"failed\") >= 0)\n"
              "    __krkrzFailureCount++;\n"
              "};\n"
              "Debug.notice = function(message) {};\n"),
        nullptr, nullptr, TJS_W("krkrz-harness-init.tjs")));
}

void executeCorpus(tTJS *engine, const std::filesystem::path &root,
                   const std::vector<std::filesystem::path> &files) {
    for(const auto &relative : files) {
        const auto path = root / relative;
        const std::string source = withoutStorageDispatches(readUtf8(path));
        const ttstr script(source);
        const ttstr name(path.string());
        try {
            engine->ExecScript(script, nullptr, nullptr, &name, 0);
        } catch(...) {
            FAIL("upstream TJS script threw: " + path.string());
            return;
        }
    }
}

void requireNoCorpusFailures(tTJS *engine) {
    tTJSVariant failures;
    REQUIRE_NOTHROW(
        engine->EvalExpression(TJS_W("__krkrzFailureCount"), &failures));
    CHECK(failures.AsInteger() == 0);
}

} // namespace

TEST_CASE("krkrz upstream TJS2 corpus runs on the Aether VM") {
    const std::filesystem::path root(AETHER_KRKRZ_TJS2_TEST_ROOT);
    REQUIRE_FALSE(root.empty());

    ScriptEngineOwner engine;
    // The upstream corpus reports failures through Debug.message().  Install
    // a minimal script-side sink so the test can assert the corpus result
    // without linking the full Aether ScriptMgnIntf/Storage runtime.
    installDebugSink(engine.operator->());

    const std::vector<std::filesystem::path> files = {
        "test_variant.tjs", "test_misc.tjs", "test_class.tjs",
        "test_function.tjs", "test_string.tjs", "test_with.tjs",
        "test.tjs"};
    executeCorpus(engine.operator->(), root, files);
    requireNoCorpusFailures(engine.operator->());
}

TEST_CASE("krkrz issue regressions and in-operator scripts run on the Aether VM") {
    const std::filesystem::path tjsRoot(AETHER_KRKRZ_TJS2_TEST_ROOT);
    REQUIRE_FALSE(tjsRoot.empty());

    ScriptEngineOwner engine;
    installDebugSink(engine.operator->());
    const std::filesystem::path testRoot = tjsRoot.parent_path();
    executeCorpus(engine.operator->(), testRoot,
                  {"issue226/TestClass.tjs", "issue226/TestForSyntax.tjs",
                   "issue226/startup.tjs", "in_operator/test_in.tjs"});
    requireNoCorpusFailures(engine.operator->());
}

TEST_CASE("krkrz KAG entrypoints remain direct submodule contracts") {
    const std::filesystem::path tjsRoot(AETHER_KRKRZ_TJS2_TEST_ROOT);
    REQUIRE_FALSE(tjsRoot.empty());
    const std::filesystem::path scriptRoot = tjsRoot.parent_path().parent_path();

    const std::vector<std::filesystem::path> requiredFiles = {
        "KAG3/data/startup.tjs",
        "KAG3/data/system/Initialize.tjs",
        "KAG3/data/scenario/first.ks",
        "KAG3_Ham/data/startup.tjs",
        "Krkr2Compat/data/startup.tjs",
        "Sample/tooltip/startup.tjs"};
    for(const auto &relative : requiredFiles)
        REQUIRE(std::filesystem::is_regular_file(scriptRoot / relative));

    const std::string kag3Startup =
        readUtf8(scriptRoot / "KAG3/data/startup.tjs");
    const std::string kag3Initialize =
        readUtf8(scriptRoot / "KAG3/data/system/Initialize.tjs");
    CHECK(kag3Startup.find("Scripts.execStorage(\"system/Initialize.tjs\")") !=
          std::string::npos);
    CHECK(kag3Initialize.find("KAGWindow") != std::string::npos);
    CHECK(kag3Initialize.find("Scripts.execStorage") != std::string::npos);
}
