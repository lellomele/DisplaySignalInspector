#include <windows.h>
#include <wbemidl.h>
#include <cwchar>
#include <string>
#include <memory>

template<class T> struct Release { void operator()(T *p) const { p->Release(); } };
template<class T> using Com = std::unique_ptr<T, Release<T>>;
struct Value : VARIANT {
    Value() { VariantInit(this); }
    ~Value() { VariantClear(this); }
};
struct Bstr {
    BSTR value;
    Bstr(const wchar_t *s) : value(SysAllocString(s)) {}
    ~Bstr() { SysFreeString(value); }
    operator BSTR() const { return value; }
};

static unsigned read_blocks(const wchar_t *device, BYTE *out, unsigned capacity) {
    std::wstring id(device);
    if (id.size()<5 || id.substr(0,4)!=L"\\\\?\\") return 0;
    id.erase(0,4);
    auto end=id.rfind(L"#{");
    if(end==std::wstring::npos) return 0;
    id.resize(end);
    for(auto &c:id) if(c==L'#') c=L'\\';
    id+=L"_";
    IWbemLocator *rawLocator=nullptr;
    if(FAILED(CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_IWbemLocator,(void**)&rawLocator))) return 0;
    Com<IWbemLocator> locator(rawLocator);
    IWbemServices *rawService=nullptr;
    if(FAILED(locator->ConnectServer(Bstr(L"ROOT\\WMI"),nullptr,nullptr,nullptr,0,nullptr,nullptr,&rawService))) return 0;
    Com<IWbemServices> service(rawService);
    if(FAILED(CoSetProxyBlanket(service.get(),RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE))) return 0;
    const wchar_t *cls=L"WmiMonitorDescriptorMethods", *method=L"WmiGetMonitorRawEEdidV1Block";
    IEnumWbemClassObject *rawEnum=nullptr;
    if(FAILED(service->CreateInstanceEnum(Bstr(cls),WBEM_FLAG_FORWARD_ONLY,nullptr,&rawEnum))) return 0;
    Com<IEnumWbemClassObject> objects(rawEnum);
    for(;;) {
        IWbemClassObject *raw=nullptr; ULONG count=0;
        if(FAILED(objects->Next(1500,1,&raw,&count)) || !count) return 0;
        Com<IWbemClassObject> obj(raw); Value name,path;
        if(FAILED(obj->Get(L"InstanceName",0,&name,nullptr,nullptr)) || name.vt!=VT_BSTR || _wcsnicmp(name.bstrVal,id.c_str(),id.size())) continue;
        if(FAILED(obj->Get(L"__PATH",0,&path,nullptr,nullptr)) || path.vt!=VT_BSTR) return 0;
        IWbemClassObject *rawClass=nullptr,*rawDef=nullptr,*rawArgs=nullptr;
        if(FAILED(service->GetObject(Bstr(cls),0,nullptr,&rawClass,nullptr))) return 0;
        Com<IWbemClassObject> definition(rawClass);
        if(FAILED(definition->GetMethod(method,0,&rawDef,nullptr))) return 0;
        Com<IWbemClassObject> params(rawDef);
        if(FAILED(params->SpawnInstance(0,&rawArgs))) return 0;
        Com<IWbemClassObject> args(rawArgs);
        unsigned blocks=1,i=0;
        for(;i<blocks && i<capacity;i++){
            Value index;index.vt=VT_UI1;index.bVal=(BYTE)i;
            if(FAILED(args->Put(L"BlockId",0,&index,0))) break;
            IWbemClassObject *rawResult=nullptr;
            if(FAILED(service->ExecMethod(path.bstrVal,Bstr(method),0,nullptr,args.get(),&rawResult,nullptr))) break;
            Com<IWbemClassObject> result(rawResult);Value data;
            if(FAILED(result->Get(L"BlockContent",0,&data,nullptr,nullptr)) || data.vt!=(VT_ARRAY|VT_UI1) || SafeArrayGetDim(data.parray)!=1 || data.parray->rgsabound[0].cElements!=128) break;
            BYTE *bytes=nullptr;
            if(FAILED(SafeArrayAccessData(data.parray,(void**)&bytes))) break;
            memcpy(out+128*i,bytes,128);SafeArrayUnaccessData(data.parray);
            if(!i) blocks=1+out[126];
        }
        return i;
    }
}
extern "C" unsigned read_edid(const wchar_t *device,BYTE *out,unsigned capacity) {
    HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(hr) && hr!=RPC_E_CHANGED_MODE) return 0;
    unsigned n=0;
    try { n=read_blocks(device,out,capacity); } catch(...) {}
    if(SUCCEEDED(hr)) CoUninitialize();
    return n;
}
