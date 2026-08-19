
#ifndef BROWSER_PROXY_SERVICE_FACTORY_H_
#define BROWSER_PROXY_SERVICE_FACTORY_H_

#include <memory>

#include "base/memory/singleton.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "tanya/components/proxy/browser/tanya_proxy_service.h"

namespace content {
class BrowserContext;
}

namespace tanya {

class TanyaProxyService;

class TanyaProxyServiceFactory : public ProfileKeyedServiceFactory {
 public:

  static TanyaProxyService* GetForProfile(content::BrowserContext* context);

  static TanyaProxyServiceFactory* GetInstance();

 private:
  friend struct base::DefaultSingletonTraits<TanyaProxyServiceFactory>;

  TanyaProxyServiceFactory();
  ~TanyaProxyServiceFactory() override;

  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  

#endif  
