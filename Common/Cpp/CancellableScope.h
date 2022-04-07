/*  Cancellable Scope
 *
 *  From: https://github.com/PokemonAutomation/Arduino-Source
 *
 *      A cancellable scope is a node with a tree. If "cancel()" is called on
 *  a scope, only the scope and all child scopes will be canceled. Parents are
 *  not affected.
 *
 *  This is used in nested async-cancel routines.
 *
 *  If the user stops the program, "cancel()" is called on the root node which
 *  will propagate down the entire tree.
 *
 *  If a subroutine cancels due to an inference trigger, it ends just that
 *  scope and passes control up to the parent.
 *
 *  The lifetime of a parent must entirely enclose that of the children and
 *  attached cancellables. This must hold even when an exception is thrown.
 *
 */

#ifndef PokemonAutomation_CancellableScope_H
#define PokemonAutomation_CancellableScope_H

#include <chrono>
#include <atomic>
#include "Pimpl.h"
#include "LifetimeSanitizer.h"

namespace PokemonAutomation{


class CancellableScope;

class Cancellable{
public:
    virtual ~Cancellable(){
        detach();
    }

    CancellableScope* scope() const;

    bool cancelled() const;

    void throw_if_cancelled();
    void throw_if_parent_cancelled();

    //  Returns true if it was already cancelled.
    virtual bool cancel() noexcept;


protected:
    //  This class needs special handling for subclasses.
    //
    //  1.  The constructors of all sub-classes should be protected except for
    //      the most-derived class.
    //
    //  2.  The most-derived class must be marked final.
    //
    //  3.  The most-derived class must call "attach()" at the end of its
    //      constructor.
    //
    //  4.  The most-derived class must call "detach()" at the start of its
    //      destructor.
    //
    //  The moment you attach to a scope, the scope may call "cancel()" on you
    //  at any time - even before you are done constructing. Therefore you must
    //  not attach until you are done constructing.
    //
    //  Because "cancel()" can be called on you at any time, you must detach
    //  before you begin destructing.
    Cancellable()
        : m_cancelled(false)
    {}

    //  You must call last in the constructor of the most-derived subclass.
    void attach(CancellableScope& scope);

    //  You must call this first in the destructor of the most-derived subclass.
    void detach() noexcept;


private:
    CancellableScope* m_scope = nullptr;
    std::atomic<bool> m_cancelled;
    LifetimeSanitizer m_sanitizer;
};



struct CancellableScopeData;
class CancellableScope : public Cancellable{
public:
    virtual ~CancellableScope() override;

    virtual bool cancel() noexcept override;

    void wait_for(std::chrono::milliseconds duration);
    void wait_until(std::chrono::system_clock::time_point stop);

protected:
    CancellableScope();

private:
    friend class Cancellable;
    void operator+=(Cancellable& cancellable);
    void operator-=(Cancellable& cancellable);

private:
    Pimpl<CancellableScopeData> m_impl;
    LifetimeSanitizer m_sanitizer;
};



template <typename CancellableType>
class CancellableHolder final : public CancellableType{
public:
    //  Construct with no parent.
    template <class... Args>
    CancellableHolder(Args&&... args)
        : CancellableType(std::forward<Args>(args)...)
    {}

    //  Construct with a parent.
    template <class... Args>
    CancellableHolder(CancellableScope& parent, Args&&... args)
        : CancellableType(std::forward<Args>(args)...)
    {
        this->attach(parent);
    }

    virtual ~CancellableHolder(){
        this->detach();
    }
};






}
#endif