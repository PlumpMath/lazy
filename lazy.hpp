#include <ucontext.h>
#include <memory>

#include <iostream>
#include <string>

namespace lazy {

namespace impl {
// Todo make this platform independent
template<class P>
constexpr auto chunkSize(P) {
  return 2;
}


// Todo make this platform independent
template<class P>
constexpr auto chunkOf(P p, size_t i) {
  return reinterpret_cast<size_t>(p) >> (32 * i);
}


template<class F, class Handler>
struct CoroutineBase {
  CoroutineBase(F func, size_t stackSize = SIGSTKSZ)
    : stack{new unsigned char[stackSize]}
    , func{func} {
    getcontext(&callee);
    callee.uc_link = &caller;
    callee.uc_stack.ss_size = stackSize;
    callee.uc_stack.ss_sp = stack.get();
    makecontext(
      &callee,
      reinterpret_cast<void (*)()>(&coroutine_call),
      chunkSize(this),
      chunkOf(this, 1),
      chunkOf(this, 0)
    );
  }

  ~CoroutineBase(){}
  CoroutineBase(CoroutineBase const&) = delete;
  CoroutineBase & operator=(CoroutineBase const&) = delete;
  CoroutineBase(CoroutineBase &&) = default;
  CoroutineBase & operator=(CoroutineBase &&) = default;

  bool done() const {
    return isDone;
  }

  operator bool() const {
    return !isDone;
  }

  void release() {
    swapcontext(&callee, &caller);
  }

  void digest() {
    swapcontext(&caller, &callee);
  }


  template<class...IN>
  auto operator()(IN...in) {
    return handler.take(this, in...);
  }

  template<class...OUT>
  auto yield(OUT...out) {
    return handler.yield(this, out...);
  }

private:
  ucontext_t caller;
  ucontext_t callee;
  std::unique_ptr<unsigned char[]> stack;
  F func;
  Handler handler;
  bool isDone = false;

  static void coroutine_call (uint32_t lhs,  uint32_t rhs) {
    auto& self = *reinterpret_cast<CoroutineBase*>(
      (static_cast<size_t>(lhs) << 32) + rhs
    );

    self.func([&](auto...x) {
      return self.yield(x...);
    });

    self.isDone = true;
  }
};

template<class IN = void, class OUT = void>
struct CoHandle;

template<>
struct CoHandle<void,void> {

  template<class Co, class...X>
  static void take(Co co, X...) {
    co->digest();
  }

  template<class Co, class...X>
  static void yield(Co co, X...) {
    co->release();
  }
};

template<class IN, class OUT>
struct CoHandle {

  template<class Co>
  OUT take(Co co, IN in) {
    if(co->done()) throw "nothing to yield";
    this->in = in;
    co->digest();
    return this->out;
  }

  template<class Co>
  IN yield(Co co, OUT out) {
    this->out = out;
    co->release();
    return this->in;
  }

  IN in;
  OUT out;
};


template<class IN>
struct CoHandle<IN, void> {

  template<class Co>
  void take(Co co, IN in) {
    if(co->done()) throw "nothing to yield";
    this->in = in;
    co->digest();
  }

  template<class Co, class...X>
  IN yield(Co co, X...) {
    co->release();
    return this->in;
  }

  IN in;
};



template<class OUT>
struct CoHandle<void, OUT> {

  template<class Co, class...X>
  OUT take(Co co, X...) {
    if(co->done()) throw "nothing to yield";
    co->digest();
    return this->out;
  }

  template<class Co>
  void yield(Co co, OUT out) {
    this->out = out;
    co->release();
  }

  OUT out;
};

}

template<
  class IN = void,
  class OUT = void,
  class F>
auto co(F f) {
  return impl::CoroutineBase<F, impl::CoHandle<IN, OUT> >{f};
}


/*example :
auto y = co<int,float>([i=0](auto yield) mutable {
  while(1) {
    i+=yield(i);
  }
});

  auto x = lazy::co<float>( [](auto yield) {
     while(1) {
        yield(42);
      }

});
*/

}
