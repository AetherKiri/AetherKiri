#ifndef ___TJSARY_H___
#define ___TJSARY_H___

class tTJSDic;

#include "tjsArray.h"
#include "DebugIntf.h"

// TJSの配列をC++から簡単に使えるようにするクラス


class tTJSAry : public tTJSVariant {
	void init_tTJSAry() {
		iTJSDispatch2 *ary = TJSCreateArrayObject();
		SetObject(ary, ary);
		ary->Release();
	}
	void printRef() { // test use only
		if (Type() == tvtVoid || AsObjectNoAddRef() == NULL) {
			TVPAddLog(TJS_W("(NULL)"));
			return;
		}
		TVPAddLog(TJS_W("ref=")+ttstr((int)AsObjectNoAddRef()->AddRef()-1));
		AsObjectNoAddRef()->Release();
	}
public:
	iTJSDispatch2 *GetAry() { return AsObject(); }
	iTJSDispatch2 *GetAryNoAddRef() const { return AsObjectNoAddRef(); }

	// constructors
	tTJSAry();
	tTJSAry(tTJSVariant &srcary);
	tTJSAry(tTJSAry &srcary);
	// Note: tTJSAry(tTJSAry) (without &) is translated to tTJSAry(tTJSDispatch2*)
	tTJSAry(tTJSDic &srcdic);
	// tTJSAry(iTJSDispatch2 *srcary);
	// Note: tTJSAry(iTJSDispatch2 *srcary) is removed as this could be
	//       a cause of significant memory leak.
	//       This class does not have a const-copy-constructor:
	//       (tTJSAry(const tTJSAry&))
	//       because of chainging refcount at assigning argument-array
	//       onto itself. And this brings a translation from
	//       "tTJSAry a(tTJSAry)" onto "tTJSAry a(iTJSDispatch2*)" at the
	//       variable definition, which could increment refcount unexpectedly.

	tTJSVariant& ToVariant() { return *this; }

	// methods
	void Empty(); // Clear() has been reserved by tTJSVariant

	tTJSAry& Assign(iTJSDispatch2 *src);
	tTJSAry& Assign(tTJSVariant &src);
	tTJSAry& Assign(tTJSAry &src);
	tTJSAry& Assign(tTJSDic &src);

	void SetProp(tjs_int index, tTJSVariant &value);
	tTJSVariant GetProp(tjs_int index);

	tjs_int GetSize() const;
	void SetSize(tjs_int count);

	// for accessing array elements
	tTJSVariant operator [](tjs_int index);
	tTJSVariant GetLast();
	tTJSVariant GetPrev();
	void Push(tTJSVariant &var);
	tTJSVariant Pop();

	// store this object onto "dic.dic_entry"
	void Store(iTJSDispatch2 *dic, const tjs_char *dic_entry=NULL);
	// restore this object from "dic.dic_entry"
        void Restore(iTJSDispatch2 *dic, const tjs_char *dic_entry=NULL);
};


#endif // ___TJSARY_H___
