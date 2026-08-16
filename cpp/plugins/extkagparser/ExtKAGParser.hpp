//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// KAG Parser Utility Class
//---------------------------------------------------------------------------

#ifndef KAGParserH
#define KAGParserH
//---------------------------------------------------------------------------

#include "ExttjsHashSearch.h"
#include <vector>
#include "TJSAry.h"
#include "TJSDic.h"
using namespace TJS;
/*[*/
//---------------------------------------------------------------------------
// KAG Parser debug level
//---------------------------------------------------------------------------
enum tTVPKAGDebugLevel
{
	tkdlNone, // none is reported
	tkdlSimple, // simple report
	tkdlVerbose // complete report ( verbose )
};
/*]*/


//---------------------------------------------------------------------------
// tTVPCharHolder
//---------------------------------------------------------------------------
class tTVPCharHolder
{
	tjs_char *Buffer;
	size_t BufferSize;
public:
	tTVPCharHolder() : Buffer(NULL), BufferSize(0)
	{
	}
	~tTVPCharHolder()
	{
		Clear();
	}

	tTVPCharHolder(const tTVPCharHolder &ref) : Buffer(NULL), BufferSize(0)
	{
		operator =(ref);
	}

	void Clear()
	{
		if(Buffer) delete [] Buffer, Buffer = NULL;
		BufferSize = 0;
	}

	void operator =(const tTVPCharHolder &ref)
	{
		Clear();
		BufferSize = ref.BufferSize;
		Buffer = new tjs_char[BufferSize];
		memcpy(Buffer, ref.Buffer, BufferSize *sizeof(tjs_char));
	}

	void operator =(const tjs_char *ref)
	{
		Clear();
		if(ref)
		{
			BufferSize = TJS_strlen(ref) + 1;
			Buffer = new tjs_char[BufferSize];
			memcpy(Buffer, ref, BufferSize*sizeof(tjs_char));
		}
	}

	operator const tjs_char *() const
	{
		return Buffer;
	}

	operator tjs_char *()
	{
		return Buffer;
	}
};
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// tTVPScenarioCacheItem : Scenario Cache Item
//---------------------------------------------------------------------------
class tExtTVPScenarioCacheItem
{
public:
	struct tLine
	{
		const tjs_char *Start;
		tjs_int Length;
	};

private:
	tTVPCharHolder Buffer;
	tLine *Lines;
	tjs_int LineCount;

public:
	struct tLabelCacheData
	{
		tjs_int Line;
		tjs_int Count;
		tLabelCacheData(tjs_int line, tjs_int count)
		{
			Line = line;
			Count = count;
		}
	};

public:
        typedef ExttTJSHashTable<ttstr, tLabelCacheData> tLabelCacheHash;
private:
	tLabelCacheHash LabelCache; // Label cache
	std::vector<ttstr> LabelAliases;
	bool LabelCached; // whether the label is cached

	tjs_int RefCount;

public:
	tExtTVPScenarioCacheItem(const ttstr & name, bool istring);
protected:
	~tExtTVPScenarioCacheItem();
public:
	void AddRef();
	void Release();
private:
	void LoadScenario(const ttstr & name, bool isstring);
		// load file or string to buffer
public:
	const ttstr & GetLabelAliasFromLine(tjs_int line) const
		{ return LabelAliases[line]; }
	void EnsureLabelCache();

	tLine * GetLines() const { return Lines; }
	tjs_int GetLineCount() const { return LineCount; }
	const tLabelCacheHash & GetLabelCache() const { return LabelCache; }
};
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// tTJSNI_KAGParser
//---------------------------------------------------------------------------
class tTJSNI_ExtKAGParser : public tTJSNativeInstance
{
	typedef tTJSNativeInstance inherited;

public:
	tTJSNI_ExtKAGParser();
	tjs_error Construct(tjs_int numparams, tTJSVariant **param,
			iTJSDispatch2 *tjs_obj);
	void Invalidate();

private:
	iTJSDispatch2 * Owner; // owner object

	tTJSDic DicObj; // DictionaryObject

	tTJSDic Macros; // Macro Dictionary Object
	tTJSDic ParamMacros; // Parameter Macro Dictionary Object

	std::vector<tTJSDic*> MacroArgs; // Macro arguments
	tjs_uint MacroArgStackDepth;
	tjs_uint MacroArgStackBase;

	// ExtKAGParser: for local variables extention.
	// can be accessed by "lf" from TJS, these variables
	// are nested and be local one in if/while/call scope.
	tTJSAry LocalVariables;

	// ExtKagParser: for [while]/[endwhile] extention
	ttstr WhileLevelExp;
	ttstr WhileLevelEach;

	struct tWhileStackData
	{
		ttstr Storage; // caller storage
		ttstr Label; // caller nearest label
		tjs_int Offset; // line offset from the label
		ttstr OrgLineStr; // original line string
		ttstr LineBuffer; // line string (if alive)
		tjs_int Pos;
		bool LineBufferUsing; // whether LineBuffer is used or not
        tjs_int ExcludeLevel;
        tjs_int IfLevel;
		ttstr WhileLevelExp;
		ttstr WhileLevelEach;

		tWhileStackData(const ttstr &storage, const ttstr &label,
			tjs_int offset, const ttstr &orglinestr, const ttstr &linebuffer,
			tjs_int pos, bool linebufferusing,
			tjs_int excludelevel, tjs_int iflevel,
			const ttstr &whilelevelexp, const ttstr &whileleveleach) :
			Storage(storage), Label(label), Offset(offset), OrgLineStr(orglinestr),
			LineBuffer(linebuffer), Pos(pos), LineBufferUsing(linebufferusing),
			ExcludeLevel(excludelevel), IfLevel(iflevel),
			WhileLevelExp(whilelevelexp), WhileLevelEach(whileleveleach) {;}
	};
	std::vector<tWhileStackData> WhileStack;

	struct tCallStackData
	{
		ttstr Storage; // caller storage
		ttstr Label; // caller nearest label
		tjs_int Offset; // line offset from the label
		ttstr OrgLineStr; // original line string
		ttstr LineBuffer; // line string (if alive)
		tjs_int Pos;
		bool LineBufferUsing; // whether LineBuffer is used or not
		tjs_uint MacroArgStackBase;
		tjs_uint MacroArgStackDepth;
		std::vector<tjs_int> ExcludeLevelStack;
		std::vector<bool> IfLevelExecutedStack;
		tjs_int ExcludeLevel;
		tjs_int IfLevel;

		// ExtKAGParser extends
		tjs_uint WhileStackDepth;
		tjs_int  LocalVariablesCount;

		tCallStackData(const ttstr &storage, const ttstr &label,
			tjs_int offset, const ttstr &orglinestr, const ttstr &linebuffer,
			tjs_int pos, bool linebufferusing, tjs_uint macroargstackbase,
			tjs_uint macroargstackdepth,
			const std::vector<tjs_int> &excludelevelstack, tjs_int excludelevel,
			const std::vector<bool> &iflevelexecutedstack, tjs_int iflevel,
			tjs_uint whilestackdepth,
			tjs_int localvariablescount) :
			Storage(storage), Label(label), Offset(offset), OrgLineStr(orglinestr),
			LineBuffer(linebuffer), Pos(pos), LineBufferUsing(linebufferusing),
			MacroArgStackBase(macroargstackbase),
			MacroArgStackDepth(macroargstackdepth),
			ExcludeLevelStack(excludelevelstack), ExcludeLevel(excludelevel),
			IfLevelExecutedStack(iflevelexecutedstack), IfLevel(iflevel),
			WhileStackDepth(whilestackdepth),
			LocalVariablesCount(localvariablescount) {;}
	};
	std::vector<tCallStackData> CallStack;

	tExtTVPScenarioCacheItem* Scenario;
	tExtTVPScenarioCacheItem::tLine * Lines; // is copied from Scenario
	tjs_int LineCount; // is copied from Scenario

	ttstr StorageName;
	ttstr StorageShortName;

	tjs_int CurLine; // current processing line
	tjs_int CurPos; // current processing position ( column )
	const tjs_char *CurLineStr; // current line string
	ttstr LineBuffer; // line buffer ( if any macro/emb was expanded )
	bool LineBufferUsing;
	ttstr CurLabel; // Current Label
	ttstr CurPage; // Current Page Name

	tTVPKAGDebugLevel DebugLevel; // debugging log level
	bool ProcessSpecialTags; // whether to process special tags
	bool IgnoreCR; // CR is not interpreted as [r] tag when this is true
	bool RecordingMacro; // recording a macro
	ttstr RecordingMacroStr; // recording macro content
	ttstr RecordingMacroName; // recording macro's name

	bool EnableNP; // macro [np] is called when IgnoreCR=true and "\n\n" exists
	bool FuzzyReturn; // whether [return] can return before/after 10 lines even if the source script was changed
	ttstr ReturnErrorStorage; // KAG script which is run when [return] fails

	tjs_int ExcludeLevel;
	tjs_int IfLevel;

	std::vector<tjs_int> ExcludeLevelStack;
	std::vector<bool> IfLevelExecutedStack;

	bool Interrupted;

	// ExtKAGParser extends
	bool MultiLineTagEnabled;
	bool NumericMacroArgumentsEnabled;

public:
        void operator=(const tTJSNI_ExtKAGParser& ref);
	iTJSDispatch2 *Store();
	void Restore(iTJSDispatch2 *dic);

	void Clear(); // clear all states

private:
	void ClearBuffer(); // clear internal buffer

	void Rewind(); // set current position to first
	void inline GoToNextLine() { CurLine++; CurPos = 0; LineBufferUsing = false; }
	void BreakConditionAndMacro(); // break condition state and macro expansion

public:
	void LoadScenario(const ttstr & name);
	const ttstr & GetStorageName() const { return StorageName; }
	void GoToLabel(const ttstr &name); // search label and set current position
	void GoToLine(tjs_int line); // set current position to an absolute zero-based line
	void GoToStorageAndLabel(const ttstr &storage, const ttstr &label);
	void CallLabel(const ttstr &name);
private:
	bool SkipCommentOrLabel(); // skip comment or label and go to next line
	bool inline NeedToDoThisTag() { return (ExcludeLevel == -1); }
	bool inline IsEndOfLine(tjs_int idx=0) { return (GetCurChar(idx) == 0); }
	bool inline IsWhiteSpace(tjs_char ch) { return (ch == TJS_W(' ') || ch == TJS_W('\t')); } // is white space ?
	void inline SkipWhiteSpace() { while(IsWhiteSpace(CurLineStr[CurPos]) && !IsEndOfLine()) CurPos++; } // skip white spece to delim
	void inline SkipWhiteSpaceAndComma() { SkipWhiteSpace(); if (CurLineStr[CurPos] == TJS_W('\'')) CurPos++; SkipWhiteSpace(); };
	void inline SkipTab() { while(CurLineStr[CurPos] == TJS_W('\t')) CurPos++; }
	const tjs_char inline GetCurChar(tjs_int idx=0) { return CurLineStr[CurPos+idx]; }
	ttstr GetTagName(const tjs_char ldelim);
	ttstr GetAttributeName(const tjs_char ldelim);
	ttstr GetAttributeValue(const tjs_char ldelim);
	inline ttstr &InsertStringOntoLineBuffer(tjs_int startpos, const ttstr &insertstr, tjs_int replacesiz=0);
//	inline ttstr EscapeValueString(const ttstr &valuestr);
//	inline ttstr UnEscapeValueString(const ttstr &valuestr);

	void PushMacroArgs(tTJSDic &dic);
public:
	void PopMacroArgs();
private:
	void ClearMacroArgs();
	void PopMacroArgsTo(tjs_uint base);

	void FindNearestLabel(tjs_int start, tjs_int &labelline, ttstr &labelname);

	void PushCallStack(const tTJSVariant &copyvar, tTJSDic &dicobj);
	bool CanReturn(const tCallStackData &data);
	void PopCallStack(const ttstr &storage, const ttstr &label);
	void StoreIntStackToDic(iTJSDispatch2 *dic, std::vector<tjs_int> &stack, const tjs_char *membername);
	void StoreBoolStackToDic(iTJSDispatch2 *dic, std::vector<bool> &stack, const tjs_char *membername);
	void RestoreIntStackFromStr(std::vector<tjs_int> &stack, const ttstr &str);
	void RestoreBoolStackFromStr(std::vector<bool> &stack, const ttstr &str);

	// ExtKAGParser [while] extends
	void PushWhileStack();
	void PopWhileStack(const bool &loop_again);
	void ClearWhileStack();
	void WhileStackControlForEndwhile(const bool &loop_again);
	void ClearWhileStackToTheLatestCallStack();
	// ExtKAGParser [localvar] extends
	void PushLocalVariables(const tTJSVariant &copyvar, tTJSDic &dicobj);
	void PopLocalVariables();
	void PopLocalVariablesBeforeTheLatestCallStack();
	void CopyDicToCurrentLocalVariables(tTJSDic &dic);
	// Note: ClearLocalVariables() pushes one vars at least.
        void ClearLocalVariables() {tTJSVariant tmp = tTJSDic(); LocalVariables.Empty(); LocalVariables.Push(tmp); }

	// For MultiLine extends
	void AddMultiLine();

public:
	void ClearCallStack();

	void Interrupt() { Interrupted = true; };
	void ResetInterrupt() { Interrupted = false; };

private:
	iTJSDispatch2 * _GetNextTag();

public:
	iTJSDispatch2 * GetNextTag();

	const ttstr & GetCurLabel() const { return CurLabel; }
	tjs_int GetCurLine() const { return CurLine; }
	tjs_int GetCurPos() const { return CurPos; }
	const tjs_char* GetCurLineStr() const { return CurLineStr; }

	void SetProcessSpecialTags(bool b) { ProcessSpecialTags = b; }
	bool GetProcessSpecialTags() const { return ProcessSpecialTags; }

	void SetIgnoreCR(bool b) { IgnoreCR = b; }
	bool GetIgnoreCR() const { return IgnoreCR; }

	void SetDebugLevel(tTVPKAGDebugLevel level) { DebugLevel = level; }
	tTVPKAGDebugLevel GetDebugLevel(void) const { return DebugLevel; }

	void SetEnableNP(bool b) { EnableNP = b; }
	bool GetEnableNP() const { return EnableNP; }
	void SetFuzzyReturn(bool b) { FuzzyReturn = b; }
	bool GetFuzzyReturn() const { return FuzzyReturn; }
	void SetReturnErrorStorage(ttstr s) { ReturnErrorStorage = s; }
	const ttstr & GetReturnErrorStorage() const { return ReturnErrorStorage; }

	iTJSDispatch2 *GetMacrosNoAddRef() const { return Macros.GetDicNoAddRef(); }

	tTJSDic &GetMacroTopNoAddRef() const;
		// get current macro argument (parameters)

	tjs_int GetCallStackDepth() const { return CallStack.size(); }
	tjs_int GetIfLevel() const { return IfLevel; }
	tjs_int GetWhileStackDepth() const { return WhileStack.size(); }
	tjs_int GetLocalVariablesDepth() const { return LocalVariables.GetSize(); }
	tTJSAry &GetLocalVariables() { return LocalVariables; }

	void Assign(tTJSNI_ExtKAGParser& ref) { operator=(ref); }

	// ExtKAGParser extends
	void SetMultiLineTagEnabled(bool b) { MultiLineTagEnabled = b; }
	bool GetMultiLineTagEnabled() const { return MultiLineTagEnabled; }
	void SetNumericMacroArgumentsEnabled(bool b) { NumericMacroArgumentsEnabled = b; }
	bool GetNumericMacroArgumentsEnabled() const { return NumericMacroArgumentsEnabled; }
	tTJSVariant GetCurrentLocalVariables() { return LocalVariables.GetLast(); }
	tTJSVariant GetPreviousLocalVariables() { return LocalVariables.GetPrev(); }
	tTJSVariant GetParamMacros() { return ParamMacros; }
};

//---------------------------------------------------------------------------
// tTJSNC_KAGParser
//---------------------------------------------------------------------------
class tTJSNC_ExtKAGParser : public tTJSNativeClass
{
    typedef tTJSNativeClass inherited;

public:
    tTJSNC_ExtKAGParser();

    static tjs_uint32 ClassID;

protected:
    tTJSNativeInstance* CreateNativeInstance();
};
//---------------------------------------------------------------------------
extern tTJSNativeClass* TVPCreateNativeClass_ExtKAGParser();

#endif
