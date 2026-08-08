//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Script Event Handling and Dispatching
//---------------------------------------------------------------------------
#ifndef EventImplH
#define EventImplH

#include "EventIntf.h"

//---------------------------------------------------------------------------
// Release platform event producer threads for every embedded title session.
// The legacy AtExit registry only runs once per process and is therefore not
// sufficient when the host returns to its library and opens another title.
extern void TVPResetEventPlatformForHostSession();

//---------------------------------------------------------------------------
#endif
