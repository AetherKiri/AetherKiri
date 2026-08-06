#ifndef ___TJSDIC_H___
#define ___TJSDIC_H___

class tTJSAry;

#include "DebugIntf.h"

// TJSの辞書をC++から簡単に使えるようにするクラス


class tTJSDic : public tTJSVariant {
	// link to Dictionary.Clear and Dictionary.Assign
	// defined as static one, which are shared by all instances.
	static iTJSDispatch2 *DicClear;
	static iTJSDispatch2 *DicAssign;
	static iTJSDispatch2 *DicAssignStruct;

	// set DicClear/DicAssign in the constructors
	void init_tTJSDic();

	void printRef() { // test use only
		if (Type() == tvtVoid || AsObjectNoAddRef() == NULL) {
			TVPAddLog(TJS_W("(NULL)"));
			return;
		}
		TVPAddLog(TJS_W("ref=")+ttstr((int)AsObjectNoAddRef()->AddRef()-1));
		AsObjectNoAddRef()->Release();
	}
public:
	iTJSDispatch2 *GetDic() { return AsObject(); }
	iTJSDispatch2 *GetDicNoAddRef() const { return AsObjectNoAddRef(); }

	// constructors
	tTJSDic();
//	tTJSDic(iTJSDispatch2 *srcdic);
	tTJSDic(tTJSVariant &srcdic);
	tTJSDic(tTJSDic &src);

	// destructor
	// ~tTJSDic();

	tTJSVariant& ToVariant() { return *this; }

	// methods
	void Empty(); // Clear() has been reserved by tTJSVariant

	tTJSDic& Assign(iTJSDispatch2 *srcdic);
	tTJSDic& Assign(tTJSVariant &srcdic);
	tTJSDic& Assign(tTJSDic &srcdic);

	void SetProp(const ttstr &name, tTJSVariant &value);

	// to access dic elements
	tTJSVariant GetProp(const tjs_char *name);
	tTJSVariant GetProp(ttstr &name);
	tTJSVariant operator [](const tjs_char *name) { return GetProp(name); }
	tTJSVariant operator [](ttstr &name)          { return GetProp(name); }

	tjs_error DelProp(const tjs_char *name);
	tjs_error DelProp(ttstr &name);

	tTJSAry GetKeys(); // get keys of the dictionary, return tTJSAry

	void AddDic(tTJSDic &sdic);

	// store this object onto savedic
        void Store(iTJSDispatch2 *savedic, const tjs_char *dic_entry=NULL);
	// store this object onto saveary
	void Store(iTJSDispatch2 *saveary, tjs_int index);
	// restore this object from loaddic
        void Restore(iTJSDispatch2 *loaddic, const tjs_char *dic_entry=NULL);
	// restore this object from loadary
	void Restore(iTJSDispatch2 *saveary, tjs_int index);

};


#endif // ___TJSDIC_TJSARY_H___
