#include <catch2/catch_test_macros.hpp>

#include "tjs.h"
#include "tjsArray.h"
#include "tjsDictionary.h"

#include <memory>
#include <vector>

namespace {

struct TJSReleaser {
    void operator()(tTJS *engine) const { engine->Release(); }
};

class DictionaryEnumProbe final : public tTJSDispatch {
public:
    explicit DictionaryEnumProbe(iTJSDispatch2 *source) : Source(source) {}

    tjs_error NativeInstanceSupport(tjs_uint32 flag, tjs_int32 classid,
                                    iTJSNativeInstance **pointer) override {
        return Source->NativeInstanceSupport(flag, classid, pointer);
    }

    tjs_error EnumMembers(tjs_uint32 flag, tTJSVariantClosure *callback,
                          iTJSDispatch2 *) override {
        Flags.push_back(flag);
        return Source->EnumMembers(flag, callback, Source);
    }

    std::vector<tjs_uint32> Flags;

private:
    iTJSDispatch2 *Source;
};

} // namespace

TEST_CASE("TJS structured history snapshots remain independent") {
    std::unique_ptr<tTJS, TJSReleaser> engine(new tTJS());
    tTJSVariant result;
    REQUIRE_NOTHROW(engine->EvalExpression(
        TJS_W("(function() {"
              "  var histories = [];"
              "  for(var i = 0; i < 256; i++)"
              "    histories.add(%['text' => 'line', 'num' => i,"
              "                   'flags' => %['values' => [i, i + 1]]]);"
              "  var snapshot = [];"
              "  snapshot.assignStruct(histories);"
              "  histories[0].flags.values[0] = -1;"
              "  histories[255].text = 'changed';"
              "  snapshot[1].flags.values[1] = -2;"
              "  return snapshot.count == 256 &&"
              "    snapshot[0].flags.values[0] == 0 &&"
              "    snapshot[255].text == 'line' &&"
              "    histories[1].flags.values[1] == 2;"
              "})()"), &result));
    CHECK(result.AsInteger() == 1);
}

TEST_CASE("TJS assignStruct preserves cycles aliases and non-container objects") {
    std::unique_ptr<tTJS, TJSReleaser> engine(new tTJS());
    tTJSVariant result;
    REQUIRE_NOTHROW(engine->EvalExpression(
        TJS_W("(function() {"
              "  var shared = %['value' => 7];"
              "  var callback = function() { return 42; };"
              "  var source = %['left' => shared, 'right' => shared,"
              "                  'callback' => callback, 'empty' => void];"
              "  var cycle = [source];"
              "  source.cycle = cycle;"
              "  var copy = %[];"
              "  (Dictionary.assignStruct incontextof copy)(source);"
              "  cycle.clear();"
              "  (Dictionary.clear incontextof source)();"
              "  copy.left.value = 9;"
              "  return copy.right.value == 7 && shared.value == 7 &&"
              "    copy.cycle[0] === null && copy.empty === void &&"
              "    copy.callback === callback && copy.callback() == 42;"
              "})()"), &result));
    CHECK(result.AsInteger() == 1);
}

TEST_CASE("TJS assignStruct counts visible members without fetching values twice") {
    std::unique_ptr<tTJS, TJSReleaser> engine(new tTJS());
    tTJSVariant source;
    tTJSVariant destination;
    engine->EvalExpression(TJS_W("%['visible' => %['value' => 17]]"), &source);
    engine->EvalExpression(TJS_W("%[]"), &destination);
    auto *source_object = source.AsObjectNoAddRef();
    auto *destination_object = destination.AsObjectNoAddRef();
    tTJSVariant hidden(99);
    REQUIRE(TJS_SUCCEEDED(source_object->PropSet(
        TJS_MEMBERENSURE | TJS_IGNOREPROP | TJS_HIDDENMEMBER,
        TJS_W("hidden"), nullptr, &hidden, source_object)));

    tTJSDictionaryNI *native = nullptr;
    REQUIRE(TJS_SUCCEEDED(destination_object->NativeInstanceSupport(
        TJS_NIS_GETINSTANCE, TJSGetDictionaryClassID(),
        reinterpret_cast<iTJSNativeInstance **>(&native))));
    DictionaryEnumProbe probe(source_object);
    std::vector<iTJSDispatch2 *> stack;
    native->AssignStructure(&probe, stack);
    REQUIRE(probe.Flags.size() == 2);
    CHECK((probe.Flags[0] & TJS_ENUM_NO_VALUE) != 0);
    CHECK((probe.Flags[1] & TJS_ENUM_NO_VALUE) == 0);
    CHECK(stack.empty());
    tjs_int count = 0;
    REQUIRE(TJS_SUCCEEDED(destination_object->GetCount(
        &count, nullptr, nullptr, destination_object)));
    CHECK(count == 1);
    tTJSVariant visible;
    REQUIRE(TJS_SUCCEEDED(destination_object->PropGet(
        0, TJS_W("visible"), nullptr, &visible, destination_object)));
    CHECK(visible.AsObjectNoAddRef() != nullptr);
}
