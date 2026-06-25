//---------------------------------------------------------------------------
/*
        TJS2 Script Engine
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Intermediate Code Execution
//---------------------------------------------------------------------------
#pragma once

namespace TJS {
    extern void TJSRememberSaveLoadFileStoreObject(iTJSDispatch2 *object);

    extern tjs_error TJSInvokeRememberedSaveLoadFileStoreItem(tjs_int item_index);

    extern void TJSVariantArrayStackCompact();

    extern void TJSVariantArrayStackCompactNow();

    extern void TJSSetGlobalVariantArrayStack(class tTJSVariantArrayStack *stack);

    class tTJSVariantArrayStack {

        struct tVariantArray {
            tTJSVariant *Array;
            tjs_int Using;
            tjs_int Allocated;
        };

        tVariantArray *Arrays; // array of array
        tjs_int NumArraysAllocated;
        tjs_int NumArraysUsing;
        tVariantArray *Current;
        tjs_int CompactVariantArrayMagic;
        tjs_int OperationDisabledCount;

        void IncreaseVariantArray(tjs_int num);

        void DecreaseVariantArray();

        void InternalCompact();

    public:
        tTJSVariantArrayStack();

        ~tTJSVariantArrayStack();

        tTJSVariant *Allocate(tjs_int num);

        void Deallocate(tjs_int num, tTJSVariant *ptr);

        void Compact() { InternalCompact(); }
    };
    //---------------------------------------------------------------------------
} // namespace TJS
