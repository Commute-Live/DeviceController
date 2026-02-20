#include "core/transit_provider_registry.h"

#include <string.h>

namespace core {

TransitProviderRegistry::TransitProviderRegistry() : providers_{}, count_(0) {}

bool TransitProviderRegistry::register_provider(const ProviderStyle &style) {
  if (count_ >= kMaxProviders) {
    return false;
  }
  providers_[count_++] = style;
  return true;
}

const ProviderStyle *TransitProviderRegistry::find(const char *providerId) const {
  if (!providerId) {
    return nullptr;
  }
  for (size_t i = 0; i < count_; ++i) {
    if (strncmp(providers_[i].providerId, providerId, sizeof(providers_[i].providerId)) == 0) {
      return &providers_[i];
    }
  }
  return nullptr;
}

size_t TransitProviderRegistry::size() const { return count_; }

}  // namespace core
