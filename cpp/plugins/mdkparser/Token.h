
#ifndef __MDK_TOKEN_H__
#define __MDK_TOKEN_H__

namespace AetherKiri::MDKParser {

enum class Token : int {
	EMPTY = -1,
	EOL = 0,
	NEXT_SCENARIO,	// >
	BEGIN_TRANS,	// >>>
	END_TRANS,		// <<<
	AT,				// @
	LINE_COMMENTS,	// //
	SELECT,			// [0-9]+\.
	LABEL,			// #
	BEGIN_FIX_NAME,	// <=
	END_FIX_NAME,	// =>

	BEGIN_TAG,		// [
	TEXT,			// normal text
	VERTLINE,		// |
	WAIT_RETURN,	// >
	BEGIN_RUBY,		// 《
	END_RUBY,		// 》
	BEGIN_TXT_DECORATION,	// {
	END_TXT_DECORATION,	// }
	INNER_IMAGE,	// :(

	GT,		// >
	CONSTVAL,
	LT,		// <
	EQUAL,	// =
	EXCRAMATION,	// !
	AMPERSAND,		// &
	DOT,			// .
	PLUS,			// +
	MINUS,			// -
	ASTERISK,		// *
	SLASH,			// /
	BACKSLASH,		// backslash
	PERCENT,		// %
	CHEVRON,		// ^
	LBRACKET,		// [
	RBRACKET,		// ]
	LPARENTHESIS,	// (
	RPARENTHESIS,	// )
	TILDE,			// ~
	QUESTION,		// ?
	COLON,			// :
	DOUBLE_COLON,	// ::
	COMMA,			// ,
	SEMICOLON,		// ;
	LBRACE,			// {
	RBRACE,			// }
	SHARP,			// #
	DOLLAR,			// $
	SINGLE_TEXT,	// '...'
	DOUBLE_TEXT,	// "..."
	NUMBER,			// 0-9
	OCTET,			// <% ...  %>
	T_TRUE,			// true
	T_FALSE,		// false
	T_NULL,			// null
	T_NAN,
	T_INFINITY,
	T_VOID,
	SYMBOL,			//
};

} // namespace AetherKiri::MDKParser


#endif
