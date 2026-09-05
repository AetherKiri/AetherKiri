#include "tjsCommHead.h"
#include "TJSAry.h"
#include "TJSDic.h"

#include "MsgIntf.h"

// constructors
tTJSAry::tTJSAry() : tTJSVariant()
{
    //TVPAddLog(TJS_W("tTJSAry()"));
    init_tTJSAry();
}

#if 0
tTJSAry::tTJSAry(iTJSDispatch2 *srcary) : tTJSVariant()
{
//TVPAddLog(TJS_W("tTJSAry(iTJSDispatch2*)"));
        if (srcary == NULL)
                TVPThrowExceptionMessage(TJS_W("tTJSAry::tTJSAry() is invoked with NULL ary"));
        init_tTJSAry();
        Assign(srcary);
}
#endif

tTJSAry::tTJSAry(tTJSVariant &srcary) : tTJSVariant()
{
    //TVPAddLog(TJS_W("tTJSAry(tTJSVariant&)"));
    if (srcary.Type() != tvtObject)
        TVPThrowExceptionMessage(TJS_W("tTJSAry::tTJSAry(tTJSVariant&) is invoked with non tvtObject"));
    init_tTJSAry();
    Assign(srcary);
}

tTJSAry::tTJSAry(tTJSAry &srcary) : tTJSVariant()
{
    //TVPAddLog(TJS_W("tTJSAry(tTJSAry&)"));
    if (srcary.Type() != tvtObject)
        TVPThrowExceptionMessage(TJS_W("tTJSAry::tTJSAry(tTJSAry&) is invoked with non tvtObject"));
    init_tTJSAry();
    Assign(srcary);
}

tTJSAry::tTJSAry(tTJSDic &srcdic) : tTJSVariant()
{
    //TVPAddLog(TJS_W("tTJSAry(tTJSDic&)"));
    if (srcdic.Type() != tvtObject)
        TVPThrowExceptionMessage(TJS_W("tTJSAry::tTJSAry(tTJSDic&) is invoked with non tvtObject"));
    init_tTJSAry();
    Assign(srcdic);
}


void tTJSAry::Empty()
{
    GetAryNoAddRef()->FuncCall(0, TJS_W("clear"), NULL, NULL, 0, NULL, GetAryNoAddRef());
}

tTJSAry& tTJSAry::Assign(iTJSDispatch2 *src)
{
    //TVPAddLog(TJS_W("tTJSAry::Assign(iTJSDispatch2*)"));
    if (src == NULL)
        TVPThrowExceptionMessage(TJS_W("tTJSAry::Assign(iTJSDispatch2*) is invoked with NULL"));

    tTJSVariant srcary(src, src);
    return Assign(srcary);
}

tTJSAry& tTJSAry::Assign(tTJSVariant &src)
{
    //TVPAddLog(TJS_W("tTJSAry::Assign(iTJSVariant&)"));
    if (src.Type() != tvtObject)
        TVPThrowExceptionMessage(TJS_W("tTJSAry::Assign(tTJSVariant&) is invoked with non tvtObject"));

    iTJSDispatch2 *ary = GetAryNoAddRef();
    tTJSVariant *psrc = &src;
    ary->FuncCall(0, TJS_W("assign"), NULL, NULL, 1, &psrc, ary);
    return *this;
}

tTJSAry& tTJSAry::Assign(tTJSAry &src)
{
    //TVPAddLog(TJS_W("tTJSAry::Assign(tTJSAry&)"));
    if (src.Type() != tvtObject)
        TVPThrowExceptionMessage(TJS_W("tTJSAry::Assign(tTJSAry&) is invoked with non tvtObject"));

    return Assign(src.ToVariant());
}

tTJSAry& tTJSAry::Assign(tTJSDic &src)
{
    //TVPAddLog(TJS_W("tTJSAry::Assign(iTJSDic&)"));
    if (src.Type() != tvtObject)
        TVPThrowExceptionMessage(TJS_W("tTJSAry::Assign(tTJSDic&) is invoked with non tvtObject"));

    return Assign(src.ToVariant());
}


void tTJSAry::SetProp(tjs_int index, tTJSVariant &value)
{
    iTJSDispatch2 *ary = GetAryNoAddRef();
    ary->PropSetByNum(TJS_MEMBERENSURE, index, &value, ary);
}

tTJSVariant tTJSAry::GetProp(tjs_int index)
{
    if (index < 0 || GetSize() <= index) {
        ttstr str = TJS_W("tTJSAry::GetProp(")+ttstr(index)+TJS_W(") is not in the range of 0-")+ttstr(GetSize()-1);
        TVPThrowExceptionMessage(str.c_str());
    }
    iTJSDispatch2 *ary = GetAryNoAddRef();
    tTJSVariant value;
    ary->PropGetByNum(TJS_MEMBERENSURE, index, &value, ary);
    return value;
}

tjs_int tTJSAry::GetSize() const
{
    iTJSDispatch2 *ary = GetAryNoAddRef();
    tTJSVariant val;
    ary->PropGet(0, TJS_W("count"), NULL, &val, ary);
    return (tjs_int)val.AsInteger();
}

void tTJSAry::SetSize(tjs_int count)
{
    iTJSDispatch2 *ary = GetAryNoAddRef();
    tTJSVariant val(count);
    // I believe this can remove unnecessary elements
    ary->PropSet(0, TJS_W("count"), NULL, &val, ary);
}



// for accessing array elements
tTJSVariant tTJSAry::operator [](tjs_int index)
{
    return GetProp(index);
}

tTJSVariant tTJSAry::GetLast()
{
    return GetProp(GetSize()-1);
}


tTJSVariant tTJSAry::GetPrev()
{
    return GetProp(GetSize()-2);
}

void tTJSAry::Push(tTJSVariant &var)
{
    SetProp(GetSize(), var);
}

tTJSVariant tTJSAry::Pop()
{
    if (GetSize() <= 0)
        TVPThrowExceptionMessage(TJS_W("tTJSAry::Pop() stack under run"));
    tTJSVariant v = this[GetSize()-1];
    SetSize(GetSize()-1);
    return v;
}


// store this object onto "dic.ary_entry"
void tTJSAry::Store(iTJSDispatch2 *savedic, const tjs_char *ary_entry)
{
    iTJSDispatch2 *tmpary = savedic;
    if (ary_entry) {
        tmpary = TJSCreateArrayObject();
        tTJSVariant tmp(tmpary, tmpary);
        tmpary->Release();
        savedic->PropSet(TJS_MEMBERENSURE, ary_entry, NULL, &tmp, savedic);
    }

    tTJSVariant src(*this);
    tTJSVariant *psrc = &src;
    tmpary->FuncCall(0, TJS_W("assignStruct"), NULL, NULL, 1, &psrc, tmpary);
}

// restore this object from "dic.ary_entry"
void tTJSAry::Restore(iTJSDispatch2 *loaddic, const tjs_char *ary_entry)
{
    iTJSDispatch2 *tmpary = loaddic;
    if (ary_entry) {
        tTJSVariant tmp;
        loaddic->PropGet(TJS_MEMBERENSURE, ary_entry, NULL, &tmp, loaddic);
        tmpary = tmp.AsObjectNoAddRef();
    }
    tTJSVariant src(tmpary, tmpary);
    tTJSVariant *psrc = &src;
    GetAryNoAddRef()->FuncCall(0, TJS_W("assignStruct"), NULL, NULL, 1, &psrc, GetAryNoAddRef());
}
