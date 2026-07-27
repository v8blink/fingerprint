
#include "tanya/components/proxy/browser/tanya_proxy_service.h"

#include <string>

namespace tanya {

TanyaProxyService::TanyaProxyService() = default;

TanyaProxyService::~TanyaProxyService() = default;

void TanyaProxyService::SetProxyFor(
    int64_t tabId,
    std::unique_ptr<net::TanyaProxy> proxy,
    bool canModify) {

  proxies_[tabId] = std::move(proxy);
  canModify_[tabId] = canModify;
  NotifyProxyChanged(tabId);
}

net::TanyaProxy* TanyaProxyService::GetProxyFor(int64_t tabId) {
  if (tabId < 0) {
    return nullptr;
  }

  auto it = proxies_.find(tabId);
  if (it == proxies_.end()) {
    return nullptr;
  }
  return it->second.get();
}

net::TanyaProxy* TanyaProxyService::GetAnyActiveProxy() {
  if (proxies_.empty()) {
    return nullptr;
  }
  return proxies_.begin()->second.get();
}

void TanyaProxyService::DropProxyFor(int64_t tabId) {
  auto it = proxies_.find(tabId);
  if (it == proxies_.end()) {
    return;
  }

  NotifyProxyChanged(tabId);
  proxies_.erase(it);
  canModify_.erase(tabId);
}

void TanyaProxyService::NotifyProxyChanged(int64_t tabId) {
  for (auto& observer : observers_) {
    observer.OnProxyChanged(tabId);
  }
}

void TanyaProxyService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void TanyaProxyService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

}  
