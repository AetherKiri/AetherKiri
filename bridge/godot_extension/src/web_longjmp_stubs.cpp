#include <cstddef>
#include <cstdint>
#include <cstdlib>

#if defined(__EMSCRIPTEN__)
namespace std {
inline namespace __2 {

__attribute__((weak)) size_t __hash_memory(const void *ptr, size_t size) noexcept {
    const auto *bytes = static_cast<const unsigned char *>(ptr);
    size_t hash = 2166136261u;
    size_t prime = 16777619u;
    if constexpr (sizeof(size_t) == 8) {
        hash = 14695981039346656037ull;
        prime = 1099511628211ull;
    }
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= prime;
    }
    return hash;
}

__attribute__((weak)) size_t __next_prime(size_t value) {
    if (value <= 2) {
        return value == 0 ? 0 : 2;
    }
    if ((value & 1u) == 0) {
        ++value;
    }
    for (;;) {
        bool prime = true;
        for (size_t factor = 3; factor <= value / factor; factor += 2) {
            if (value % factor == 0) {
                prime = false;
                break;
            }
        }
        if (prime) {
            return value;
        }
        value += 2;
    }
}

}
}

extern "C" {

void *eglGetProcAddress(const char *) {
    return nullptr;
}

void *eglGetDisplay(void *) {
    return nullptr;
}

int eglInitialize(void *, int *, int *) {
    return 0;
}

int eglTerminate(void *) {
    return 0;
}

int eglChooseConfig(void *, const int *, void **, int, int *) {
    return 0;
}

int eglGetConfigAttrib(void *, void *, int, int *) {
    return 0;
}

void *eglCreateContext(void *, void *, void *, const int *) {
    return nullptr;
}

int eglDestroyContext(void *, void *) {
    return 0;
}

void *eglCreateWindowSurface(void *, void *, void *, const int *) {
    return nullptr;
}

int eglDestroySurface(void *, void *) {
    return 0;
}

int eglMakeCurrent(void *, void *, void *, void *) {
    return 0;
}

int eglSwapBuffers(void *, void *) {
    return 0;
}

int eglSwapInterval(void *, int) {
    return 0;
}

int eglWaitNative(int) {
    return 0;
}

int eglWaitGL() {
    return 0;
}

int eglBindAPI(unsigned int) {
    return 0;
}

const char *eglQueryString(void *, int) {
    return nullptr;
}

int eglGetError() {
    return 0x3000;
}

}
#endif
