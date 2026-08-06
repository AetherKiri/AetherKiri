#include "tjsCommHead.h"
#include "TJSDic.h"
#include "TJSAry.h"
#include "tjsDictionary.h"
#include "ScriptMgnIntf.h"
#include "MsgIntf.h"

// =========================================================

// these are the real definition of static member variables.
iTJSDispatch2 *tTJSDic::DicAssign;
iTJSDispatch2 *tTJSDic::DicAssignStruct;
iTJSDispatch2 *tTJSDic::DicClear;

// Function definitions below:


// initialize DicClear/DicAssign, would be better to lock to avoid multi/overwriting
void tTJSDic::init_tTJSDic() {
    if (DicClear == NULL /* || DicAssign == NULL */) {
        tTJSVariant dic;
        TVPExecuteExpression(TJS_W("Dictionary"), &dic);
        iTJSDispatch2 *dicclass = dic.AsObjectNoAddRef();

        tjs_error err;
        tTJSVariant val;

        err = dicclass->PropGet(0, TJS_W("clear"), NULL, &val, dicclass);
        if (TJS_FAILED(err))
            TVPThrowExceptionMessage(TJS_W("tTJSDic: can not get 'clear' method of Dictionary"));
        DicClear = val.AsObject();

        err = dicclass->PropGet(0, TJS_W("assign"), NULL, &val, dicclass);
        if (TJS_FAILED(err))
            TVPThrowExceptionMessage(TJS_W("tTJSDic: can not get 'assign' method of Dictionary"));
        DicAssign = val.AsObject();

        err = dicclass->PropGet(0, TJS_W("assignStruct"), NULL, &val, dicclass);
        if (TJS_FAILED(err))
            TVPThrowExceptionMessage(TJS_W("tTJSDic: can not get 'assignStruct' method of Dictionary"));
        DicAssignStruct = val.AsObject();
    }

    iTJSDispatch2 *dic = TJSCreateDictionaryObject();
    SetObject(dic, dic);
    dic->Release();
}


// constructors
tTJSDic::tTJSDic() : tTJSVariant()
{
    init_tTJSDic();
}

tTJSDic::tTJSDic(tTJSVariant &srcdic) : tTJSVariant()
{
    if (srcdic.Type() == tvtVoid)
        TVPThrowExceptionMessage(TJS_W("tTJSDic::tTJSDic(tTJSVariant&) is invoked with void dic"));
    init_tTJSDic();
    Assign(srcdic);
}

tTJSDic::tTJSDic(tTJSDic &srcdic) : tTJSVariant()
{
    if (srcdic.Type() == tvtVoid)
        TVPThrowExceptionMessage(TJS_W("tTJSDic::tTJSDic(tTJSDic&) is invoked with void dic"));
    init_tTJSDic();
    Assign(srcdic);
}


// destructor
//tTJSDic::~tTJSDic()
//{
// We need to release DicClear/DicAssign at the time all of
// tTJSDic instances are destructed, however, currently
// Ignore it...
//
// if (--RefCount <= 0) {
//	RefCount = 0;
//	if(DicClear)  DicClear->Release();
//	if(DicAssign) DicAssign->Release();
// }
//}

// methods
void tTJSDic::Empty()
{
    DicClear->FuncCall(0, NULL, NULL, NULL, 0, NULL, GetDicNoAddRef());
}

tTJSDic& tTJSDic::Assign(iTJSDispatch2 *srcdic)
{
    if (srcdic == NULL)
        TVPThrowExceptionMessage(TJS_W("tTJSDic::Assign(tTJSDispatch*) is invoked with NULL dic"));

    tTJSVariant src(srcdic, srcdic);
    return Assign(src);
}

tTJSDic& tTJSDic::Assign(tTJSVariant &srcdic)
{
    if (srcdic.Type() != tvtObject)
        TVPThrowExceptionMessage(TJS_W("tTJSDic::Assign(tTJSVariant&) is invoked with non tvtObject"));

    tTJSVariant *psrc = &srcdic;
    DicAssign->FuncCall(0, NULL, NULL, NULL, 1, &psrc, GetDicNoAddRef());
    return *this;
}

tTJSDic& tTJSDic::Assign(tTJSDic &srcdic)
{
    if (srcdic.Type() == tvtVoid)
        TVPThrowExceptionMessage(TJS_W("tTJSDic::Assign(tTJSDic&) is invoked with non tvtObject"));

    tTJSVariant *psrc = &srcdic;
    DicAssign->FuncCall(0, NULL, NULL, NULL, 1, &psrc, GetDicNoAddRef());
    return *this;
}


void tTJSDic::SetProp(const ttstr &name, tTJSVariant &value)
{
    iTJSDispatch2 *dic = GetDicNoAddRef();
    dic->PropSetByVS(TJS_MEMBERENSURE, name.AsVariantStringNoAddRef(), &value, dic);
}


tTJSVariant tTJSDic::GetProp(const tjs_char *name)
{
    iTJSDispatch2 *dic = GetDicNoAddRef();
    tTJSVariant val;
    dic->PropGet(0, name, NULL, &val, dic);
    return val;
}

tTJSVariant tTJSDic::GetProp(ttstr &name)
{
    iTJSDispatch2 *dic = GetDicNoAddRef();
    tTJSVariant val;
    dic->PropGet(0, name.c_str(), name.GetHint(), &val, dic);
    return val;
}


tjs_error tTJSDic::DelProp(const tjs_char *name)
{
    iTJSDispatch2 *dic = GetDicNoAddRef();
    return dic->DeleteMember(0, name, NULL, dic);
}

tjs_error tTJSDic::DelProp(ttstr &name)
{
    iTJSDispatch2 *dic = GetDicNoAddRef();
    return dic->DeleteMember(0, name.c_str(), name.GetHint(), dic);
}


tTJSAry tTJSDic::GetKeys()
{
    tTJSAry ary(*this);
    tTJSAry ret;
    for (tjs_int i = 0; i < ary.GetSize()/2; i++) {
        tTJSVariant v = ary.GetProp(i*2);
        ret.SetProp(i, v);
    }
    return ret;
}

void tTJSDic::AddDic(tTJSDic &sdic)
{
    tTJSAry keyary(sdic.GetKeys());
    for (tjs_int i = 0; i < keyary.GetSize(); i++)
    {
        ttstr key(keyary.GetProp(i));
        tTJSVariant tmp = sdic.GetProp(key);
        SetProp(key, tmp);
    }
}


// store this object onto savedic
void tTJSDic::Store(iTJSDispatch2 *savedic, const tjs_char *dic_entry)
{
    iTJSDispatch2 *tmpdic = savedic;
    if (dic_entry) {
        tmpdic = TJSCreateDictionaryObject();
        tTJSVariant tmp(tmpdic, tmpdic);
        tmpdic->Release();
        savedic->PropSet(TJS_MEMBERENSURE, dic_entry, NULL, &tmp, savedic);
    }

    tTJSVariant src(*this);
    tTJSVariant *psrc = &src;
    DicAssignStruct->FuncCall(0, NULL, NULL, NULL, 1, &psrc, tmpdic);
}

// store this object onto saveary
void tTJSDic::Store(iTJSDispatch2 *saveary, tjs_int index)
{
    iTJSDispatch2 *tmpdic = TJSCreateDictionaryObject();
    tTJSVariant tmp(tmpdic, tmpdic);
    tmpdic->Release();
    saveary->PropSetByNum(TJS_MEMBERENSURE, index, &tmp, saveary);

    tTJSVariant src(*this);
    tTJSVariant *psrc = &src;
    DicAssignStruct->FuncCall(0, NULL, NULL, NULL, 1, &psrc, tmpdic);
}

// restore this object from loaddic
void tTJSDic::Restore(iTJSDispatch2 *loaddic, const tjs_char *dic_entry)
{
    if (dic_entry == NULL) {
        tTJSVariant src(loaddic, loaddic);
        tTJSVariant *psrc = &src;
        DicAssignStruct->FuncCall(0, NULL, NULL, NULL, 1, &psrc, GetDicNoAddRef());
        return;
    }

    tTJSVariant tmp;
    loaddic->PropGet(TJS_MEMBERENSURE, dic_entry, NULL, &tmp, loaddic);
    if (tmp.Type() == tvtVoid) {
        // leave current one, do not clear it.
        // this is necessary to handle the case
        // "kag.conductor.macro === void", means "saveMacros=false"
        return;
    }
    if (tmp.Type() != tvtObject) {
        ttstr mes = TJS_W("tTJSDic::Restore(loaddic, \"")+ttstr(dic_entry)+TJS_W("\"): is not dictionary");
        TVPThrowExceptionMessage(mes.c_str());
    }
    tTJSVariant *psrc = &tmp;
    DicAssignStruct->FuncCall(0, NULL, NULL, NULL, 1, &psrc, GetDicNoAddRef());
}

// restore this object from loaddic
void tTJSDic::Restore(iTJSDispatch2 *loadary, tjs_int index)
{
    tTJSVariant tmp;
    loadary->PropGetByNum(TJS_MEMBERENSURE, index, &tmp, loadary);
    if (tmp.Type() == tvtVoid) {
        // leave current one.
        // this is necessary to handle the case
        // "kag.conductor.macro === void".
        return;
    }
    if (tmp.Type() != tvtObject) {
        ttstr mes = TJS_W("tTJSDic::Restore(loadary, \"")+ttstr(index)+TJS_W("\"): is not dictionary");
        TVPThrowExceptionMessage(mes.c_str());
    }
    tTJSVariant *psrc = &tmp;
    DicAssignStruct->FuncCall(0, NULL, NULL, NULL, 1, &psrc, GetDicNoAddRef());
}
