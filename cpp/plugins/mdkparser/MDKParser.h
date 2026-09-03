
#ifndef AETHERKIRI_MDK_NATIVE_PARSER_H
#define AETHERKIRI_MDK_NATIVE_PARSER_H


#ifdef _WIN32
#include <windows.h>
#endif
#include "tp_stub.h"
#include <memory>

#ifndef TJS_INTF_METHOD
#define TJS_INTF_METHOD
#endif

namespace AetherKiri::MDKParser {


//---------------------------------------------------------------------------
// tTJSNI_MDKParser
//---------------------------------------------------------------------------
class tTJSNI_MDKParser : public tTJSNativeInstance
{
	typedef tTJSNativeInstance inherited;

	std::unique_ptr<class Parser> Script;

public:
	tTJSNI_MDKParser();
	virtual ~tTJSNI_MDKParser();
	tjs_error TJS_INTF_METHOD Construct(tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *tjs_obj);
	void TJS_INTF_METHOD Invalidate();

	iTJSDispatch2 * ParseMDKScenario( const ttstr& storage );

private:
	iTJSDispatch2 * Owner = nullptr; // owner object

};

} // namespace AetherKiri::MDKParser

#endif // AETHERKIRI_MDK_NATIVE_PARSER_H
