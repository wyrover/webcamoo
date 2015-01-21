// -*- mode: c++ -*-

#pragma once
#include <windows.h>
#include <dshow.h>

#include "WebCamoo.h"

class FiltaaInputPin;

class Filtaa : public IBaseFilter
{
private:
    int _refCount;
    LPCWSTR _name;
    FILTER_STATE _state;
    IReferenceClock* _clock;
    IFilterGraph* _graph;
    FiltaaInputPin* _pIn;
    FiltaaInputPin* _pOut;
    AM_MEDIA_TYPE _mediatype;
    IMemInputPin* _transport;
    IMemAllocator* _allocatorIn;
    IMemAllocator* _allocatorOut;
    
    ~Filtaa();
    
public:
    Filtaa();
    
    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID iid, void** ppvObject);
    STDMETHODIMP_(ULONG) AddRef() {
        _refCount++; return _refCount;
    }
    STDMETHODIMP_(ULONG) Release() {
        _refCount--;
        if (_refCount) return _refCount;
        delete this;
        return 0;
    }

    // IPersist methods
    STDMETHODIMP GetClassID(CLSID* pClassID)
        { return E_FAIL; }

    // IMediaFilter methods
    STDMETHODIMP GetState(DWORD dwMilliSecsTimeout, FILTER_STATE* pState) {
        if (pState == NULL) return E_POINTER;
        *pState = _state; return S_OK;
    }
    STDMETHODIMP Run(REFERENCE_TIME tStart)
        { _state = State_Running; return S_OK; }
    STDMETHODIMP Pause()
        { _state = State_Paused; return S_OK; }
    STDMETHODIMP Stop()
        { _state = State_Stopped; return S_OK; }

    STDMETHODIMP GetSyncSource(IReferenceClock** ppClock);
    STDMETHODIMP SetSyncSource(IReferenceClock* pClock);

    // IBaseFilter methods
    STDMETHODIMP QueryVendorInfo(LPWSTR* pVendorInfo)
        { return E_NOTIMPL; }
    STDMETHODIMP JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName);
    STDMETHODIMP EnumPins(IEnumPins** ppEnum);
    STDMETHODIMP FindPin(LPCWSTR Id, IPin** ppPin);
    STDMETHODIMP QueryFilterInfo(FILTER_INFO* pInfo);

    // Others
    const AM_MEDIA_TYPE* GetMediaType();
    HRESULT Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* mt);
    HRESULT ReceiveConnection(const AM_MEDIA_TYPE* mt);
    HRESULT DisconnectInput();
    HRESULT DisconnectOutput();
    HRESULT BeginFlush();
    HRESULT EndFlush();
    HRESULT EndOfStream();
    HRESULT NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate);
    
    HRESULT GetAllocator(IMemAllocator** ppAllocator);
    HRESULT NotifyAllocator(IMemAllocator* pAllocator, BOOL bReadOnly);
    HRESULT Receive(IMediaSample* pSample);
    HRESULT Transform(IMediaSample* pSample);
};
