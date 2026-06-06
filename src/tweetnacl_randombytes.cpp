#include <cstddef>
#include <cstdint>
#include <mutex>
#include <random>

extern "C" void randombytes(unsigned char* output, unsigned long long length) {
  static std::mutex mutex;
  static std::random_device randomDevice;

  std::lock_guard<std::mutex> lock(mutex);

  for (unsigned long long index = 0; index < length; ++index) {
    output[index] = static_cast<unsigned char>(randomDevice() & 0xffu);
  }
}
