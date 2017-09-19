// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef ABSL_BASE_INTERNAL_ATOMIC_HOOK_H_
#define ABSL_BASE_INTERNAL_ATOMIC_HOOK_H_

#include <cassert>
#include <atomic>
#include <utility>

namespace absl {
namespace base_internal {

// In current versions of MSVC (as of July 2017), a std::atomic<T> where T is a
// pointer to function cannot be constant-initialized with an address constant
// expression.  That is, the following code does not compile:
//   void NoOp() {}
//   constexpr std::atomic<void(*)()> ptr(NoOp);
//
// This is the only compiler we support that seems to have this issue.  We
// conditionalize on MSVC here to use a fallback implementation.  But we
// should revisit this occasionally.  If MSVC fixes this compiler bug, we
// can then change this to be conditionalized on the value on _MSC_FULL_VER
// instead.
#ifdef _MSC_FULL_VER
#define ABSL_HAVE_FUNCTION_ADDRESS_CONSTANT_EXPRESSION 0
#else
#define ABSL_HAVE_FUNCTION_ADDRESS_CONSTANT_EXPRESSION 1
#endif

template <typename T>
class AtomicHook;

// AtomicHook is a helper class, templatized on a raw function pointer type, for
// implementing Abseil customization hooks.  It is a callable object that
// dispatches to the registered hook, or performs a no-op (and returns a default
// constructed object) if no hook has been registered.
//
// Reads and writes guarantee memory_order_acquire/memory_order_release
// semantics.
template <typename ReturnType, typename... Args>
class AtomicHook<ReturnType (*)(Args...)> {
 public:
  using FnPtr = ReturnType (*)(Args...);

  constexpr AtomicHook() : hook_(DummyFunction) {}

  // Stores the provided function pointer as the value for this hook.
  //
  // This is intended to be called once.  Multiple calls are legal only if the
  // same function pointer is provided for each call.  The store is implemented
  // as a memory_order_release operation, and read accesses are implemented as
  // memory_order_acquire.
  void Store(FnPtr fn) {
    assert(fn);
    FnPtr expected = DummyFunction;
    hook_.compare_exchange_strong(expected, fn, std::memory_order_acq_rel,
                                  std::memory_order_acquire);
    // If the compare and exchange failed, make sure that's because hook_ was
    // already set to `fn` by an earlier call.  Any other state reflects an API
    // violation (calling Store() multiple times with different values).
    //
    // Avoid ABSL_RAW_CHECK, since raw logging depends on AtomicHook.
    assert(expected == DummyFunction || expected == fn);
  }

  // Invokes the registered callback.  If no callback has yet been registered, a
  // default-constructed object of the appropriate type is returned instead.
  template <typename... CallArgs>
  ReturnType operator()(CallArgs&&... args) const {
    FnPtr hook = hook_.load(std::memory_order_acquire);
    if (ABSL_HAVE_FUNCTION_ADDRESS_CONSTANT_EXPRESSION || hook) {
      return hook(std::forward<CallArgs>(args)...);
    } else {
      return ReturnType();
    }
  }

  // Returns the registered callback, or nullptr if none has been registered.
  // Useful if client code needs to conditionalize behavior based on whether a
  // callback was registered.
  //
  // Note that atomic_hook.Load()() and atomic_hook() have different semantics:
  // operator()() will perform a no-op if no callback was registered, while
  // Load()() will dereference a null function pointer.  Prefer operator()() to
  // Load()() unless you must conditionalize behavior on whether a hook was
  // registered.
  FnPtr Load() const {
    FnPtr ptr = hook_.load(std::memory_order_acquire);
    return (ptr == DummyFunction) ? nullptr : ptr;
  }

 private:
#if ABSL_HAVE_FUNCTION_ADDRESS_CONSTANT_EXPRESSION
  static ReturnType DummyFunction(Args...) {
    return ReturnType();
  }
#else
  static constexpr FnPtr DummyFunction = nullptr;
#endif

  std::atomic<FnPtr> hook_;
};

#undef ABSL_HAVE_FUNCTION_ADDRESS_CONSTANT_EXPRESSION

}  // namespace base_internal
}  // namespace absl

#endif  // ABSL_BASE_INTERNAL_ATOMIC_HOOK_H_
