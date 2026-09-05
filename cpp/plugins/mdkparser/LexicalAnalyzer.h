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
// TJS2 lexical analyzer
//---------------------------------------------------------------------------
#ifndef AETHERKIRI_MDK_LEXICAL_ANALYZER_H
#define AETHERKIRI_MDK_LEXICAL_ANALYZER_H

#ifdef _WIN32
#include <windows.h>
#endif
#include "tp_stub.h"

#include "Token.h"
#include <vector>
#include <deque>
#include <memory>
#include <cctype>


#define TJS_malloc			malloc
#define TJS_free			free
#define TJS_realloc			realloc

#define TJS_D(x)
#define TJS_F_TRACE(x)

namespace AetherKiri::MDKParser {

inline bool TJS_iswspace( tjs_char ch ) {
	// the standard iswspace misses when non-zero page code
	if( ch & 0xff00 ) return false; else return 0 != ::isspace( ch );
}
inline bool TJS_iswdigit( tjs_char ch ) {
	// the standard iswdigit misses when non-zero page code
	if( ch & 0xff00 ) return false; else return 0 != ::isdigit( ch );
}
inline bool TJS_iswalpha( tjs_char ch ) {
	// the standard iswalpha misses when non-zero page code
	if( ch & 0xff00 ) return true; else return 0 != ::isalpha( ch );
}
//extern void TJS_eTJSCompileError( const ttstr & msg, class tTJSScriptBlock *block, tjs_int srcpos );
//extern void TJS_eTJSCompileError( const tjs_char *msg, class tTJSScriptBlock *block, tjs_int srcpos );

// Defining this enables quick-hack, avoiding the dictionary/array parser
// memory overflow.
// This is done with replacing %[ ... ] to function { return %[ ... ]; }()
// and replacing [ ... ] to function { return [ ... ]; }().
// These replacing is applied for expression which starts with "%[" or "[",
// may cause some sideeffects....

//---------------------------------------------------------------------------
extern tjs_int TJSHexNum(tjs_char ch) throw();
extern tjs_int TJSOctNum(tjs_char ch) throw();
extern tjs_int TJSDecNum(tjs_char ch) throw();
extern tjs_int TJSBinNum(tjs_char ch) throw();

bool TJSParseString(tTJSVariant &val, const tjs_char **ptr);
bool TJSParseNumber(tTJSVariant &val, const tjs_char **ptr);
void TJSReservedWordsHashAddRef();
void TJSReservedWordsHashRelease();
enum tTJSSkipCommentResult
{ scrContinue, scrEnded, scrNotComment };
//---------------------------------------------------------------------------
class Parser;
class LexicalAnalyzer
{
public:
	LexicalAnalyzer(Parser *block);
	~LexicalAnalyzer();

private:
	const tjs_char *Current = nullptr;
	tjs_int PrevPos;
	tjs_int PrevToken;
	bool First;
	tjs_int NestLevel;

	struct tTokenPair
	{
		Token token;
		tjs_int value;

		tTokenPair( Token token, tjs_int value)
		{
			this->token = token;
			this->value = value;
		}
	};

	std::deque<tTokenPair>		RetValDeque;
	std::vector<tjs_char>		TextBody;
	std::unique_ptr<tjs_char[]>	ScriptWork;
	tjs_int						ScriptWorkSize;

	bool RegularExpression;
	bool BareWord;

	enum tEmbeddableExpressionState
	{	evsStart, evsNextIsStringLiteral, evsNextIsExpression };

	struct tEmbeddableExpressionData
	{
		tEmbeddableExpressionState State;
		tjs_int WaitingNestLevel;
		tjs_int WaitingToken;
		tjs_char Delimiter;
		bool NeedPlus;
	};

	std::vector<tEmbeddableExpressionData> EmbeddableExpressionDataStack;


	Parser *Block;

	tjs_char *Script = nullptr;

	std::vector<tTJSVariant *> Values;

	tjs_int PutValue(const tTJSVariant &val);

	void PutChar( tjs_char c );
	ttstr GetText();

public:
	void reset( const tjs_char *str, tjs_int length );

	const tTJSVariant & GetValue(tjs_int idx) const { return *Values[idx]; }
	const tjs_char * GetString(tjs_int idx) const { return Values[idx]->GetString(); }

	void Unlex( Token token, tjs_int value ) {
		RetValDeque.push_back( tTokenPair(token,value) );
	}
	void Unlex() {
		Current = Script + PrevPos;
	}
	/* 残っている文字列を取得する */
	ttstr GetRemainString() const { return ttstr( Current ); }

	/* | までの文字列を読み取る */
	tjs_int ReadToVerline() { return ReadToChar( TJS_W('|')); }
	/* スペースまでの文字列を読み取る */
	tjs_int ReadToSpace() { return ReadToChar( TJS_W(' ')); }
	/* 指定文字までの文字列を読み取る。文字列の末尾まで読んでも見付からない場合は末尾までの文字列インデックスを返す */
	tjs_int ReadToChar( tjs_char end );
	/* 指定文字までの文字列を読み取る。end文字が見付からない場合は-1を返す */
	tjs_int ReadToCharStrict( tjs_char end );

	void SkipSpace();

	void Free(void);

//	void NextBraceIsBlockBrace();

	tjs_int GetCurrentPosition();

	//tjs_int GetNext(tjs_int &value);

	void SetStartOfRegExp(void);
	void SetNextIsBareWord();

	Token GetFirstToken( tjs_int &n );
	Token ReturnText( tjs_int &n );
	Token GetTextToken( tjs_int &n );
	//Token GetRubyDecorationToken( tjs_int &n );
	Token GetInTagToken( tjs_int &n );
};
//---------------------------------------------------------------------------

} // namespace AetherKiri::MDKParser

#endif // AETHERKIRI_MDK_LEXICAL_ANALYZER_H
