#pragma once

#include <stddef.h>
#include <stdint.h>

namespace core {

struct ProviderStyle {
  char providerId[24];
  uint16_t routeColor;
  uint16_t backgroundColor;
  uint16_t etaColor;
};

class TransitProviderRegistry final {
 public:
  static constexpr size_t kMaxProviders = 16;

  TransitProviderRegistry();
  bool register_provider(const ProviderStyle &style);
  const ProviderStyle *find(const char *providerId) const;
  size_t size() const;

 private:
  ProviderStyle providers_[kMaxProviders];
  size_t count_;
};

}  // namespace core
