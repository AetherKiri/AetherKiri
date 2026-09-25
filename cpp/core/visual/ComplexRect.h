//---------------------------------------------------------------------------
/*
        TVP2 ( T Visual Presenter 2 )  A script authoring tool
        Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

        See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Complex Rectangle Class
//---------------------------------------------------------------------------

#ifndef ComplexRectUnitH
#define ComplexRectUnitH

#include <stdlib.h>
#include "tjsTypes.h"
// tTVPPoint/tTVPPointD/tTVPRect (plus the TVPIntersectRect/TVPUnionRect
// declarations) moved to the shared GPU bridge ABI header
// bridge/engine_api/include/engine_gpu_bridge.h so the Godot extension and
// the engine runtimes share one geometry definition.
#include "engine_gpu_bridge.h"

//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPRegionRect : a class for a rectangle in region
//---------------------------------------------------------------------------
class tTVPRegionRect;
extern tTVPRegionRect *TVPAllocateRegionRect();
extern void TVPDeallocateRegionRect(tTVPRegionRect *rect);
//---------------------------------------------------------------------------
class tTVPRegionRect : public tTVPRect {
public: // data members
    tTVPRegionRect *Prev; // previous link
    tTVPRegionRect *Next; // next link

public: // new and delete
    void *operator new(size_t size) { return TVPAllocateRegionRect(); }
    void operator delete(void *p) {
        TVPDeallocateRegionRect((tTVPRegionRect *)p);
    }

public: // constructors and destructor
    tTVPRegionRect() {};
    tTVPRegionRect(const tTVPRect &r) : tTVPRect(r) {};
    ~tTVPRegionRect() {};

public: // link operations
    void LinkAfter(tTVPRegionRect *r) {
        // Insert this after r.
        tTVPRegionRect *n = r->Next;
        r->Next = this;
        n->Prev = this;
        Prev = r;
        Next = n;
    }

    void LinkBefore(tTVPRegionRect *r) {
        // Insert this before r.
        tTVPRegionRect *p = r->Prev;
        r->Prev = this;
        p->Next = this;
        Prev = p;
        Next = r;
    }

    void Unlink() {
        // unchain from the link list
        tTVPRegionRect *prev = Prev;
        tTVPRegionRect *next = Next;
        prev->Next = next;
        next->Prev = prev;
    }
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPComplexRect
//---------------------------------------------------------------------------
class tTVPComplexRect {
public: // iterator
    class tIterator {
    private: // data members
        const tTVPRegionRect *Head;
        const tTVPRegionRect *Current;

    public: // constructor and destructor
        tIterator() : Head(nullptr), Current(nullptr) {}
        tIterator(const tTVPRegionRect *head) : Head(head), Current(nullptr) {
            ;
        }

    public: // operator function (data access)
        const tTVPRect &operator*() const { return *Current; }
        const tTVPRect *operator->() const { return Current; }

        const tTVPRegionRect &Get() const { return *Current; }

    public: // stepping forward; this object supports only forward
            // step. method step returns true if stepping successful,
            // otherwise returns false (when already at last of the
            // list) sample:
            //   tTVPComplexRect::tIterator it = rects.GetIterator();
            //   while(it.Step()) { .. do something with it .. }
        bool Step() {
            // Step forward
            if(!Head)
                return false;
            if(!Current) {
                Current = Head;
                return true;
            }
            if(Current->Next == Head) {
                return false;
            }
            Current = Current->Next;
            return true;
        }
    };

private: // data members
    tTVPRegionRect *Head; // head of the link list
    tTVPRegionRect *Current; // a rectangle which is touched last time
    tjs_int Count; // rectangle Count
    tTVPRect Bound; // bounding rectangle
    bool BoundValid; // whether the bounding rectangle is ready to use

public: // constructors and destructors
    tTVPComplexRect();
    tTVPComplexRect(const tTVPComplexRect &ref);
    ~tTVPComplexRect();

public: // storage management
    void Clear();

private: // storage management
    void FreeAllRectangles(); // free all rectangles
    void Init(); // initialize internal states
    void SetCount(tjs_int count); // grow or shrink rectangle storage area
    bool Insert(const tTVPRect &rect); // insert inplace
    void Remove(tTVPRegionRect *rect); // remove a rectangle
    void Merge(
        const tTVPComplexRect &rects); // merge non-overlaped complex rectangle
public:
    tjs_int GetCount() const { return Count; }

public: // logical operations
    void Or(const tTVPRect &r);
    void Or(const tTVPComplexRect &ref);
    void Sub(const tTVPRect &r);
    void Sub(const tTVPComplexRect &ref);
    void And(const tTVPRect &r);

public: // operation utilities
    void CopyWithOffsets(const tTVPComplexRect &ref, const tTVPRect &clip,
                         tjs_int ofsx, tjs_int ofsy);

public: // bounding rectangle
    const tTVPRect &GetBound() const {
        (const_cast<tTVPComplexRect *>(this))->EnsureBound();
        return Bound;
    }

    void Unite() {
        // make union (bounding) one rectangle
        tTVPRect r(GetBound());
        Clear();
        Or(r);
    }

private:
    void EnsureBound() {
        if(!BoundValid)
            CalcBound();
    }
    void CalcBound();

private: // geometric rectangle operations
    tjs_int GetRectangleIntersectionCode(const tTVPRect &r,
                                         const tTVPRect &rr) {
        // Retrieve condition code which represents
        // how two rectangles have the intersection.
        tjs_int cond;
        if(rr.left <= r.left && rr.right >= r.left)
            cond = 8;
        else
            cond = 0;
        if(rr.left <= r.right && rr.right >= r.right)
            cond |= 4;
        if(rr.top <= r.top && rr.bottom >= r.top)
            cond |= 2;
        if(rr.top <= r.bottom && rr.bottom >= r.bottom)
            cond |= 1;
        /*
                           +8             +4

                        +------+ +---+ +------+
                        |rr    | |rr | |    rr|     +2
                        |    +-----------+    |
                        +----|-+ +---+ +-|----+
                        +----|-+       +-|----+
                        |rr  | |   r   | |  rr|
                        +----|-+       +-|----+
                        +----|-+ +---+ +-|----+
                        |    +-----------+    |     +1
                        |rr    | | rr| |    rr|
                        +------+ +---+ +------+
        */
        return cond;
    }

    void RectangleSub(tTVPRegionRect *r, const tTVPRect *rr);

public:
    void AddOffsets(tjs_int x, tjs_int y);

public: // iterator
    tIterator GetIterator() const {
        if(Count)
            return { Head };
        else
            return { nullptr };
    }

public: // debug
    void DumpChain();
};
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
// tTVPComplexRectIterator : iterator for walking over rectangles
//---------------------------------------------------------------------------
//---------------------------------------------------------------------------

#endif
