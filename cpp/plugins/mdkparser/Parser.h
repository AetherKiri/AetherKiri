/**
 * TJS2 の字句抽出器をベースにMDKParser用の字句抽出器を作る。
 */

//---------------------------------------------------------------------------
/*
	TJS2 Script Engine
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Script Block Management
//---------------------------------------------------------------------------
#ifndef AETHERKIRI_MDK_PARSER_H
#define AETHERKIRI_MDK_PARSER_H


#ifdef _WIN32
#include <windows.h>
#endif
#include "tp_stub.h"
#include "Token.h"

#include "LexicalAnalyzer.h"

#include <list>
#include <memory>
#include <map>
#include <stack>

namespace AetherKiri::MDKParser {

/**
 * tagは以下のような辞書形式で格納されている
%[
	tag : "tag name",	// tag名
	command : [
		"name",	// 属性値がない場合は、コマンドとして格納される
	]
	attribute : %[		// 存在しない時はattribute要素自体ない(属性)
		attrname : "value",
		attrname : %[ ref : "name" ],		// 変数参照の時
		attrname : %[  file : "name", prop : "name" ],	// ファイル参照の時
	],
	parameter : %[		// 存在しない時はparameter要素自体ない
		name :  "value",
	],
]

%[
	lines : [
		[ 1 ],		// 記号
		[ "text" ],	// テキストはそのまま格納
		[ %[name:"tag"] ],	// タグ
		[ %[name:"ruby",text:"きりきり"], "吉里吉里", %[name:"endruby"] ],
	]
]
 * label の時は以下のような辞書形式で行に直接格納されている
%[
	tag : "label",
	name : "name",
	description : "desc"
]
 * > の時は以下のような形で行に直接格納されている。
%[
	tag : "next",
	target : "filename",
	cond : "flag == true",
]
 * コメントの時
void

行の意味番号
void : コメント行/タグ名固定関係
0 : 空行
 */

//---------------------------------------------------------------------------
// Parser
//---------------------------------------------------------------------------
class Parser
{
	enum class LogType {
		Warning,
		Error,
	};

	std::map<Token,ttstr>			TagCommandPair;
	std::map<tjs_char,Token>		SignToToken;

public:
	Parser();
	virtual ~Parser();

private:
	std::unique_ptr<tjs_char[]> Script;

	tjs_int CurrentLine = 0;
	bool LineAttribute = false;		// 1行で属性を書くスタイルの状態時true
	bool MultiLineTag = false;
	bool HasSelectLine = false;
	bool TextAttribute = false;		// {}内に記述された属性

	// 現在設定されているタグ名、解除されるまでこの名前がタグ名として強制追加される
	ttstr FixTagName;

	// tagに必要な要素をクラス化して、管理したほうが間違いが減るな……
	std::unique_ptr<class Tag> CurrentTag;

	std::unique_ptr<class ScenarioDictionary> Scenario;
	iTJSDispatch2* ArrayAddFunc = nullptr;

	// ルビ/文字装飾ネスト用スタック
	std::stack<iTJSDispatch2*> RubyDecorationStack;

	std::unique_ptr<LexicalAnalyzer> Lex;

	std::vector<tjs_int> LineVector;
	std::vector<tjs_int> LineLengthVector;

	tTJSString FirstError;
	tjs_int CompileErrorCount;

public:
	const tjs_char * GetLine(tjs_int line, tjs_int *linelength) const;
	tjs_int SrcPosToLine(tjs_int pos) const;
	tjs_int LineToSrcPos(tjs_int line) const;

	const tjs_char *GetScript() const { return Script.get(); }

	LexicalAnalyzer * GetLexicalAnalyzer() { return Lex.get(); }

	void WarningLog( const tjs_char* message );
	void ErrorLog( const tjs_char* message );
	void WarningLog( ttstr message, const ttstr& p1 );
	void ErrorLog( ttstr message, const ttstr& p1 );
	void Log( LogType type, const tjs_char* message );

	void Initialize();
	void AddSignWord( tjs_char sign, const ttstr& word );

private:
	static void ConsoleOutput(const tjs_char *msg, void *data);

	/** ルビ/文字装飾用スタックをクリアする。 */
	void ClearRubyDecorationStack();

	/** 指定された名前で現在の辞書の属性(もしくはパラメータ)に値を設定する。 */
	void PushAttribute( const tTJSVariantString* name, iTJSDispatch2* dic, bool isparameter = false );
	/** 指定された名前で現在の辞書の属性(もしくはパラメータ)に値を設定する。 */
	void PushAttribute( const tTJSVariantString* name, const tTJSVariant& value, bool isparameter = false );

	/** 指定された名前で現在の辞書の属性(もしくはパラメータ)に参照を設定する。 */
	void PushAttributeReference( const tTJSVariantString& name, const tTJSVariant& value, bool isparameter = false );
	/** 指定された名前で現在の辞書の属性(もしくはパラメータ)にファイルプロパティを設定する。 */
	void PushAttributeFileProperty( const tTJSVariantString& name, const tTJSVariant& file, const tTJSVariant& prop, bool isparameter = false );

	void ParseAttributeValueSymbol( const tTJSVariant& symbol, const tTJSVariant& valueSymbol, bool isparameter=false );
	void ParseAttribute( const tTJSVariant& symbol, bool isparameter=false );
	bool ParseSpecialAttribute( Token token, tjs_int value );
	void ParseTag();
	void ParseAttributes();
	void ParseTransition();
	void ParseCharacter();
	void ParseLabel();
	void ParseSelect( tjs_int number );
	void ParseNextScenario();
	bool ParseTag( Token token, tjs_int value );
	void ParseLine( tjs_int line );

	ttstr* GetTagSignWord( Token token );

	iTJSDispatch2* CreateEmptyScenario();
public:
	iTJSDispatch2* ParseText( const tjs_char* text );
};
//---------------------------------------------------------------------------

} // namespace AetherKiri::MDKParser

#endif // AETHERKIRI_MDK_PARSER_H
