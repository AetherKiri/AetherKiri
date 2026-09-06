//---------------------------------------------------------------------------
/*
	TJS2 Script Engine
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TJS2 lexical analyzer
//---------------------------------------------------------------------------


#include "LexicalAnalyzer.h"
#include <math.h>
#include <ctype.h>
#include "Parser.h"
#include "MDKMessages.h"

namespace AetherKiri::MDKParser {

void MDKThrowError( const ttstr & msg ) { TVPThrowExceptionMessage( msg.c_str() ); }
void MDKThrowError( const tjs_char* msg ) { TVPThrowExceptionMessage( msg ); }

//---------------------------------------------------------------------------
// FPU control
//---------------------------------------------------------------------------
#if defined(__WIN32__) && !defined(__GNUC__)
static unsigned int TJSDefaultFPUCW = 0;
static unsigned int TJSNewFPUCW = 0;
static unsigned int TJSDefaultMMCW = 0;
static bool TJSFPUInit = false;
#endif
// FPU例外をマスク
void TJSSetFPUE() {
#if defined(__WIN32__) && !defined(__GNUC__)
	if( !TJSFPUInit ) {
		TJSFPUInit = true;
#if defined(_M_X64)
		TJSDefaultMMCW = _MM_GET_EXCEPTION_MASK();
#else
		TJSDefaultFPUCW = _control87( 0, 0 );

#ifdef _MSC_VER
		TJSNewFPUCW = _control87( MCW_EM, MCW_EM );
#else
		_default87 = TJSNewFPUCW = _control87( MCW_EM, MCW_EM );
#endif	// _MSC_VER
#ifdef TJS_SUPPORT_VCL
		Default8087CW = TJSNewFPUCW;
#endif	// TJS_SUPPORT_VCL
#endif	// _M_X64
	}

#if defined(_M_X64)
	_MM_SET_EXCEPTION_MASK( _MM_MASK_INVALID | _MM_MASK_DIV_ZERO | _MM_MASK_DENORM | _MM_MASK_OVERFLOW | _MM_MASK_UNDERFLOW | _MM_MASK_INEXACT );
#else
	//	_fpreset();
	_control87( TJSNewFPUCW, 0xffff );
#endif
#endif	// defined(__WIN32__) && !defined(__GNUC__)

}
// 例外マスクを解除し元に戻す
void TJSRestoreFPUE() {
#if defined(__WIN32__) && !defined(__GNUC__)
	if( !TJSFPUInit ) return;
#if defined(_M_X64)
	_MM_SET_EXCEPTION_MASK( TJSDefaultMMCW );
#else
	_control87( TJSDefaultFPUCW, 0xffff );
#endif
#endif
}

//---------------------------------------------------------------------------
const tjs_char TJS_SKIP_CODE = (tjs_char)~((tjs_char)0);
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TJSNext
//---------------------------------------------------------------------------
static void _TJSNext(const tjs_char **ptr)
{
	do
	{
		(*ptr)++;
	} while(*(*ptr) && *(*ptr)==TJS_SKIP_CODE);
}

bool inline TJSNext(const tjs_char **ptr)
{
	(*ptr)++;
	if(*(*ptr) == TJS_SKIP_CODE) _TJSNext(ptr);
	return *(*ptr)!=0;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TJSSkipSpace
//---------------------------------------------------------------------------
bool TJSSkipSpace(const tjs_char **ptr)
{
	while(*(*ptr) && (*(*ptr)==TJS_SKIP_CODE || TJS_iswspace(*(*ptr))))
		(*ptr)++;
	return *(*ptr)!=0;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TJSHexNum
tjs_int TJSHexNum(tjs_char ch) throw()
{
	if(ch>=TJS_W('a') && ch<=TJS_W('f')) return ch-TJS_W('a')+10;
	if(ch>=TJS_W('A') && ch<=TJS_W('F')) return ch-TJS_W('A')+10;
	if(ch>=TJS_W('0') && ch<=TJS_W('9')) return ch-TJS_W('0');
	return -1;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TJSOctNum
//---------------------------------------------------------------------------
tjs_int TJSOctNum(tjs_char ch) throw()
{
	if(ch>=TJS_W('0') && ch<=TJS_W('7')) return ch-TJS_W('0');
	return -1;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TJSDecNum
//---------------------------------------------------------------------------
tjs_int TJSDecNum(tjs_char ch) throw()
{
	if(ch>=TJS_W('0') && ch<=TJS_W('9')) return ch-TJS_W('0');
	return -1;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TJSBinNum
//---------------------------------------------------------------------------
tjs_int TJSBinNum(tjs_char ch) throw()
{
	if(ch==TJS_W('0')) return 0;
	if(ch==TJS_W('1')) return 1;
	return -1;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TJSUnescapeBackSlash
//---------------------------------------------------------------------------
tjs_int TJSUnescapeBackSlash(tjs_char ch) throw()
{
	// convert "\?"
	// ch must indicate "?"
	switch(ch)
	{
	case TJS_W('a'): return 0x07;
	case TJS_W('b'): return 0x08;
	case TJS_W('f'): return 0x0c;
	case TJS_W('n'): return 0x0a;
	case TJS_W('r'): return 0x0d;
	case TJS_W('t'): return 0x09;
	case TJS_W('v'): return 0x0b;
	default : return ch;
	}
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TJSSkipComment
//---------------------------------------------------------------------------
static tTJSSkipCommentResult TJSSkipComment(const tjs_char **ptr)
{
	if((*ptr)[0] != TJS_W('/')) return scrNotComment;

	if((*ptr)[1] == TJS_W('/'))
	{
		// line comment; skip to newline
		while(*(*ptr)!=TJS_W('\n')) if(!TJSNext(&(*ptr))) break;
		if(*(*ptr) ==0) return scrEnded;
		(*ptr)++;
		TJSSkipSpace(&(*ptr));
		if(*(*ptr) ==0) return scrEnded;

		return scrContinue;
	}
	else if((*ptr)[1] == TJS_W('*'))
	{
		// block comment; skip to the next '*' '/'
		// and we must allow nesting of the comment.
		(*ptr) += 2;
		if(*(*ptr) == 0) MDKThrowError( TVPMdkGetText( NUM_MDK_UNTERMINATED_COMMENT ) );
		tjs_int level = 0;
		for(;;)
		{
			if((*ptr)[0] == TJS_W('/') && (*ptr)[1] == TJS_W('*'))
			{
				// note: we cannot avoid comment processing when the
				// nested comment is in string literals.
				level ++;
			}
			if((*ptr)[0] == TJS_W('*') && (*ptr)[1] == TJS_W('/'))
			{
				if(level == 0)
				{
					(*ptr) += 2;
					break;
				}
				level --;
			}
			if(!TJSNext(&(*ptr))) MDKThrowError( TVPMdkGetText( NUM_MDK_UNTERMINATED_COMMENT ) );
		}
		if(*(*ptr) ==0) return scrEnded;
		TJSSkipSpace(&(*ptr));
		if(*(*ptr) ==0) return scrEnded;

		return scrContinue;
	}

	return scrNotComment;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TJSStringMatch
//---------------------------------------------------------------------------
bool TJSStringMatch(const tjs_char **sc, const tjs_char *wrd, bool isword)
{
	// compare string with a script starting from sc and wrd.
	// word matching is processed if isword is true.
	const tjs_char *save = (*sc);
	while(*wrd && *(*sc))
	{
		if(*(*sc) != *wrd) break;
		TJSNext(sc);
		wrd++;
	}

	if(*wrd) { (*sc)=save; return false; }
	if(isword)
	{
		if(TJS_iswalpha(*(*sc)) || *(*sc) == TJS_W('_'))
			{ (*sc)=save; return false; }
	}
	return true;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TJSParseString
//---------------------------------------------------------------------------
enum tTJSInternalParseStringResult
{ psrNone, psrDelimiter, psrAmpersand, psrDollar };
static tTJSInternalParseStringResult
	TJSInternalParseString(tTJSVariant &val, const tjs_char **ptr,
		tjs_char delim, bool embexpmode)
{
	// delim1 must be '\'' or '"'
	// delim2 must be '&' or '\0'

	ttstr str;

	tTJSInternalParseStringResult status = psrNone;

	for(;*(*ptr);)
	{
		if(*(*ptr)==TJS_W('\\'))
		{
			// escape
			if(!TJSNext(ptr)) break;
			if(*(*ptr)==TJS_W('x') || *(*ptr)==TJS_W('X'))
			{
				// hex
				// starts with a "\x", be parsed while characters are
				// recognized as hex-characters, but limited of size of tjs_char.
				// on Windows, \xXXXXX will be parsed to UNICODE 16bit characters.
				if(!TJSNext(ptr)) break;
				tjs_int num;
				tjs_int code = 0;
				tjs_int count = 0;
				while((num = TJSHexNum(*(*ptr)))!=-1 && count<(sizeof(tjs_char)*2))
				{
					code*=16;
					code+=num;
					count ++;
					if(!TJSNext(ptr)) break;
				}
				if(*(*ptr) == 0) break;
				str+=(tjs_char)code;
			}
			else if(*(*ptr) == TJS_W('0'))
			{
				// octal
				if(!TJSNext(ptr)) break;

				tjs_int num;
				tjs_int code=0;
				while((num=TJSOctNum(*(*ptr)))!=-1)
				{
					code*=8;
					code+=num;
					if(!TJSNext(ptr)) break;
				}
				if(*(*ptr) == 0) break;
				str += (tjs_char)code;
			}
			else
			{
				str += (tjs_char)TJSUnescapeBackSlash(*(*ptr));
				TJSNext(ptr);
			}
		}
		else if(*(*ptr) == delim)
		{
			// string delimiters
			if(!TJSNext(ptr))
			{
				status = psrDelimiter;
				break;
			}

			const tjs_char *p=(*ptr);
			TJSSkipSpace(&p);
			if(*p == delim)
			{
				// sequence of 'A' 'B' will be combined as 'AB'
				(*ptr) = p;
				TJSNext(ptr);
			}
			else
			{
				status = psrDelimiter;
				break;
			}
		}
		else if(embexpmode && *(*ptr) == TJS_W('&'))
		{
			// '&'
			if(!TJSNext(ptr)) break;
			status = psrAmpersand;
			break;
		}
		else if(embexpmode && *(*ptr) == TJS_W('$'))
		{
			// '$'
			// '{' must be placed immediately after '$'
			const tjs_char *p = (*ptr);
			if(!TJSNext(ptr)) break;
			if(*(*ptr) == TJS_W('{'))
			{
				if(!TJSNext(ptr)) break;
				status = psrDollar;
				break;
			}
			else
			{
				(*ptr) = p;
				str += *(*ptr);
				TJSNext(ptr);
			}
		}
		else
		{
			str+=*(*ptr);
			TJSNext(ptr);
		}
	}

	if(status == psrNone)
	{
		// error
		MDKThrowError( TVPMdkGetText( NUM_MDK_UNTERMINATED_STRING_REGEX_OCTET ) );
	}

	str.FixLen();
	val = str;

	return status;
}
//---------------------------------------------------------------------------
bool TJSParseString(tTJSVariant &val, const tjs_char **ptr)
{
	// parse a string starts with '\'' or '"'

	tjs_char delimiter=*(*ptr);

	TJSNext(ptr);

	return TJSInternalParseString(val, ptr, delimiter, false) == psrDelimiter;
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// TJSParseNumber
//---------------------------------------------------------------------------
static tTJSString TJSExtractNumber(tjs_int (*validdigits)(tjs_char ch),
	const tjs_char *expmark, const tjs_char **ptr, bool &isreal)
{
	tTJSString tmp;

	bool point_found = false;
	bool exp_found = false;
	while(true)
	{
		if(validdigits(**ptr) != -1 && !exp_found)
		{
			tmp += **ptr;
			if(!TJSNext(ptr)) break;
		}
		else if(**ptr == TJS_W('.') && !point_found && !exp_found)
		{
			point_found = true;
			tmp += **ptr;
			if(!TJSNext(ptr)) break;
		}
		else if((**ptr == expmark[0] || **ptr == expmark[1]) && !exp_found)
		{
			exp_found = true;
			tmp += **ptr;
			if(!TJSNext(ptr)) break;
			if(!TJSSkipSpace(ptr)) break;
			if(**ptr == TJS_W('+'))
			{
				tmp += **ptr;
				if(!TJSNext(ptr)) break;
				if(!TJSSkipSpace(ptr)) break;
			}
			else if(**ptr == TJS_W('-'))
			{
				tmp += **ptr;
				if(!TJSNext(ptr)) break;
				if(!TJSSkipSpace(ptr)) break;
			}
		}
		else if (TJSDecNum(**ptr) != -1 && exp_found)
		{
			tmp += **ptr;
			if(!TJSNext(ptr)) break;
		}
		else
		{
			break;
		}
	}

	isreal = point_found || exp_found;

	return tmp;
}

static bool TJSParseNonDecimalReal(tTJSVariant &val, const tjs_char **ptr,
	tjs_int (*validdigits)(tjs_char ch), tjs_int basebits)
{
	// parse non-decimal(hexiadecimal, octal or binary) floating-point number.
	// this routine heavily depends on IEEE double floating-point number expression.

	tjs_uint64 main = TJS_UI64_VAL(0); // significand
	tjs_int exp = 0; // 2^n exponental
	tjs_int numsignif = 0; // significand bit count (including leading left-most '1') in "main"
	bool pointpassed = false;


	// scan input
	while(true)
	{
		if(**ptr == TJS_W('.'))
		{
			pointpassed = true;
		}
		else if(**ptr == TJS_W('p') || **ptr == TJS_W('P'))
		{
			if(!TJSNext(ptr)) break;
			if(!TJSSkipSpace(ptr)) break;

			bool biassign = false;
			if(**ptr == TJS_W('+'))
			{
				biassign = false;
				if(!TJSNext(ptr)) break;
				if(!TJSSkipSpace(ptr)) break;
			}

			if(**ptr == TJS_W('-'))
			{
				biassign = true;
				if(!TJSNext(ptr)) break;
				if(!TJSSkipSpace(ptr)) break;
			}

			tjs_int bias = 0;
			while(true)
			{
				bias *= 10;
				bias += TJSDecNum(**ptr);
				if(!TJSNext(ptr)) break;
			}
			if(biassign) bias = -bias;
			exp += bias;
			break;
		}
		else
		{
			tjs_int n = validdigits(**ptr);
			if(numsignif == 0)
			{
				// find msb flaged bit
				tjs_int b = basebits - 1;
				while(b >= 0)
				{
					if((1<<b) & n) break;
					b--;
				}

				b++;
				if(b)
				{
					// n is not zero
					// place it to the main's msb
					numsignif = b;
					main |= ((tjs_uint64)n << (64 - numsignif));
					if(pointpassed)
						exp -= (basebits - b + 1);
					else
						exp = b - 1;
				}
				else
				{
					// n is zero
					if(pointpassed) exp -= basebits;
				}
			}
			else
			{
				// append to main
				if(numsignif + basebits < 64)
				{
					numsignif += basebits;
					main |= ((tjs_uint64)n << (64 - numsignif));
				}
				if(!pointpassed) exp += basebits;
			}
		}
		if(!TJSNext(ptr)) break;
	}

	main >>= (64 - 1 - TJS_IEEE_D_SIGNIFICAND_BITS);

	if(main == 0)
	{
		// zero
		val = (tTVReal)0.0;
		return true;
	}

	main &= ((TJS_UI64_VAL(1) << TJS_IEEE_D_SIGNIFICAND_BITS) - TJS_UI64_VAL(1));

	if(exp < TJS_IEEE_D_EXP_MIN)
	{
		// denormal
		// treat as zero
		val = (tTVReal)0.0;
		return true;
	}

	if(exp > TJS_IEEE_D_EXP_MAX)
	{
		// too large
		// treat as infinity
		tjs_real d;
		*(tjs_uint64*)&d = TJS_IEEE_D_P_INF;
		val = d;
		return true;
	}

	// compose IEEE double
	tjs_real d;
	*(tjs_uint64*)&d =
		TJS_IEEE_D_MAKE_SIGN(0) |
		TJS_IEEE_D_MAKE_EXP(exp) |
		TJS_IEEE_D_MAKE_SIGNIFICAND(main);
	val = d;
	return true;
}

static bool TJSParseNonDecimalInteger(tTJSVariant &val, const tjs_char **ptr,
	tjs_int (*validdigits)(tjs_char ch), tjs_int basebits)
{
	tjs_int64 v = 0;
	while(true)
	{
		v <<= basebits;
		v += validdigits(**ptr);
		if(!TJSNext(ptr)) break;
	}
	val = (tTVInteger)v;
	return true;
}

static bool TJSParseNonDecimalNumber(tTJSVariant &val, const tjs_char **ptr,
	tjs_int (*validdigits)(tjs_char ch), tjs_int base)
{
	bool isreal = false;
	tTJSString tmp(TJSExtractNumber(validdigits, TJS_W("Pp"), ptr, isreal));

	if(tmp.IsEmpty()) return false;

	const tjs_char *p = tmp.c_str();
	const tjs_char **pp = &p;

	if(isreal)
		return TJSParseNonDecimalReal(val, pp, validdigits, base);
	else
		return TJSParseNonDecimalInteger(val, pp, validdigits, base);
}

static bool TJSParseDecimalReal(tTJSVariant &val, const tjs_char **pp)
{
	val = (tTVReal)TJS_strtod(*pp, nullptr);
	return true;
}

static bool TJSParseDecimalInteger(tTJSVariant &val, const tjs_char **pp)
{
	int n;
	tjs_int64 num = 0;
	while((n = TJSDecNum(**pp)) != -1)
	{
		num *= 10;
		num += n;
		if(!TJSNext(pp)) break;
	}
	val = (tTVInteger)num;
	return true;
}

static bool TJSParseNumber2(tTJSVariant &val, const tjs_char **ptr)
{
	// stage 2

	if(TJSStringMatch(ptr, TJS_W("true"), true))
	{
		val = (tjs_int)true;
		return true;
	}
	if(TJSStringMatch(ptr, TJS_W("false"), true))
	{
		val = (tjs_int)false;
		return true;
	}
	if(TJSStringMatch(ptr, TJS_W("NaN"), true))
	{
		// Not a Number
		tjs_real d;
		*(tjs_uint64*)&d = TJS_IEEE_D_P_NaN;
		val = d;
		return true;
	}
	if(TJSStringMatch(ptr, TJS_W("Infinity"), true))
	{
		// positive inifinity
		tjs_real d;
		*(tjs_uint64*)&d = TJS_IEEE_D_P_INF;
		val = d;
		return true;
	}

	const tjs_char *ptr_save = *ptr;

	if(**ptr == TJS_W('0'))
	{
		if(!TJSNext(ptr))
		{
			val = (tjs_int) 0;
			return true;
		}

		tjs_char mark = **ptr;

		if(mark == TJS_W('X') || mark == TJS_W('x'))
		{
			// hexadecimal
			if(!TJSNext(ptr)) return false;
			return TJSParseNonDecimalNumber(val, ptr, TJSHexNum, 4);
		}

		if(mark == TJS_W('B') || mark == TJS_W('b'))
		{
			// binary
			if(!TJSNext(ptr)) return false;
			return TJSParseNonDecimalNumber(val, ptr, TJSBinNum, 1);
		}

		if(mark == TJS_W('.'))
		{
			// decimal point
			*ptr = ptr_save;
			goto decimal;
		}

		if(mark == TJS_W('E') || mark == TJS_W('e'))
		{
			// exp
			*ptr = ptr_save;
			goto decimal;
		}

		if(mark == TJS_W('P') || mark == TJS_W('p'))
		{
			// 2^n exp
			return false;
		}

		// octal
		*ptr = ptr_save;
		return TJSParseNonDecimalNumber(val, ptr, TJSOctNum, 3);
	}

	// integer decimal or real decimal
decimal:
	bool isreal = false;
	tTJSString tmp(TJSExtractNumber(TJSDecNum, TJS_W("Ee"), ptr, isreal));

	if(tmp.IsEmpty()) return false;

	const tjs_char *p = tmp.c_str();
	const tjs_char **pp = &p;

	if(isreal)
		return TJSParseDecimalReal(val, pp);
	else
		return TJSParseDecimalInteger(val, pp);
}


bool TJSParseNumber(tTJSVariant &val, const tjs_char **ptr)
{
	// parse a number pointed by (*ptr)
	TJSSetFPUE();

	bool sign = false; // true if negative

	if(**ptr == TJS_W('+'))
	{
		sign = false;
		if(!TJSNext(ptr)) return false;
		if(!TJSSkipSpace(ptr)) return false;
	}
	else if(**ptr == TJS_W('-'))
	{
		sign = true;
		if(!TJSNext(ptr)) return false;
		if(!TJSSkipSpace(ptr)) return false;
	}

	if(TJSParseNumber2(val, ptr))
	{
		if(sign) val = -val;
		return true;
	}

	return false;
}

//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TJSParseOctet
//---------------------------------------------------------------------------
static bool TJSParseOctet(tTJSVariant &val, const tjs_char **ptr)
{
	// parse a octet literal;
	// syntax is:
	// <% xx xx xx xx xx xx ... %>
	// where xx is hexadecimal 8bit(octet) binary representation.
	TJSNext(ptr);
	TJSNext(ptr);   // skip <%

	tjs_uint8 *buf = NULL;
	tjs_uint buflen = 0;

	bool leading = true;
	tjs_uint8 cur = 0;

	for(;*(*ptr);)
	{
		switch(TJSSkipComment(ptr))
		{
		case scrEnded:
			MDKThrowError( TVPMdkGetText( NUM_MDK_UNTERMINATED_STRING_REGEX_OCTET ) );
		case scrContinue:
		case scrNotComment:
			;
		}


		const tjs_char *next = *ptr;
		TJSNext(&next);
		if(*(*ptr) == TJS_W('%') && *next == TJS_W('>'))
		{
			*ptr = next;
			TJSNext(ptr);

			// literal ended

			if(!leading)
			{
				buf = (tjs_uint8*)TJS_realloc(buf, buflen+1);
				if(!buf)
					MDKThrowError( TVPMdkGetText( NUM_MDK_INSUFFICIENT_MEMORY ) );
				buf[buflen] = cur;
				buflen++;
			}

			val = tTJSVariant(buf, buflen); // create octet variant
			if(buf) TJS_free(buf);
			return true;
		}

		tjs_char ch = *(*ptr);
		tjs_int n = TJSHexNum(ch);
		if(n != -1)
		{
			if(leading)
			{
				cur = (tjs_uint8)(n);
				leading = false;
			}
			else
			{
				cur <<= 4;
				cur += n;

				// store cur
				buf = (tjs_uint8*)TJS_realloc(buf, buflen+1);
				if(!buf)
					MDKThrowError( TVPMdkGetText( NUM_MDK_INSUFFICIENT_MEMORY ) );
				buf[buflen] = cur;
				buflen++;

				leading = true;
			}
		}

		if(!leading && ch == TJS_W(','))
		{
			buf = (tjs_uint8*)TJS_realloc(buf, buflen+1);
			if(!buf)
				MDKThrowError( TVPMdkGetText( NUM_MDK_INSUFFICIENT_MEMORY ) );
			buf[buflen] = cur;
			buflen++;

			leading = true;
		}

		*ptr = next;
	}

	// error
	MDKThrowError( TVPMdkGetText( NUM_MDK_UNTERMINATED_STRING_REGEX_OCTET ) );

	return false;
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TJSParseRegExp
//---------------------------------------------------------------------------
static bool TJSParseRegExp(tTJSVariant &pat, const tjs_char **ptr)
{
	// parse a regular expression pointed by 'ptr'.
	// this is essencially the same as string parsing, except for
	// not to decode escaped characters by '\\'.
	// the regexp must be terminated by the delimiter '/', not succeeded by '\\'.

	// this returns an internal representation: '//flag/pattern' that can be parsed by
	// RegExp._compile

//	if(!TJSNext((*ptr))) MDKThrowError(TVPMdkGetText( NUM_MDK_UNTERMINATED_STRING_REGEX_OCTET ));

	bool ok = false;
	bool lastbackslash = false;
	ttstr str;

	for(;*(*ptr);)
	{
		if(*(*ptr)==TJS_W('\\'))
		{
			str+=*(*ptr);
			if(lastbackslash)
				lastbackslash = false;
			else
				lastbackslash = true;
		}
		else if(*(*ptr)==TJS_W('/') && !lastbackslash)
		{
			// string delimiters
//			lastbackslash = false;

			if(!TJSNext(ptr))
			{
				ok = true;
				break;
			}

			// flags can be here
			ttstr flag;
			while(*(*ptr) >= TJS_W('a') && *(*ptr) <= TJS_W('z'))
			{
				flag += *(*ptr);
				if(!TJSNext(ptr)) break;
			}
			str = TJS_W("/")TJS_W("/")+ flag + TJS_W("/") + str;
			ok = true;
			break;
		}
		else
		{
			lastbackslash = false;
			str+=*(*ptr);
		}
		TJSNext(ptr);
	}

	if(!ok)
	{
		// error
		MDKThrowError( TVPMdkGetText( NUM_MDK_UNTERMINATED_STRING_REGEX_OCTET ) );
	}

	pat = str;

	return true;
}

//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// hash table for reserved words
//---------------------------------------------------------------------------
static iTJSDispatch2 * TJSReservedWordHash = NULL;
static bool TJSReservedWordHashInit = false;
static tjs_int TJSReservedWordHashRefCount;
//---------------------------------------------------------------------------
void TJSReservedWordsHashAddRef()
{
	if(TJSReservedWordHashRefCount == 0)
	{
		TJSReservedWordHashInit = false;
		TJSReservedWordHash = TJSCreateDictionaryObject(nullptr);
	}
	TJSReservedWordHashRefCount ++;
}
//---------------------------------------------------------------------------
void TJSReservedWordsHashRelease()
{
	TJSReservedWordHashRefCount --;

	if(TJSReservedWordHashRefCount == 0)
	{
		TJSReservedWordHash->Release();
		TJSReservedWordHash = NULL;
	}
}
//---------------------------------------------------------------------------
static void TJSRegisterReservedWordsHash(const tjs_char *word, tjs_int num)
{
	tTJSVariant val(num);
	TJSReservedWordHash->PropSet(TJS_MEMBERENSURE, word, NULL, &val, TJSReservedWordHash);
}
//---------------------------------------------------------------------------
#define TJS_REG_RES_WORD(word, value) TJSRegisterReservedWordsHash(TJS_W(word), static_cast<tjs_int>(value));
static void TJSInitReservedWordsHashTable()
{
	if(TJSReservedWordHashInit) return;
	TJSReservedWordHashInit = true;

	TJS_REG_RES_WORD("false", Token::T_FALSE);
	//TJS_REG_RES_WORD("int", Token::T_INT);
	//TJS_REG_RES_WORD("if", T_IF);
	TJS_REG_RES_WORD("null", Token::T_NULL);
	//TJS_REG_RES_WORD("real", Token::T_REAL);
	//TJS_REG_RES_WORD("string", Token::T_STRING);
	TJS_REG_RES_WORD("true", Token::T_TRUE);
	TJS_REG_RES_WORD("void", Token::T_VOID);
	TJS_REG_RES_WORD("NaN", Token::T_NAN);
	TJS_REG_RES_WORD("Infinity", Token::T_INFINITY);
}
//---------------------------------------------------------------------------
static tjs_int TJSParseInteger( const tjs_char **ptr ) {
	tjs_int v = 0;
	tjs_int c = 0;
	while( (c = TJSDecNum(**ptr)) != -1 ) {
		v *= 10;
		v += c;
		if(!TJSNext(ptr)) break;
	}
	return v;
}


//---------------------------------------------------------------------------
// tTJSLexicalAnalyzer
//---------------------------------------------------------------------------
LexicalAnalyzer::LexicalAnalyzer(Parser *block)
 : ScriptWork(new tjs_char[1024]), ScriptWorkSize(1024), Block(block)
{
	// resneeded is valid only if exprmode is true
	TJS_F_TRACE("tTJSLexicalAnalyzer::tTJSLexicalAnalyzer");
	TJSInitReservedWordsHashTable();

	PrevToken = -1;
	PrevPos = 0;
	NestLevel = 0;
	First = true;
	RegularExpression = false;
	BareWord = false;
	PutValue(tTJSVariant());
}
//---------------------------------------------------------------------------
LexicalAnalyzer::~LexicalAnalyzer()
{
	Free();
}
//---------------------------------------------------------------------------
void LexicalAnalyzer::reset( const tjs_char *str, tjs_int length ) {
	if( length > (ScriptWorkSize-1) ) {
		ScriptWork.reset( new tjs_char[length+1] );
		ScriptWorkSize = length + 1;
	}
	TJS_strncpy( ScriptWork.get(), str, length );
	ScriptWork[length] = 0;
	Script = ScriptWork.get();
	Current = Script;
	First = true;
	RetValDeque.clear();
}
//---------------------------------------------------------------------------
#define TJS_MATCH_W(word, code) \
	if(TJSStringMatch(&Current, TJS_W(word), true)) return (code)
#define TJS_MATCH_S(word, code) \
	if(TJSStringMatch(&Current, TJS_W(word), false)) return (code)
#define TJS_MATCH_W_V(word, code, val) \
	if(TJSStringMatch(&Current, TJS_W(word), true)) { n=PutValue(val); return (code); }
#define TJS_MATCH_S_V(word, code, val) \
	if(TJSStringMatch(&Current, TJS_W(word), false)) { n=PutValue(val); return (code); }
#define TJS_1CHAR(code) \
	TJSNext(&Current); return (code)


/**
 * 行頭トークンを取得する
 */
Token LexicalAnalyzer::GetFirstToken(tjs_int &n) {
	if(*Current == 0) return Token::EOL;

	PrevPos = (tjs_int)(Current - Script); // remember current position as "PrevPos"

	while( *Current == TJS_W( '\t' ) );	// skip tab

	switch(*Current)
	{
	case TJS_W('>'):
		TJS_MATCH_S(">>>", Token::BEGIN_TRANS);
		TJS_1CHAR(Token::NEXT_SCENARIO);

	case TJS_W('@'):
		TJS_1CHAR(Token::AT);

	case TJS_W('#'):
		TJS_1CHAR(Token::LABEL);

	case TJS_W('<'):
		TJS_MATCH_S( "<<<", Token::END_TRANS );
		TJS_MATCH_S( "<=", Token::BEGIN_FIX_NAME );
		return GetTextToken( n );

	case TJS_W('='):
		if( Current[1] == TJS_W( '>' ) ) {
			Current += 2;
			return Token::END_FIX_NAME;
		} else {
			return GetTextToken( n );
		}

	case TJS_W('0'):
	case TJS_W('1'):
	case TJS_W('2'):
	case TJS_W('3'):
	case TJS_W('4'):
	case TJS_W('5'):
	case TJS_W('6'):
	case TJS_W('7'):
	case TJS_W('8'):
	case TJS_W('9'): {	// number
		const tjs_char* num = Current;
		tjs_int val = TJSParseInteger( &num );
		if( (*num) == TJS_W('.') ) {
			n = val;
			num++;
			Current = num;
			return Token::SELECT;
		} else {
			// 選択肢ではない通常の文字列
			Block->WarningLog( TVPMdkGetText( NUM_MDK_SELECTOR_PARSE_ERROR ).c_str() );
			return GetTextToken(n);
		}
	}
	case TJS_W( '/' ): {
		if( Current[1] == TJS_W( '/' ) ) {
			TJSSkipComment( &Current );
			return Token::LINE_COMMENTS;
		} else {
			return GetTextToken( n );
		}
		break;
	}
	default:
		return GetTextToken(n);
	}
}

void LexicalAnalyzer::PutChar( tjs_char c ) {
	TextBody.push_back( c );
}
ttstr LexicalAnalyzer::GetText() {
	return ttstr( &TextBody[0], TextBody.size() );
}
Token LexicalAnalyzer::ReturnText(tjs_int &n) {
	if( RetValDeque.size() ) {
		tTokenPair pair = RetValDeque.front();
		RetValDeque.pop_front();
		n = pair.value;
		return pair.token;
	}
	if( TextBody.size() == 0 ) {
		return Token::EOL;
	}
	tTJSVariant variant( GetText() );
	n = PutValue( variant );
	return Token::TEXT;
}

/**
 * 指定文字まで読む
 */
tjs_int LexicalAnalyzer::ReadToChar( tjs_char end ) {
	if(*Current == 0) return -1;

	PrevPos = (tjs_int)(Current - Script); // remember current position as "PrevPos"
	TextBody.clear();
	const tjs_char* start = Current;
	tjs_int result = -1;

	while( true ) {
		if( ( *Current ) == 0 ) { // end of text
			ReturnText( result );
			return result;
		} else if( ( *Current ) == end ) {
			Current++;
			if( TextBody.size() ) {
				ReturnText( result );
				return result;
			}
			return -1;
		}
		PutChar( *Current );
		Current++;
	}
	// ここには来ないはず
	return -1;
}
/* 指定文字までの文字列を読み取る。end文字が見付からない場合は-1を返す */
tjs_int LexicalAnalyzer::ReadToCharStrict( tjs_char end ) {
	if( *Current == 0 ) return -1;

	PrevPos = (tjs_int)( Current - Script ); // remember current position as "PrevPos"
	TextBody.clear();
	const tjs_char* start = Current;
	tjs_int result = -1;

	while( true ) {
		if( ( *Current ) == 0 ) { // end of text
			TextBody.clear();
			return -1;	// not found 'end'
		} else if( ( *Current ) == end ) {
			Current++;
			if( TextBody.size() ) {
				ReturnText( result );
				return result;
			}
			return -1;
		}
		PutChar( *Current );
		Current++;
	}
	// ここには来ないはず
	return -1;
}
/**
 * 通常文をパースする
 */
Token LexicalAnalyzer::GetTextToken(tjs_int &n) {
	if( RetValDeque.size() ) {
		tTokenPair pair = RetValDeque.front();
		RetValDeque.pop_front();
		n = pair.value;
		return pair.token;
	}
	if(*Current == 0) return Token::EOL;

	PrevPos = (tjs_int)(Current - Script); // remember current position as "PrevPos"
	TextBody.clear();
	const tjs_char* start = Current;

	while( true ) {
		switch(*Current) {
		case 0: // end of text
			return ReturnText( n );

		case TJS_W('\\'):
			// escape next
			Current++;
			if( *Current == 0 ) {
				return ReturnText( n );
			}
			break;

		case TJS_W('['):
			if( TextBody.size() ) return ReturnText( n );
			Current++;
			return Token::BEGIN_TAG;

/*
		case TJS_W('/'): {
			if( Current[1] == TJS_W('/') ) {
				if( TextBody.size() ) return ReturnText( n );
				TJSSkipComment(&Current);
				return Token::LINE_COMMENTS;
			}
			break;
		}
*/
		case TJS_W('|'):
			if( TextBody.size() ) return ReturnText( n );
			Current++;
			return Token::VERTLINE;

		case TJS_W('>'):
			if( TextBody.size() ) return ReturnText( n );
			Current++;
			return Token::WAIT_RETURN;

		case TJS_W( '《' ):
			if( TextBody.size() ) return ReturnText( n );
			Current++;
			return Token::BEGIN_RUBY;

		case TJS_W( '》' ):	// ルビ辞書を利用する
			if( TextBody.size() ) return ReturnText( n );
			Current++;
			return Token::END_RUBY;

		case TJS_W( '{' ):
			if( TextBody.size() ) return ReturnText( n );
			Current++;
			return Token::BEGIN_TXT_DECORATION;

		case TJS_W(':'):
			if( Current[1] == TJS_W( '(' ) ) {
				if( TextBody.size() ) return ReturnText( n );
				Current+=2;
				return Token::INNER_IMAGE;
			} else {
				if( TextBody.size() ) return ReturnText( n );
				Current++;
				return Token::COLON;
			}

		}

		PutChar( *Current );
		Current++;
	}
	// ここには来ないはず
	return Token::EOL;
}
void LexicalAnalyzer::SkipSpace() {
	TJSSkipSpace( &Current );
}
#if 0
/**
 * ルビか文字装飾を行う中間部分
 */
Token tTJSLexicalAnalyzer::GetRubyDecorationToken(tjs_int &n) {
	if( RetValDeque.size() ) {
		tTokenPair pair = RetValDeque.front();
		RetValDeque.pop_front();
		n = pair.value;
		return pair.token;
	}
	if(*Current == 0) return Token::EOL;

	PrevPos = (tjs_int)(Current - Script); // remember current position as "PrevPos"
	TextBody.clear();
	const tjs_char* start = Current;

	while( true ) {
		switch(*Current) {
		case 0: // end of text
			return ReturnText( n );

		case TJS_W('\\'):
			// escape next
			Current++;
			if( *Current == 0 ) {
				return ReturnText( n );
			}
			break;

		case TJS_W('《'):
			if( TextBody.size() ) return ReturnText( n );
			Current++;
			return Token::BEGIN_RUBY;

		case TJS_W('》'):	// ルビ辞書を利用する
			if( TextBody.size() ) return ReturnText( n );
			Current++;
			return Token::END_RUBY;

		case TJS_W('{'):
			if( TextBody.size() ) return ReturnText( n );
			Current++;
			return Token::BEGIN_TXT_DECORATION;
/*
		case TJS_W('/'): {
			if( Current[1] == TJS_W('/') ) {
				if( TextBody.size() ) return ReturnText( n );
				TJSSkipComment(&Current);
				return Token::LINE_COMMENTS;	// コメントは文法違反
			}
			break;
		}
*/
		case TJS_W('|'):	// nest
			if( TextBody.size() ) return ReturnText( n );
			Current++;
			return Token::VERTLINE;

		case TJS_W('>'):
			if( TextBody.size() ) return ReturnText( n );
			Current++;
			return Token::WAIT_RETURN;

		case TJS_W('['):
			if( TextBody.size() ) return ReturnText( n );
			Current++;
			return Token::BEGIN_TAG;
		}
		PutChar( *Current );
		Current++;
	}
	// ここには来ないはず
	return Token::EOL;
}
#endif
Token LexicalAnalyzer::GetInTagToken(tjs_int &n) {
	if( RetValDeque.size() ) {
		tTokenPair pair = RetValDeque.front();
		RetValDeque.pop_front();
		n = pair.value;
		return pair.token;
	}

	PrevPos = (tjs_int)( Current - Script ); // remember current position as "PrevPos"

	if(!TJSSkipSpace(&Current)) return Token::EOL;	// skip space
	if(*Current == 0) return Token::EOL;

	switch(*Current)
	{
	case TJS_W('>'):
		TJS_1CHAR(Token::GT);

	case TJS_W('<'):
		{
			const tjs_char *next = Current;
			TJSNext(&next);
			if(*next == TJS_W('%'))
			{
				// '<%'   octet literal
				tTJSVariant v;
				TJSParseOctet(v, &Current);
				n = PutValue(v);
				return Token::OCTET;
			}
		}
		TJS_1CHAR(Token::LT);

	case TJS_W('='):
		TJS_1CHAR(Token::EQUAL);

	case TJS_W('!'):
		TJS_1CHAR(Token::EXCRAMATION);

	case TJS_W('&'):
		TJS_1CHAR(Token::AMPERSAND);

	case TJS_W('|'):
		TJS_1CHAR(Token::VERTLINE);

	case TJS_W('.'):
		if(Current[1] >= TJS_W('0') && Current[1] <= TJS_W('9'))
		{
			// number
			tTJSVariant v;
			TJSParseNumber(v, &Current);
			n=PutValue(v);
			return Token::NUMBER;
		}
		TJS_1CHAR(Token::DOT);

	case TJS_W('+'):
		TJS_1CHAR(Token::PLUS);

	case TJS_W('-'):
		TJS_1CHAR(Token::MINUS);

	case TJS_W('*'):
		TJS_1CHAR(Token::ASTERISK);

	case TJS_W('/'):
		/*
		if( Current[1] == TJS_W('/') ) {
			return Token::LINE_COMMENTS;
		}
		*/
		TJS_1CHAR(Token::SLASH);

	case TJS_W('\\'):
		TJS_1CHAR(Token::BACKSLASH);

	case TJS_W('%'):
		TJS_1CHAR(Token::PERCENT);

	case TJS_W('^'):
		TJS_1CHAR(Token::CHEVRON);

	case TJS_W('['):
		TJS_1CHAR(Token::LBRACKET);

	case TJS_W(']'):	// tag 終了
		TJS_1CHAR(Token::RBRACKET);

	case TJS_W('('):
		TJS_1CHAR(Token::LPARENTHESIS);

	case TJS_W(')'):
		TJS_1CHAR(Token::RPARENTHESIS);

	case TJS_W('~'):
		TJS_1CHAR(Token::TILDE);

	case TJS_W('?'):
		TJS_1CHAR(Token::QUESTION);

	case TJS_W(':'):
		if( Current[1] == TJS_W(':') ) {
			Current += 2;
			return Token::DOUBLE_COLON;
		}
		TJS_1CHAR(Token::COLON);

	case TJS_W(','):
		TJS_1CHAR(Token::COMMA);

	case TJS_W(';'):
		TJS_1CHAR(Token::SEMICOLON);

	case TJS_W('{'):
		TJS_1CHAR(Token::LBRACE);

	case TJS_W('}'):
		TJS_1CHAR(Token::RBRACE);

	case TJS_W('#'):
		TJS_1CHAR(Token::SHARP);

	case TJS_W('$'):
		TJS_1CHAR(Token::DOLLAR);

		// literal string
	case TJS_W('\''): {
		tTJSVariant v;
		TJSParseString(v, &Current);
		n=PutValue(v);
		return Token::SINGLE_TEXT;
	}
	case TJS_W('\"'): {
		tTJSVariant v;
		TJSParseString(v, &Current);
		n=PutValue(v);
		return Token::DOUBLE_TEXT;
	}

	case TJS_W('@'):
		TJS_1CHAR(Token::AT);

	case TJS_W('0'):
	case TJS_W('1'):
	case TJS_W('2'):
	case TJS_W('3'):
	case TJS_W('4'):
	case TJS_W('5'):
	case TJS_W('6'):
	case TJS_W('7'):
	case TJS_W('8'):
	case TJS_W('9'): {	// number
		tTJSVariant v;
		bool r = TJSParseNumber(v, &Current);
		if(!r) Block->ErrorLog( TVPMdkGetText( NUM_MDK_NUMBER_PARSE_ERRROR ).c_str() );
		n=PutValue(v);
		return Token::NUMBER;
	}
	}

	if(!TJS_iswalpha(*Current) && *Current!=TJS_W('_'))
	{
		ttstr str( TVPMdkGetText( NUM_MDK_INVALID_CHAR ) );
		ttstr mes;
		ttstr( *Current ).EscapeC( mes );
		str.Replace(TJS_W("%1"), mes );
		MDKThrowError(str);
	}


	const tjs_char *ptr = Current;
	tjs_int nch = 0;
	while(TJS_iswdigit(*ptr) || TJS_iswalpha(*ptr) || *ptr==TJS_W('_') || *ptr>0x0100 )
		ptr++, nch++;

	if(nch == 0)
	{
		ttstr str( TVPMdkGetText( NUM_MDK_INVALID_CHAR ) );
		ttstr mes;
		ttstr( *Current ).EscapeC( mes );
		str.Replace(TJS_W("%1"), mes );
		MDKThrowError(str);
	}

	ttstr str(Current, nch);
	Current += nch;

	tjs_char *s, *d;
	s = d = str.Independ();
	while(*s)
	{
		*d = *s;
		d++, s++;
	}
	*d = 0;
	str.FixLen();

	Token retnum = Token::EMPTY;

	if( !BareWord ) {
		tTJSVariant val;
		tjs_error hr = TJSReservedWordHash->PropGet( TJS_MEMBERMUSTEXIST, str.c_str(), str.GetHint(), &val, TJSReservedWordHash );
		if( hr == TJS_S_OK ) {
			retnum = static_cast<Token>( (tjs_int)val );
		}
	}
	BareWord = false;
	if(retnum == Token::EMPTY) {
		// not a reserved word
		n = PutValue(str);
		return Token::SYMBOL;
	}

	switch(retnum)
	{
	case Token::T_VOID:
		n = PutValue( tTJSVariant() );
		return Token::CONSTVAL;
	case Token::T_FALSE:
		n = PutValue(tTJSVariant(false));
		return Token::NUMBER;
	case Token::T_NULL:
		n = PutValue(tTJSVariant((iTJSDispatch2*)nullptr));
		return Token::CONSTVAL;
	case Token::T_TRUE:
		n = PutValue(tTJSVariant(true));
		return Token::NUMBER;
	case Token::T_NAN:
	  {
		TJSSetFPUE();
		tjs_real d;
		*(tjs_uint64*)&d = TJS_IEEE_D_P_NaN;
		n = PutValue(tTJSVariant(d));
		return Token::NUMBER;
	  }
	case Token::T_INFINITY:
	  {
		TJSSetFPUE();
		tjs_real d;
		*(tjs_uint64*)&d = TJS_IEEE_D_P_INF;
		n = PutValue(tTJSVariant(d));
		return Token::NUMBER;
	  }
	default:
		break;
	}
	return retnum;
}
#if 0
tjs_int tTJSLexicalAnalyzer::GetToken(tjs_int &n)
{
	// returns token, pointed by 'Current'

	if(*Current == 0) return 0;

	if(RegularExpression)
	{
		// the next token was marked as a regular expression by the parser
		RegularExpression = false;

		Current = Script + PrevPos; // draws position of the first '/' back

		TJSNext(&Current);

		tTJSVariant pat;
		TJSParseRegExp(pat, &Current);
		n = PutValue(pat);

		return T_REGEXP;
	}

re_match:

	PrevPos = (tjs_int)(Current - Script); // remember current position as "PrevPos"

	switch(*Current)
	{
	case TJS_W('>'):
		TJS_MATCH_S(">>>=", T_RBITSHIFTEQUAL);
		TJS_MATCH_S(">>>", T_RBITSHIFT);
		TJS_MATCH_S(">>=", T_RARITHSHIFTEQUAL);
		TJS_MATCH_S(">>", T_RARITHSHIFT);
		TJS_MATCH_S(">=", T_GTOREQUAL);
		TJS_1CHAR(T_GT);

	case TJS_W('<'):
		TJS_MATCH_S("<<=", T_LARITHSHIFTEQUAL);
		TJS_MATCH_S("<->", T_SWAP);
		TJS_MATCH_S("<=", T_LTOREQUAL);
		TJS_MATCH_S("<<", T_LARITHSHIFT);
		{
			const tjs_char *next = Current;
			TJSNext(&next);
			if(*next == TJS_W('%'))
			{
				// '<%'   octet literal
				tTJSVariant v;
				TJSParseOctet(v, &Current);
				n = PutValue(v);
				return T_CONSTVAL;
			}
		}
		TJS_1CHAR(T_LT);

	case TJS_W('='):
		TJS_MATCH_S("===", T_DISCEQUAL);
		TJS_MATCH_S("==", T_EQUALEQUAL);
		TJS_MATCH_S("=>", T_COMMA);
			// just a replacement for comma, like perl
		TJS_1CHAR(T_EQUAL);

	case TJS_W('!'):
		TJS_MATCH_S("!==", T_DISCNOTEQUAL);
		TJS_MATCH_S("!=", T_NOTEQUAL);
		TJS_1CHAR(T_EXCRAMATION);

	case TJS_W('&'):
		TJS_MATCH_S("&&=", T_LOGICALANDEQUAL);
		TJS_MATCH_S("&&", T_LOGICALAND);
		TJS_MATCH_S("&=", T_AMPERSANDEQUAL);
		TJS_1CHAR(T_AMPERSAND);

	case TJS_W('|'):
		TJS_MATCH_S("||=", T_LOGICALOREQUAL);
		TJS_MATCH_S("||", T_LOGICALOR);
		TJS_MATCH_S("|=", T_VERTLINEEQUAL);
		TJS_1CHAR(T_VERTLINE);

	case TJS_W('.'):
		if(Current[1] >= TJS_W('0') && Current[1] <= TJS_W('9'))
		{
			// number
			tTJSVariant v;
			TJSParseNumber(v, &Current);
			n=PutValue(v);
			return T_CONSTVAL;
		}
		TJS_MATCH_S("...", T_OMIT);
		TJS_1CHAR(T_DOT);

	case TJS_W('+'):
		TJS_MATCH_S("++", T_INCREMENT);
		TJS_MATCH_S("+=", T_PLUSEQUAL);
		TJS_1CHAR(T_PLUS);

	case TJS_W('-'):
		TJS_MATCH_S("-=", T_MINUSEQUAL);
		TJS_MATCH_S("--", T_DECREMENT);
		TJS_1CHAR(T_MINUS);

	case TJS_W('*'):
		TJS_MATCH_S("*=", T_ASTERISKEQUAL);
		TJS_1CHAR(T_ASTERISK);

	case TJS_W('/'):
		// check comments
		switch(TJSSkipComment(&Current))
		{
		case scrContinue:
			goto re_match;
		case scrEnded:
			return 0;
		case scrNotComment:
			;
		}

		TJS_MATCH_S("/=", T_SLASHEQUAL);
		TJS_1CHAR(T_SLASH);

	case TJS_W('\\'):
		TJS_MATCH_S("\\=", T_BACKSLASHEQUAL);
		TJS_1CHAR(T_BACKSLASH);

	case TJS_W('%'):
		TJS_MATCH_S("%=", T_PERCENTEQUAL);
		TJS_1CHAR(T_PERCENT);

	case TJS_W('^'):
		TJS_MATCH_S("^=", T_CHEVRONEQUAL);
		TJS_1CHAR(T_CHEVRON);

	case TJS_W('['):
		NestLevel++;
		TJS_1CHAR(T_LBRACKET);

	case TJS_W(']'):
		NestLevel--;
		TJS_1CHAR(T_RBRACKET);

	case TJS_W('('):
		NestLevel++;
		TJS_1CHAR(T_LPARENTHESIS);

	case TJS_W(')'):
		NestLevel--;
		TJS_1CHAR(T_RPARENTHESIS);

	case TJS_W('~'):
		TJS_1CHAR(T_TILDE);

	case TJS_W('?'):
		TJS_1CHAR(T_QUESTION);

	case TJS_W(':'):
		TJS_1CHAR(T_COLON);

	case TJS_W(','):
		TJS_1CHAR(T_COMMA);

	case TJS_W(';'):
		TJS_1CHAR(T_SEMICOLON);

	case TJS_W('{'):
		NestLevel++;
		TJS_1CHAR(T_LBRACE);

	case TJS_W('}'):
		NestLevel--;
		TJS_1CHAR(T_RBRACE);

	case TJS_W('#'):
		TJS_1CHAR(T_SHARP);

	case TJS_W('$'):
		TJS_1CHAR(T_DOLLAR);

	case TJS_W('\''):
	case TJS_W('\"'):
		// literal string
	  {
		tTJSVariant v;
		TJSParseString(v, &Current);
		n=PutValue(v);
		return T_CONSTVAL;
	  }

	case TJS_W('@'):
		// embeddable expression in string (such as @"this can be embeddable like &variable;")
	  {
		const tjs_char *org = Current;
		if(!TJSNext(&Current)) return 0;
		if(!TJSSkipSpace(&Current)) return 0;
		if(*Current == TJS_W('\'') || *Current == TJS_W('\"'))
		{
			tEmbeddableExpressionData data;
			data.State = evsStart;
			data.WaitingNestLevel = NestLevel;
			data.Delimiter = *Current;
			data.NeedPlus = false;
			if(!TJSNext(&Current)) return 0;
			EmbeddableExpressionDataStack.push_back(data);
			return -1;
		}
		else
		{
			Current = org;
		}

		// possible pre-prosessor statements
		switch(ProcessPPStatement())
		{
		case scrContinue:
			goto re_match;
		case scrEnded:
			return 0;
		case scrNotComment:
			Current = org;
			break;
		}
		break;
	  }

	case TJS_W('0'):
	case TJS_W('1'):
	case TJS_W('2'):
	case TJS_W('3'):
	case TJS_W('4'):
	case TJS_W('5'):
	case TJS_W('6'):
	case TJS_W('7'):
	case TJS_W('8'):
	case TJS_W('9'):
		// number
	  {
		tTJSVariant v;
		bool r = TJSParseNumber(v, &Current);
		if(!r) Block->ErrorLog( TVPMdkGetText( NUM_MDK_NUMBER_PARSE_ERRROR ).c_str() );
		n=PutValue(v);
		return T_CONSTVAL;
	  }
	}

	if(!TJS_iswalpha(*Current) && *Current!=TJS_W('_'))
	{
		ttstr str( TVPMdkGetText( NUM_MDK_INVALID_CHAR ) );
		ttstr mes;
		ttstr( *Current ).EscapeC( mes );
		str.Replace(TJS_W("%1"), mes );
		MDKThrowError(str);
	}


	const tjs_char *ptr = Current;
	tjs_int nch = 0;
	while(TJS_iswdigit(*ptr) || TJS_iswalpha(*ptr) || *ptr==TJS_W('_') ||
		*ptr>0x0100 || *ptr == TJS_SKIP_CODE)
		ptr++, nch++;

	if(nch == 0)
	{
		ttstr str( TVPMdkGetText( NUM_MDK_INVALID_CHAR ) );
		ttstr mes;
		ttstr( *Current ).EscapeC( mes );
		str.Replace(TJS_W("%1"), mes );
		MDKThrowError(str);
	}

	ttstr str(Current, nch);
	Current += nch;

	tjs_char *s, *d;
	s = d = str.Independ();
	while(*s)
	{
		// eliminate TJS_SKIP_CODE
		if(*s == TJS_SKIP_CODE)
		{
			s++;
			continue;
		}
		*d = *s;
		d++, s++;
	}
	*d = 0;
	str.FixLen();

	tjs_int retnum;

	if(BareWord)
		retnum = -1;
	else {
		tTJSVariant val;
		TJSReservedWordHash->PropGet( TJS_MEMBERMUSTEXIST, str.c_str(), str.GetHint(), &val, TJSReservedWordHash );
		retnum = (tjs_int)val;
	}

	BareWord = false;

	if(retnum == -1)
	{
		// not a reserved word
		n = PutValue(str);
		return T_SYMBOL;
	}

	switch(retnum)
	{
	case T_FALSE:
		n = PutValue(tTJSVariant(false));
		return T_CONSTVAL;
	case T_NULL:
		n = PutValue(tTJSVariant((iTJSDispatch2*)NULL));
		return T_CONSTVAL;
	case T_TRUE:
		n = PutValue(tTJSVariant(true));
		return T_CONSTVAL;
	case T_NAN:
	  {
		TJSSetFPUE();
		tjs_real d;
		*(tjs_uint64*)&d = TJS_IEEE_D_P_NaN;
		n = PutValue(tTJSVariant(d));
		return T_CONSTVAL;
	  }
	case T_INFINITY:
	  {
		TJSSetFPUE();
		tjs_real d;
		*(tjs_uint64*)&d = TJS_IEEE_D_P_INF;
		n = PutValue(tTJSVariant(d));
		return T_CONSTVAL;
	  }
	}

	return retnum;
}
#endif
//---------------------------------------------------------------------------
tjs_int LexicalAnalyzer::PutValue(const tTJSVariant &val)
{
	tTJSVariant *v = new tTJSVariant(val);
	Values.push_back(v);
	return (tjs_int)(Values.size() -1);
}
//---------------------------------------------------------------------------
void LexicalAnalyzer::Free(void)
{
	std::vector<tTJSVariant*>::iterator i;
	for(i = Values.begin(); i != Values.end(); i++)
	{
		delete *i;
	}
	Values.clear();
}
//---------------------------------------------------------------------------
tjs_int LexicalAnalyzer::GetCurrentPosition()
{
	return (tjs_int)(Current - Script);
}
//---------------------------------------------------------------------------
void LexicalAnalyzer::SetStartOfRegExp(void)
{
	// notifies that a regular expression ( regexp ) 's
	// first '/' ( that indicates the start of the regexp ) had been detected.
	// this will be called by the parser.

	RegularExpression = true;
}
//---------------------------------------------------------------------------
void LexicalAnalyzer::SetNextIsBareWord(void)
{
	// notifies that the next word must be treated as a bare word
	// (not a reserved word)
	// this will be called after . (dot) operator by the parser.

	BareWord = true;
}
//---------------------------------------------------------------------------

} // namespace AetherKiri::MDKParser
