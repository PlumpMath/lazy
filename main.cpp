#include <ucontext.h>
#include <cstdint>
#include <memory>
#include <functional>


namespace lazy {

template<class F, class OUT, class IN>
struct coroutine {
  coroutine(F func, size_t stack_size = SIGSTKSZ)
    : stack{new unsigned char[stack_size]}
    , func{func} {
    getcontext(&callee);
    callee.uc_link = &caller;
    callee.uc_stack.ss_size = stack_size;
    callee.uc_stack.ss_sp = stack.get();
    makecontext(
      &callee,
      reinterpret_cast<void (*)()>(&coroutine_call),
      2,
      reinterpret_cast<size_t>(this) >> 32,
      this
    );
  }

  coroutine(const coroutine &) = delete;
  coroutine & operator=(const coroutine &) = delete;
  coroutine(coroutine &&) = default;
  coroutine & operator=(coroutine &&) = default;


  OUT operator()(IN in) {
    this->in = in;
    swapcontext(&caller, &callee);
    return this->out;
  }

  operator bool() const {
    return !done;
  }

  IN yield(OUT out) {
    this->out = out;
    swapcontext(&callee, &caller);
    return this->in;
  }

private:
  ucontext_t caller;
  ucontext_t callee;
  std::unique_ptr<unsigned char[]> stack;
  F func;
  bool done = false;
  IN in;
  OUT out;

  static void coroutine_call (
    uint32_t this_pointer_left_half,
    uint32_t this_pointer_right_half
  ) {

    coroutine & self = *reinterpret_cast<coroutine *>(
      (static_cast<size_t>(this_pointer_left_half) << 32) +
      this_pointer_right_half
    );

    self.func([&](auto...x) {
      return self.yield(x...);
    });

    self.done = true;
  }
};

template<class OUT, class IN, class F>
auto coro(F f){
  return coroutine<F, OUT, IN>{f};
}

template<class OUT, class F>
auto gen(F f) {
  return [f](auto...x) {
    auto g = [=](auto...y) {
      return f(y...,x...);
    };
    return coro(g);
  };
}

}

#include <iostream>
#include <string>

using namespace lazy;

int main()
{

  auto test = coro<int, std::string>([](auto yield) {
    for (int i = 0; i < 5; ++i) {
      std::cout << "coroutine " << i << std::endl;
      auto put = yield(i);
      std::cout<<"put" << put << std::endl;
    }
  });


  while(test) {
    std::cout << "main" << std::endl;
    auto a =test(std::string("42"));
    std::cout << "ret" << a << std::endl;
  }


  return 0;
}
