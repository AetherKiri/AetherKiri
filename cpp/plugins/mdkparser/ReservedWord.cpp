
#include "ReservedWord.h"
#include "tjsGlobalStringMap.h"

namespace AetherKiri::MDKParser {

ReservedWord::ReservedWord() {
	endtrans_ = TJS::TJSMapGlobalStringMap(TJS_W("endtrans"));
	begintrans_ = TJS::TJSMapGlobalStringMap(TJS_W("begintrans"));

	storage_ = TJS::TJSMapGlobalStringMap(TJS_W("storage"));
	type_ = TJS::TJSMapGlobalStringMap(TJS_W("type"));
	name_ = TJS::TJSMapGlobalStringMap(TJS_W("name"));
	value_ = TJS::TJSMapGlobalStringMap(TJS_W("value"));

	tag_ = TJS::TJSMapGlobalStringMap(TJS_W("tag"));
	label_ = TJS::TJSMapGlobalStringMap(TJS_W("label"));
	select_ = TJS::TJSMapGlobalStringMap(TJS_W("select"));
	next_ = TJS::TJSMapGlobalStringMap(TJS_W("next"));
	selopt_ = TJS::TJSMapGlobalStringMap( TJS_W( "selopt" ) );

	attribute_ = TJS::TJSMapGlobalStringMap(TJS_W("attribute"));
	parameter_ = TJS::TJSMapGlobalStringMap(TJS_W("parameter"));
	command_ = TJS::TJSMapGlobalStringMap(TJS_W("command"));
	ref_ = TJS::TJSMapGlobalStringMap(TJS_W("ref"));
	file_ = TJS::TJSMapGlobalStringMap(TJS_W("file"));
	prop_ = TJS::TJSMapGlobalStringMap(TJS_W("prop"));

	trans_ = TJS::TJSMapGlobalStringMap(TJS_W("trans"));
	charname_ = TJS::TJSMapGlobalStringMap(TJS_W("charname"));
	alias_ = TJS::TJSMapGlobalStringMap(TJS_W("alias"));
	description_ = TJS::TJSMapGlobalStringMap(TJS_W("description"));
	text_ = TJS::TJSMapGlobalStringMap(TJS_W("text"));
	image_ = TJS::TJSMapGlobalStringMap(TJS_W("image"));
	target_ = TJS::TJSMapGlobalStringMap(TJS_W("target"));
	if_ = TJS::TJSMapGlobalStringMap(TJS_W("if"));
	cond_ = TJS::TJSMapGlobalStringMap(TJS_W("cond"));
	comment_ = TJS::TJSMapGlobalStringMap(TJS_W("comment"));
	number_ = TJS::TJSMapGlobalStringMap( TJS_W( "number" ) );

	voice_ = TJS::TJSMapGlobalStringMap(TJS_W("voice"));
	time_ = TJS::TJSMapGlobalStringMap(TJS_W("time"));
	wait_ = TJS::TJSMapGlobalStringMap(TJS_W("wait"));
	fade_ = TJS::TJSMapGlobalStringMap(TJS_W("fade"));

	lines_ = TJS::TJSMapGlobalStringMap( TJS_W( "lines" ) );

	ruby_ = TJS::TJSMapGlobalStringMap( TJS_W( "ruby" ) );
	endruby_ = TJS::TJSMapGlobalStringMap( TJS_W( "endruby" ) );
	l_ = TJS::TJSMapGlobalStringMap( TJS_W( "l" ) );
	textstyle_ = TJS::TJSMapGlobalStringMap( TJS_W( "textstyle" ) );
	endtextstyle_ = TJS::TJSMapGlobalStringMap( TJS_W( "endtextstyle" ) );
	inlineimage_ = TJS::TJSMapGlobalStringMap( TJS_W( "inlineimage" ) );
	emoji_ = TJS::TJSMapGlobalStringMap( TJS_W( "emoji" ) );
}

static ReservedWord* gReservedWord = nullptr;
void InitializeReservedWord() {
	if( !gReservedWord ) {
		gReservedWord = new ReservedWord();
	}
}
void FinalizeReservedWord() {
	if( gReservedWord ) {
		delete gReservedWord;
		gReservedWord = nullptr;
	}
}
const ReservedWord* GetRWord() {
	return gReservedWord;
}

} // namespace AetherKiri::MDKParser
