
#ifndef SAFETYIMPL_H
#define SAFETYIMPL_H
#include <QAxAggregated>
#include <objsafe.h>
class CSafetyImp : public QAxAggregated, public IObjectSafety
{
public:
    long queryInterface(const QUuid &iid, void **iface);
    QAXAGG_IUNKNOWN
    HRESULT WINAPI GetInterfaceSafetyOptions(REFIID riid,DWORD *pwSupporteOptions,DWORD *pdwEnabledOptions);
    HRESULT WINAPI SetInterfaceSafetyOptions(REFIID riid,DWORD wSupporteOptions,DWORD dwEnabledOptions);
};
#endif // SAFETYIMPL_H
