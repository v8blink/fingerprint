#ifndef TANYA_PROXY_SERVICE_H_
#define TANYA_PROXY_SERVICE_H_

#include <stdint.h>
#include <memory>
#include <string>
#include <unordered_map>

#include "base/functional/callback.h"
#include "base/observer_list.h"
#include "net/base/proxy_server.h"
#include "components/keyed_service/core/keyed_service.h"
#include "tanya/net/tanya_proxy.h"

namespace content {
class BrowserContext;
}

class GURL;
class PrefService;

namespace tanya {

class TanyaProxyService : public KeyedService {
 public:
  TanyaProxyService();

  TanyaProxyService(const TanyaProxyService&) = delete;
  TanyaProxyService& operator=(const TanyaProxyService&) = delete;
  ~TanyaProxyService() override;

  using ProxyGetter = base::RepeatingCallback<net::TanyaProxy*(void)>;

  class Observer: public base::CheckedObserver  {
      public:
      virtual void OnProxyChanged(int64_t tabId) {}
  };

  void SetProxyFor(int64_t tabId,
                   std::unique_ptr<net::TanyaProxy> proxy,
                   bool canModify = true);
  net::TanyaProxy* GetProxyFor(int64_t tabId);
  void DropProxyFor(int64_t tabId);

  net::TanyaProxy* GetAnyActiveProxy();

  void AddObserver(Observer* observer);
  void RemoveObserver(Observer* observer);

 private:
  void NotifyProxyChanged(int64_t tabId);

  base::ObserverList<Observer> observers_;

  std::unordered_map<int64_t, std::unique_ptr<net::TanyaProxy>> proxies_;
  std::unordered_map<int64_t, bool> canModify_;

};

}  

#endif
