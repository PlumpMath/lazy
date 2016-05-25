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
  auto operator()(IN&&...in) {
    return handler.take(this, std::move(in)...);
  }

  template<class...OUT>
  auto yield(OUT&&...out) {
    return handler.yield(this, std::move(out)...);
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

    self.func([&](auto&&...x) {
      return self.yield(x...);
    });

    self.isDone = true;
  }
};

template<class IN = void, class OUT = void>
struct CoHandle;

template<>
struct CoHandle<void,void> {

  template<class Co>
  static void take(Co co) {
    co->digest();
  }

  template<class Co>
  static void yield(Co co) {
    co->release();
  }
};

template<class IN, class OUT>
struct CoHandle {

  template<class Co>
  OUT take(Co co, IN&& in) {
    if(co->done()) throw "nothing to yield";
    this->in = std::move(in);
    co->digest();
    return std::move(this->out);
  }

  template<class Co>
  IN yield(Co co, OUT&& out) {
    this->out = std::move(out);
    co->release();
    return std::move(this->in);
  }

  IN in;
  OUT out;
};


template<class IN>
struct CoHandle<IN, void> {

  template<class Co>
  void take(Co co, IN&& in) {
    if(co->done()) throw "nothing to yield";
    in = std::move(in);
    co->digest();
  }

  template<class Co >
  IN yield(Co co) {
    co->release();
    return std::move(this->in);
  }

  IN in;
};



template<class OUT>
struct CoHandle<void, OUT> {

  template<class Co>
  OUT take(Co co) {
    if(co->done()) throw "nothing to yield";
    co->digest();
    return std::move(this->out);
  }

  template<class Co>
  void yield(Co co, OUT const out) {
    out = std::move(out);
    co->release();
  }

  OUT out;
};

}

template<
  class OUT = void,
  class IN = void,
  class F>
auto co(F f) {
  return impl::CoroutineBase<F, impl::CoHandle<OUT, IN> >{f};
}


/*example :
auto y = co<int,float>([i=0](auto yield) mutable {
  while(1) {
    i+=yield(i);
  }
});

y(1); y(2);

*/


}
