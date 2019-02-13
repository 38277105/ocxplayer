/*
 * Version:  1.0
 * Author:   Juan Zhou
 * Date-Time:2017-08-22 14:41:30
 * Description:the safety obj to avoid JScript to popup warning or error tip-box about
 *             safety
 */
#include <QUuid>
#include "safetyimpl.h"
long CSafetyImp::queryInterface(const QUuid &iid, void **iface)
{
    *iface = 0;
    if(IID_IObjectSafety == iid)
    {
        *iface = (IObjectSafety *)this;
    }
    else
    {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}
HRESULT WINAPI CSafetyImp::GetInterfaceSafetyOptions(REFIID riid,DWORD *pwSupporteOptions,DWORD *pdwEnabledOptions)
{
    *pwSupporteOptions = INTERFACESAFE_FOR_UNTRUSTED_DATA | INTERFACESAFE_FOR_UNTRUSTED_CALLER;
    *pdwEnabledOptions = *pwSupporteOptions;
    return S_OK;
}
 HRESULT WINAPI CSafetyImp::SetInterfaceSafetyOptions(REFIID riid,DWORD wSupporteOptions,DWORD dwEnabledOptions)
 {
     return S_OK;
 }
