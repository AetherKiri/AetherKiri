
#ifndef __RESERVED_WORD_H__
#define __RESERVED_WORD_H__

#ifdef _WIN32
#include <windows.h>
#endif
#include "tp_stub.h"

namespace AetherKiri::MDKParser {

struct ReservedWord {
	ttstr endtrans_;
	ttstr begintrans_;

	ttstr storage_;
	ttstr type_;
	ttstr name_;
	ttstr value_;

	ttstr tag_;
	ttstr label_;
	ttstr select_;
	ttstr next_;
	ttstr selopt_;

	ttstr attribute_;
	ttstr parameter_;
	ttstr command_;
	ttstr ref_;
	ttstr file_;
	ttstr prop_;

	ttstr trans_;
	ttstr charname_;
	ttstr alias_;
	ttstr description_;
	ttstr text_;
	ttstr image_;
	ttstr target_;
	ttstr if_;
	ttstr cond_;
	ttstr comment_;
	ttstr number_;

	ttstr voice_;
	ttstr time_;
	ttstr wait_;
	ttstr fade_;

	ttstr lines_;

	ttstr ruby_;
	ttstr endruby_;
	ttstr l_;
	ttstr textstyle_;
	ttstr endtextstyle_;
	ttstr inlineimage_;
	ttstr emoji_;

	ReservedWord();

	tTJSVariantString* endtrans() const { return endtrans_.AsVariantStringNoAddRef(); }
	tTJSVariantString* begintrans() const { return begintrans_.AsVariantStringNoAddRef(); }
	tTJSVariantString* storage() const { return storage_.AsVariantStringNoAddRef(); }
	tTJSVariantString* type() const { return type_.AsVariantStringNoAddRef(); }
	tTJSVariantString* name() const { return name_.AsVariantStringNoAddRef(); }
	tTJSVariantString* value() const { return value_.AsVariantStringNoAddRef(); }
	tTJSVariantString* tag() const { return tag_.AsVariantStringNoAddRef(); }
	tTJSVariantString* label() const { return label_.AsVariantStringNoAddRef(); }
	tTJSVariantString* select() const { return select_.AsVariantStringNoAddRef(); }
	tTJSVariantString* next() const { return next_.AsVariantStringNoAddRef(); }
	tTJSVariantString* selopt() const { return selopt_.AsVariantStringNoAddRef(); }
	tTJSVariantString* attribute() const { return attribute_.AsVariantStringNoAddRef(); }
	tTJSVariantString* parameter() const { return parameter_.AsVariantStringNoAddRef(); }
	tTJSVariantString* command() const { return command_.AsVariantStringNoAddRef(); }
	tTJSVariantString* ref() const { return ref_.AsVariantStringNoAddRef(); }
	tTJSVariantString* file() const { return file_.AsVariantStringNoAddRef(); }
	tTJSVariantString* prop() const { return prop_.AsVariantStringNoAddRef(); }
	tTJSVariantString* trans() const { return trans_.AsVariantStringNoAddRef(); }
	tTJSVariantString* charname() const { return charname_.AsVariantStringNoAddRef(); }
	tTJSVariantString* alias() const { return alias_.AsVariantStringNoAddRef(); }
	tTJSVariantString* description() const { return description_.AsVariantStringNoAddRef(); }
	tTJSVariantString* text() const { return text_.AsVariantStringNoAddRef(); }
	tTJSVariantString* image() const { return image_.AsVariantStringNoAddRef(); }
	tTJSVariantString* target() const { return target_.AsVariantStringNoAddRef(); }
	tTJSVariantString* if_word() const { return if_.AsVariantStringNoAddRef(); }
	tTJSVariantString* cond() const { return cond_.AsVariantStringNoAddRef(); }
	tTJSVariantString* comment() const { return comment_.AsVariantStringNoAddRef(); }
	tTJSVariantString* number() const { return number_.AsVariantStringNoAddRef(); }
	tTJSVariantString* voice() const { return voice_.AsVariantStringNoAddRef(); }
	tTJSVariantString* time() const { return time_.AsVariantStringNoAddRef(); }
	tTJSVariantString* wait() const { return wait_.AsVariantStringNoAddRef(); }
	tTJSVariantString* fade() const { return fade_.AsVariantStringNoAddRef(); }
	tTJSVariantString* lines() const { return lines_.AsVariantStringNoAddRef(); }
	tTJSVariantString* ruby() const { return ruby_.AsVariantStringNoAddRef(); }
	tTJSVariantString* endruby() const { return endruby_.AsVariantStringNoAddRef(); }
	tTJSVariantString* l() const { return l_.AsVariantStringNoAddRef(); }
	tTJSVariantString* textstyle() const { return textstyle_.AsVariantStringNoAddRef(); }
	tTJSVariantString* endtextstyle() const { return endtextstyle_.AsVariantStringNoAddRef(); }
	tTJSVariantString* inlineimage() const { return inlineimage_.AsVariantStringNoAddRef(); }
	tTJSVariantString* emoji() const { return emoji_.AsVariantStringNoAddRef(); }
};

extern void InitializeReservedWord();
extern void FinalizeReservedWord();
extern const ReservedWord* GetRWord();

} // namespace AetherKiri::MDKParser

#endif // __RESERVED_WORD_H__
