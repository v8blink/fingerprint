
#include "tanya/browser/tanya_proxy_service_factory.h"

namespace content {
class BrowserContext;
}

namespace tanya {

TanyaProxyServiceFactory* TanyaProxyServiceFactory::GetInstance() {
  return base::Singleton<TanyaProxyServiceFactory>::get();
}

TanyaProxyService* TanyaProxyServiceFactory::GetForProfile(
    content::BrowserContext* context) {
  return static_cast<TanyaProxyService*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

TanyaProxyServiceFactory::TanyaProxyServiceFactory()
    : ProfileKeyedServiceFactory(
          "TanyaProxyService",
          ProfileSelections::BuildForRegularAndIncognito()) {
}

TanyaProxyServiceFactory::~TanyaProxyServiceFactory() = default;

std::unique_ptr<KeyedService>
TanyaProxyServiceFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  return std::make_unique<TanyaProxyService>();
}

}  
