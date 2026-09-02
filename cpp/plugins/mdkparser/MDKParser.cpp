

#include "MDKParser.h"
#include "Parser.h"

namespace AetherKiri::MDKParser {

//---------------------------------------------------------------------------
tTJSNI_MDKParser::tTJSNI_MDKParser()
	: Script( new Parser() ) {
	Script->Initialize();
}
//---------------------------------------------------------------------------
tTJSNI_MDKParser::~tTJSNI_MDKParser() {
}
//---------------------------------------------------------------------------
tjs_error TJS_INTF_METHOD tTJSNI_MDKParser::Construct(tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *tjs_obj) {
	tjs_error hr = inherited::Construct(numparams, param, tjs_obj);
	if(TJS_FAILED(hr)) return hr;

	Owner = tjs_obj;
	return TJS_S_OK;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTJSNI_MDKParser::Invalidate() {
	Owner = nullptr;
	inherited::Invalidate();
}
//---------------------------------------------------------------------------
iTJSDispatch2 * tTJSNI_MDKParser::ParseMDKScenario( const ttstr& storage ) {
//	Script->SetText()
	iTJSDispatch2 *result = nullptr;
	iTJSTextReadStream * stream = nullptr;
	try {
		stream = TVPCreateTextStreamForRead( storage, TJS_W( "" ) );
		ttstr tmp;
		if( stream ) {
			stream->Read( tmp, 0 );
		}
		const tjs_char* buffer = tmp.c_str();
		result = Script->ParseText( buffer );
	} catch( ... ) {
		if( stream ) stream->Destruct();
		throw;
	}
	if( stream ) stream->Destruct();
	return result;
}
//---------------------------------------------------------------------------

} // namespace AetherKiri::MDKParser
