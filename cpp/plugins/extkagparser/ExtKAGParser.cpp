//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"

        Modified by KAICHO for extentions
*/
//---------------------------------------------------------------------------
// KAG Parser Utility Class
//---------------------------------------------------------------------------

#include "tjsCommHead.h"
#include "StorageImpl.h"
#include "tjsDictionary.h"
#include "MsgIntf.h"
#include "DebugIntf.h"
#include "ScriptMgnIntf.h"
#include "TextStream.h"
#include "tjsGlobalStringMap.h"
#include "EventIntf.h"

#include "ExtKAGParser.hpp"

namespace {

int utf16_char_len(const tjs_char *text)
{
    if (!text || text[0] == 0)
        return 0;
    const tjs_char first = text[0];
    if (first >= 0xd800 && first <= 0xdbff)
    {
        const tjs_char second = text[1];
        if (second >= 0xdc00 && second <= 0xdfff)
            return 2;
    }
    return 1;
}

} // namespace

//---------------------------------------------------------------------------
/*
  KAG System (Kirikiri Adventure Game System) is an application script for
  TVP(Kirikiri), providing core system for Adventure Game/Visual Novel.
  KAG has a simple tag-based mark-up language ("scenario file").
  Version under 2.x of KAG is slow since the parser is written in TJS1.
  KAG 3.x uses TVP's internal KAG parser written in C++ in this unit, will
  acquire speed in compensation for ability of customizing.
*/
//---------------------------------------------------------------------------

#define TVPCurPosErrorString \
    (TJS_W("CurPos = ") + ttstr(CurPos) + TJS_W(", CurLineStr = \"") + ttstr(CurLineStr) + \
     TJS_W("\""))
#define SWITCH_TVPEXECUTEEXPRESSION(content, context, result) \
    TVPExecuteExpression((content), (context), (result))

const tjs_char* TVPExtKAGNoLine = TJS_W("読み込もうとしたシナリオファイル %1 は空です");
const tjs_char* TVPExtKAGCannotOmmitFirstLabelName =
    TJS_W("シナリオファイルの最初のラベル名は省略できません");
//const tjs_char* TVPInternalError = TJS_W("内部エラーが発生しました: at %1 line %2");
const tjs_char* TVPExtKAGMalformedSaveData =
    TJS_W("栞データが異常です。データが破損している可能性があります");
const tjs_char* TVPExtKAGLabelNotFound =
    TJS_W("シナリオファイル %1 内にラベル %2 が見つかりません");
const tjs_char* TVPExtKAGLineNotFound =
    TJS_W("シナリオファイル %1 にライン %2 は存在しません");
const tjs_char* TVPExtLabelInMacro = TJS_W("ラベルはマクロ中に記述できません");
const tjs_char* TVPExtKAGInlineScriptNotEnd = TJS_W("[endscript] または @endscript が見つかりません");
const tjs_char* TVPExtKAGSyntaxError =
    TJS_W("タグの文法エラーです。'[' や ']' の対応、\" と \" "
          "の対応、スペースの入れ忘れ、余分な改行、macro ～ endmacro "
          "の対応、必要な属性の不足などを確認してください\n\n%1\n%2");
const tjs_char* TVPExtKAGCallStackUnderflow =
    TJS_W("return タグが call タグと対応していません ( return タグが多い )");
const tjs_char* TVPExtKAGReturnLostSync =
    TJS_W("シナリオファイルに変更があったため return の戻り先位置を特定できません");
const tjs_char* TVPExtKAGSpecifyKAGParser = TJS_W("KAGParser クラスのオブジェクトを指定してください");
const tjs_char* TVPExtUnknownMacroName = TJS_W("マクロ \"%1\" は登録されていません");
const tjs_char* TVPExtKAGWhileStackUnderflow =
    TJS_W("endwhile/continue/break タグが while タグと対応していません");
const tjs_char* TVPExtKAGWhileLostSync =
    TJS_W("シナリオファイルに変更があったため endwhile の戻り先位置を特定できません");
const tjs_char* TVPExtKAGLabelInConditionScope =
    TJS_W("ラベル %1 が if/ignore/while/pushlocalvar スコープ中に存在しています");
const tjs_char* TVPExtKAGIfStackUnderflow =
    TJS_W("else, endif, endignore タグが if, ignore タグと対応していません");
const tjs_char* TVPKAGWrongAttrOutOfMacro =
    TJS_W("マクロ外でタグの属性に '*' や arg=%val が指定されています");
const tjs_char* TVPExtKAGMultiLineDisabled = TJS_W(
    "タグ内に複数行タグを示す '\' が存在しますが、multiLineTagEnabled が true ではありません");
const tjs_char* TVPExtNoStorageAndLabelAtJump =
    TJS_W("call/jump タグに storage と label が両方指定されていません");
const tjs_char* TVPExtLocalVaiablesUnderFlow =
    TJS_W("poplocalvar タグが pushlocalvar タグと対応していません");

#define TVPThrowInternalError TVPThrowExceptionMessage(TVPInternalError, __FILE__, __LINE__)

//---------------------------------------------------------------------------
// tTVPScenarioCacheItem : Scenario Cache Item
//---------------------------------------------------------------------------
tExtTVPScenarioCacheItem::tExtTVPScenarioCacheItem(const ttstr& name, bool isstring)
{
    RefCount = 1;
    Lines = NULL;
    LineCount = 0;
    LabelCached = false;
    try
    {
        LoadScenario(name, isstring);
    }
    catch (...)
    {
        if (Lines)
            delete[] Lines;
        throw;
    }
}
//---------------------------------------------------------------------------
tExtTVPScenarioCacheItem::~tExtTVPScenarioCacheItem()
{
    if (Lines)
        delete[] Lines;
}
//---------------------------------------------------------------------------
void tExtTVPScenarioCacheItem::AddRef()
{
    RefCount++;
}
//---------------------------------------------------------------------------
void tExtTVPScenarioCacheItem::Release()
{
    if (RefCount == 1)
        delete this;
    else
        RefCount--;
}
//---------------------------------------------------------------------------
void tExtTVPScenarioCacheItem::LoadScenario(const ttstr& name, bool isstring)
{
    // load scenario from file or string to buffer

    if (isstring)
    {
        // when onScenarioLoad returns string;
        // assumes the string is scenario
        Buffer = name.c_str();
    }
    else
    {
        // else load from file

        iTJSTextReadStream* stream = NULL;

        try
        {
            stream = TVPCreateTextStreamForRead(name, TJS_W(""));
            //			stream = TVPCreateTextStreamForReadByEncoding(name, TJS_W(""),
            //TJS_W("Shift_JIS"));
            ttstr tmp;
            if (stream)
                stream->Read(tmp, 0);
            Buffer = tmp.c_str();
        }
        catch (...)
        {
            if (stream)
                stream->Destruct();
            throw;
        }
        if (stream)
            stream->Destruct();
    }

    tjs_char* buffer_p = Buffer;

    // pass1: count lines
    tjs_int count = 0;
    tjs_char* ls = buffer_p;
    tjs_char* p = buffer_p;
    while (*p)
    {
        if (*p == TJS_W('\r') || *p == TJS_W('\n'))
        {
            count++;
            if (*p == TJS_W('\r') && p[1] == TJS_W('\n'))
                p++;
            p++;
            ls = p;
        }
        else
        {
            p++;
        }
    }

    if (p != ls)
    {
        count++;
    }

    if (count == 0)
        TVPThrowExceptionMessage(TVPExtKAGNoLine, name);

    Lines = new tLine[count];
    LineCount = count;

    // pass2: split lines
    count = 0;
    ls = buffer_p;
    while (*ls == '\t')
        ls++; // skip leading tabs
    p = ls;
    while (*p)
    {
        if (*p == TJS_W('\r') || *p == TJS_W('\n'))
        {
            Lines[count].Start = ls;
            Lines[count].Length = p - ls;
            count++;
            tjs_char* rp = p;
            if (*p == TJS_W('\r') && p[1] == TJS_W('\n'))
                p++;
            p++;
            ls = p;
            while (*ls == '\t')
                ls++; // skip leading tabs
            p = ls;
            *rp = 0; // end line with null terminater
        }
        else
        {
            p++;
        }
    }

    if (p != ls)
    {
        Lines[count].Start = ls;
        Lines[count].Length = p - ls;
        count++;
    }

    LineCount = count;
    // tab-only last line will not be counted in pass2, thus makes
    // pass2 counted lines are lesser than pass1 lines.
}
//---------------------------------------------------------------------------
void tExtTVPScenarioCacheItem::EnsureLabelCache()
{
    // construct label cache
    if (!LabelCached)
    {
        // make label cache
        LabelAliases.resize(LineCount);
        ttstr prevlabel;
        const tjs_char* p;
        const tjs_char* vl;
        tjs_int i;
        bool out_of_iscript = true;
        for (i = 0; i < LineCount; i++)
        {
            p = Lines[i].Start;

            // check whether this is in/out of iscript range
            if (p[0] == TJS_W('[') || p[0] == TJS_W('@'))
            {
                if (TJS_strcmp(p, TJS_W("[iscript]")) == 0 ||
                    TJS_strcmp(p, TJS_W("[iscript]\\")) == 0 ||
                    TJS_strcmp(p, TJS_W("@iscript")) == 0)
                {
                    out_of_iscript = false;
                    continue;
                }
                if (TJS_strcmp(p, TJS_W("[endscript]")) == 0 ||
                    TJS_strcmp(p, TJS_W("[endscript]\\")) == 0 ||
                    TJS_strcmp(p, TJS_W("@endscript")) == 0)
                {
                    out_of_iscript = true;
                    continue;
                }
            }

            // if this is not in iscript range, search label(*XXX)
            if (out_of_iscript && Lines[i].Length >= 2 && p[0] == TJS_W('*'))
            {
                vl = TJS_strchr(p, TJS_W('|'));
                ttstr label;
                if (vl)
                {
                    // page name found
                    label = ttstr(p, vl - p);
                }
                else
                {
                    label = p;
                }

                if (!label.c_str()[1])
                {
                    if (prevlabel.IsEmpty())
                        TVPThrowExceptionMessage(TVPExtKAGCannotOmmitFirstLabelName);
                    label = prevlabel;
                }

                prevlabel = label;

                tLabelCacheData* data = LabelCache.Find(label);
                if (data)
                {
                    // previous label name found (duplicated label)
                    data->Count++;
                    label = label + TJS_W(":") + ttstr(data->Count);
                }

                LabelCache.Add(label, tLabelCacheData(i, 1));
                LabelAliases[i] = label;
            }
        }

        LabelCached = true;
    }
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPScenarioCache
//---------------------------------------------------------------------------
#define TVP_SCENARIO_MAX_CACHE_SIZE 8
typedef tTJSRefHolder<tExtTVPScenarioCacheItem> tTVPScenarioCacheItemHolder;
typedef tTJSHashCache<ttstr,
                      tTVPScenarioCacheItemHolder,
                      tTJSHashFunc<ttstr>,
                      (TVP_SCENARIO_MAX_CACHE_SIZE * 2)>
    tTVPScenarioCache;
tTVPScenarioCache ExtTVPScenarioCache(TVP_SCENARIO_MAX_CACHE_SIZE);
//---------------------------------------------------------------------------
void ExtTVPClearScnearioCache()
{
    ExtTVPScenarioCache.Clear();
}
//---------------------------------------------------------------------------
struct tTVPClearScenarioCacheCallback : public tTVPCompactEventCallbackIntf
{
    virtual void OnCompact(tjs_int level)
    {
        if (level >= TVP_COMPACT_LEVEL_DEACTIVATE)
        {
            // clear the scenario cache
#ifndef _DEBUG
            ExtTVPClearScnearioCache();
#endif
        }
    }
} static TVPClearScenarioCacheCallback;
static bool TVPClearScenarioCacheCallbackInit = false;
//---------------------------------------------------------------------------
void ExtTVPResetScenarioCache()
{
    if (TVPClearScenarioCacheCallbackInit)
    {
        TVPRemoveCompactEventHook(&TVPClearScenarioCacheCallback);
        TVPClearScenarioCacheCallbackInit = false;
    }
    ExtTVPClearScnearioCache();
}
//---------------------------------------------------------------------------
static tExtTVPScenarioCacheItem* TVPGetScenario(const ttstr& storagename, bool isstring)
{
    // compact interface initialization
    if (!TVPClearScenarioCacheCallbackInit)
    {
        TVPAddCompactEventHook(&TVPClearScenarioCacheCallback);
        TVPClearScenarioCacheCallbackInit = true;
    }

    if (isstring)
    {
        // we do not cache when the string is passed as a scenario
        return new tExtTVPScenarioCacheItem(storagename, true);
    }

    // make hash and search over cache
    tjs_uint32 hash = tTVPScenarioCache::MakeHash(storagename);

    tTVPScenarioCacheItemHolder* ptr = ExtTVPScenarioCache.FindAndTouchWithHash(storagename, hash);
    if (ptr)
    {
        // found in the cache
        return ptr->GetObject();
    }

    // not found in the cache
    tExtTVPScenarioCacheItem* item = new tExtTVPScenarioCacheItem(storagename, false);
    try
    {
        // push into scenario cache hash
        tTVPScenarioCacheItemHolder holder(item);
        ExtTVPScenarioCache.AddWithHash(storagename, hash, holder);
    }
    catch (...)
    {
        item->Release();
        throw;
    }

    return item;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNI_KAGParser : KAGParser TJS native instance
//---------------------------------------------------------------------------
// KAGParser is implemented as a TJS native class/object
tTJSNI_ExtKAGParser::tTJSNI_ExtKAGParser()
{
    Owner = NULL;
    Scenario = NULL;
    Lines = NULL;
    CurLineStr = NULL;
    ProcessSpecialTags = true;
    IgnoreCR = false;
    RecordingMacro = false;
    DebugLevel = tkdlSimple;
    Interrupted = false;
    MacroArgStackDepth = 0;
    MacroArgStackBase = 0;

    EnableNP = false;
    FuzzyReturn = false;
    ReturnErrorStorage = TJS_W("");

    MultiLineTagEnabled = false;
    NumericMacroArgumentsEnabled = false;

    ClearLocalVariables();
}
//---------------------------------------------------------------------------
tjs_error tTJSNI_ExtKAGParser::Construct(tjs_int numparams,
                                         tTJSVariant** param,
                                         iTJSDispatch2* tjs_obj)
{
    tjs_error hr = inherited::Construct(numparams, param, tjs_obj);
    if (TJS_FAILED(hr))
        return hr;

    Owner = tjs_obj;

    return TJS_S_OK;
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::Invalidate()
{
    // invalidate this object

    // release objects
    ClearMacroArgs();
    ClearBuffer();

    Owner = NULL;

    inherited::Invalidate();
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::operator=(const tTJSNI_ExtKAGParser& ref)
{
    // copy Macros
    Macros.Assign(ref.Macros.GetDicNoAddRef()); // GetDicNoAddRef() is const

    // copy ParamMacros
    ParamMacros.Assign(ref.ParamMacros.GetDicNoAddRef()); // GetDicNoAddRef() is const

    // copy MacroArgs
    {
        ClearMacroArgs();

        for (tjs_uint i = 0; i < ref.MacroArgStackDepth; i++)
        {
            tTJSDic* dicp = new tTJSDic(*ref.MacroArgs[i]);
            MacroArgs.push_back(dicp);
        }
        MacroArgStackDepth = ref.MacroArgStackDepth;
    }
    MacroArgStackBase = ref.MacroArgStackBase;

    // copy CallStack
    CallStack = ref.CallStack;

    // copy StorageName, StorageShortName
    StorageName = ref.StorageName;
    StorageShortName = ref.StorageShortName;

    // copy Scenario
    if (Scenario != ref.Scenario)
    {
        if (Scenario)
            Scenario->Release(), Scenario = NULL, Lines = NULL, CurLineStr = NULL;
        Scenario = ref.Scenario;
        Lines = ref.Lines;
        LineCount = ref.LineCount;
        if (Scenario)
            Scenario->AddRef();
    }

    // copy CurStorage, CurLine, CurPos
    CurLine = ref.CurLine;
    CurPos = ref.CurPos;

    // copy CurLineStr, LineBuffer, LineBufferUsing
    CurLineStr = ref.CurLineStr;
    LineBuffer = ref.LineBuffer;
    LineBufferUsing = ref.LineBufferUsing;

    // copy CurLabel, CurPage
    CurLabel = ref.CurLabel;
    CurPage = ref.CurPage;

    // copy DebugLebel, IgnoreCR
    DebugLevel = ref.DebugLevel;
    IgnoreCR = ref.IgnoreCR;

    // copy EnableNP, FuzzyReturn, ReturnErrorStorage
    EnableNP = ref.EnableNP;
    FuzzyReturn = ref.FuzzyReturn;
    ReturnErrorStorage = ref.ReturnErrorStorage;

    // copy RecordingMacro, RecordingMacroStr, RecordingMacroName
    RecordingMacro = ref.RecordingMacro;
    RecordingMacroStr = ref.RecordingMacroStr;
    RecordingMacroName = ref.RecordingMacroName;

    // copy ExcludeLevel, IfLevel
    ExcludeLevel = ref.ExcludeLevel;
    IfLevel = ref.IfLevel;
    ExcludeLevelStack = ref.ExcludeLevelStack;
    IfLevelExecutedStack = ref.IfLevelExecutedStack;

    // copy WhileStack/WhileLevel
    WhileLevelExp = ref.WhileLevelExp;
    WhileLevelEach = ref.WhileLevelEach;
    WhileStack = ref.WhileStack;

    // copy LocalVariables
    // This is a bit silly, but need to have "tmp" as
    // tTJSAry.Assign() needs non-const argument.
    tTJSVariant tmp(ref.LocalVariables);
    LocalVariables.Assign(tmp);
}
//---------------------------------------------------------------------------
iTJSDispatch2* tTJSNI_ExtKAGParser::Store()
{
    // store current status into newly created dictionary object
    // and return the dictionary object.
    iTJSDispatch2* dic = TJSCreateDictionaryObject();
    try
    {
        tTJSVariant val;

        // store LocalVariables
        LocalVariables.Store(dic, TJS_W("LocalVariables"));

        // store MacroArgs
        ParamMacros.Store(dic, TJS_W("paramMacros"));

        // create and assign macro dictionary
        Macros.Store(dic, TJS_W("macros"));

        // create and assign macro arguments
        {
            iTJSDispatch2* dsp;
            dsp = TJSCreateArrayObject();
            tTJSVariant tmp(dsp, dsp);
            dsp->Release();
            dic->PropSet(TJS_MEMBERENSURE, TJS_W("macroArgs"), NULL, &tmp, dic);

            for (tjs_uint i = 0; i < MacroArgStackDepth; i++)
                MacroArgs[i]->Store(dsp, i);
        }

        // create call stack array and copy call stack status
        {
            iTJSDispatch2* dsp;
            dsp = TJSCreateArrayObject();
            tTJSVariant tmp(dsp, dsp);
            dsp->Release();
            dic->PropSet(TJS_MEMBERENSURE, TJS_W("callStack"), NULL, &tmp, dic);

            std::vector<tCallStackData>::iterator i;
            for (i = CallStack.begin(); i != CallStack.end(); i++)
            {
                iTJSDispatch2* dic;
                dic = TJSCreateDictionaryObject();
                tTJSVariant tmp(dic, dic);
                dic->Release();
                dsp->PropSetByNum(TJS_MEMBERENSURE, i - CallStack.begin(), &tmp, dsp);

                tTJSVariant val;
                val = i->Storage;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("storage"), NULL, &val, dic);
                val = i->Label;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("label"), NULL, &val, dic);
                val = i->Offset;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("offset"), NULL, &val, dic);
                val = i->OrgLineStr;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("orgLineStr"), NULL, &val, dic);
                val = i->LineBuffer;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("lineBuffer"), NULL, &val, dic);
                val = i->Pos;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("pos"), NULL, &val, dic);
                val = (tjs_int)i->LineBufferUsing;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("lineBufferUsing"), NULL, &val, dic);
                val = (tjs_int)i->MacroArgStackBase;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("macroArgStackBase"), NULL, &val, dic);
                val = (tjs_int)i->MacroArgStackDepth;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("macroArgStackDepth"), NULL, &val, dic);
                val = i->ExcludeLevel;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("ExcludeLevel"), NULL, &val, dic);
                val = (tjs_int)i->IfLevel;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("IfLevel"), NULL, &val, dic);

                StoreIntStackToDic(dic, i->ExcludeLevelStack, TJS_W("ExcludeLevelStack"));
                StoreBoolStackToDic(dic, i->IfLevelExecutedStack, TJS_W("IfLevelExecutedStack"));

                val = (tjs_int)i->WhileStackDepth;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("WhileStackDepth"), NULL, &val, dic);
                val = i->LocalVariablesCount;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("LocalVariablesCount"), NULL, &val, dic);
            }
        }

        // store StorageName, StorageShortName ( Buffer is not stored )
        val = StorageName;
        dic->PropSet(TJS_MEMBERENSURE, TJS_W("storageName"), NULL, &val, dic);
        val = StorageShortName;
        dic->PropSet(TJS_MEMBERENSURE, TJS_W("storageShortName"), NULL, &val, dic);

        // ( Lines and LineCount are not stored )

        // store CurStorage, CurLine, CurPos
        val = CurLine;
        dic->PropSet(TJS_MEMBERENSURE, TJS_W("curLine"), NULL, &val, dic);
        val = CurPos;
        dic->PropSet(TJS_MEMBERENSURE, TJS_W("curPos"), NULL, &val, dic);

        // ( CurLineStr is not stored )

        // LineBuffer, LineBufferUsing
        val = LineBuffer;
        dic->PropSet(TJS_MEMBERENSURE, TJS_W("lineBuffer"), NULL, &val, dic);
        val = (tjs_int)LineBufferUsing;
        dic->PropSet(TJS_MEMBERENSURE, TJS_W("lineBufferUsing"), NULL, &val, dic);

        // store CurLabel ( CurPage is not stored )
        val = CurLabel;
        dic->PropSet(TJS_MEMBERENSURE, TJS_W("curLabel"), NULL, &val, dic);

        //// ( DebugLabel and IgnoreCR are not stored )
        // KAICHO added: IgnoreCR should be stored/restored for the
        // case it is changed in the KAG scenario.
        // If it is not stored/restored, the change of IgnoreCR in KAG
        // senario could be forgotten after loading data.
        val = IgnoreCR;
        dic->PropSet(TJS_MEMBERENSURE, TJS_W("IgnoreCR"), NULL, &val, dic);

        // ( RecordingMacro, RecordingMacroStr, RecordingMacroName are not stored)

        // EnableNP, FuzzyReturn, ReturnErrorStorage
        val = EnableNP;
        dic->PropSet(TJS_MEMBERENSURE, TJS_W("EnableNP"), NULL, &val, dic);
        // Note: FussyReturn is not stored for changing during plaing games
        // val = FuzzyReturn;
        // dic->PropSet(TJS_MEMBERENSURE, TJS_W("FuzzyReturn"), NULL,
        //	&val, dic);
        //
        // Note: ReturnErrorStorage is not stored for changing during plaing games
        // val = ReturnErrorStorage;
        // dic->PropSet(TJS_MEMBERENSURE, TJS_W("ReturnErrorStorage"), NULL,
        // 	&val, dic);

        // ExcludeLevel, IfLevel, ExcludeLevelStack, IfLevelExecutedStack
        val = ExcludeLevel;
        dic->PropSet(TJS_MEMBERENSURE, TJS_W("ExcludeLevel"), NULL, &val, dic);
        val = IfLevel;
        dic->PropSet(TJS_MEMBERENSURE, TJS_W("IfLevel"), NULL, &val, dic);
        StoreIntStackToDic(dic, ExcludeLevelStack, TJS_W("ExcludeLevelStack"));
        StoreBoolStackToDic(dic, IfLevelExecutedStack, TJS_W("IfLevelExecutedStack"));

        val = WhileLevelExp;
        dic->PropSet(TJS_MEMBERENSURE, TJS_W("WhileLevelExp"), NULL, &val, dic);
        val = WhileLevelEach;
        dic->PropSet(TJS_MEMBERENSURE, TJS_W("WhileLevelEach"), NULL, &val, dic);
        // create while stack array and copy call stack status
        {
            iTJSDispatch2* dsp;
            dsp = TJSCreateArrayObject();
            tTJSVariant tmp(dsp, dsp);
            dsp->Release();
            dic->PropSet(TJS_MEMBERENSURE, TJS_W("whileStack"), NULL, &tmp, dic);

            std::vector<tWhileStackData>::iterator i;
            for (i = WhileStack.begin(); i != WhileStack.end(); i++)
            {
                iTJSDispatch2* dic;
                dic = TJSCreateDictionaryObject();
                tTJSVariant tmp(dic, dic);
                dic->Release();
                dsp->PropSetByNum(TJS_MEMBERENSURE, i - WhileStack.begin(), &tmp, dsp);

                tTJSVariant val;
                val = i->Storage;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("storage"), NULL, &val, dic);
                val = i->Label;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("label"), NULL, &val, dic);
                val = i->Offset;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("offset"), NULL, &val, dic);
                val = i->OrgLineStr;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("orgLineStr"), NULL, &val, dic);
                val = i->LineBuffer;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("lineBuffer"), NULL, &val, dic);
                val = i->Pos;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("pos"), NULL, &val, dic);
                val = (tjs_int)i->LineBufferUsing;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("lineBufferUsing"), NULL, &val, dic);
                val = i->ExcludeLevel;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("ExcludeLevel"), NULL, &val, dic);
                val = (tjs_int)i->IfLevel;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("IfLevel"), NULL, &val, dic);

                val = i->WhileLevelExp;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("WhileLevelExp"), NULL, &val, dic);
                val = i->WhileLevelEach;
                dic->PropSet(TJS_MEMBERENSURE, TJS_W("WhileLevelEach"), NULL, &val, dic);
            }
        }

        // store MacroArgStackBase, MacroArgStackDepth
        val = (tjs_int)MacroArgStackBase;
        dic->PropSet(TJS_MEMBERENSURE, TJS_W("macroArgStackBase"), NULL, &val, dic);

        val = (tjs_int)MacroArgStackDepth;
        dic->PropSet(TJS_MEMBERENSURE, TJS_W("macroArgStackDepth"), NULL, &val, dic);
    }
    catch (...)
    {
        dic->Release();
        throw;
    }
    return dic;
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::StoreIntStackToDic(iTJSDispatch2* dic,
                                             std::vector<tjs_int>& stack,
                                             const tjs_char* membername)
{
    ttstr stack_str;
    const static tjs_char hex[] = TJS_W("0123456789abcdef");
    tjs_char* p = stack_str.AllocBuffer(stack.size() * 8);
    for (std::vector<tjs_int>::iterator it = stack.begin(); it != stack.end(); ++it)
    {
        tjs_int v = *it;
        p[0] = hex[(v >> 28) & 0x000f];
        p[1] = hex[(v >> 24) & 0x000f];
        p[2] = hex[(v >> 20) & 0x000f];
        p[3] = hex[(v >> 16) & 0x000f];
        p[4] = hex[(v >> 12) & 0x000f];
        p[5] = hex[(v >> 8) & 0x000f];
        p[6] = hex[(v >> 4) & 0x000f];
        p[7] = hex[(v >> 0) & 0x000f];
        p += 8;
    }
    *p = '\0';
    stack_str.FixLen();
    tTJSVariant val;
    val = stack_str;
    dic->PropSet(TJS_MEMBERENSURE, membername, NULL, &val, dic);
}

void tTJSNI_ExtKAGParser::RestoreIntStackFromStr(std::vector<tjs_int>& stack, const ttstr& str)
{
    stack.clear();
    tjs_int len = str.length() / 8;
    for (tjs_int i = 0; i < len; ++i)
    {
        stack.push_back(
            (((str[i + 0] <= '9') ? (str[i + 0] - '0') : (str[i + 0] - 'a' + 10)) << 28) |
            (((str[i + 1] <= '9') ? (str[i + 1] - '0') : (str[i + 1] - 'a' + 10)) << 24) |
            (((str[i + 2] <= '9') ? (str[i + 2] - '0') : (str[i + 2] - 'a' + 10)) << 20) |
            (((str[i + 3] <= '9') ? (str[i + 3] - '0') : (str[i + 3] - 'a' + 10)) << 16) |
            (((str[i + 4] <= '9') ? (str[i + 4] - '0') : (str[i + 4] - 'a' + 10)) << 12) |
            (((str[i + 5] <= '9') ? (str[i + 5] - '0') : (str[i + 5] - 'a' + 10)) << 8) |
            (((str[i + 6] <= '9') ? (str[i + 6] - '0') : (str[i + 6] - 'a' + 10)) << 4) |
            (((str[i + 7] <= '9') ? (str[i + 7] - '0') : (str[i + 7] - 'a' + 10)) << 0));
    }
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::RestoreBoolStackFromStr(std::vector<bool>& stack, const ttstr& str)
{
    stack.clear();
    tjs_int len = str.length();
    for (tjs_int i = 0; i < len; ++i)
    {
        stack.push_back(str[i] == '1');
    }
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::StoreBoolStackToDic(iTJSDispatch2* dic,
                                              std::vector<bool>& stack,
                                              const tjs_char* membername)
{
    ttstr stack_str;
    const static tjs_char bit[] = TJS_W("01");
    tjs_char* p = stack_str.AllocBuffer(stack.size());
    for (std::vector<bool>::iterator it = stack.begin(); it != stack.end(); ++it)
    {
        *p = bit[(tjs_int)(*it)];
        ++p;
    }
    *p = '\0';
    stack_str.FixLen();
    tTJSVariant val;
    val = stack_str;
    dic->PropSet(TJS_MEMBERENSURE, membername, NULL, &val, dic);
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::Restore(iTJSDispatch2* dic)
{
    if (DebugLevel > tkdlVerbose)
        TVPAddLog(TJS_W("Invoking: KAGParser::Restore()"));

    // restore status from "dic"
    tTJSVariant val;
    //	tjs_error er;

    // restore LocalVariables
    val.Clear();
    const bool has_ext_local_variables =
        TJS_SUCCEEDED(dic->PropGet(0, TJS_W("LocalVariables"), NULL, &val, dic)) &&
        val.Type() == tvtObject && val.AsObjectNoAddRef() != NULL;
    if (has_ext_local_variables)
        LocalVariables.Restore(dic, TJS_W("LocalVariables"));
    else
        ClearLocalVariables();

    // restore ParamMacros
    ParamMacros.Restore(dic, TJS_W("paramMacros"));

    // restore macros
    Macros.Restore(dic, TJS_W("macros"));

    {
        // restore macro args
        MacroArgStackDepth = 0;

        val.Clear();
        dic->PropGet(0, TJS_W("macroArgs"), NULL, &val, dic);
        if (val.Type() != tvtVoid)
        {
            iTJSDispatch2* ary = val.AsObjectNoAddRef();
            tTJSVariant v;
            ary->PropGet(0, TJS_W("count"), NULL, &v, NULL);
            tjs_int count = v;

            ClearMacroArgs();

            val.Clear();
            dic->PropGet(0, TJS_W("macroArgStackDepth"), NULL, &val, dic);
            if (val.Type() != tvtVoid)
                MacroArgStackDepth = (tjs_uint)(tjs_int)val;

            for (tjs_int i = 0; i < count; i++)
            {
                tTJSDic* dicp = new tTJSDic();
                dicp->Restore(ary, i);
                MacroArgs.push_back(dicp);
            }
        }

        if (MacroArgStackDepth != MacroArgs.size())
            TVPThrowExceptionMessage(TVPExtKAGMalformedSaveData);

        MacroArgStackBase = MacroArgs.size(); // later reset to MacroArgStackBase

        // restore call stack
        val.Clear();
        dic->PropGet(0, TJS_W("callStack"), NULL, &val, dic);
        if (val.Type() != tvtVoid)
        {
            tTJSVariantClosure clo = val.AsObjectClosureNoAddRef();
            tTJSVariant v;
            tjs_int count = 0;
            clo.PropGet(0, TJS_W("count"), NULL, &v, NULL);
            count = v;

            CallStack.clear();

            for (tjs_int i = 0; i < count; i++)
            {
                ttstr Storage;
                ttstr Label;
                tjs_int Offset;
                ttstr OrgLineStr;
                ttstr LineBuffer;
                tjs_int Pos;
                bool LineBufferUsing;
                tjs_uint MacroArgStackBase;
                tjs_uint MacroArgStackDepth;
                tjs_int ExcludeLevel;
                tjs_int IfLevel;
                std::vector<tjs_int> ExcludeLevelStack;
                std::vector<bool> IfLevelExecutedStack;

                tTJSVariant val;

                clo.PropGetByNum(0, i, &v, NULL);
                tTJSVariantClosure dic = v.AsObjectClosureNoAddRef();
                dic.PropGet(0, TJS_W("storage"), NULL, &val, NULL);
                Storage = val;
                dic.PropGet(0, TJS_W("label"), NULL, &val, NULL);
                Label = val;
                dic.PropGet(0, TJS_W("offset"), NULL, &val, NULL);
                Offset = val;
                dic.PropGet(0, TJS_W("orgLineStr"), NULL, &val, NULL);
                OrgLineStr = val;
                dic.PropGet(0, TJS_W("lineBuffer"), NULL, &val, NULL);
                LineBuffer = val;
                dic.PropGet(0, TJS_W("pos"), NULL, &val, NULL);
                Pos = val;
                dic.PropGet(0, TJS_W("lineBufferUsing"), NULL, &val, NULL);
                LineBufferUsing = 0 != (tjs_int)val;
                dic.PropGet(0, TJS_W("macroArgStackBase"), NULL, &val, NULL);
                MacroArgStackBase = (tjs_int)val;
                dic.PropGet(0, TJS_W("macroArgStackDepth"), NULL, &val, NULL);
                MacroArgStackDepth = (tjs_int)val;
                dic.PropGet(0, TJS_W("ExcludeLevel"), NULL, &val, NULL);
                ExcludeLevel = val;
                dic.PropGet(0, TJS_W("IfLevel"), NULL, &val, NULL);
                IfLevel = val;

                ttstr stack_str;
                dic.PropGet(0, TJS_W("ExcludeLevelStack"), NULL, &val, NULL);
                stack_str = val;
                RestoreIntStackFromStr(ExcludeLevelStack, stack_str);

                dic.PropGet(0, TJS_W("IfLevelExecutedStack"), NULL, &val, NULL);
                stack_str = val;
                RestoreBoolStackFromStr(IfLevelExecutedStack, stack_str);

                val.Clear();
                tjs_uint WhileStackDepth = 0;
                if (TJS_SUCCEEDED(
                        dic.PropGet(0, TJS_W("WhileStackDepth"), NULL, &val, NULL)) &&
                    val.Type() != tvtVoid)
                    WhileStackDepth = tjs_int(val);

                val.Clear();
                tjs_int LocalVariablesCount = 0;
                if (TJS_SUCCEEDED(
                        dic.PropGet(0, TJS_W("LocalVariablesCount"), NULL, &val, NULL)) &&
                    val.Type() != tvtVoid)
                    LocalVariablesCount = tjs_int(val);

                CallStack.push_back(tCallStackData(
                    Storage, Label, Offset, OrgLineStr, LineBuffer, Pos, LineBufferUsing,
                    MacroArgStackBase, MacroArgStackDepth, ExcludeLevelStack, ExcludeLevel,
                    IfLevelExecutedStack, IfLevel, WhileStackDepth, LocalVariablesCount));
            }
        }

        // restore StorageName, StorageShortName, CurStorage, CurLabel
        val.Clear();
        dic->PropGet(0, TJS_W("storageName"), NULL, &val, dic);
        if (val.Type() != tvtVoid)
            StorageName = val;
        val.Clear();
        dic->PropGet(0, TJS_W("storageShortName"), NULL, &val, dic);
        if (val.Type() != tvtVoid)
            StorageShortName = val;
        val.Clear();
        dic->PropGet(0, TJS_W("curLabel"), NULL, &val, dic);
        if (val.Type() != tvtVoid)
            CurLabel = val;

        // restore while stack
        val.Clear();
        const bool has_ext_while_stack =
            TJS_SUCCEEDED(dic->PropGet(0, TJS_W("whileStack"), NULL, &val, dic)) &&
            val.Type() == tvtObject && val.AsObjectNoAddRef() != NULL;
        if (has_ext_while_stack)
        {
            tTJSVariantClosure clo = val.AsObjectClosureNoAddRef();
            tTJSVariant v;
            tjs_int count = 0;
            clo.PropGet(0, TJS_W("count"), NULL, &v, NULL);
            count = v;

            WhileStack.clear();

            for (tjs_int i = 0; i < count; i++)
            {
                ttstr Storage;
                ttstr Label;
                tjs_int Offset;
                ttstr OrgLineStr;
                ttstr LineBuffer;
                tjs_int Pos;
                bool LineBufferUsing;
                tjs_int ExcludeLevel;
                tjs_int IfLevel;
                ttstr WhileLevelExp;
                ttstr WhileLevelEach;

                tTJSVariant val;

                clo.PropGetByNum(0, i, &v, NULL);
                tTJSVariantClosure dic = v.AsObjectClosureNoAddRef();
                dic.PropGet(0, TJS_W("storage"), NULL, &val, NULL);
                Storage = val;
                dic.PropGet(0, TJS_W("label"), NULL, &val, NULL);
                Label = val;
                dic.PropGet(0, TJS_W("offset"), NULL, &val, NULL);
                Offset = val;
                dic.PropGet(0, TJS_W("orgLineStr"), NULL, &val, NULL);
                OrgLineStr = val;
                dic.PropGet(0, TJS_W("lineBuffer"), NULL, &val, NULL);
                LineBuffer = val;
                dic.PropGet(0, TJS_W("pos"), NULL, &val, NULL);
                Pos = val;
                dic.PropGet(0, TJS_W("lineBufferUsing"), NULL, &val, NULL);
                LineBufferUsing = 0 != (tjs_int)val;
                dic.PropGet(0, TJS_W("ExcludeLevel"), NULL, &val, NULL);
                ExcludeLevel = val;
                dic.PropGet(0, TJS_W("IfLevel"), NULL, &val, NULL);
                IfLevel = val;

                dic.PropGet(0, TJS_W("WhileLevelExp"), NULL, &val, NULL);
                WhileLevelExp = val;
                dic.PropGet(0, TJS_W("WhileLevelEach"), NULL, &val, NULL);
                WhileLevelEach = val;

                WhileStack.push_back(tWhileStackData(Storage, Label, Offset, OrgLineStr, LineBuffer,
                                                     Pos, LineBufferUsing, ExcludeLevel, IfLevel,
                                                     WhileLevelExp, WhileLevelEach));
            }
        }
        else
        {
            ClearWhileStack();
        }
        val.Clear();
        dic->PropGet(0, TJS_W("WhileLevelExp"), NULL, &val, dic);
        if (val.Type() != tvtVoid)
            WhileLevelExp = (ttstr)val;
        val.Clear();
        dic->PropGet(0, TJS_W("WhileLevelEach"), NULL, &val, dic);
        if (val.Type() != tvtVoid)
            WhileLevelEach = (ttstr)val;

        // Aether builds before the dedicated ExtKAGParser plugin stored the
        // core KAGParser state. That format has neither ExtKAGParser's
        // per-call while sentinels nor its local-variable frames. Recreate
        // those internal frames so an existing save can return from an
        // active [call] without underflowing either stack.
        if (!has_ext_local_variables && !has_ext_while_stack)
        {
            ClearWhileStack();
            ClearLocalVariables();

            for (std::vector<tCallStackData>::iterator i = CallStack.begin();
                 i != CallStack.end(); ++i)
            {
                WhileStack.push_back(tWhileStackData(
                    i->Storage, i->Label, i->Offset, i->OrgLineStr, i->LineBuffer,
                    i->Pos, i->LineBufferUsing, i->ExcludeLevel, i->IfLevel,
                    TJS_W(""), TJS_W("")));
                i->WhileStackDepth = WhileStack.size();

                tTJSVariant local = tTJSDic();
                LocalVariables.Push(local);
                i->LocalVariablesCount = LocalVariables.GetSize();
            }

            if (!CallStack.empty())
                TVPAddLog(TJS_W("ExtKAGParser: migrated legacy core KAGParser save state"));
        }

        // load scenario
        ttstr storage = StorageName, label = CurLabel;
        ClearBuffer(); // ensure re-loading the scenario
        LoadScenario(storage);
        GoToLabel(label);

        // KAICHO added: restore IgnoreCR, see tTJSNI_ExtKAGParser::Store()
        // for more details and reasons to add this
        val.Clear();
        dic->PropGet(0, TJS_W("IgnoreCR"), NULL, &val, dic);
        IgnoreCR = int(val) ? true : false; // to avoid compiler warning

        // restore EnableNP, FuzzyReturn, ReturnErrorStorage
        val.Clear();
        dic->PropGet(0, TJS_W("EnableNP"), NULL, &val, dic);
        EnableNP = int(val) ? true : false; // to avoid compiler warning
        // Note: FussyReturn is not restored
        // val.Clear();
        // dic->PropGet(0, TJS_W("FuzzyReturn"), NULL, &val, dic);
        // FuzzyReturn = int(val) ? true : false; // to avoid compiler warning
        // Note: ReturnErrorStorage is not restored
        // val.Clear();
        // dic->PropGet(0, TJS_W("ReturnErrorStorage"), NULL, &val, dic);
        // ReturnErrorStorage = val;

        // ExcludeLevel, IfLevel
        val.Clear();
        dic->PropGet(0, TJS_W("ExcludeLevel"), NULL, &val, dic);
        if (val.Type() != tvtVoid)
            ExcludeLevel = (tjs_int)val;
        val.Clear();
        dic->PropGet(0, TJS_W("IfLevel"), NULL, &val, dic);
        if (val.Type() != tvtVoid)
            IfLevel = (tjs_int)val;

        // ExcludeLevelStack, IfLevelExecutedStack
        val.Clear();
        dic->PropGet(0, TJS_W("ExcludeLevelStack"), NULL, &val, dic);
        if (val.Type() != tvtVoid)
        {
            ttstr stack_str;
            stack_str = val;
            RestoreIntStackFromStr(ExcludeLevelStack, stack_str);
        }

        val.Clear();
        dic->PropGet(0, TJS_W("IfLevelExecutedStack"), NULL, &val, dic);
        if (val.Type() != tvtVoid)
        {
            ttstr stack_str;
            stack_str = val;
            RestoreBoolStackFromStr(IfLevelExecutedStack, stack_str);
        }

        // restore MacroArgStackBase
        val.Clear();
        dic->PropGet(0, TJS_W("macroArgStackBase"), NULL, &val, dic);
        if (val.Type() != tvtVoid)
            MacroArgStackBase = (tjs_uint)(tjs_int)val;
    }
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::LoadScenario(const ttstr& name)
{
    if (DebugLevel > tkdlVerbose)
        TVPAddLog(TJS_W("Invoking: KAGParser::LoadScenario(") + name + TJS_W(")"));

    // load scenario to buffer

    BreakConditionAndMacro();

    if (StorageName == name)
    {
        // avoid re-loading
        Rewind();
    }
    else
    {
        ClearBuffer();

        // fire onScenarioLoad
        tTJSVariant param = name;
        tTJSVariant* pparam = &param;
        tTJSVariant result;
        static ttstr funcname(TJSMapGlobalStringMap(TJS_W("onScenarioLoad")));
        tjs_error status =
            Owner->FuncCall(0, funcname.c_str(), funcname.GetHint(), &result, 1, &pparam, Owner);

        if (status == TJS_S_OK && result.Type() == tvtString)
        {
            // when onScenarioLoad returns string;
            // assumes the string is scenario
            Scenario = TVPGetScenario(ttstr(result), true);
        }
        else
        {
            // else load from file
            Scenario = TVPGetScenario(name, false);
        }

        Lines = Scenario->GetLines();
        LineCount = Scenario->GetLineCount();

        Rewind();

        StorageName = name;
        StorageShortName = TVPExtractStorageName(name);

        if (DebugLevel >= tkdlSimple)
        {
            static ttstr hr(TJS_W("========================================")
                                TJS_W("========================================"));
            TVPAddLog(hr);
            TVPAddLog(TJS_W("Scenario loaded : ") + name);
        }
    }

    if (Owner)
    {
        tTJSVariant param = StorageName;
        tTJSVariant* pparam = &param;
        static ttstr funcname(TJSMapGlobalStringMap(TJS_W("onScenarioLoaded")));
        Owner->FuncCall(0, funcname.c_str(), funcname.GetHint(), NULL, 1, &pparam, Owner);
    }
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::Clear()
{
    if (DebugLevel > tkdlVerbose)
        TVPAddLog(TJS_W("Invoking: KAGParser::Clear()"));
    // clear all states
    ExtTVPClearScnearioCache(); // also invalidates the scenario cache
    ClearBuffer();
    ClearMacroArgs();
    ClearCallStack();
    ClearWhileStack();
    ClearLocalVariables();
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::ClearBuffer()
{
    if (DebugLevel > tkdlVerbose)
        TVPAddLog(TJS_W("Invoking: KAGParser::ClearBuffer()"));
    // clear internal buffer
    BreakConditionAndMacro();
    // BreakConditionAndMacro() should be before Scenario = NULL,
    // as Scenario is referenced in PopWhileStack();
    // however it has been checked in the func. This is a "double check".
    if (Scenario)
        Scenario->Release(), Scenario = NULL, Lines = NULL, CurLineStr = NULL;
    StorageName.Clear();
    StorageShortName.Clear();
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::Rewind()
{
    // set current position to first
    CurLine = 0;
    CurPos = 0;
    CurLineStr = Lines[0].Start;
    LineBufferUsing = false;
    BreakConditionAndMacro();
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::BreakConditionAndMacro()
{
    if (DebugLevel > tkdlVerbose)
        TVPAddLog(TJS_W("Invoking: BreakConditionAndMacro()"));

    // pop LocalVariables to the top level of the senario
    PopLocalVariablesBeforeTheLatestCallStack();

    // pop WhileStack to the top level of the senerio
    ClearWhileStackToTheLatestCallStack();
    // BreakCondition means that it will begin to run new senario
    // (could be called), need to clear WhileLevel* also.
    WhileLevelExp = TJS_W("");
    WhileLevelEach = TJS_W("");

    // break condition state and macro recording
    RecordingMacro = false;
    ExcludeLevel = -1;
    ExcludeLevelStack.clear();
    IfLevelExecutedStack.clear();
    IfLevel = 0;
    PopMacroArgsTo(MacroArgStackBase);
    // clear macro argument down to current base stack position
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::GoToLabel(const ttstr& name)
{
    // search label and set current position
    // parameter "name" must start with '*'
    if (name.IsEmpty())
        return;

    Scenario->EnsureLabelCache();

    tExtTVPScenarioCacheItem::tLabelCacheData* newline;

    newline = Scenario->GetLabelCache().Find(name);

    if (newline)
    {
        // found the label in cache
        const tjs_char* vl;
        vl = TJS_strchr(Lines[newline->Line].Start, TJS_W('|'));

        CurLabel = Scenario->GetLabelAliasFromLine(newline->Line);
        if (vl)
            CurPage = vl + 1;
        else
            CurPage.Clear();
        CurLine = newline->Line;
        CurPos = 0;
        LineBufferUsing = false;
    }
    else
    {
        // not found
        TVPThrowExceptionMessage(TVPExtKAGLabelNotFound, StorageName, name);
    }

    if (DebugLevel >= tkdlSimple)
    {
        static ttstr hr(TJS_W("- - - - - - - - - - - - - - - - - - - - ")
                            TJS_W("- - - - - - - - - - - - - - - - - - - - "));
        TVPAddLog(hr);
        TVPAddLog(StorageShortName + TJS_W(" : jumped to : ") + name);
    }

    BreakConditionAndMacro();
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::GoToLine(tjs_int line)
{
    if (line < 0 || line >= LineCount)
        TVPThrowExceptionMessage(TVPExtKAGLineNotFound, StorageName, line);

    Scenario->EnsureLabelCache();

    tjs_int labelline;
    ttstr labelname;
    FindNearestLabel(line, labelline, labelname);
    CurLabel = labelname;
    CurLine = line;
    CurPos = 0;
    LineBufferUsing = false;

    if (DebugLevel >= tkdlSimple)
    {
        static ttstr hr(TJS_W("- - - - - - - - - - - - - - - - - - - - ")
                            TJS_W("- - - - - - - - - - - - - - - - - - - - "));
        TVPAddLog(hr);
        TVPAddLog(StorageShortName + TJS_W(" : jumped to line : ") + ttstr(line));
    }

    BreakConditionAndMacro();
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::GoToStorageAndLabel(const ttstr& storage, const ttstr& label)
{
    // This is added for further strong check
    if (storage.IsEmpty() && label.IsEmpty())
        TVPThrowExceptionMessage(TVPExtNoStorageAndLabelAtJump);

    if (!storage.IsEmpty())
        LoadScenario(storage);
    if (!label.IsEmpty())
        GoToLabel(label);
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::CallLabel(const ttstr& name)
{
    // for extra conductor or so
    tTJSDic tmp;
    PushCallStack(tTJSVariant(false), tmp);
    GoToLabel(name);
}
//---------------------------------------------------------------------------
bool tTJSNI_ExtKAGParser::SkipCommentOrLabel()
{
    // skip comment or label, and go to next line.
    // fire OnScript event if [script] thru [endscript] ( or @script thru
    // @endscript ) is found.
    Scenario->EnsureLabelCache();

    CurPos = 0;
    if (CurLine >= LineCount)
        return false;
    for (; CurLine < LineCount; CurLine++)
    {
        if (!Lines)
            return false; // in this loop, Lines can be NULL when onScript does so.

        const tjs_char* p = Lines[CurLine].Start;

        if (p[0] == TJS_W(';'))
            continue; // comment

        if (p[0] == TJS_W('*'))
        {
            // label
            if (RecordingMacro)
                TVPThrowExceptionMessage(TVPExtLabelInMacro);
            // check IfLevel, whether the label is placed in
            // [if][ignore][while] scope
            if (IfLevel > 0)
                TVPThrowExceptionMessage(TVPExtKAGLabelInConditionScope, p);
            // check whether the label is in LocalVariables scope
            if ((CallStack.empty() && LocalVariables.GetSize() != 1) ||
                (!CallStack.empty() &&
                 LocalVariables.GetSize() != CallStack.back().LocalVariablesCount))
                TVPThrowExceptionMessage(TVPExtKAGLabelInConditionScope, p);

            tjs_char* vl = TJS_strchr(const_cast<tjs_char*>(p), TJS_W('|'));
            bool pagename;
            if (vl)
            {
                CurLabel = Scenario->GetLabelAliasFromLine(CurLine);
                CurPage = ttstr(vl + 1);
                pagename = true;
            }
            else
            {
                CurLabel = Scenario->GetLabelAliasFromLine(CurLine);
                CurPage.Clear();
                pagename = false;
            }

            // fire onLabel callback event
            if (Owner)
            {
                tTJSVariant param[2];
                param[0] = CurLabel;
                if (pagename)
                    param[1] = CurPage;
                tTJSVariant* pparam[2] = {param, param + 1};
                static ttstr onLabel_name(TJSMapGlobalStringMap(TJS_W("onLabel")));
                Owner->FuncCall(0, onLabel_name.c_str(), onLabel_name.GetHint(), NULL, 2, pparam,
                                Owner);
            }

            continue;
        }

        if (p[0] == TJS_W('[') &&
                (!TJS_strcmp(p, TJS_W("[iscript]")) || !TJS_strcmp(p, TJS_W("[iscript]\\"))) ||
            p[0] == TJS_W('@') && (!TJS_strcmp(p, TJS_W("@iscript"))))
        {
            // inline TJS script
            ttstr script;
            CurLine++;

            tjs_int script_start = CurLine;

            for (; CurLine < LineCount; CurLine++)
            {
                p = Lines[CurLine].Start;
                if (p[0] == TJS_W('[') && (!TJS_strcmp(p, TJS_W("[endscript]")) ||
                                           !TJS_strcmp(p, TJS_W("[endscript]\\"))) ||
                    p[0] == TJS_W('@') && (!TJS_strcmp(p, TJS_W("@endscript"))))
                {
                    break;
                }

                if (RecordingMacro)
                { // in [macro]-[endmacro] tag
                    // generate escaped(with '`') string onto "script"
                    // for passing to [eval]
                    while (*p)
                    {
                        if (*p == TJS_W('`') || *p == TJS_W('\''))
                            script += TJS_W('`');
                        script += *p++;
                    }
                    script += TJS_W("`\r`\n");
                }
                else if (NeedToDoThisTag())
                {
                    script += p;
                    script += TJS_W("\r\n");
                }
            }

            if (CurLine == LineCount)
                TVPThrowExceptionMessage(TVPExtKAGInlineScriptNotEnd);

            if (RecordingMacro)
            {
                // if this is in macro, transrate it to [eval exp=...]
                LineBuffer = TJS_W("[eval exp='") + script + TJS_W("']");
                LineBufferUsing = true;
                CurLineStr = LineBuffer.c_str();
                return true;
            }
            else if (NeedToDoThisTag())
            {
                // fire onScript callback event
                if (Owner)
                {
                    tTJSVariant param[3] = {script, StorageShortName, script_start};
                    tTJSVariant* pparam[3] = {param, param + 1, param + 2};
                    static ttstr onScript_name(TJSMapGlobalStringMap(TJS_W("onScript")));
                    Owner->FuncCall(0, onScript_name.c_str(), onScript_name.GetHint(), NULL, 3,
                                    pparam, Owner);
                }
            }

            continue;
        }

        break;
    }

    if (CurLine >= LineCount)
        return false;

    CurLineStr = Lines[CurLine].Start;
    LineBufferUsing = false;

    if (DebugLevel >= tkdlVerbose)
    {
        TVPAddLog(StorageShortName + TJS_W(" : ") + CurLineStr);
    }

    return true;
}
//---------------------------------------------------------------------------
ttstr tTJSNI_ExtKAGParser::GetTagName(const tjs_char ldelim)
{
    SkipWhiteSpace();
    if (IsEndOfLine() || GetCurChar() == ldelim)
        TVPThrowExceptionMessage(TVPExtKAGSyntaxError, TJS_W("タグ名が取得できません"),
                                 TVPCurPosErrorString);

    const tjs_char* tagnamestart = CurLineStr + CurPos;
    while (!IsEndOfLine() && !IsWhiteSpace(GetCurChar()) && GetCurChar() != ldelim)
        CurPos++;

    if (tagnamestart == CurLineStr + CurPos)
        TVPThrowExceptionMessage(TVPExtKAGSyntaxError, TJS_W("タグ名が指定されていません"),
                                 TVPCurPosErrorString);

    const tjs_char* tagnameend = CurLineStr + CurPos;

    ttstr tagname(tagnamestart, tagnameend - tagnamestart);
    tagname.ToLowerCase();

    // now attribname is the attribution name, as "attr1" in [tagname attr1=value1]
    return tagname;
}
//---------------------------------------------------------------------------
ttstr tTJSNI_ExtKAGParser::GetAttributeName(const tjs_char ldelim)
{
    SkipWhiteSpace();
    if (IsEndOfLine() || GetCurChar() == ldelim)
        TVPThrowExceptionMessage(TVPExtKAGSyntaxError, TJS_W("タグの属性名が取得できません"),
                                 TVPCurPosErrorString);

    const tjs_char* attribnamestart = CurLineStr + CurPos;
    while (!IsEndOfLine() && !IsWhiteSpace(GetCurChar()) && GetCurChar() != TJS_W('=') &&
           GetCurChar() != ldelim)
        CurPos++;

    const tjs_char* attribnameend = CurLineStr + CurPos;

    ttstr attribname(attribnamestart, attribnameend - attribnamestart);
    attribname.ToLowerCase();

    // now attribname is the attribution name, as "attr1" in [tagname attr1=value1]
    return attribname;
}
//---------------------------------------------------------------------------
ttstr tTJSNI_ExtKAGParser::GetAttributeValue(const tjs_char ldelim)
{
    // now The CurPos is on the next to '=' of "[tagname attrib=value]"
    // pick up the value string from the tag string onto "ttstr value"
    SkipWhiteSpace();
    if (IsEndOfLine() || GetCurChar() == ldelim)
        TVPThrowExceptionMessage(TVPExtKAGSyntaxError, TJS_W("タグの属性値が取得できません"),
                                 TVPCurPosErrorString);

    // attribute value which will be returned
    ttstr value(TJS_W(""));

    tjs_char ch;

    if ((ch = GetCurChar()) == TJS_W('&'))
    {
        value += ch;
        CurPos++; // for expression &"abc def"
        ch = GetCurChar();
    }

    tjs_char vdelim = 0; // value delimiter

    if (ch == TJS_W('\"') || ch == TJS_W('\''))
    {
        vdelim = GetCurChar();
        CurPos++;
    }

    while ((ch = GetCurChar()) && // <- !IsEndOfLine()
           (vdelim ? ch != vdelim : (ch != ldelim && !IsWhiteSpace(ch))))
    {
        if (ch == TJS_W('`'))
        {
            // escaped with '`'
            CurPos++;
            if (IsEndOfLine())
                TVPThrowExceptionMessage(TVPExtKAGSyntaxError,
                                         TJS_W("'`'(バッククォート)直後が行末です"),
                                         TVPCurPosErrorString);
            ch = GetCurChar();
        }
        value += ch;
        CurPos++;
    }
    if ((ldelim != 0 || (vdelim && GetCurChar() != vdelim)) && IsEndOfLine())
        TVPThrowExceptionMessage(TVPExtKAGSyntaxError,
                                 TJS_W("属性値が無いか、\' や \" の対応が間違っています"),
                                 TVPCurPosErrorString);

    if (vdelim)
        CurPos++;

    value.FixLen();

    // return attribute value string and now CurPos is in the next to "attrib=value"
    return value;
}

//---------------------------------------------------------------------------
inline ttstr& tTJSNI_ExtKAGParser::InsertStringOntoLineBuffer(tjs_int startpos,
                                                              const ttstr& insertstr,
                                                              tjs_int replacesiz)
{
    ttstr firststr = ttstr(CurLineStr, startpos);
    ttstr laststr = ttstr(CurLineStr + startpos + replacesiz);
    LineBuffer = firststr + insertstr + laststr;
    LineBufferUsing = true;
    CurLineStr = LineBuffer.c_str();
    return LineBuffer;
}

#if 0  /* unused */
// escape valuestring with '`'
inline ttstr tTJSNI_ExtKAGParser::EscapeValueString(const ttstr &valuestr)
{
	const tjs_char *p = valuestr.c_str();

	if (TJS_strchr(p, TJS_W(' '))  == NULL &&
		TJS_strchr(p, TJS_W('\t')) == NULL &&
		TJS_strchr(p, TJS_W('`'))  == NULL)
		return valuestr;

	ttstr ret;
	tjs_char ch;
	while ((ch = *p++) != 0)
	{
		if (ch == TJS_W('`')  || ch == TJS_W(' ') || ch == TJS_W('\t'))
			ret += TJS_W('`');
		ret += ch;
	}
	return ret;
}

// unescape valuestring with '`'
inline ttstr tTJSNI_ExtKAGParser::UnEscapeValueString(const ttstr &valuestr)
{
	const tjs_char *p = valuestr.c_str();

	if (TJS_strchr(p, TJS_W('`')) == NULL)
		return valuestr;

	tjs_char ch;
	ttstr ret;
	while ((ch = *p++) != 0)
	{
		if (ch == TJS_W('`'))
			ch = *p++;
		ret += ch;
	}
	return ret;
}
#endif /* unused */

//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::PushMacroArgs(tTJSDic& args)
{
    if (MacroArgs.size() > MacroArgStackDepth)
        MacroArgs[MacroArgStackDepth]->Assign(args);
    else
    {
        if (MacroArgStackDepth > MacroArgs.size())
            TVPThrowInternalError;
        tTJSDic* newdic = new tTJSDic(args);
        MacroArgs.push_back(newdic);
    }
    MacroArgStackDepth++;
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::PopMacroArgs()
{
    if (MacroArgStackDepth == 0)
        TVPThrowExceptionMessage(TVPExtKAGSyntaxError,
                                 TJS_W("マクロスタックが空のため、PopMacroArg() が実行できません"),
                                 TVPCurPosErrorString);
    MacroArgStackDepth--;
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::ClearMacroArgs()
{
    for (std::vector<tTJSDic*>::iterator i = MacroArgs.begin(); i != MacroArgs.end(); i++)
    {
        delete *i;
    }
    MacroArgs.clear();
    MacroArgStackDepth = 0;
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::PopMacroArgsTo(tjs_uint base)
{
    MacroArgStackDepth = base;
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::FindNearestLabel(tjs_int start, tjs_int& labelline, ttstr& labelname)
{
    // find nearest label which be pleced before "start".
    // "labelline" is to be the label's line number (0-based), and
    // "labelname" is to be its label name.
    // "labelline" will be -1 and "labelname" be empty if the label is not found.
    Scenario->EnsureLabelCache();

    bool out_of_iscript = true;
    for (start--; start >= 0; start--)
    {
        const tjs_char* p = Lines[start].Start;
        // check whether the label is in [iscript] range
        if (p[0] == TJS_W('[') &&
                (!TJS_strcmp(p, TJS_W("[endscript]")) || !TJS_strcmp(p, TJS_W("[endscript]\\"))) ||
            p[0] == TJS_W('@') && (!TJS_strcmp(p, TJS_W("@endscript"))))
        {
            out_of_iscript = false;
            continue;
        }
        if (p[0] == TJS_W('[') &&
                (!TJS_strcmp(p, TJS_W("[iscript]")) || !TJS_strcmp(p, TJS_W("[iscript]\\"))) ||
            p[0] == TJS_W('@') && (!TJS_strcmp(p, TJS_W("@iscript"))))
        {
            out_of_iscript = true;
            continue;
        }

        if (out_of_iscript && p[0] == TJS_W('*'))
        {
            // label found
            labelname = Scenario->GetLabelAliasFromLine(start);
            break;
        }
    }
    labelline = start;
    if (labelline == -1)
        labelname.Clear();
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::PushCallStack(const tTJSVariant& copyvar, tTJSDic& DicObj)
{
    // push current position information
    if (DebugLevel > tkdlVerbose)
    {
        TVPAddLog(TJS_W("Invoking: PushCallStack()"));
        TVPAddLog(StorageShortName + TJS_W(" : call stack depth before calling : ") +
                  ttstr((int)CallStack.size()));
    }

    tjs_int labelline;
    ttstr labelname;
    FindNearestLabel(CurLine, labelline, labelname);
    if (labelline < 0)
        labelline = 0;

    const tjs_char* curline_content;
    if (Lines && CurLine < LineCount)
        curline_content = Lines[CurLine].Start;
    else
        curline_content = TJS_W("");

    // whilestack/localvariables must be pushed *before* saving
    // callstack.
    PushWhileStack();
    PushLocalVariables(copyvar, DicObj);

    CallStack.push_back(tCallStackData(
        StorageName, labelname, CurLine - labelline, curline_content, LineBuffer, CurPos,
        LineBufferUsing, MacroArgStackBase, MacroArgStackDepth, ExcludeLevelStack, ExcludeLevel,
        IfLevelExecutedStack, IfLevel, WhileStack.size(), LocalVariables.GetSize()));
    MacroArgStackBase = MacroArgStackDepth;
}
//---------------------------------------------------------------------------
// This is called by PopCallStack() only,
// return true if it could [return] to the call source
// return false if it could NOT [return] to there.
bool tTJSNI_ExtKAGParser::CanReturn(const tCallStackData& data)
{
    if (CurLine == LineCount || (CurLine < LineCount && data.OrgLineStr == Lines[CurLine].Start))
        // this is normal case, could [return]
        return true;

    // following is abnormal case, there are changes on source script
    // after [call] and before [return].

    if (!FuzzyReturn)
        return false;

    // if FuzzyReturn is enabled,
    // search whether the call source exists between +-10 lines.
    // At first, check the 10 lines *after* CurLine, as a senario could be
    // increased in most cases.
    for (tjs_int i = 1; i <= 10; i++)
    {
        // Note: CurLine+i == LineCount can not be checked
        if (CurLine + i < LineCount && data.OrgLineStr == Lines[CurLine + i].Start)
        {
            CurLine += i;
            return true; // found the call source at CurLine+i
        }
    }
    // Then next, check the 10 lines *before* CurLine.
    for (tjs_int i = 1; i <= 10; i++)
    {
        if (CurLine - i >= 0 && data.OrgLineStr == Lines[CurLine - i].Start)
        {
            CurLine -= i;
            return true; // found the call source at CurLine-i
        }
    }

    // could not find call source
    return false;
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::PopCallStack(const ttstr& storage, const ttstr& label)
{
    if (DebugLevel > tkdlVerbose)
        TVPAddLog(TJS_W("Invoking: PopCallStack(storage=") + storage + TJS_W(", label=") + label +
                  TJS_W(")"));

    // check call stack
    if (CallStack.empty())
        TVPThrowExceptionMessage(TVPExtKAGCallStackUnderflow);

    // get current call stack data
    tCallStackData& data = CallStack.back();

    // pop macro argument information
    MacroArgStackBase = data.MacroArgStackDepth; // later reset to MacroArgStackBase
    PopMacroArgsTo(data.MacroArgStackDepth);

    // get back local variables to previous one,
    // as the "call subroutine" will be exited.
    PopLocalVariablesBeforeTheLatestCallStack();
    // take back WhileStack before the [call]
    ClearWhileStackToTheLatestCallStack();
    // Note: whilestack/localvariables are necessary
    // to be popped one more later to the correct place

    // goto label or previous position
    if (!storage.IsEmpty() || !label.IsEmpty())
    {
        // return to specified position
        GoToStorageAndLabel(storage, label);
    }
    else
    {
        // return to previous calling position
        LoadScenario(data.Storage);
        if (!data.Label.IsEmpty())
            GoToLabel(data.Label);
        CurLine += data.Offset;

        if (!CanReturn(data))
        {
            // if we can NOT [return] as the data.Storage was changed
            if (ReturnErrorStorage.IsEmpty() || ReturnErrorStorage == TJS_W(""))
                TVPThrowExceptionMessage(TVPExtKAGReturnLostSync);

            // run ReturnErrorStorage immediately
            LoadScenario(ReturnErrorStorage);
            return;
        }

        if (data.LineBufferUsing)
        {
            LineBuffer = data.LineBuffer;
            CurLineStr = LineBuffer.c_str();
            LineBufferUsing = true;
        }
        else
        {
            if (CurLine < LineCount)
            {
                CurLineStr = Lines[CurLine].Start;
                LineBufferUsing = false;
            }
        }
        CurPos = data.Pos;

        ExcludeLevelStack = data.ExcludeLevelStack;
        ExcludeLevel = data.ExcludeLevel;
        IfLevelExecutedStack = data.IfLevelExecutedStack;
        IfLevel = data.IfLevel;

        if (DebugLevel >= tkdlSimple)
        {
            ttstr label;
            if (data.Label.IsEmpty())
                label = TJS_W("(start)");
            else
                label = data.Label;
            TVPAddLog(StorageShortName + TJS_W(" : returned to : ") + label +
                      TJS_W(" line offset ") + ttstr(data.Offset));
        }
    }

    // reset MacroArgStackBase
    MacroArgStackBase = data.MacroArgStackBase;

    // pop call stack
    CallStack.pop_back();

    // These "pop" belows must be here because LoadScenario()
    // above invokes BreakConditionAndMacro() which breaks
    // while state.
    // pop while stack after pop_call_stack
    PopWhileStack(false /*=loop_again*/);
    // pop local variables after pop_call_stack
    PopLocalVariables();

    if (!storage.IsEmpty() || !label.IsEmpty())
    {
        // if return position is specified, we need to
        // clear whilestack/localvariablesstack here.
        BreakConditionAndMacro();
        // this considers the case below:
        // [while exp=1]
        //     [call target=*label1]
        // [endwhile]
        // *label2
        // [s]
        //
        // *label1
        // [return target=*label2]
    }

    // call function back
    if (Owner)
    {
        static ttstr onAfterReturn_name(TJS_W("onAfterReturn"));
        Owner->FuncCall(0, onAfterReturn_name.c_str(), onAfterReturn_name.GetHint(), NULL, 0, NULL,
                        Owner);
    }

    if (DebugLevel > tkdlVerbose)
    {
        TVPAddLog(StorageShortName + TJS_W(" : call stack depth after returning : ") +
                  ttstr((int)CallStack.size()));
    }
}
//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::PushWhileStack()
{
    // just only for debugging
    if (DebugLevel > tkdlVerbose)
    {
        TVPAddLog(TJS_W("Invoking: PushWhileStack()"));
        TVPAddLog(StorageShortName + TJS_W(" : while stack depth before PushWhileStack() : " +
                                           ttstr((int)WhileStack.size())));
    }

    tjs_int labelline;
    ttstr labelname;
    FindNearestLabel(CurLine, labelline, labelname);
    if (labelline < 0)
        labelline = 0;

    const tjs_char* curline_content;
    if (Lines && CurLine < LineCount)
        curline_content = Lines[CurLine].Start;
    else
        curline_content = TJS_W("");

    WhileStack.push_back(tWhileStackData(StorageName, labelname, CurLine - labelline,
                                         curline_content, LineBuffer, CurPos, LineBufferUsing,
                                         ExcludeLevel, IfLevel, WhileLevelExp, WhileLevelEach));

    if (DebugLevel > tkdlVerbose)
        TVPAddLog(StorageShortName + TJS_W(" : while stack depth after PushWhileStack() : ") +
                  ttstr((int)WhileStack.size()));
}

// pop while stack information
void tTJSNI_ExtKAGParser::PopWhileStack(const bool& loop_again)
{
    if (DebugLevel > tkdlVerbose)
    {
        TVPAddLog(TJS_W("Invoking: PopWhileStack(") + ttstr(loop_again) + TJS_W(")"));
        TVPAddLog(StorageShortName + TJS_W(" : while stack depth before PopWhileStack() : " +
                                           ttstr((int)WhileStack.size())));
    }

    if (WhileStack.empty() ||
        (!CallStack.empty() && CallStack.back().WhileStackDepth >= WhileStack.size()))
        TVPThrowExceptionMessage(TVPExtKAGWhileStackUnderflow);

    // pop macro argument information
    const tWhileStackData& data = WhileStack.back();

    if (loop_again && Scenario != NULL)
    // Note: Scenario should be checked as it could be NULL when this is called from ClearBuffer().
    {
        // return to previous [while] position
        if (data.Label.IsEmpty())
        {
            // Do not call Rewind() to avoid infinite loop of
            // BreakConditionAndMacro() -> PopWhileStack()
            CurLine = 0;
        }
        else
        {
            tExtTVPScenarioCacheItem::tLabelCacheData* newline;
            if (!(newline = Scenario->GetLabelCache().Find(data.Label)))
                TVPThrowExceptionMessage(TVPExtKAGLabelNotFound, StorageName, data.Label);
            CurLine = newline->Line;
        }
        CurLine += data.Offset;

        if (CurLine > LineCount)
            TVPThrowExceptionMessage(TVPExtKAGWhileStackUnderflow);
        /* CurLine == LineCount is OK (at end of file) */

        if (CurLine < LineCount)
            if (data.OrgLineStr != Lines[CurLine].Start) // check original line information
                TVPThrowExceptionMessage(TVPExtKAGWhileLostSync);
        if (data.LineBufferUsing)
        {
            LineBuffer = data.LineBuffer;
            CurLineStr = LineBuffer.c_str();
            LineBufferUsing = true;
        }
        else
        {
            if (CurLine < LineCount)
            {
                CurLineStr = Lines[CurLine].Start;
                LineBufferUsing = false;
            }
        }
        CurPos = data.Pos;
    }

    IfLevel = data.IfLevel;
    ExcludeLevel = data.ExcludeLevel;

    WhileLevelExp = data.WhileLevelExp;
    WhileLevelEach = data.WhileLevelEach;

    // pop while stack
    WhileStack.pop_back();

    if (DebugLevel > tkdlVerbose)
        TVPAddLog(StorageShortName + TJS_W(" : while stack depth after PopWhileStack() : ") +
                  ttstr((int)WhileStack.size()));
}

void tTJSNI_ExtKAGParser::WhileStackControlForEndwhile(const bool& loop_again)
{
    if (DebugLevel > tkdlVerbose)
        TVPAddLog(TJS_W("Invoking: WhileStackControlForEndwhile(") + ttstr(loop_again) +
                  TJS_W(")"));

    if (loop_again)
    { // continue the loop
        // back to just after [while], with saving current
        // WhileLevelExp and WhileLevelEach
        ttstr exp = WhileLevelExp, each = WhileLevelEach;
        PopWhileStack(true /*=loop_again*/);
        PushWhileStack();
        WhileLevelExp = exp, WhileLevelEach = each;
        IfLevel++;
    }
    else
    { // exit the loop
        // Note: this must be at the end of [endmacro] to exit while
        // loop and goto next line. otherwise, consider calling this
        // func with loop_again=true and set ExcludeIfLevel=IfLevel
        // to skip next loop
        PopWhileStack(false /*=loop_exit*/);
    }
}

void tTJSNI_ExtKAGParser::ClearWhileStack()
{
    if (DebugLevel > tkdlVerbose)
        TVPAddLog(TJS_W("Invoking: ClearWhileStack()"));

    // WhileLevel* are cleared
    WhileStack.clear();
    WhileLevelExp = TJS_W("");
    WhileLevelEach = TJS_W("");
}

void tTJSNI_ExtKAGParser::ClearWhileStackToTheLatestCallStack()
{
    if (DebugLevel > tkdlVerbose)
    {
        TVPAddLog(TJS_W("Invoking: ClearWhileStackToTheLatestCallStack()"));
        TVPAddLog(StorageShortName +
                  TJS_W(" : while stack depth before ClearWhileStackToTheLatestCallStack() : " +
                        ttstr((int)WhileStack.size())));
    }

    if (CallStack.empty())
    {
        ClearWhileStack();
        if (DebugLevel > tkdlVerbose)
            TVPAddLog(StorageShortName +
                      TJS_W(" : while stack depth after ClearWhileStackToTheLatestCallStack() : " +
                            ttstr((int)WhileStack.size())));
        return;
    }

    tjs_uint depth = CallStack.back().WhileStackDepth;

    // This check must be omitted, in case this is called
    // from LoadScenario(), after PopWhileStack() in PopCallStack().
    if (depth > (tjs_int)WhileStack.size())
    {
        TVPThrowExceptionMessage(TJS_W("WhileStack: saveddepth = %1, size = %2"),
                                 ttstr((tjs_int)depth), WhileStack.size());
        // TVPThrowExceptionMessage(TVPInternalError, __FILE__, __LINE__);
    }

    // WhileStack.resize() is not used since it needs constructor "WhileStack(void)",
    for (tjs_uint i = WhileStack.size(); i > depth; i--)
        PopWhileStack(false);

    if (DebugLevel > tkdlVerbose)
        TVPAddLog(StorageShortName +
                  TJS_W(" : while stack depth after ClearWhileStackToTheLatestCallStack() : " +
                        ttstr((int)WhileStack.size())));
}

void tTJSNI_ExtKAGParser::PushLocalVariables(const tTJSVariant& copyvar, tTJSDic& dicobj)
{
    if (DebugLevel > tkdlVerbose)
    {
        TVPAddLog(TJS_W("Invoking: PushLocalVariables()"));
        TVPAddLog(TJS_W("LocalVariables stack depth beforer PushLocalVariables() = ") +
                  ttstr(LocalVariables.GetSize()));
    }

    // This is invoked by [call] and [localvar]tag.
    // This creates new local variables in this scope by copying current
    // one or creating new one, switched by "copyvar" parameter in the tags.

    if (copyvar.Type() == tvtVoid)
    {
        tTJSDic tmp;
        LocalVariables.Push(tmp); // push new one
    }
    else
    {
        tTJSVariant v;
        SWITCH_TVPEXECUTEEXPRESSION(ttstr(copyvar), Owner, &v);
        if (v.operator bool())
        {
            tTJSVariant tmpV = LocalVariables.GetLast();
            tTJSDic tmp(tmpV);
            LocalVariables.Push(tmp); // push copyed one
        }
        else
        {
            tTJSDic tmp;
            LocalVariables.Push(tmp); // push new one
        }
    }

    CopyDicToCurrentLocalVariables(dicobj);

    if (DebugLevel > tkdlVerbose)
        TVPAddLog(TJS_W("LocalVariables stack depth after PushLocalVariables() = ") +
                  ttstr(LocalVariables.GetSize()));
}

void tTJSNI_ExtKAGParser::PopLocalVariables()
{
    if (DebugLevel > tkdlVerbose)
    {
        TVPAddLog(TJS_W("Invoking: PopLocalVariables()"));
        TVPAddLog(TJS_W("LocalVariables stack depth before PopLocalVariables() = ") +
                  ttstr(LocalVariables.GetSize()));
    }

    if (LocalVariables.GetSize() == 0 ||
        (!CallStack.empty() && CallStack.back().LocalVariablesCount >= LocalVariables.GetSize()))
        TVPThrowExceptionMessage(TVPExtLocalVaiablesUnderFlow);

    LocalVariables.Pop();

    if (DebugLevel > tkdlVerbose)
        TVPAddLog(TJS_W("LocalVariables stack depth after PopLocalVariables() = ") +
                  ttstr(LocalVariables.GetSize()));
}

void tTJSNI_ExtKAGParser::PopLocalVariablesBeforeTheLatestCallStack()
{
    if (DebugLevel > tkdlVerbose)
    {
        TVPAddLog(TJS_W("Invoking: PopLocalVariablesBeforeTheLatestCallStack()"));
        TVPAddLog(TJS_W("LocalVariables stack depth before "
                        "PopLocalVariablesBeforeTheLatestCallStack() = ") +
                  ttstr(LocalVariables.GetSize()));
    }

    // if CallStack.empty(), it will go back to 1, as the
    // top level of LocalVariables ary is 1.
    tjs_int count = 1;

    if (!CallStack.empty())
        count = CallStack.back().LocalVariablesCount;

    // This check must be omitted, in case this is called
    // from LoadScenario(), after PopWhileStack() in PopCallStack().
    if (count > LocalVariables.GetSize())
    {
        TVPThrowExceptionMessage(TJS_W("LocalVariables: savedsize = %1, size = %2"),
                                 ttstr((tjs_int)count), LocalVariables.GetSize());
        // TVPThrowExceptionMessage(TVPInternalError, __FILE__, __LINE__);
    }

    for (tjs_int i = LocalVariables.GetSize(); i > count; i--)
        PopLocalVariables();

    if (DebugLevel > tkdlVerbose)
        TVPAddLog(
            TJS_W(
                "LocalVariables stack depth after PopLocalVariablesBeforeTheLatestCallStack() = ") +
            ttstr(LocalVariables.GetSize()));
}

void tTJSNI_ExtKAGParser::CopyDicToCurrentLocalVariables(tTJSDic& dic)
{
    // Note:this includes tagname, sometimes also storage/target.
    // What to do here is "LocalVariables.GetLast().AddDic(dicobj);",
    // isn't this a bad coding to convert instance from tTJSVariant to tTJSDic?
    tTJSVariant tmp = LocalVariables.GetLast();
    ((tTJSDic*)&tmp)->AddDic(dic);
}

//---------------------------------------------------------------------------
// followings are for multiline extention

// add Lines[CurLine] to CurLineStr, and set LineBufferUsint=true;
// CurPos is now on '\' in CurLineStr
void tTJSNI_ExtKAGParser::AddMultiLine()
{
    ttstr newbuf;
    tjs_int curlen =
        CurPos; // CurPos is now on '\\'. So, to exclude '\\', this is not "CurPos-1" but "CurPos"
    tjs_int newlen = curlen + Lines[CurLine].Length;
    tjs_char* d = newbuf.AllocBuffer(newlen + 1);
    TJS_strncpy(d, CurLineStr, curlen); // ignore the last '\\'
    TJS_strcpy(d + curlen, Lines[CurLine].Start);
    newbuf.FixLen();
    LineBuffer = newbuf;
    CurLineStr = LineBuffer.c_str();
    LineBufferUsing = true;
}

//---------------------------------------------------------------------------
void tTJSNI_ExtKAGParser::ClearCallStack()
{
    CallStack.clear();
    PopMacroArgsTo(MacroArgStackBase = 0); // macro arguments are also cleared

    // Clearing whilestack/localvariables in this function
    // would be a bad conding but this is necessary for
    // script compatibility.
    // Because most TJS scripts call clearCallStack() as
    // meaning of clearing all data on the parser before
    // running new KAG script.
    ClearWhileStack();
    ClearLocalVariables();
}
//---------------------------------------------------------------------------
iTJSDispatch2* tTJSNI_ExtKAGParser::_GetNextTag()
{
    // get next tag and return information dictionary object.
    // return NULL if the tag not found.
    // normal characters are interpreted as a "ch" tag.
    // CR code is interpreted as a "r" tag.
    // tag paremeters are stored into return value(DicObj).
    // tag name is also stored into return value(DicObj), as a "tagname" tag.

    // pretty a nasty code.

    static ttstr __tag_name(TJSMapGlobalStringMap(TJS_W("tagname")));
    static ttstr __eol_name(TJSMapGlobalStringMap(TJS_W("eol")));
    static ttstr __storage_name(TJSMapGlobalStringMap(TJS_W("storage")));
    static ttstr __target_name(TJSMapGlobalStringMap(TJS_W("target")));
    static ttstr __exp_name(TJSMapGlobalStringMap(TJS_W("exp")));
    static ttstr __name(TJSMapGlobalStringMap(TJS_W("name")));

    while (true)
    {
    parse_start:
        // start tag analyzing
        // "continue" (== parse_start) in this while lopp means read next tag/char
        // from the tag begining.
        // "continue" is O.K. but "goto parse_start" can be used in this function
        // for more readable code, since this function is really long.

        // clear dictionary object at first
        DicObj.Empty();

        if (Interrupted)
        {
            // interrupt current parsing
            // return as "interrupted" tag
            static tTJSVariant r_val(TJS_W("interrupt"));
            DicObj.SetProp(__tag_name, r_val);
            Interrupted = false;
            return DicObj.GetDic(); // We need AddRef() here
        }

        if (!Lines || CurLine >= LineCount)
            return NULL; // all of scenario was decoded

        if (!LineBufferUsing && CurPos == 0) // If this is pure new line
        {
            if (!SkipCommentOrLabel()) // this also processes [iscript]-[endscript], and set
                                       // CurLineStr
                return NULL;
        }

        SkipTab(); // skipping '\t', which is meeningless in KAG script

        if (IsEndOfLine()) // if now on the end of the line
        {
            if (IgnoreCR)
            {
                GoToNextLine();
                continue;
            }

            // Followings are for the case IgnoreCR=false

            if (CurPos >= 3 && TJS_strcmp(&(CurLineStr[CurPos - 3]), TJS_W("[p]")) == 0)
            {
                // if !IgnoreCR and the line ends with "[p]"
                // then do nothing and go to next line
                GoToNextLine();
                continue;
            }

            // this must be here
            GoToNextLine();

            if (EnableNP && CurLine < LineCount && IsEndOfLine())
            {
                // if EnableNP and appears "\n\n", then insert "[np]".
                // No need to set LineBuffer with Lines[CurLine], since
                // now Lines[CurLine] == "" and LineBufferUsing = false,
                LineBufferUsing = true;
                LineBuffer = TJS_W("[np]\\");
                CurLineStr = LineBuffer.c_str();

                // if this is in macro, this will be added to RecordingMacroStr
                // in the next loop, necessary as "[np]" is a macro.
                continue;
            }

            if (RecordingMacro)
            {
                // insert [r eol=true]
                RecordingMacroStr += TJS_W("[r eol=true]");
                continue;
            }

            if (NeedToDoThisTag())
            {
                static tTJSVariant r_val(TJS_W("r"));
                static tTJSVariant true_val(TJS_W("true"));
                DicObj.SetProp(__tag_name, r_val);
                DicObj.SetProp(__eol_name, true_val);
                return DicObj.GetDic(); // We need AddRef() here
            }
            continue;
        }

        // if (!IgnoreCR) and here is at the end of line with '\'
        if (!IgnoreCR && GetCurChar() == TJS_W('\\') && IsEndOfLine(+1))
        {
            // go to next line without adding "[r eol=true]"
            GoToNextLine();
            continue;
        }

        tjs_int tagstartpos = CurPos;
        tjs_char ldelim; // last delimiter
                         // is 0 if the tag begins with '@', is ']' if it begins with '['

        if (!LineBufferUsing && CurPos == 0 && GetCurChar() == TJS_W('@'))
        // need to check LineBufferUsing/CurPos for the case "str@not_tag"
        // and [emb exp="'@abc'"] on the top of the line.
        {
            // line command mode (@tag mode)
            ldelim = 0; // tag last delimiter is a null terminater
        }
        else if (GetCurChar() == TJS_W('[') && GetCurChar(+1) != TJS_W('['))
        {
            // brace command mode ([tag] mode)
            ldelim = TJS_W(']'); // tag last delimiter is a ']'
        }
        else
        {
            // normal character
            const tjs_char* chstart = CurLineStr + CurPos;
            int chLen = utf16_char_len(CurLineStr + CurPos);
            CurPos += chLen;

            if (chLen == 1 && chstart[0] == TJS_W('['))
                CurPos++; // If '[' then the next char must be skipped as it is '['

            if (RecordingMacro)
            { // if record tag
                RecordingMacroStr += ttstr(chstart, chLen);
                if (chLen == 1 && chstart[0] == TJS_W('['))
                    RecordingMacroStr += ttstr(chstart, chLen);
                continue;
            }

            if (NeedToDoThisTag())
            {
                static tTJSVariant tag_val(TJS_W("ch"));
                DicObj.SetProp(__tag_name, tag_val);
                tTJSVariant ch_val(ttstr(chstart, chLen));
                static ttstr text_name(TJSMapGlobalStringMap(TJS_W("text")));
                DicObj.SetProp(text_name, ch_val);

                return DicObj.GetDic(); // We need AddRef() here
            }

            continue; // same as "goto parse_start"
        } // end of processing normal character

        CurPos++;

        // ********************************************************************
        // a tag analysis below:
        // CurPos is now on "[(here:CurPos)tagname attr=val]"

        bool condition = true; // = result of "cond=###" in the tag
        ttstr tagname;         // will be "tagname" in [tagname arg1=val1]

        // get tagname
        SkipWhiteSpace();
        if (GetCurChar() != TJS_W('&'))
            tagname = GetTagName(ldelim); // for normal tagname
        else
            tagname = GetAttributeValue(ldelim); // for [&embval]

        // register tagname into DicObj as "tagname" => tagname
        tTJSVariant tmptag(tagname);
        DicObj.SetProp(__tag_name, tmptag);

        // check special control tags
        enum tSpecialTags
        {
            tag_other,
            tag_if,
            tag_else,
            tag_elsif,
            tag_ignore,
            tag_endif,
            tag_endignore,
            tag_emb,
            tag_macro,
            tag_endmacro,
            tag_macropop,
            tag_erasemacro,
            tag_jump,
            tag_call,
            tag_return,
            tag_while,
            tag_endwhile,
            tag_break,
            tag_continue,
            tag_spemb,
            tag_localvar,
            tag_pushlocalvar,
            tag_poplocalvar,
            tag_pmacro
        } tagkind;
        static bool tag_checker_init = false;
        static tTJSHashTable<ttstr, tjs_int> special_tags_hash;
        if (!tag_checker_init)
        {
            tag_checker_init = true;
            special_tags_hash.Add(ttstr(TJS_W("if")), (tjs_int)tag_if);
            special_tags_hash.Add(ttstr(TJS_W("ignore")), (tjs_int)tag_ignore);
            special_tags_hash.Add(ttstr(TJS_W("endif")), (tjs_int)tag_endif);
            special_tags_hash.Add(ttstr(TJS_W("endignore")), (tjs_int)tag_endignore);
            special_tags_hash.Add(ttstr(TJS_W("else")), (tjs_int)tag_else);
            special_tags_hash.Add(ttstr(TJS_W("elsif")), (tjs_int)tag_elsif);
            special_tags_hash.Add(ttstr(TJS_W("emb")), (tjs_int)tag_emb);
            special_tags_hash.Add(ttstr(TJS_W("macro")), (tjs_int)tag_macro);
            special_tags_hash.Add(ttstr(TJS_W("endmacro")), (tjs_int)tag_endmacro);
            special_tags_hash.Add(ttstr(TJS_W("macropop")), (tjs_int)tag_macropop);
            special_tags_hash.Add(ttstr(TJS_W("erasemacro")), (tjs_int)tag_erasemacro);
            special_tags_hash.Add(ttstr(TJS_W("jump")), (tjs_int)tag_jump);
            special_tags_hash.Add(ttstr(TJS_W("call")), (tjs_int)tag_call);
            special_tags_hash.Add(ttstr(TJS_W("return")), (tjs_int)tag_return);
            special_tags_hash.Add(ttstr(TJS_W("while")), (tjs_int)tag_while);
            special_tags_hash.Add(ttstr(TJS_W("endwhile")), (tjs_int)tag_endwhile);
            special_tags_hash.Add(ttstr(TJS_W("break")), (tjs_int)tag_break);
            special_tags_hash.Add(ttstr(TJS_W("continue")), (tjs_int)tag_continue);
            special_tags_hash.Add(ttstr(TJS_W("localvar")), (tjs_int)tag_localvar);
            special_tags_hash.Add(ttstr(TJS_W("pushlocalvar")), (tjs_int)tag_pushlocalvar);
            special_tags_hash.Add(ttstr(TJS_W("poplocalvar")), (tjs_int)tag_poplocalvar);
            special_tags_hash.Add(ttstr(TJS_W("pmacro")), (tjs_int)tag_pmacro);
        }

        // set tagkind
        tagkind = tag_other;
        if (ProcessSpecialTags) // if "do not ignore special tags"
        {
            if (tagname.StartsWith(TJS_W('&')))
                tagkind = tag_spemb; // in case [&special_emb]
            else
            {
                // in case normal tag [tagname attr=val]
                tjs_int* tag = special_tags_hash.Find(tagname);
                if (tag)
                    tagkind = (tSpecialTags)*tag;
            }
        }

        // This is used for bringing %1, %2, ... in Macro, which
        // indicate 1st attribname, 2nd attribname...
        std::vector<ttstr> MacroArgArray;
        // Note: This does not write %0 as tagname

        // ********************************************************************
        // tag attributes analysis below:
        // now CurPos is next to tagname, [tagname(here:CurPos) attr1=val1]
        // This "while" repeats to read each attribution in the tag

        while (true)
        {
            SkipWhiteSpace();

            if (GetCurChar() == ldelim)
            {
                // exit this loop, now on the end of the tag
                if (ldelim == TJS_W(']'))
                    CurPos++;
                break;
            }
            // TVPAddLog(TJS_W("CurLineStr = ") + ttstr(CurLineStr) + TJS_W(", CurPos = ") +
            // ttstr(CurPos)); For Multiline tag which is separated by "\\\n", can NOT include
            // comment lines. Generate CurLineStr as one line which is combined from multiline. ex:
            //	   [tag arg1=abc \
			//        arg2=def]
            // is combined to:
            //     [tag arg1=abc   arg2=def]
            // Note: Any characters after '\' to the end of the line is ignored!
            if (GetCurChar() == TJS_W('\\')) // check CurLineStr
            {
                if (!MultiLineTagEnabled)
                    TVPThrowExceptionMessage(TVPExtKAGMultiLineDisabled);
                // Do NOT invoke GoToNextLine() to avoid clearing CurLinStr/CurPos
                if (++CurLine >= LineCount)
                    TVPThrowExceptionMessage(TVPExtKAGSyntaxError,
                                             TJS_W("複数行タグ(行末 \\)がスクリプト末尾にあります"),
                                             TVPCurPosErrorString);
                // combine multilines to CurLineStr with removing the
                // last '\' of CurLineStr.
                AddMultiLine();
                continue; // try to read next attribute
            }

            if (IsEndOfLine())
                TVPThrowExceptionMessage(TVPExtKAGSyntaxError, TJS_W("タグ途中で行末に達しました"),
                                         TVPCurPosErrorString);

            ttstr attribname(GetAttributeName(ldelim));
            // now attribname is the attribution name, as "attr1" in [tagname attr1=value1]
            // and CurLineStr[CurPos] is on the next to the "attr1"

            // searching '='
            SkipWhiteSpace();

            ttstr value(TJS_W("true")); // def=true in case attr value is omitted
            if (GetCurChar() == TJS_W('='))
            {
                CurPos++; // attr value is specified
                value = GetAttributeValue(ldelim);
            }
            else
            {
                // Check Parameter Macros
                // We check this here to reduce parser work load,
                // as pmacro does not need anything after "=".
                tTJSVariant v = ParamMacros[attribname];
                // v should not be tTJSDic here, as it could be void and
                // tTJSDic can not be void.
                if (v.Type() != tvtVoid)
                {
                    tTJSDic d(v);
                    DicObj.AddDic(d);
                    // now DicObj includes ParamMacro[attribname]

                    if (NumericMacroArgumentsEnabled)
                    {
                        // Copy keys onto MacroArgArray
                        // FIXME: this does not show us the order of the keys
                        tTJSAry keys(d.GetKeys());
                        for (tjs_int i = 0; i < keys.GetSize(); i++)
                            MacroArgArray.push_back(keys[i]);
                    }

                    continue; // try to read next attribute
                }
                // If parameter macros can not be found, go through down
            }

            // now value is attribute value string, occasionally including
            // '&', '%' or '$' on its top.
            // CurPos is on: ex.(at 1st loop) [tagname attr1=value1(here:CurPos) attr2=value2]

            // if the attribname=value does not need to store onto DicObj,
            // just skip the string and continue. "elsif" is the only tag
            // which could change ExcludeLevel and has attributions
            if (RecordingMacro || (!NeedToDoThisTag() && tagkind != tag_elsif))
                continue; // try to read next attribute

            // special attibute processing, "*", "&", macroarg "%", localval "$", "|" and cond=.
            // process expression entity or macro argument

            // check if attribname == '*'
            if (attribname == TJS_W('*'))
            {
                // macro entity all
                // assign macro arguments to current arguments
                DicObj.AddDic(GetMacroTopNoAddRef());
                // overwrite tag_name as marg includes caller's "tagname"
                tTJSVariant tag_val(tagname);
                DicObj.SetProp(__tag_name, tag_val);

                continue; // try to read next attribute
            }

            tTJSVariant ValueVariant = value;

            if (value.StartsWith(TJS_W('&')))
            {
                // if this is entity like: "&int(mp.arg)+1"
                SWITCH_TVPEXECUTEEXPRESSION(value.c_str() + 1, Owner, &ValueVariant);
                if (ValueVariant.Type() != tvtVoid)
                    ValueVariant.ToString();
            }
            else if (value.StartsWith(TJS_W('%')))
            {
                // if this is macro argument like: "%arg" or "%arg|defval"
                tTJSDic& args = GetMacroTopNoAddRef();

                value = value.c_str() + 1;
                tjs_char* vp = TJS_strchr(const_cast<tjs_char*>(value.c_str()), TJS_W('|'));

                if (vp)
                { // if this includes "|"
                    ttstr name(value.c_str(), vp - value.c_str());
                    ValueVariant = args[name];
                    if (ValueVariant.Type() == tvtVoid)
                        ValueVariant = ttstr(vp + 1);
                }
                else
                { // if this does not include "|"
                    ValueVariant = args[value];
                }
            }
            else if (value.StartsWith(TJS_W('$')))
            {
                // if this is local value argument like: "$arg" or "$arg|defval"
                tTJSVariant argV = LocalVariables.GetLast();
                tTJSDic args(argV);

                value = value.c_str() + 1;
                tjs_char* vp = TJS_strchr(const_cast<tjs_char*>(value.c_str()), TJS_W('|'));

                if (vp)
                { // if this includes "|"
                    ttstr name(value.c_str(), vp - value.c_str());
                    ValueVariant = args[name];
                    if (ValueVariant.Type() == tvtVoid)
                        ValueVariant = ttstr(vp + 1);
                }
                else
                { // if this does not include "|"
                    ValueVariant = args[value];
                }

                // Cast to String, as this could be Integer or others.
                if (ValueVariant.Type() != tvtVoid)
                    ValueVariant.ToString();
            }

            if (attribname == TJS_W("cond"))
            {
                // condition
                tTJSVariant val;
                SWITCH_TVPEXECUTEEXPRESSION(ttstr(ValueVariant), Owner, &val);
                condition = val.operator bool();
                continue; // try to read next attribute
            }

            // store value into the dictionary object, "attribname => ValueVariant"
            DicObj.SetProp(attribname, ValueVariant);

            if (NumericMacroArgumentsEnabled && tagkind == tag_other)
            {
                // In case the tag is NOT internal one,
                // set "#(number) => attribname" onto MacroArgArry
                // for argument %1, %2... in macro.
                // we can not check "ismacro" here, which is good
                // enough to pass %#, because it has not been set
                // and will be done on the end of the tag processing.
                MacroArgArray.push_back(attribname);
            }
            // try to read next attribute

        } // while loop reading tag attribution

        // ********************************************************************
        // now tag has ended:
        // now CurPos is on the end of the tag, "[tagname arg=val](here:CurPos)"
        // DO NOT goto next line, because we need to get the character number
        // in case RecordingMacro==true by using variable "tagstartpos"

        bool ismacro = false; // Actually, these variables should NOT be defined
        ttstr macrocontent;   // here, see commented variable definitions below.

        // record macro
        // this must be in the case RecordingMacro==true except "[endmacro]",
        // as endmacro changes RecordingMacro into false.
        // Note "[endmacro cond=0]" is also considered.
        if (RecordingMacro && (tagkind != tag_endmacro || !condition))
        { // no need to check "NeedToDoThisTag()(ExcludeLevel == -1)", as RecordingMacro guarantees
          // it
            // add the macro string onto RecordingMacroStr with '[' and ']'
            if (ldelim != 0)
            {
                // normal tag (=[tag])
                RecordingMacroStr += ttstr(CurLineStr + tagstartpos, CurPos - tagstartpos);
            }
            else
            {
                // line command (=@tag)
                RecordingMacroStr += TJS_W("[") +
                                     ttstr(CurLineStr + tagstartpos + 1, CurPos - tagstartpos - 1) +
                                     TJS_W("]");
            }
            goto tag_last; // last check and go to next tag
        }

        // ********************************************************************
        // At first, check if/ignore/elsif/else/while/endwhile here
        // without checking NeedToDoThisTag(), as they could change
        // exec status like ExcludeLevel/IfLevel, IfLevelExcludeStack and so on.

        // if/ignore
        if (tagkind == tag_if || tagkind == tag_ignore)
        {
            IfLevel++;
            IfLevelExecutedStack.push_back(false);
            ExcludeLevelStack.push_back(ExcludeLevel);

            if (NeedToDoThisTag())
            {
                tTJSVariant val = DicObj[__exp_name];

                if (ttstr(val) == TJS_W(""))
                    TVPThrowExceptionMessage(TVPExtKAGSyntaxError,
                                             TJS_W("if/ignore タグに exp= が指定されていません"),
                                             TVPCurPosErrorString);
                SWITCH_TVPEXECUTEEXPRESSION(val, Owner, &val);

                bool cond = val.operator bool();
                if (tagkind == tag_ignore)
                    cond = !cond;

                IfLevelExecutedStack.back() = cond;
                if (!cond)
                {
                    ExcludeLevel = IfLevel;
                }
            }
            goto tag_last; // last check and go to next tag
        }

        // elsif
        else if (tagkind == tag_elsif)
        {
            if (IfLevelExecutedStack.empty() || ExcludeLevelStack.empty() || IfLevel <= 0)
            {
                // no preceded if/ignore tag.
                TVPThrowExceptionMessage(TVPExtKAGIfStackUnderflow);
            }

            if (IfLevelExecutedStack.back())
            {
                ExcludeLevel = IfLevel;
            }
            else if (IfLevel == ExcludeLevel)
            {
                tTJSVariant val = DicObj[__exp_name];
                ttstr exp = val;

                // const std::string s = exp.AsStdString();
                if (exp == TJS_W(""))
                    TVPThrowExceptionMessage(TVPExtKAGSyntaxError,
                                             TJS_W("elsif タグの exp= が指定されていません"),
                                             TVPCurPosErrorString);
                SWITCH_TVPEXECUTEEXPRESSION(exp, Owner, &val);

                bool cond = val.operator bool();
                if (cond)
                {
                    IfLevelExecutedStack.back() = true;
                    ExcludeLevel = -1;
                }
            }
            goto tag_last; // last check and go to next tag
        }

        // else
        else if (tagkind == tag_else)
        {
            if (IfLevelExecutedStack.empty() || ExcludeLevelStack.empty() || IfLevel <= 0)
            {
                // too much [else]
                TVPThrowExceptionMessage(TVPExtKAGIfStackUnderflow);
            }

            if (IfLevelExecutedStack.back())
            {
                ExcludeLevel = IfLevel;
            }
            else if (IfLevel == ExcludeLevel)
            {
                IfLevelExecutedStack.back() = true;
                ExcludeLevel = -1;
            }
            goto tag_last; // last check and go to next tag
        }

        // endif/endignore
        else if (tagkind == tag_endif || tagkind == tag_endignore)
        {
            if (IfLevelExecutedStack.empty() || ExcludeLevelStack.empty() || IfLevel <= 0)
            {
                // too much [endif] or [endignore]
                TVPThrowExceptionMessage(TVPExtKAGIfStackUnderflow);
            }

            ExcludeLevel = ExcludeLevelStack.back();
            ExcludeLevelStack.pop_back();
            IfLevelExecutedStack.pop_back();
            IfLevel--;

            goto tag_last; // last check and go to next tag
        }

        // [while init= exp= each=]
        else if (tagkind == tag_while)
        {
            // Note: To save correct place in PushWhileStack(),
            // check ldelim and goto next line if necessary here.
            if (ldelim == 0)
                GoToNextLine();

            // save the state just after [while], to back from [endwhile]
            PushWhileStack();
            IfLevel++;

            if (NeedToDoThisTag())
            {
                // in case the [while] loop is necessary,
                tTJSVariant val;

                // run "init=" at first
                ttstr init(DicObj[TJS_W("init")]);
                if (init != TJS_W(""))
                    SWITCH_TVPEXECUTEEXPRESSION(init, Owner, &val);

                // read the "exp=" string for [endwhile] and [break]
                WhileLevelExp = DicObj[__exp_name.c_str()];
                if (WhileLevelExp == TJS_W(""))
                    TVPThrowExceptionMessage(TVPExtKAGSyntaxError,
                                             TJS_W("while タグに exp= が指定されていません"),
                                             TVPCurPosErrorString);

                // read the "each=" string for [endwhile]
                WhileLevelEach = DicObj[TJS_W("each")];

                // check "exp=" whether we need to do this while scope
                SWITCH_TVPEXECUTEEXPRESSION(WhileLevelExp, Owner, &val);
                bool cond = val.operator bool();
                if (!cond)
                    ExcludeLevel = IfLevel;
            }

            goto parse_start;
            // Do not goto tag_last, as it has been on the next line if
            // necessary, especially for this tag
        }

        // [endwhile]
        // Note: This evaluates "exp=" and "each=" which were specified in [while]
        else if (tagkind == tag_endwhile)
        {
            // if [endwhile] illegally appear, this notifies it
            if (WhileStack.empty() || IfLevel - 1 != WhileStack.back().IfLevel)
            {
                if (!WhileStack.empty())
                    TVPAddLog(TJS_W("IfLevel = ") + ttstr(IfLevel) + TJS_W(", saved = ") +
                              ttstr(WhileStack.back().IfLevel));
                TVPThrowExceptionMessage(TVPExtKAGWhileStackUnderflow);
            }

            bool cond = false;
            // Checking NeedToDoThisTag() is necessary in case the cond was false
            // at the 1st loop of [while] and another case handling [break].
            if (NeedToDoThisTag())
            { // in case the next [while] loop will be necessary,
                tTJSVariant val;

                // evaluate "each="
                SWITCH_TVPEXECUTEEXPRESSION(WhileLevelEach, Owner, &val);

                // check whether exp= is true (=cond), must be after
                // executing "each=" expression.
                SWITCH_TVPEXECUTEEXPRESSION(WhileLevelExp, Owner, &val);
                cond = val.operator bool();
            }

            // cond is the flag which decides if the loop will be continued
            // cond = true:  in case the loop should continue, same as [continue]
            // cond = false: in case the loop should end
            WhileStackControlForEndwhile(cond);

            // if loop again, goto parse_start to go back to correct place
            // as WhileStack saves it as next line of the @while tab
            if (cond)
                goto parse_start;

            goto tag_last; // last check and go to next tag
        }

        // ********************************************************************
        // other tags (except if/elsif/else/endif/while/endwhile) should
        // only be handled when "condition && NeedToDoThisTag()"

        if (!condition || !NeedToDoThisTag())
        {
            // this tag can be skipped.
            goto tag_last; // last check and go to next tag
        }

        // ********************************************************************
        // After here, this tag is obviously necessary to be processed

        // bool ismacro = false; // These variables should be defined here,
        // ttstr macrocontent;   // but defined above to avoid silly compiler
        // (vc2010 only?) errors...
        {
            // is the tagname macro ?
            tTJSVariant macroval = Macros[tagname];
            if (macroval.Type() != tvtVoid)
            {
                ismacro = true;
                macrocontent = ttstr(macroval);
            }
        }

        // tag-specific processing
        if (tagkind == tag_other && !ismacro)
        {
            if (ldelim == 0)
                GoToNextLine();     // do GoToNextLine before this return
            return DicObj.GetDic(); // do AddRef()
        }

        else if (tagkind == tag_emb || tagkind == tag_spemb)
        {
            // embed string, in the case this is [emb exp=...] or [&...],
            // insert emb string to current position

            // execute expression
            // generate LineBuffer with replacing [emb] in CurLineStr into its actual string
            // ex:
            // "[r][p][emb exp=tjsval][r][p]" -> "[r][p]tjsvalstring[r][p]"
            // "[r][p][&tjsval][r][p]"        -> "[r][p]tjsvalstring[r][p]"
            tTJSVariant val;
            ttstr exp;
            if (tagkind == tag_emb)
            { // for default [emb], get string of "exp="
                val = DicObj[__exp_name];
                exp = val;
            }
            else // if (tag_epemb)
            {    // for entity tag [&embval], get string of embval;
                exp = tagname.c_str() + 1;
            }
            if (exp == TJS_W(""))
                TVPThrowExceptionMessage(TVPExtKAGSyntaxError,
                                         TJS_W("emb/& タグに exp= が指定されていません"),
                                         TVPCurPosErrorString);
            SWITCH_TVPEXECUTEEXPRESSION(exp, Owner, &val);
            exp = val;

            // replace '[' into '[[' to avoid "recursive tag handling".
            // ex. if [emb exp="'[p]'"]  is replaced to "[p]", then
            // unlikely it could be handled as a [p] tag. To avoid this,
            // [emb exp="'[p]'"] should be replaced to "[[p]"

            const tjs_char* p = exp.c_str();
            tjs_int r_count = 0;
            while (*p)
            {
                if (*p == TJS_W('['))
                    r_count++;
                p++;
                r_count++;
            }

            tjs_int curposlen = TJS_strlen(CurLineStr + CurPos);
            tjs_int finallen = r_count + tagstartpos + curposlen;

            if (ldelim == 0 && !IgnoreCR)
                finallen++;

            ttstr newbuf;

            tjs_char* d = newbuf.AllocBuffer(finallen + 1);

            TJS_strncpy(d, CurLineStr, tagstartpos);
            d += tagstartpos;

            // escape '['
            p = exp.c_str();
            while (*p)
            {
                if (*p == TJS_W('['))
                {
                    *d = TJS_W('[');
                    d++;
                    *d = TJS_W('[');
                    d++;
                }
                else
                {
                    *d = *p;
                    d++;
                }
                p++;
            }

            TJS_strcpy(d, CurLineStr + CurPos);

            if (ldelim == 0 && !IgnoreCR)
            {
                d += curposlen;
                *d = TJS_W('\\');
                d++;
                *d = 0;
            }

            newbuf.FixLen();
            LineBuffer = newbuf;

            CurLineStr = LineBuffer.c_str();
            CurPos = tagstartpos;
            LineBufferUsing = true;

            goto parse_start; // start analyzing *THIS REPLACED* tag
        }

        else if (ismacro /* && tag_others is not needed to be checked here */)
        { // in case this is invoking defined macro
            // generate LineBuffer with replacing macro in CurLineStr
            // into its actual string
            // ex:
            // "[r][p][macronam arg=val][r][p]" -> "[r][p][if exp="val"]a[endif][macropop][p][r]"
            tjs_int maclen = macrocontent.GetLen();              // = strlen(macro string)
            tjs_int curposlen = TJS_strlen(CurLineStr + CurPos); // = strlen(after the tag)
            tjs_int finallen = tagstartpos + maclen + curposlen; // = strlen(replaced CurLineStr)

            if (ldelim == 0 && !IgnoreCR)
                finallen++;

            ttstr newbuf;
            tjs_char* d = newbuf.AllocBuffer(finallen + 1);

            TJS_strncpy(d, CurLineStr, tagstartpos);
            d += tagstartpos;
            TJS_strcpy(d, macrocontent.c_str());
            d += maclen;
            TJS_strcpy(d, CurLineStr + CurPos);

            if (ldelim == 0 && !IgnoreCR)
            {
                // if it is "@macro" and IgnoreCR=false, need to
                // replace it into "[actual_macro_str]\" to avoid
                // unexpected "return(=[r])" on EOL.
                d += curposlen;
                *d = TJS_W('\\');
                d++;
                *d = 0;
            }

            newbuf.FixLen();
            LineBuffer = newbuf;

            if (NumericMacroArgumentsEnabled)
            {
                // add macro argument %1, %2, %3... (=mp['1'], mp['2'], ...)
                for (tjs_int i = 0; i < (int)MacroArgArray.size(); i++)
                {
                    ttstr index(i + 1);
                    tTJSVariant argname(MacroArgArray.at(i));
                    DicObj.SetProp(index.AsVariantStringNoAddRef(), argname);
                }
            }

            // push macro arguments, will be "mp"
            PushMacroArgs(DicObj);

            CurLineStr = LineBuffer.c_str();
            CurPos = tagstartpos;
            LineBufferUsing = true;

            goto parse_start; // start analyzing *THIS REPLACED* tag
        }

        else if (tagkind == tag_jump)
        {
            // jump tag
            ttstr attrib_storage = DicObj[__storage_name];
            ttstr attrib_target = DicObj[__target_name];

            // fire onJump event
            bool process = true;
            if (Owner)
            {
                tTJSVariant param(DicObj);
                tTJSVariant* pparam = &param;
                static ttstr event_name(TJSMapGlobalStringMap(TJS_W("onJump")));
                tTJSVariant res;
                tjs_error er = Owner->FuncCall(0, event_name.c_str(), event_name.GetHint(), &res, 1,
                                               &pparam, Owner);
                if (er == TJS_S_OK)
                    process = res.operator bool();
            }

            if (process)
            {
                // Adding "CopyDicToCurrentLocalVariables(DicObj);" here
                // WAS a good idea for the feature to set and
                // pass local variables on @jump tag, however now
                // it does not work since @jump pop_back the local
                // variables stack.

                GoToStorageAndLabel(attrib_storage, attrib_target);
                goto parse_start; // go to next tag
            }
            goto tag_last; // last check and go to next tag
        }

        else if (tagkind == tag_call)
        {
            // call tag
            ttstr attrib_storage = DicObj[__storage_name];
            ttstr attrib_target = DicObj[__target_name];

            // fire onCall event
            bool process = true;
            if (Owner)
            {
                tTJSVariant param(DicObj);
                tTJSVariant* pparam = &param;
                static ttstr event_name(TJSMapGlobalStringMap(TJS_W("onCall")));
                tTJSVariant res;
                tjs_error er = Owner->FuncCall(0, event_name.c_str(), event_name.GetHint(), &res, 1,
                                               &pparam, Owner);
                if (er == TJS_S_OK)
                    process = res.operator bool();
            }

            if (process)
            {
                // Note: To save correct place in PushCallStack(),
                // check ldelim and goto next line if necessary here.
                if (ldelim == 0)
                    GoToNextLine();

                // push call stack and goto subroutine
                PushCallStack(DicObj[TJS_W("copyvar")], DicObj);
                GoToStorageAndLabel(attrib_storage, attrib_target);
                goto parse_start; // go to next tag
            }
            goto tag_last; // last check and go to next tag
        }

        else if (tagkind == tag_return)
        {
            // return tag
            ttstr attrib_storage = DicObj[__storage_name];
            ttstr attrib_target = DicObj[__target_name];

            // fire onReturn event
            bool process = true;
            if (Owner)
            {
                tTJSVariant param(DicObj);
                tTJSVariant* pparam = &param;
                static ttstr event_name(TJSMapGlobalStringMap(TJS_W("onReturn")));
                tTJSVariant res;
                tjs_error er = Owner->FuncCall(0, event_name.c_str(), event_name.GetHint(), &res, 1,
                                               &pparam, Owner);
                if (er == TJS_S_OK)
                    process = res.operator bool();
            }

            if (process)
            {
                PopCallStack(attrib_storage, attrib_target);
                goto parse_start; // go to next tag
            }

            goto tag_last; // last check and go to next tag
        }

        // [continue]
        // similar to continuous case of [endwhile]
        else if (tagkind == tag_continue)
        {
            // if [continue] illegally appear, this notifies it
            if (WhileStack.empty() || IfLevel - 1 != WhileStack.back().IfLevel)
            {
                if (!WhileStack.empty())
                    TVPAddLog(TJS_W("IfLevel = ") + ttstr(IfLevel) + TJS_W(", saved = ") +
                              ttstr(WhileStack.back().IfLevel));
                TVPThrowExceptionMessage(TVPExtKAGWhileStackUnderflow);
            }

            // back to just after [while]
            WhileStackControlForEndwhile(true /*=loop_again*/);

            tTJSVariant val;

            // evaluate "each="
            SWITCH_TVPEXECUTEEXPRESSION(WhileLevelEach, Owner, &val);

            // check whether exp= is true (=cond), must be after
            // executing "each=" expression.
            SWITCH_TVPEXECUTEEXPRESSION(WhileLevelExp, Owner, &val);
            bool cond = val.operator bool();

            if (!cond)
                ExcludeLevel = IfLevel; // next loop will be skipped

            goto parse_start; // go to next tag
        }

        // [break]
        // realized as [continue] and ExcludeLevel=IfLevel.
        // This wastes a bit time since this brings
        // "one more empty loop" after the [break],
        // although so simple.
        else if (tagkind == tag_break)
        {
            // if [break] illegally appear, this notifies it
            if (WhileStack.empty() || IfLevel - 1 != WhileStack.back().IfLevel)
            {
                if (!WhileStack.empty())
                    TVPAddLog(TJS_W("IfLevel = ") + ttstr(IfLevel) + TJS_W(", saved = ") +
                              ttstr(WhileStack.back().IfLevel));
                TVPThrowExceptionMessage(TVPExtKAGWhileStackUnderflow);
            }

            // back to just after [while] with ExcludeLevel != -1
            WhileStackControlForEndwhile(true /*=loop_again*/);
            ExcludeLevel = IfLevel; // next loop will be skipped

            goto parse_start; // go to next tag
        }

        else if (tagkind == tag_macro)
        {
            // start recording macro
            RecordingMacroName = DicObj[__name];
            RecordingMacroName.ToLowerCase();
            if (RecordingMacroName == TJS_W(""))
                TVPThrowExceptionMessage(TVPExtKAGSyntaxError,
                                         TJS_W("macro タグに name= が指定されていません"),
                                         TVPCurPosErrorString);
            // missing macro name
            RecordingMacro = true; // start recording macro
            RecordingMacroStr.Clear();

            // for initial argument for [macro]
            // description ex: [macro name=abc attr1=val1 attr2=val2...]

            tTJSAry ary(DicObj.GetKeys());
            tjs_int count = ary.GetSize();
            for (tjs_int i = 0; i < count; i++)
            {
                ttstr member_name = ary[i];
                if (member_name == __tag_name || member_name == __name)
                    continue;
                ttstr member_value = DicObj[member_name];

                // add [eval exp="mp.name='initial value'" cond=mp.name===void]
                // onto the begining of the macro definitions.
                // escape all single/double quotes
                member_value.Replace(TJS_W("'"), TJS_W("\\'"));
                member_value.Replace(TJS_W("\""), TJS_W("\\\""));
                if ((member_name[0] & 0xff00) || !isdigit(member_name[0]))
                {
                    // If !isdigit(member_name), then try putting mp.member_name
                    RecordingMacroStr += TJS_W("[eval exp=mp.") + member_name + TJS_W("='") +
                                         member_value + TJS_W("'") + TJS_W(" cond=mp.") +
                                         member_name + TJS_W("===void]");
                }
                else
                {
                    // If !isdigit(member_name), then try putting mp.member_name
                    RecordingMacroStr += TJS_W("[eval exp=\"mp['") + member_name + TJS_W("']='") +
                                         member_value + TJS_W("'\"") + TJS_W(" cond=\"mp['") +
                                         member_name + TJS_W("']===void\"]");
                }
            }

            goto tag_last; // last check and go to next tag
        }
        else if (tagkind == tag_endmacro)
        {
            // end recording macro
            if (!RecordingMacro)
                TVPThrowExceptionMessage(TVPExtKAGSyntaxError,
                                         TJS_W("macro タグなしに endmacro タグが使用されました"),
                                         TVPCurPosErrorString);
            RecordingMacro = false;
            if (DebugLevel >= tkdlVerbose)
            {
                TVPAddLog(TJS_W("macro : ") + RecordingMacroName + TJS_W(" : ") +
                          RecordingMacroStr);
            }

            RecordingMacroStr += TJS_W("[macropop]");
            // ensure macro arguments are to be popped

            // register macro
            tTJSVariant tmp(RecordingMacroStr);
            Macros.SetProp(RecordingMacroName, tmp);

            goto tag_last; // last check and go to next tag
        }
        else if (tagkind == tag_macropop)
        {
            // pop macro arguments
            PopMacroArgs();
            goto tag_last; // last check and go to next tag
        }
        else if (tagkind == tag_erasemacro)
        {
            ttstr macroname = DicObj.GetProp(__name);
            if (TJS_FAILED(Macros.DelProp(macroname)))
                TVPThrowExceptionMessage(TVPExtUnknownMacroName, macroname);

            goto tag_last; // last check and go to next tag
        }

        else if (tagkind == tag_pmacro)
        {
            // for [pmacro name=xxx arg1=#1 arg2=#2]
            tTJSDic pmacrodic(DicObj);
            tTJSVariant pmacroname = pmacrodic[__name];
            if (pmacroname.Type() == tvtVoid)
                TVPThrowExceptionMessage(TVPExtKAGSyntaxError,
                                         TJS_W("pmacro タグに name= がありません"),
                                         TVPCurPosErrorString);
            pmacrodic.DelProp(__name);
            pmacrodic.DelProp(__tag_name);
            ParamMacros.SetProp(pmacroname.AsStringNoAddRef(), pmacrodic);

            goto tag_last; // last check and go to next tag
        }

        else if (tagkind == tag_localvar)
        {
            CopyDicToCurrentLocalVariables(DicObj);

            goto tag_last; // last check and go to next tag
        }

        else if (tagkind == tag_pushlocalvar)
        {
            // create new local variables in this scope
            PushLocalVariables(DicObj[TJS_W("copyvar")], DicObj);

            goto tag_last; // last check and go to next tag
        }

        else if (tagkind == tag_poplocalvar)
        {
            PopLocalVariables();

            goto tag_last; // last check and go to next tag
        }

        // no need to check "else" tags, as all of others have been checled above.
        // But just for more strict error check...
        else
        {
            TVPThrowExceptionMessage(TVPExtKAGSyntaxError,
                                     TJS_W("未知のエラーです tagkind=") + ttstr(tagkind),
                                     TVPCurPosErrorString);
        }

    tag_last:
        // if this is @tag, goto next line as CurPos is now at the end of
        //  the line: "@tag(here:CurPos)"
        // IsEndOfLine() can not be used here because we need newline
        // procedure in the next loop if (!ignoreCR && at_the_end_of("[tag]\0"))
        if (ldelim == 0)
            GoToNextLine();
        // now go back to while loop top, same as "goto parse_start;"
    } // while(true) for reading next tag/char

    return NULL;
}
//---------------------------------------------------------------------------
iTJSDispatch2* tTJSNI_ExtKAGParser::GetNextTag()
{
    return _GetNextTag();
}
//---------------------------------------------------------------------------
tTJSDic& tTJSNI_ExtKAGParser::GetMacroTopNoAddRef() const
{
    if (MacroArgStackDepth == 0)
        TVPThrowExceptionMessage(TVPKAGWrongAttrOutOfMacro);
    return *MacroArgs[MacroArgStackDepth - 1];
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTJSNC_KAGParser : KAGParser TJS native class
//---------------------------------------------------------------------------
tjs_uint32 tTJSNC_ExtKAGParser::ClassID = (tjs_uint32)-1;
tTJSNC_ExtKAGParser::tTJSNC_ExtKAGParser()
  : tTJSNativeClass(TJS_W("KAGParser")){

        TJS_BEGIN_NATIVE_MEMBERS(KAGParser) TJS_DECL_EMPTY_FINALIZE_METHOD
            //----------------------------------------------------------------------
            // constructor/methods
            //----------------------------------------------------------------------
            TJS_BEGIN_NATIVE_CONSTRUCTOR_DECL(/*var.name*/ _this,
                                              /*var.type*/ tTJSNI_ExtKAGParser,
                                              /*TJS class name*/ KAGParser){return TJS_S_OK;
}
TJS_END_NATIVE_CONSTRUCTOR_DECL(/*TJS class name*/ KAGParser)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ loadScenario)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    _this->LoadScenario(*param[0]);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ loadScenario)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ goToLabel)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    _this->GoToLabel(*param[0]);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ goToLabel)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ goToLine)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    _this->GoToLine(*param[0]);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ goToLine)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ callLabel)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    _this->CallLabel(*param[0]);
    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ callLabel)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ getNextTag)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
    //	if(numparams < 0) return TJS_E_BADPARAMCOUNT;
    iTJSDispatch2* dsp = _this->GetNextTag();
    if (dsp == NULL)
    {
        if (result)
            result->Clear(); // return void ( not null )
    }
    else
    {
        if (result)
            *result = tTJSVariant(dsp, dsp);
        dsp->Release();
    }

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ getNextTag)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ assign)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);

    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;

    tTJSNI_ExtKAGParser* src = NULL;
    tTJSVariantClosure clo = param[0]->AsObjectClosureNoAddRef();
    if (clo.Object)
    {
        if (TJS_FAILED(clo.Object->NativeInstanceSupport(TJS_NIS_GETINSTANCE, tTJSNC_ExtKAGParser::ClassID,
                                                         (iTJSNativeInstance**)&src)))
            TVPThrowExceptionMessage(TVPExtKAGSpecifyKAGParser);
    }
    else
    {
        TVPThrowExceptionMessage(TVPExtKAGSpecifyKAGParser);
    }

    _this->Assign(*src);

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ assign)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ clear)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);

    _this->Clear();

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ clear)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ store)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);

    iTJSDispatch2* dsp = _this->Store();
    if (result)
        *result = tTJSVariant(dsp, dsp);
    dsp->Release();

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ store)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ restore)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);

    if (numparams < 1)
        return TJS_E_BADPARAMCOUNT;
    iTJSDispatch2* dsp = param[0]->AsObjectNoAddRef();

    _this->Restore(dsp);

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ restore)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ clearCallStack)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);

    _this->ClearCallStack();

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ clearCallStack)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ popMacroArgs) // undoc
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);

    _this->PopMacroArgs();

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ popMacroArgs)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ interrupt)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);

    _this->Interrupt();

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ interrupt)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_METHOD_DECL(/*func. name*/ resetInterrupt)
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);

    _this->ResetInterrupt();

    return TJS_S_OK;
}
TJS_END_NATIVE_METHOD_DECL(/*func. name*/ resetInterrupt)
//----------------------------------------------------------------------

//---------------------------------------------------------------------------

//----------------------------------------------------------------------
// properties
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(curLine){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = _this->GetCurLine();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(curLine)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(curPos){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = _this->GetCurPos();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(curPos)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(curLineStr){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = ttstr(_this->GetCurLineStr());
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(curLineStr)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(processSpecialTags){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = (tjs_int)_this->GetProcessSpecialTags();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
    _this->SetProcessSpecialTags(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(processSpecialTags)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(ignoreCR){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = (tjs_int)_this->GetIgnoreCR();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
    _this->SetIgnoreCR(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(ignoreCR)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(debugLevel){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = (tjs_int)_this->GetDebugLevel();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
    _this->SetDebugLevel((tTVPKAGDebugLevel)(tjs_int)(*param));
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(debugLevel)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(enableNP){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = (tjs_int)_this->GetEnableNP();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
    _this->SetEnableNP(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(enableNP)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(fuzzyReturn){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = (tjs_int)_this->GetFuzzyReturn();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
    _this->SetFuzzyReturn(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(fuzzyReturn)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(returnErrorStorage){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = _this->GetReturnErrorStorage();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
    _this->SetReturnErrorStorage(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(returnErrorStorage)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(macros){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
iTJSDispatch2* macros = _this->GetMacrosNoAddRef();
*result = tTJSVariant(macros, macros);
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(macros)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(macroParams){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
iTJSDispatch2* params = _this->GetMacroTopNoAddRef().GetDicNoAddRef();
*result = tTJSVariant(params, params);
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(macroParams)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(mp){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
iTJSDispatch2* params = _this->GetMacroTopNoAddRef().GetDicNoAddRef();
*result = tTJSVariant(params, params);
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(mp)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(callStackDepth){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = _this->GetCallStackDepth();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(callStackDepth)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(curStorage){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = _this->GetStorageName();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
    _this->LoadScenario(*param);
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(curStorage)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(curLabel){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = _this->GetCurLabel();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(curLabel)
//---------------------------------------------------------------------------
// For Multiline feature
TJS_BEGIN_NATIVE_PROP_DECL(multiLineTagEnabled){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = (tjs_int)_this->GetMultiLineTagEnabled();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
    _this->SetMultiLineTagEnabled(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(multiLineTagEnabled)
//---------------------------------------------------------------------------
// For Numeric Macro Arguments
TJS_BEGIN_NATIVE_PROP_DECL(numericMacroArgumentsEnabled){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = (tjs_int)_this->GetNumericMacroArgumentsEnabled();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_BEGIN_NATIVE_PROP_SETTER
{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
    _this->SetNumericMacroArgumentsEnabled(param->operator bool());
    return TJS_S_OK;
}
TJS_END_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(numericMacroArgumentsEnabled)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(currentLocalVariables){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = _this->GetCurrentLocalVariables();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(currentLocalVariables)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(lf){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = _this->GetCurrentLocalVariables();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(lf)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(prelf){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = _this->GetPreviousLocalVariables();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(prelf)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(pmacros){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = _this->GetParamMacros();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(pmacros)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(ifLevel){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = _this->GetIfLevel();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(ifLevel)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(whileStackDepth){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = _this->GetWhileStackDepth();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(whileStackDepth)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(localVariablesDepth){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = _this->GetLocalVariablesDepth();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(localVariablesDepth)
//----------------------------------------------------------------------
TJS_BEGIN_NATIVE_PROP_DECL(localVariables){TJS_BEGIN_NATIVE_PROP_GETTER{
    TJS_GET_NATIVE_INSTANCE(/*var. name*/ _this, /*var. type*/ tTJSNI_ExtKAGParser);
*result = _this->GetLocalVariables();
return TJS_S_OK;
}
TJS_END_NATIVE_PROP_GETTER

TJS_DENY_NATIVE_PROP_SETTER
}
TJS_END_NATIVE_PROP_DECL(localVariables)
//----------------------------------------------------------------------

TJS_END_NATIVE_MEMBERS
}
//---------------------------------------------------------------------------
tTJSNativeInstance* tTJSNC_ExtKAGParser::CreateNativeInstance()
{
    return new tTJSNI_ExtKAGParser();
}
//---------------------------------------------------------------------------

tTJSNativeClass* TVPCreateNativeClass_ExtKAGParser()
{
    return new tTJSNC_ExtKAGParser();
}
//---------------------------------------------------------------------------
