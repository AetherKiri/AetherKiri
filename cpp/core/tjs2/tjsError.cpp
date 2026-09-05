//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// TJS2's C++ exception class and exception message
//---------------------------------------------------------------------------
#include "tjsCommHead.h"

#include "tjsScriptBlock.h"
#include "tjsError.h"
#include "tjs.h"

#include <spdlog/spdlog.h>

#define TJS_MAX_TRACE_TEXT_LEN 1500

namespace TJS {

    //---------------------------------------------------------------------------
    // TJSGetExceptionObject : retrieves TJS 'Exception' object
    //---------------------------------------------------------------------------
    void TJSGetExceptionObject(tTJS *tjs, tTJSVariant *res, tTJSVariant &msg,
                               tTJSVariant *trace /* trace is optional */) {
        if(!res)
            return; // not prcess

        // retrieve class "Exception" from global
        iTJSDispatch2 *global = tjs->GetGlobalNoAddRef();
        tTJSVariant val;
        static tTJSString Exception_name(TJS_W("Exception"));
        tjs_error hr = global->PropGet(0, Exception_name.c_str(),
                                       Exception_name.GetHint(), &val, global);
        if(TJS_FAILED(hr))
            TJS_eTJSError(TJSExceptionNotFound);
        // create an Exception object
        iTJSDispatch2 *excpobj;
        tTJSVariantClosure clo = val.AsObjectClosureNoAddRef();
        tTJSVariant *pmsg = &msg;
        hr =
            clo.CreateNew(0, nullptr, nullptr, &excpobj, 1, &pmsg, clo.ObjThis);
        if(TJS_FAILED(hr))
            TJS_eTJSError(TJSExceptionNotFound);
        if(trace) {
            static tTJSString trace_name(TJS_W("trace"));
            excpobj->PropSet(TJS_MEMBERENSURE, trace_name.c_str(),
                             trace_name.GetHint(), trace, excpobj);
        }
        *res = tTJSVariant(excpobj, excpobj);
        excpobj->Release();
    }
    //---------------------------------------------------------------------------

    //---------------------------------------------------------------------------
    // eTJSScriptException
    //---------------------------------------------------------------------------
    eTJSScriptError::tScriptBlockHolder::tScriptBlockHolder(
        tTJSScriptBlock *block) {
        Block = block;
        if(Block)
            Block->AddRef();
    }

    //---------------------------------------------------------------------------
    eTJSScriptError::tScriptBlockHolder::~tScriptBlockHolder() {
        if(Block)
            Block->Release();
    }

    //---------------------------------------------------------------------------
    eTJSScriptError::tScriptBlockHolder::tScriptBlockHolder(
        const tScriptBlockHolder &holder) {
        Block = holder.Block;
        if(Block)
            Block->AddRef();
    }

    //---------------------------------------------------------------------------
    eTJSScriptError::eTJSScriptError(const ttstr &Msg, tTJSScriptBlock *block,
                                     tjs_int pos) :
        eTJSError(Msg), Block(block), Position(pos) {}

    //---------------------------------------------------------------------------
    tjs_int eTJSScriptError::GetSourceLine() const {
        return Block.Block ? Block.Block->SrcPosToLine(Position) + 1 : 0;
    }

    //---------------------------------------------------------------------------
    const tjs_char *eTJSScriptError::GetBlockName() const {
        if(!Block.Block)
            return TJS_W("");
        const tjs_char *name = Block.Block->GetName();
        return name ? name : TJS_W("");
    }

    //---------------------------------------------------------------------------
    bool eTJSScriptError::AddTrace(tTJSScriptBlock *block, tjs_int srcpos) {
        tjs_int len = Trace.GetLen();
        if(len >= TJS_MAX_TRACE_TEXT_LEN)
            return false;

        if(len != 0)
            Trace += TJS_W("\n<-- ");
        Trace += block ? block->GetLineDescriptionString(srcpos)
                       : ttstr(TJS_W("(expression)"));

        return true;
    }

    //---------------------------------------------------------------------------
    bool eTJSScriptError::AddTrace(tTJSInterCodeContext *context,
                                   tjs_int codepos) {
        tjs_int len = Trace.GetLen();
        if(len >= TJS_MAX_TRACE_TEXT_LEN)
            return false;

        if(len != 0)
            Trace += TJS_W(" <-- ");
        Trace += context->GetPositionDescriptionString(codepos);

        return true;
    }

    //---------------------------------------------------------------------------
    bool eTJSScriptError::AddTrace(const ttstr &data) {
        tjs_int len = Trace.GetLen();
        if(len >= TJS_MAX_TRACE_TEXT_LEN)
            return false;
        if(len != 0)
            Trace += TJS_W(" <-- ");
        Trace += data;
        return true;
    }
    //---------------------------------------------------------------------------

    //---------------------------------------------------------------------------
    // throw helper functions
    //---------------------------------------------------------------------------
    //---------------------------------------------------------------------------
    static void TJSReportExceptionSource(const ttstr &msg,
                                         const tTJSScriptBlock *block,
                                         tjs_int srcpos) {
        if(!block)
            return;
        {
            tTJS *tjs = block->GetTJS();
            tjs->OutputExceptionToConsole(
                (msg + TJS_W(" at ") + block->GetLineDescriptionString(srcpos))
                    .c_str());
        }
    }

    //---------------------------------------------------------------------------
    static void TJSReportExceptionSource(const ttstr &msg,
                                         const tTJSInterCodeContext *context,
                                         tjs_int codepos) {
        if(!context)
            return;
        {
            tTJS *tjs = context->GetTJS();
            if(tjs)
                tjs->OutputExceptionToConsole(
                    (msg + TJS_W(" at ") +
                     context->GetPositionDescriptionString(codepos))
                        .c_str());
        }
    }

    //---------------------------------------------------------------------------
    void TJS_eTJS() { throw eTJS(); }

    //---------------------------------------------------------------------------
    void TJS_eTJSError(const ttstr &msg) { throw eTJSError(msg); }

    void TJS_eTJSError(const tjs_char *msg) { TJS_eTJSError(ttstr{ msg }); }

    //---------------------------------------------------------------------------
    void TJS_eTJSVariantError(const ttstr &msg) { throw eTJSVariantError(msg); }

    void TJS_eTJSVariantError(const tjs_char *msg) {
        throw eTJSVariantError(msg);
    }

    //---------------------------------------------------------------------------
    void TJS_eTJSScriptError(const ttstr &msg, tTJSScriptBlock *block,
                             tjs_int srcpos) {
        TJSReportExceptionSource(msg, block, srcpos);
        throw eTJSScriptError(msg, block, srcpos);
    }

    //---------------------------------------------------------------------------
    void TJS_eTJSScriptError(const tjs_char *msg, tTJSScriptBlock *block,
                             tjs_int srcpos) {
        TJSReportExceptionSource(msg, block, srcpos);
        throw eTJSScriptError(msg, block, srcpos);
    }

    //---------------------------------------------------------------------------
    void TJS_eTJSScriptError(const ttstr &msg, tTJSInterCodeContext *context,
                             tjs_int codepos) {
        TJSReportExceptionSource(msg, context, codepos);
        throw eTJSScriptError(msg, context ? context->GetBlock() : nullptr,
                              context ? context->CodePosToSrcPos(codepos) : 0);
    }

    //---------------------------------------------------------------------------
    void TJS_eTJSScriptError(const tjs_char *msg, tTJSInterCodeContext *context,
                             tjs_int codepos) {
        TJSReportExceptionSource(msg, context, codepos);
        throw eTJSScriptError(msg, context ? context->GetBlock() : nullptr,
                              context ? context->CodePosToSrcPos(codepos) : 0);
    }

    //---------------------------------------------------------------------------
    void TJS_eTJSScriptException(const ttstr &msg, tTJSScriptBlock *block,
                                 tjs_int srcpos, tTJSVariant &val) {
        TJSReportExceptionSource(msg, block, srcpos);
        throw eTJSScriptException(msg, block, srcpos, val);
    }

    //---------------------------------------------------------------------------
    void TJS_eTJSScriptException(const tjs_char *msg, tTJSScriptBlock *block,
                                 tjs_int srcpos, tTJSVariant &val) {
        TJSReportExceptionSource(msg, block, srcpos);
        throw eTJSScriptException(msg, block, srcpos, val);
    }

    //---------------------------------------------------------------------------
    void TJS_eTJSScriptException(const ttstr &msg,
                                 tTJSInterCodeContext *context, tjs_int codepos,
                                 tTJSVariant &val) {
        TJSReportExceptionSource(msg, context, codepos);
        throw eTJSScriptException(
            msg, context ? context->GetBlock() : nullptr,
            context ? context->CodePosToSrcPos(codepos) : 0, val);
    }

    //---------------------------------------------------------------------------
    void TJS_eTJSScriptException(const tjs_char *msg,
                                 tTJSInterCodeContext *context, tjs_int codepos,
                                 tTJSVariant &val) {
        TJSReportExceptionSource(msg, context, codepos);
        throw eTJSScriptException(
            msg, context ? context->GetBlock() : nullptr,
            context ? context->CodePosToSrcPos(codepos) : 0, val);
    }

    //---------------------------------------------------------------------------
    void TJS_eTJSCompileError(const ttstr &msg, tTJSScriptBlock *block,
                              tjs_int srcpos) {
        TJSReportExceptionSource(msg, block, srcpos);
        throw eTJSCompileError(msg, block, srcpos);
    }

    //---------------------------------------------------------------------------
    void TJS_eTJSCompileError(const tjs_char *msg, tTJSScriptBlock *block,
                              tjs_int srcpos) {
        TJSReportExceptionSource(msg, block, srcpos);
        throw eTJSCompileError(msg, block, srcpos);
    }
    //---------------------------------------------------------------------------

    //---------------------------------------------------------------------------
    void TJSThrowFrom_tjs_error(tjs_error hr, const tjs_char *name) {
        // raise an exception descripted as tjs_error
        // name = variable name ( otherwide it can be nullptr )

        switch(hr) {
            case TJS_E_MEMBERNOTFOUND: {
                if(name) {
                    ttstr str(TJSMemberNotFound);
                    str.Replace(TJS_W("%1"), name);
                    TJS_eTJSError(str);
                } else {
                    TJS_eTJSError(TJSMemberNotFoundNoNameGiven);
                }
            }
            case TJS_E_NOTIMPL:
                TJS_eTJSError(TJSNotImplemented);
            case TJS_E_INVALIDPARAM:
                TJS_eTJSError(TJSInvalidParam);
            case TJS_E_BADPARAMCOUNT:
                TJS_eTJSError(TJSBadParamCount);
            case TJS_E_INVALIDTYPE:
                TJS_eTJSError(TJSInvalidType);
            case TJS_E_ACCESSDENYED:
                TJS_eTJSError(TJSAccessDenyed);
            case TJS_E_INVALIDOBJECT:
                TJS_eTJSError(TJSInvalidObject);
            case TJS_E_NATIVECLASSCRASH:
                TJS_eTJSError(TJSNativeClassCrash);
            default:
                if(TJS_FAILED(hr)) {
                    // FIXME:
                    TJS_eTJSError(fmt::sprintf(
                        ttstr{ TJSUnknownFailure }.AsNarrowStdString(), hr));
                }
        }
    }
    //---------------------------------------------------------------------------

    //---------------------------------------------------------------------------
    // error messages  ( can be localized )
    //---------------------------------------------------------------------------
    ttstr TJSNonamedException = TJS_W("No-named exception");
    //---------------------------------------------------------------------------
} // namespace TJS

#undef tjsErrorH
#undef __TJS_ERROR_INC_H__
