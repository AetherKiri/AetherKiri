//---------------------------------------------------------------------------
/*
	TJS2 Script Engine
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Script Block Management
//---------------------------------------------------------------------------
#include "Parser.h"
#include "Tag.h"
#include "ScenarioDictionary.h"
#include <assert.h>
#include "MDKMessages.h"

namespace AetherKiri::MDKParser {

using tjs_string = std::basic_string<tjs_char>;

#define MDKThrowInternalError \
	TVPThrowExceptionMessage(TVPMdkGetText(NUM_MDK_INTERNAL_ERROR), __FILE__,  __LINE__)

template <typename TContainer>
void split( const tjs_string& val, const tjs_char& delim, TContainer& result ) {
	result.clear();
	const tjs_string::size_type n = 1;
	tjs_string::size_type pos = 0;
	while( pos != tjs_string::npos ) {
		tjs_string::size_type p = val.find( delim, pos );
		if( p == tjs_string::npos ) {
			if( pos >= ( val.size() - 1 ) ) {
				if( val[pos] != delim ) {
					result.push_back( val.substr( pos ) );
				}
			} else {
				result.push_back( val.substr( pos ) );
			}
			break;
		} else {
			if( ( p - pos ) == 1 ) {
				if( val[pos] != delim ) {
					result.push_back( val.substr( pos ) );
				}
			} else {
				result.push_back( val.substr( pos, p - pos ) );
			}
		}
		pos = p + n;
	}
}
inline tjs_string Trim( const tjs_string& val ) {
	static const tjs_char* TRIM_STR = TJS_W( " \01\02\03\04\05\06\a\b\t\n\v\f\r\x0E\x0F\x7F\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B\x1C\x1D\x1E\x1F" );
	tjs_string::size_type pos = val.find_first_not_of( TRIM_STR );
	tjs_string::size_type lastpos = val.find_last_not_of( TRIM_STR );
	if( pos == lastpos ) {
		if( pos == tjs_string::npos ) {
			return val;
		} else {
			return val.substr( pos, 1 );
		}
	} else {
		tjs_string::size_type len = lastpos - pos + 1;
		return val.substr( pos, len );
	}
}

static void TJSReportExceptionSource( const ttstr &msg ) {
	//if( TJSEnableDebugMode )
	{
		TVPAddLog( msg );
	}
}
//---------------------------------------------------------------------------
static void TJS_eTJSCompileError( const ttstr & msg ) {
	TJSReportExceptionSource( msg );
	TVPThrowExceptionMessage( msg.c_str() );
}
//---------------------------------------------------------------------------
static void TJS_eTJSCompileError( const tjs_char *msg ) {
	TJSReportExceptionSource( msg );
	TVPThrowExceptionMessage( msg );
}
//---------------------------------------------------------------------------
// tTJSScriptBlock
//---------------------------------------------------------------------------
void Parser::Initialize() {
	TagCommandPair.insert(std::make_pair(Token::PLUS, ttstr(TJS_W("add"))));
	TagCommandPair.insert(std::make_pair(Token::MINUS, ttstr(TJS_W("del"))));
	TagCommandPair.insert(std::make_pair(Token::ASTERISK, ttstr(TJS_W("all"))));
	//TagCommandPair.insert(std::make_pair(Token::SHARP, ttstr(TJS_W("sync"))));
	TagCommandPair.insert(std::make_pair(Token::EXCRAMATION, ttstr(TJS_W("sync"))));
	TagCommandPair.insert(std::make_pair(Token::AMPERSAND, ttstr(TJS_W("nowait"))));

	//SignToToken.insert(std::make_pair(TJS_W('>'),Token::GT));
	//SignToToken.insert(std::make_pair(TJS_W('<'),Token::LT));
	SignToToken.insert(std::make_pair(TJS_W('='),Token::EQUAL));
	SignToToken.insert(std::make_pair(TJS_W('!'),Token::EXCRAMATION));
	SignToToken.insert(std::make_pair(TJS_W('&'),Token::AMPERSAND));
	//SignToToken.insert(std::make_pair(TJS_W('.'),Token::DOT));
	SignToToken.insert(std::make_pair(TJS_W('+'),Token::PLUS));
	SignToToken.insert(std::make_pair(TJS_W('-'),Token::MINUS));
	SignToToken.insert(std::make_pair(TJS_W('*'),Token::ASTERISK));
	SignToToken.insert(std::make_pair(TJS_W('/'),Token::SLASH));
	//SignToToken.insert(std::make_pair(TJS_W('\\'),Token::BACKSLASH));
	SignToToken.insert(std::make_pair(TJS_W('%'),Token::PERCENT));
	SignToToken.insert(std::make_pair(TJS_W('^'),Token::CHEVRON));
	//SignToToken.insert(std::make_pair(TJS_W('['),Token::LBRACKET));
	//SignToToken.insert(std::make_pair(TJS_W(']'),Token::RBRACKET));
	//SignToToken.insert(std::make_pair(TJS_W('('),Token::LPARENTHESIS));
	//SignToToken.insert(std::make_pair(TJS_W(')'),Token::RPARENTHESIS));
	SignToToken.insert(std::make_pair(TJS_W('~'),Token::TILDE));
	SignToToken.insert(std::make_pair(TJS_W('?'),Token::QUESTION));
	//SignToToken.insert(std::make_pair(TJS_W(':'),Token::COLON));
	SignToToken.insert(std::make_pair(TJS_W(','),Token::COMMA));
	SignToToken.insert(std::make_pair(TJS_W(';'),Token::SEMICOLON));
	//SignToToken.insert(std::make_pair(TJS_W('{'),Token::LBRACE));
	//SignToToken.insert(std::make_pair(TJS_W('}'),Token::RBRACE));
	SignToToken.insert(std::make_pair(TJS_W('#'),Token::SHARP));
	//SignToToken.insert(std::make_pair(TJS_W('$'),Token::DOLLAR));
	SignToToken.insert(std::make_pair(TJS_W('@'),Token::AT));
	SignToToken.insert(std::make_pair(TJS_W('|'),Token::VERTLINE));
}
//---------------------------------------------------------------------------
void Parser::AddSignWord( tjs_char sign, const ttstr& word ) {
	auto tokenpair = SignToToken.find( sign );
	if( tokenpair != SignToToken.end() ) {
		auto result = TagCommandPair.insert(std::make_pair(tokenpair->second , word));
		if( !result.second ) {
			ttstr message = TVPMdkGetText(NUM_MDK_ALREADY_REGISTERED);
			message.Replace(TJS_W("%c"), ttstr(sign));
			TVPAddLog(message);
		}
	} else {
		ttstr message = TVPMdkGetText(NUM_MDK_CANNOT_REGISTER_WORD);
		message.Replace(TJS_W("%c"), ttstr(sign));
		TVPAddLog(message);
	}
}
//---------------------------------------------------------------------------
ttstr* Parser::GetTagSignWord( Token token ) {
	auto ret = TagCommandPair.find( token );
	if( ret != TagCommandPair.end() ) {
		return &ret->second;
	}
	return nullptr;
}
//---------------------------------------------------------------------------
Parser::Parser() {
	Lex.reset( new LexicalAnalyzer(this) );
}
//---------------------------------------------------------------------------
Parser::~Parser() {
	Lex.reset();
}
//---------------------------------------------------------------------------
const tjs_char * Parser::GetLine(tjs_int line, tjs_int *linelength) const
{
	// note that this function DOES matter LineOffset
	if(linelength) *linelength = LineLengthVector[line];
	return Script.get() + LineVector[line];
}
//---------------------------------------------------------------------------
tjs_int Parser::SrcPosToLine(tjs_int pos) const
{
	tjs_uint s = 0;
	tjs_uint e = (tjs_uint)LineVector.size();
	while(true)
	{
		if(e-s <= 1) return s; // LineOffset is added
		tjs_uint m = s + (e-s)/2;
		if(LineVector[m] > pos)
			e = m;
		else
			s = m;
	}
}
//---------------------------------------------------------------------------
tjs_int Parser::LineToSrcPos(tjs_int line) const
{
	// assumes line is added by LineOffset
	return LineVector[line];
}
//---------------------------------------------------------------------------
void Parser::ConsoleOutput(const tjs_char *msg, void *data)
{
	TVPAddLog( msg );
}
//---------------------------------------------------------------------------
void Parser::WarningLog( const tjs_char* message ) {
	Log( LogType::Warning, message );
}
//---------------------------------------------------------------------------
void Parser::ErrorLog( const tjs_char* message ) {
	if( CompileErrorCount == 0 ) {
		FirstError = ttstr(message);
	}
	CompileErrorCount++;
	Log( LogType::Error, message );
}
//---------------------------------------------------------------------------
void Parser::WarningLog( ttstr message, const ttstr& p1 ) {
	message.Replace( TJS_W( "%1" ), p1 );
	WarningLog( message.c_str() );
}
//---------------------------------------------------------------------------
void Parser::ErrorLog( ttstr message, const ttstr& p1 ) {
	message.Replace( TJS_W( "%1" ), p1 );
	ErrorLog( message.c_str() );
}
//---------------------------------------------------------------------------
void Parser::Log( LogType type, const tjs_char* message ) {
	ttstr typemes;
	if( type == LogType::Warning ) {
		typemes = ttstr( TJS_W("warning : ") );
	} else {
		typemes = ttstr( TJS_W("error : ") );
	}
	tjs_int line = CurrentLine + 1;
	TVPAddLog( typemes + TJS_W("(") + ttstr(line) + TJS_W(") " + message ) );
}
//---------------------------------------------------------------------------
/** ルビ/文字装飾用スタックをクリアする。 */
void Parser::ClearRubyDecorationStack() {
	while( !RubyDecorationStack.empty() ) {
		RubyDecorationStack.top()->Release();
		RubyDecorationStack.pop();
	}
}
//---------------------------------------------------------------------------
/** 指定された名前で現在の辞書の属性(もしくはパラメータ)に値を設定する。 */
void Parser::PushAttribute( const tTJSVariantString* name, iTJSDispatch2* dic, bool isparameter ) {
	tTJSVariant tmp( dic, dic );
	dic->Release();
	PushAttribute( name, tmp, isparameter );
}
//---------------------------------------------------------------------------
/** 指定された名前で現在の辞書の属性(もしくはパラメータ)に値を設定する。 */
void Parser::PushAttribute( const tTJSVariantString* name, const tTJSVariant& value, bool isparameter ) {
	if( isparameter ) {
		if( CurrentTag->setParameter( name, value ) ) {
			WarningLog( ( ttstr( *name ) + ttstr( TJS_W( " : " ) ) + TVPMdkGetText( NUM_MDK_PARAMETER_DUPLICATE ) ).c_str() );
		}
	} else {
		if( CurrentTag->setAttribute( name, value ) ) {
			WarningLog( ( ttstr( *name ) + ttstr( TJS_W( " : " ) ) + TVPMdkGetText( NUM_MDK_ATTRIBUTE_DUPLICATE ) ).c_str() );
		}
	}
}
//---------------------------------------------------------------------------
/** 指定された名前で現在の辞書の属性(もしくはパラメータ)に参照を設定する。 */
void Parser::PushAttributeReference( const tTJSVariantString& name, const tTJSVariant& value, bool isparameter ) {
	iTJSDispatch2* ref = TJSCreateDictionaryObject();
	ref->PropSetByVS( TJS_MEMBERENSURE, GetRWord()->ref(), &value, ref );
	tTJSVariant tmp(ref,ref);
	ref->Release();
	PushAttribute( &name, tmp, isparameter );
}
//---------------------------------------------------------------------------
/** 指定された名前で現在の辞書の属性(もしくはパラメータ)にファイルプロパティを設定する。 */
void Parser::PushAttributeFileProperty( const tTJSVariantString& name, const tTJSVariant& file, const tTJSVariant& prop, bool isparameter ) {
	iTJSDispatch2* ref = TJSCreateDictionaryObject();
	ref->PropSetByVS( TJS_MEMBERENSURE, GetRWord()->file(), &file, ref );
	ref->PropSetByVS( TJS_MEMBERENSURE, GetRWord()->prop(), &prop, ref );
	tTJSVariant tmp(ref,ref);
	ref->Release();
	PushAttribute( &name, tmp, isparameter );
}
//---------------------------------------------------------------------------
/**
 タグ属性に書かれた参照とファイル属性をパースする
 name : 参照
 name.value : 参照
 name::value : ファイル属性
 name.exp::value.value : ファイル属性
 */
void Parser::ParseAttributeValueSymbol( const tTJSVariant& symbol, const tTJSVariant& valueSymbol, bool isparameter ) {
	tjs_int value;
	Token token = Lex->GetInTagToken( value );
	if( token == Token::DOUBLE_COLON ) {
		//ファイル属性として解釈する
		tTJSVariantString* filename = valueSymbol.AsStringNoAddRef();
		token = Lex->GetInTagToken( value );
		if( token == Token::SYMBOL ) {
			ttstr prop( Lex->GetValue( value ).AsStringNoAddRef() );
			token = Lex->GetInTagToken( value );
			while( token == Token::DOT ) {
				prop += ttstr(TJS_W("."));
				token = Lex->GetInTagToken( value );
				if( token == Token::SYMBOL ) {
					prop += ttstr( Lex->GetValue( value ).AsStringNoAddRef() );
				} else {
					ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_FILE_ATTRIBUTE_DOT ), ttstr( symbol.AsStringNoAddRef() ) );
				}
			}
			Lex->Unlex( token, value );
			tTJSVariant file(*filename);
			tTJSVariant propvalue(prop);
			PushAttributeFileProperty( *symbol.AsStringNoAddRef(), file, prop, isparameter );
		} else {
			ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_FILE_ATTRIBUTE_DOUBLE_COLON ), ttstr( symbol.AsStringNoAddRef() ) );
		}
	} else if( token == Token::DOT ) {
		// 参照かファイル属性のファイル名に拡張子が付いているかのどちらか
		ttstr name( valueSymbol.AsStringNoAddRef() );
		name += ttstr( TJS_W(".") );
		token = Lex->GetInTagToken( value );
		if( token == Token::SYMBOL ) {
			// まずはどちらかわからないので両方のケースを考慮する
			name += ttstr( Lex->GetValue( value ).AsStringNoAddRef() );
			token = Lex->GetInTagToken( value );
			while( token == Token::DOT ) {
				name += ttstr(TJS_W("."));
				token = Lex->GetInTagToken( value );
				if( token == Token::SYMBOL ) {
					name += ttstr( Lex->GetValue( value ).AsStringNoAddRef() );
				} else {
					ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_REFERENCE_DOT ), ttstr( symbol.AsStringNoAddRef() ) );
				}
			}
			if( token == Token::DOUBLE_COLON ) {
				// ファイル属性だった
				token = Lex->GetInTagToken( value );
				if( token == Token::SYMBOL ) {
					ttstr prop( Lex->GetValue( value ).AsStringNoAddRef() );
					token = Lex->GetInTagToken( value );
					while( token == Token::DOT ) {
						prop += ttstr(TJS_W("."));
						token = Lex->GetInTagToken( value );
						if( token == Token::SYMBOL ) {
							prop += ttstr( Lex->GetValue( value ).AsStringNoAddRef() );
						} else {
							ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_FILE_ATTRIBUTE_DOT ), ttstr( symbol.AsStringNoAddRef() ) );
						}
					}
					Lex->Unlex( token, value );
					tTJSVariant file(name);
					tTJSVariant propvalue(prop);
					PushAttributeFileProperty( *symbol.AsStringNoAddRef(), file, prop, isparameter );
				} else {
					ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_FILE_ATTRIBUTE_DOUBLE_COLON ), ttstr( symbol.AsStringNoAddRef() ) );
				}
			} else {
				// 参照だった
				Lex->Unlex( token, value );
				tTJSVariant ref(name);
				PushAttributeReference( *symbol.AsStringNoAddRef(), ref, isparameter );
			}
		} else {
			ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_REFERENCE_DOUBLE_COLON ), ttstr( symbol.AsStringNoAddRef() ) );
		}
	} else {
		// . も :: もない場合は、変数参照であるとして登録する。
		PushAttributeReference( *symbol.AsStringNoAddRef(), valueSymbol );
		Lex->Unlex( token, value );
	}
}
//---------------------------------------------------------------------------
void Parser::ParseAttribute( const tTJSVariant& symbol, bool isparameter ) {
	tjs_int value;
	Token token = Lex->GetInTagToken( value );
	if( token == Token::EQUAL ) {
		token = Lex->GetInTagToken( value );
		switch( token ) {
		case Token::CONSTVAL:
		case Token::SINGLE_TEXT:
		case Token::DOUBLE_TEXT:
		case Token::NUMBER:
		case Token::OCTET: {
			const tTJSVariant& v = Lex->GetValue(value);
			PushAttribute( symbol.AsStringNoAddRef(), v, isparameter );
			}
			break;
		case Token::PLUS: {
			tjs_int v2;
			Token t = Lex->GetInTagToken( v2 );
			if( t == Token::NUMBER ) {
				const tTJSVariant& v = Lex->GetValue( v2 );
				PushAttribute( symbol.AsStringNoAddRef(), v, isparameter );
			} else {
				ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_PLUS_NUMBER ), ttstr( symbol.AsStringNoAddRef() ) );
			}
			}
			break;
		case Token::MINUS: {
			tjs_int v2;
			Token t = Lex->GetInTagToken( v2 );
			if( t == Token::NUMBER ) {
				tTJSVariant m(Lex->GetValue( v2 ));
				m.changesign();
				PushAttribute( symbol.AsStringNoAddRef(), m, isparameter );
			} else {
				ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_MINUS_NUMBER ), ttstr( symbol.AsStringNoAddRef() ) );
			}
			}
			break;
		case Token::SYMBOL:	// TJS2 value or file prop
			ParseAttributeValueSymbol( symbol, Lex->GetValue(value), isparameter );
			break;
		default:
			break;
		}
	} else {
		if( !isparameter ) {
			CurrentTag->addCommand( symbol.AsStringNoAddRef() );
		} else {
			tTJSVariant v(nullptr,nullptr);	// null
			PushAttribute( symbol.AsStringNoAddRef(), v, isparameter );
		}
		Lex->Unlex( token, value );
	}
}
//---------------------------------------------------------------------------
bool Parser::ParseSpecialAttribute( Token token, tjs_int value ) {
	switch( token ) {
	case Token::SINGLE_TEXT: {
		const tTJSVariant& v = Lex->GetValue( value );
		PushAttribute( GetRWord()->voice(), v );
		return true;
	}
	case Token::DOUBLE_TEXT: {
		const tTJSVariant& v = Lex->GetValue( value );
		PushAttribute( GetRWord()->storage(), v );
		return true;
	}
	case Token::LT:
		token = Lex->GetInTagToken( value );
		if( token == Token::NUMBER ) {
			const tTJSVariant& v = Lex->GetValue( value );
			PushAttribute( GetRWord()->time(), v );
		} else {
			ErrorLog( TVPMdkGetText( NUM_MDK_NOT_NUMBER_AFTER_LTIN_TAG ).c_str() );
		}
		token = Lex->GetInTagToken( value );
		if( token != Token::GT ) {
			ErrorLog( TVPMdkGetText( NUM_MDK_NOT_CLOSED_LTIN_TAG ).c_str() );
		}
		return true;

	case Token::LBRACE:
		token = Lex->GetInTagToken( value );
		if( token == Token::NUMBER ) {
			const tTJSVariant& v = Lex->GetValue( value );
			PushAttribute( GetRWord()->wait(), v );
		} else {
			ErrorLog( TVPMdkGetText( NUM_MDK_NOT_NUMBER_AFTER_LBRACE_IN_TAG ).c_str() );
		}
		token = Lex->GetInTagToken( value );
		if( token != Token::RBRACE ) {
			ErrorLog( TVPMdkGetText( NUM_MDK_NOT_CLOSED_LBRACE_IN_TAG ).c_str() );
		}
		return true;

	case Token::LPARENTHESIS:
		token = Lex->GetInTagToken( value );
		if( token == Token::NUMBER ) {
			const tTJSVariant& v = Lex->GetValue( value );
			PushAttribute( GetRWord()->fade(), v );
		} else {
			ErrorLog( TVPMdkGetText( NUM_MDK_NOT_NUMBER_AFTER_LPARENTHESIS_IN_TAG ).c_str() );
		}
		token = Lex->GetInTagToken( value );
		if( token != Token::RPARENTHESIS ) {
			ErrorLog( TVPMdkGetText( NUM_MDK_NOT_CLOSED_LPARENTHESIS_IN_TAG ).c_str() );
		}
		return true;

	default:
		break;
	}
	return false;
}
//---------------------------------------------------------------------------
void Parser::ParseTag() {
	if( !MultiLineTag ) {
		CurrentTag->release();
	}
	bool findtagname = false;
	if( MultiLineTag ) {
		// 複数行の時はタグ名が既にあるかチェックする
		findtagname = CurrentTag->existTagName();
	}
	if( !findtagname ) {
		// タグ名がない時は固定タグ名があるかチェックする
		if( !FixTagName.IsEmpty() ) {
			// (複数行タグの時はここには来ない)
			findtagname = true;
			CurrentTag->setTagName( FixTagName.AsVariantStringNoAddRef() );
		}
	}

	if( !findtagname ) {
		tjs_int value;
		Token token = Lex->GetInTagToken( value );
		do {
			switch( token ) {
			case Token::SYMBOL: {	// シンボルはタグ名へ
				const tTJSVariant& val = Lex->GetValue( value );
				ttstr name( val.AsStringNoAddRef() );
				CurrentTag->setTagName( name.AsVariantStringNoAddRef() );
				findtagname = true;
				break;
			}

			case Token::EOL:
				if( !MultiLineTag ) {
					if( !LineAttribute ) {
						MultiLineTag = true;
						CurrentTag->createDic();
					} else {
						LineAttribute = false;
					}
					Scenario->addTagToCurrentLine( *CurrentTag.get() );
					if( !MultiLineTag ) {
						CurrentTag->release();
					}
				}
				return;

			case Token::RBRACKET:
				if( MultiLineTag ) {
					MultiLineTag = false;
				} else {
					Scenario->addTagToCurrentLine( *CurrentTag.get() );
				}
				CurrentTag->release();
				return;	// exit tag

			default:
			{
				if( ParseSpecialAttribute( token, value ) ) {
					findtagname = true;
				} else {
					ttstr* word = GetTagSignWord( token );
					if( word != nullptr ) {
						CurrentTag->addCommand( word->AsVariantStringNoAddRef() );
						token = Lex->GetInTagToken( value );
					} else {
						// unknown symbol
						findtagname = true;
					}
				}
				break;
			}
			}
		} while( !findtagname );
	}
	bool prefMultiLine = MultiLineTag;
	ParseAttributes();
	if( !prefMultiLine && MultiLineTag ) {
		// 最初に複数行となった時にタグを追加しておく、但しタグはまだ開放しない
		Scenario->addTagToCurrentLine( *CurrentTag.get() );
	} else if( prefMultiLine && !MultiLineTag ) {
		// 複数行が解除された時、タグを開放する
		CurrentTag->release();
	} else if( !MultiLineTag ) {
		// 複数行とは関係ない時、タグ追加と解放を行う
		Scenario->addTagToCurrentLine( *CurrentTag.get() );
		CurrentTag->release();
	}
}
//---------------------------------------------------------------------------
void Parser::ParseAttributes() {
	tjs_int value;
	Token token = Lex->GetInTagToken( value );
	bool intag = true;
	do {
		switch( token ) {
		case Token::SYMBOL:
			ParseAttribute( Lex->GetValue(value) );
			break;

		case Token::DOLLAR:	// パラメータとして解釈
			token = Lex->GetInTagToken( value );
			if( token == Token::SYMBOL ) {
				ParseAttribute( Lex->GetValue(value), true );
			} else {
				ErrorLog( TVPMdkGetText( NUM_MDK_PARAMETER_PARSE_ERROR_IN_TAG ).c_str() );
			}
			break;

		case Token::RBRACKET:	// ] タグ終了
			if( TextAttribute ) {
				ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_CHAR_DECORATION ).c_str() );
			}
			MultiLineTag = false;
			intag = false;
			break;

		case Token::RBRACE:	// }
			if( TextAttribute ) {
				MultiLineTag = false;
				intag = false;
			} else {
				ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_SYMBOL_IN_TAG ).c_str() );
			}
			break;

		case Token::EOL:
			if( !LineAttribute ) {
				MultiLineTag = true;
			} else {
				LineAttribute = false;
			}
			intag = false;
			break;

		default:
			if( !ParseSpecialAttribute( token, value ) ) {
				ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_SYMBOL_IN_TAG ).c_str() );
			}
			break;
		}
		if( intag ) {
			token = Lex->GetInTagToken( value );
		}
	} while( intag );
}
//---------------------------------------------------------------------------
/**
 * <<< transname attributes
 */
void Parser::ParseTransition() {
	CurrentTag->release();
	CurrentTag->setTagName( GetRWord()->endtrans() );

	LineAttribute = true;
	tjs_int value;
	Token token = Lex->GetInTagToken( value );
	if( token == Token::SYMBOL ) {
		tjs_int v2;
		Token t = Lex->GetInTagToken( v2 );
		if( t != Token::EQUAL ) {	// <<< symbol=
			PushAttribute( GetRWord()->trans(), Lex->GetValue( value ) );
		}
		Lex->Unlex( t, v2 );
	} else {
		Lex->Unlex( token, value );
	}
	ParseAttributes();

	Scenario->setTag( *CurrentTag.get() );
	CurrentTag->release();
}
//---------------------------------------------------------------------------
/**
 * @名前指定の時、charnameタグとして処理する
 * 指定された名前は name = 属性へ
 * 代替表示名がある時は alias = 属性へ
 */
void Parser::ParseCharacter() {
	CurrentTag->release();
	CurrentTag->setTagName( GetRWord()->charname() );

	tjs_int value;
	Token token = Lex->GetInTagToken( value );
	if( token == Token::SYMBOL || token == Token::SINGLE_TEXT || token == Token::DOUBLE_TEXT ) {
		PushAttribute( GetRWord()->name(), Lex->GetValue( value ) );
		token = Lex->GetInTagToken( value );
		if( token == Token::SLASH ) {
			// 代替表示名指定
			token = Lex->GetInTagToken( value );
			if( token == Token::SYMBOL || token == Token::SINGLE_TEXT || token == Token::DOUBLE_TEXT ) {
				PushAttribute( GetRWord()->alias(), Lex->GetValue( value ) );
			} else {
				tTJSVariant voidvar;
				PushAttribute( GetRWord()->alias(), voidvar );	// alias に空文字指定
				Lex->Unlex( token, value );
			}
		} else {
			Lex->Unlex( token, value );
		}
		LineAttribute = true;
		ParseAttributes();
	} else {
		ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_CHARACTOR_IN_NAME ).c_str() );
	}

	Scenario->setTag( *CurrentTag.get() );
	CurrentTag->release();
}
//---------------------------------------------------------------------------
/**
 * #labelname|description
 */
void Parser::ParseLabel() {
	CurrentTag->release();
	CurrentTag->setTagName( GetRWord()->label() );

	tjs_int value;
	Token token = Lex->GetInTagToken( value );
	if( token == Token::SYMBOL || token == Token::VERTLINE ) {
		if( token == Token::SYMBOL ) {
			CurrentTag->setValue( GetRWord()->name(), Lex->GetValue( value ) );
			token = Lex->GetInTagToken( value );
		}
		if( token == Token::VERTLINE ) {
			ttstr desc = Lex->GetRemainString();
			if( desc.GetLen() > 0 ) {
				CurrentTag->setText( GetRWord()->description(), desc );
			}
		}

	} else {
		ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_CHARACTOR_IN_LABEL ).c_str() );
	}

	Scenario->setTag( *CurrentTag.get() );
	CurrentTag->release();
}
//---------------------------------------------------------------------------
/**
%[
	type : "select",
	text : "user reading text",	// null の時targetラベル参照
	image : "imagefile"
	target : "target"
]

 */
void Parser::ParseSelect( tjs_int number ) {
	CurrentTag->release();
	CurrentTag->setTagName( GetRWord()->select() );
	tTJSVariant n( number );
	CurrentTag->setAttribute( GetRWord()->number(), n );

	tjs_int value;
	Token token = Lex->GetInTagToken( value );
	if( token == Token::ASTERISK ) {
		// * の時は、nullを入れてtargetのラベル参照
		tTJSVariant v(nullptr,nullptr);
		PushAttribute( GetRWord()->text(), v );
		token = Lex->GetInTagToken( value );
		if( token != Token::VERTLINE ) {
			ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_SELECTOR_SYNTAX ).c_str() );
		}
	} else if( token == Token::VERTLINE ) {
		// | の時は、次の|までを画像ファイル名として読み込む
		tjs_int text = Lex->ReadToVerline();
		if( text >= 0 ) {
			tjs_string str( Lex->GetString( text ) );
			tjs_string imagefile = Trim( str );
			tTJSVariant val( imagefile.c_str() );
			PushAttribute( GetRWord()->image(), val );
			Token token = Lex->GetInTagToken( value );
			if( token != Token::VERTLINE ) {
				ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_SELECTOR_SYNTAX ).c_str() );
			}
		}
	} else {
		// それ以外の時は、|までを表示するテキストとして解釈する
		Lex->Unlex();
		tjs_int text = Lex->ReadToVerline();
		if( text >= 0 ) {
			PushAttribute( GetRWord()->text(), Lex->GetValue( text ) );
			Token token = Lex->GetInTagToken( value );
			if( token != Token::VERTLINE ) {
				ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_SELECTOR_SYNTAX ).c_str() );
			}
		}
	}
	tjs_int text = Lex->ReadToVerline();
	if( text >= 0 ) {
		tjs_string str( Lex->GetString( text ) );
		tjs_string targetfile = Trim( str );
		tTJSVariant val( targetfile.c_str() );
		PushAttribute( GetRWord()->target(), val );
	}
	// それ以降は属性として読み込む
	LineAttribute = true;
	ParseAttributes();

	Scenario->setTag( *CurrentTag.get() );
	CurrentTag->release();
}
//---------------------------------------------------------------------------
/**
%[
	type : "next",
	target : "filename",
	cond : "flag"
]
 */
void Parser::ParseNextScenario() {
	CurrentTag->release();
	CurrentTag->setTagName( GetRWord()->next() );

	Lex->SkipSpace();
	tjs_int text = Lex->ReadToSpace();
	if( text >= 0 ) {
		PushAttribute( GetRWord()->target(), Lex->GetValue( text ) );
	}
	Lex->SkipSpace();
	text = Lex->ReadToSpace();
	if( text >= 0 ) {
		if( GetRWord()->if_ == ttstr(Lex->GetString(text)) ) {
			Lex->SkipSpace();
			ttstr cond = Lex->GetRemainString();
			if( cond.GetLen() > 0 ) {
				tTJSVariant v( cond );
				PushAttribute( GetRWord()->cond(), v );
			} else {
				ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_NEXT_CONDITION ).c_str() );
			}
		} else {
			ErrorLog( (TVPMdkGetText( NUM_MDK_INVALID_TEXT_IN_NEXT_COMMAND ) + ttstr( Lex->GetString( text ) )).c_str() );
		}
	}

	Scenario->setTag( *CurrentTag.get() );
	CurrentTag->release();
}
//---------------------------------------------------------------------------
/**
 タグかテキストを解析する
 */
bool Parser::ParseTag( Token token, tjs_int value ) {
	if( token == Token::EOL ) return false;
	TextAttribute = false;

	switch( token ) {
	case Token::TEXT:
		Scenario->addValueToCurrentLine( Lex->GetValue( value ) );
		return true;

	case Token::BEGIN_TAG:
		ParseTag();
		return true;

	case Token::VERTLINE: {	// ルビ or 文字装飾
		iTJSDispatch2* dic = TJSCreateDictionaryObject();
		RubyDecorationStack.push( dic );
		tTJSVariant v( dic, dic );
		Scenario->addValueToCurrentLine( dic );	// 空の辞書を追加しておく
		return true;
	}
	case Token::BEGIN_RUBY: {	// 《が来たので、ルビ文字であるとみなす
		int text = Lex->ReadToCharStrict( TJS_W( '》') );
		if( text >= 0 ) {
			if( !RubyDecorationStack.empty() ) {
				{	// rubyタグの内容を埋める
					Tag tag( RubyDecorationStack.top() );
					RubyDecorationStack.pop();
					tag.setAttribute( GetRWord()->text(), Lex->GetValue( text ) );
					tag.setTagName( GetRWord()->ruby() );
				}
				// [endruby]タグ追加
				Tag tag( GetRWord()->endruby() );
				Scenario->addTagToCurrentLine( tag );
			} else {
				ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_RUBY_SYNTAX ).c_str() );
			}
		} else {
			ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_REGISTER_RUBY_SYNTAX ).c_str() );
		}
		return true;
	}
	case Token::END_RUBY: {	// ルビ辞書の可能性
		if( !RubyDecorationStack.empty() ) {
			{	// rubyタグの内容を埋める/ text属性がないrubyとして登録する、text属性がない場合は辞書から検索してもらう
				Tag tag( RubyDecorationStack.top() );
				RubyDecorationStack.pop();
				tag.setTagName( GetRWord()->ruby() );
			}
			// [endruby]タグ追加
			Tag tag( GetRWord()->endruby() );
			Scenario->addTagToCurrentLine( tag );
		} else {
			ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_RUBY_SYNTAX ).c_str() );
		}
		return true;
	}
	case Token::BEGIN_TXT_DECORATION: {	// { が来たので、テキスト装飾であるとみなす
		if( !RubyDecorationStack.empty() ) {
			iTJSDispatch2* dic = RubyDecorationStack.top();
			RubyDecorationStack.pop();
			Tag* oldTag = CurrentTag.release();
			CurrentTag.reset( new Tag( dic ) );
			CurrentTag->setTagName( GetRWord()->textstyle() );
			TextAttribute = true;
			ParseAttributes();
			TextAttribute = false;
			CurrentTag->release();
			CurrentTag.reset( oldTag );
			// [endtextstyle]タグ追加
			Tag tag( GetRWord()->endtextstyle() );
			Scenario->addTagToCurrentLine( tag );
		} else {
			ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_CHARACTOR_DECORATION_SYNTAX ).c_str() );
		}
		return true;
	}

	case Token::WAIT_RETURN: { // l タグ追加
		Tag tag( GetRWord()->l() );
		Scenario->addTagToCurrentLine( tag );
		return true;
	}

	case Token::INNER_IMAGE: {	// inlineimageタグ追加
		int text = Lex->ReadToCharStrict( TJS_W( ')' ) );
		if( text >= 0 ) {
			Tag tag( GetRWord()->inlineimage() );
			tag.setAttribute( GetRWord()->storage(), Lex->GetValue( text ) );
			Scenario->addTagToCurrentLine( tag );
		} else {
			ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_INLINE_GRAPHICS_SYNTAX ).c_str() );
		}
		return true;
	}
	case Token::COLON: {	// emojiタグ追加
		int text = Lex->ReadToCharStrict( TJS_W( ':' ) );
		if( text >= 0 ) {
			Tag tag( GetRWord()->emoji() );
			tag.setAttribute( GetRWord()->storage(), Lex->GetValue( text ) );
			Scenario->addTagToCurrentLine( tag );
		} else {
			ErrorLog( TVPMdkGetText( NUM_MDK_INVALID_EMOJI_SYNTAX ).c_str() );
		}
		return true;
	}

	default:
		ErrorLog( TVPMdkGetText( NUM_MDK_UNKNOWN_SYNTAX ).c_str() );
		return false;
	}
}
//---------------------------------------------------------------------------
/**
 * 1行分解析
 *
 * 返却するデータ形式
 * 文字列は文字列型でそのまま、記号については記号番号を、タグや属性は辞書型で
 * Array型に1行ずつ格納する
 * 各行もArray型で続く
 *
 * 文字列はそのまま文字列として
 * 数値型の時は、意味を
 * タグや属性は辞書型で
 */
void Parser::ParseLine( tjs_int line ) {
	if( static_cast<tjs_uint>( line ) >= LineVector.size() ) return;

	if( !MultiLineTag ) CurrentTag->release();
	ClearRubyDecorationStack();

	LineAttribute = false;
	TextAttribute = false;

	tjs_int length;
	const tjs_char *str = GetLine( line, &length );
	if( length == 0 ) {
		// 改行のみ
		if( MultiLineTag ) {
			// 複数行のタグの時はvoidを入れるだけにする
			Scenario->setVoid();
		} else if( HasSelectLine ) {
			// 直前が選択肢であった場合は、空のオプションを入れる
			HasSelectLine = false;
			Tag tag( GetRWord()->selopt() );
			Scenario->setTag( tag );
		} else {
			Scenario->setValue( 0 );
		}
	} else {
		// 字句抽出器に1行分の文字列をコピーし初期化する
		Lex->reset( str, length );

		if( MultiLineTag ) {
			ParseTag();
			if( !MultiLineTag ) {
				// 複数行のタグ終了時、その後に他のタグや文字列が続くか確認する
				tjs_int value;
				Token token = Lex->GetTextToken( value );
				if( token != Token::EOL ) {
					while( ParseTag( token, value ) ) {
						token = Lex->GetTextToken( value );
					}
				} else {
					// 複数行タグは終わり、他に要素がない場合はvoid(無視行)を入れる
					Scenario->setVoid();
				}
			} else {
				// まだタグが続いている場合は、void(無視行)を入れておく
				Scenario->setVoid();
			}
		} else {
			// 行頭字句を抽出する
			tjs_int value;
			Token token = Lex->GetFirstToken( value );
			if( HasSelectLine ) {
				// 直前の行が選択肢であった場合、次の行は選択肢か選択肢オプションとなる
				if( token == Token::SELECT ) {
					// 選択肢の場合は、選択肢として解析
					ParseSelect( value );
				} else {
					// 選択肢でない場合は選択肢オプションとして解析する。
					HasSelectLine = false;

					// 選択肢オプション
					CurrentTag->setTagName( GetRWord()->selopt() );
					LineAttribute = true;
					Lex->Unlex();
					ParseAttributes();

					Scenario->setTag( *CurrentTag.get() );
					CurrentTag->release();
				}
				return;
			}

			// 行頭字句を調べる
			switch( token ) {
			case Token::EOL:
				// タブのみの行も空行と同じ
				Scenario->setValue( 0 );
				break;

			case Token::BEGIN_TRANS:
			{	// >>>
				// トランジション開始以降の文字列は無視し、begintransタグを格納するのみ
				Tag tag( GetRWord()->begintrans() );
				Scenario->setTag( tag );
				break;
			}
			case Token::END_TRANS:	// <<<
				ParseTransition();
				break;

			case Token::AT:	// @
				ParseCharacter();
				break;

			case Token::LABEL:	// # タグ
				ParseLabel();
				break;

			case Token::SELECT:	// [0-9]+\.
								// value : select number.
				ParseSelect( value );
				HasSelectLine = true;
				break;

			case Token::NEXT_SCENARIO:	// >
				ParseNextScenario();
				break;

			case Token::LINE_COMMENTS:	// コメント行はvoidを入れる
				Scenario->setVoid();
				break;

			case Token::BEGIN_FIX_NAME:
			{	// <=name
				FixTagName.Clear();
				tjs_int text = Lex->ReadToSpace();
				if( text >= 0 ) {
					FixTagName = ttstr( Lex->GetString( text ) );
				}
				Scenario->setVoid();
				break;
			}
			case Token::END_FIX_NAME:	// =>
				FixTagName.Clear();
				Scenario->setVoid();
				break;

			default:	// 行頭記号に該当しない時は、文字列orタグとして解析する
				while( ParseTag( token, value ) ) {
					token = Lex->GetTextToken( value );
				}
				break;
			}
		}
	}
}
//---------------------------------------------------------------------------
/**
 * 空のシナリオの場合の結果を返す
 */
iTJSDispatch2* Parser::CreateEmptyScenario() {
	iTJSDispatch2* retDic = TJSCreateDictionaryObject();
	if( retDic ) {
		iTJSDispatch2* ar = TJSCreateArrayObject();
		tTJSVariant tmp( ar, ar );
		retDic->PropSetByVS( TJS_MEMBERENSURE, GetRWord()->lines(), &tmp, retDic );
		ar->Release();
	}
	return retDic;
}
//---------------------------------------------------------------------------
/**
 * 引数で渡された文字列を解析して、文字列として返す。
 */
iTJSDispatch2* Parser::ParseText( const tjs_char* text ) {
	TJS_F_TRACE( "tTJSScriptBlock::ParseText" );

	// compiles text and executes its global level scripts.
	// the script will be compiled as an expression if isexpressn is true.
	if( !text ) return CreateEmptyScenario();
	if( !text[0] ) return CreateEmptyScenario();

	TJS_D( ( TJS_W( "Counting lines ...\n" ) ) )

	// スクリプト文字列をコピーして保持する
	Script.reset( new tjs_char[TJS_strlen( text ) + 1] );
	TJS_strcpy( Script.get(), text );

	// 各種状態を初期化
	Lex->Free();
	CurrentTag.reset( new Tag() );
	Scenario.reset( new ScenarioDictionary() );
	ClearRubyDecorationStack();
	FixTagName.Clear();
	LineVector.clear();
	LineLengthVector.clear();

	// 改行位置を求める
	tjs_char *script = Script.get();
	tjs_char *ls = script;
	tjs_char *p = script;
	while( *p ) {
		if( *p == TJS_W( '\r' ) || *p == TJS_W( '\n' ) ) {
			LineVector.push_back( int( ls - script ) );
			LineLengthVector.push_back( int( p - ls ) );
			if( *p == TJS_W( '\r' ) && p[1] == TJS_W( '\n' ) ) p++;
			p++;
			ls = p;
		} else {
			p++;
		}
	}
	if( p != ls ) {
		LineVector.push_back( int( ls - script ) );
		LineLengthVector.push_back( int( p - ls ) );
	}

	// 解析状態変数を初期化
	HasSelectLine = false;
	LineAttribute = false;
	MultiLineTag = false;
	TextAttribute = false;
	FirstError.Clear();
	CompileErrorCount = 0;

	// 行ごとに解析を行う。
	for( CurrentLine = 0; static_cast<tjs_uint>(CurrentLine) < LineVector.size(); CurrentLine++ ) {
		Scenario->setCurrentLine( CurrentLine );
		ParseLine( CurrentLine );
	}
	if( MultiLineTag ) {
		ErrorLog( TVPMdkGetText( NUM_MDK_UNTARMINATED_TAG ).c_str() );
	}

	// コンパイルエラーがあった場合は例外を発生させる。
	if( CompileErrorCount ) {
		TJS_eTJSCompileError( FirstError );
	}

	// 解析結果の配列を辞書のlinesキーに入れる。
	iTJSDispatch2* retDic = TJSCreateDictionaryObject();
	if( retDic ) {
		tTJSVariant tmp( Scenario->getArray(), Scenario->getArray() );
		retDic->PropSetByVS( TJS_MEMBERENSURE, GetRWord()->lines(), &tmp, retDic );
		Scenario->release();
	}
	return retDic;
}
//---------------------------------------------------------------------------

} // namespace AetherKiri::MDKParser
