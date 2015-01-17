// -*- mode: c++ -*-

#pragma once
#include <windows.h>
#include <dshow.h>

#include "WebCamoo.h"

class FiltaaInputPin;

class Filtaa : IBaseFilter
{
private:
    int _refCount;
    FILTER_STATE _state;
    IReferenceClock* _clock;
    IFilterGraph* _graph;
    FiltaaInputPin* _pIn;
    FiltaaInputPin* _pOut;
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
        { fprintf(stderr,"Run\n"); _state = State_Running; return S_OK; }
    STDMETHODIMP Pause()
        { fprintf(stderr,"Pause\n"); _state = State_Paused; return S_OK; }
    STDMETHODIMP Stop()
        { fprintf(stderr,"Stop\n"); _state = State_Stopped; return S_OK; }
    STDMETHODIMP GetSyncSource(IReferenceClock** ppClock) {
        if (ppClock == NULL) return E_POINTER;
        if (_clock != NULL) {
            _clock->AddRef();
        }
        (*ppClock) = _clock;
        return S_OK;
    }
    STDMETHODIMP SetSyncSource(IReferenceClock* pClock) {
        if (pClock != NULL) {
            pClock->AddRef();
        }
        if (_clock != NULL) {
            _clock->Release();
        }
        _clock = pClock;
        if (_clock != NULL) {
            //_clock->AddRef(); // already done.
        }
        return S_OK;
    }

    // IBaseFilter methods
    STDMETHODIMP QueryVendorInfo(LPWSTR* pVendorInfo)
        { return E_NOTIMPL; }
    STDMETHODIMP JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName);
    STDMETHODIMP EnumPins(IEnumPins** ppEnum);
    STDMETHODIMP FindPin(LPCWSTR Id, IPin** ppPin);
    STDMETHODIMP QueryFilterInfo(FILTER_INFO* pInfo);
    
};
