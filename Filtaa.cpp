//  Filtaa.cpp

#include <stdio.h>
#include <windows.h>
#include <dshow.h>
#include "Filtaa.h"

static inline size_t align32(size_t x)
{
    return (((x+3) >> 4) << 4);
}

static BOOL isMediaTypeAcceptable(const AM_MEDIA_TYPE* mt)
{
    if (mt->majortype != MEDIATYPE_Video) return FALSE;
    if (mt->subtype != MEDIASUBTYPE_RGB24) return FALSE;
    if (mt->formattype != FORMAT_VideoInfo) return FALSE;
    return TRUE;
}

static BOOL isMediaTypeEqual(const AM_MEDIA_TYPE* mt1, const AM_MEDIA_TYPE* mt2)
{
    if (mt1->majortype != mt2->majortype) return FALSE;
    if (mt1->subtype != mt2->subtype) return FALSE;
    if (mt1->formattype != mt2->formattype) return FALSE;
    if (mt1->cbFormat != mt2->cbFormat) return FALSE;
    if (mt1->pbFormat == mt2->pbFormat) return TRUE;
    if (mt1->pbFormat == NULL || mt2->pbFormat == NULL) return FALSE;
    if (memcmp(mt1->pbFormat, mt2->pbFormat, mt1->cbFormat) != 0) return FALSE;
    return TRUE;
}

static HRESULT copyMediaType(AM_MEDIA_TYPE* dst, const AM_MEDIA_TYPE* src)
{
    if (src == NULL) return E_POINTER;
    if (dst == NULL) return E_POINTER;
    CopyMemory(dst, src, sizeof(*src));
    if (src->cbFormat) {
        BYTE* fmt = (BYTE*)CoTaskMemAlloc(src->cbFormat);
        if (fmt == NULL) return E_OUTOFMEMORY;
        CopyMemory(fmt, src->pbFormat, src->cbFormat);
        dst->pbFormat = fmt;
    }
    return S_OK;
}

static HRESULT eraseMediaType(AM_MEDIA_TYPE* mt)
{
    if (mt == NULL) return E_POINTER;
    if (mt->pbFormat != NULL) {
        CoTaskMemFree(mt->pbFormat);
    }
    return S_OK;
}

static LPCWSTR mt2str(const AM_MEDIA_TYPE* mt)
{
    if (mt == NULL) return L"<null>";
    static WCHAR major[64];
    if (mt->majortype == MEDIATYPE_Video) {
        swprintf_s(major, _countof(major), L"Video");
    } else {
        swprintf_s(major, _countof(major), L"[%08x]", mt->majortype.Data1);
    }
    static WCHAR sub[64];
    if (mt->subtype == MEDIASUBTYPE_RGB24) {
        swprintf_s(sub, _countof(sub), L"RGB24");
    } else if (mt->subtype == MEDIASUBTYPE_RGB555) {
        swprintf_s(sub, _countof(sub), L"RGB555");
    } else if (mt->subtype == MEDIASUBTYPE_RGB565) {
        swprintf_s(sub, _countof(sub), L"RGB565");
    } else {
        swprintf_s(sub, _countof(sub), L"[%08x]", mt->subtype.Data1);
    }
    static WCHAR format[64];
    if (mt->formattype == FORMAT_VideoInfo && mt->cbFormat) {
        VIDEOINFOHEADER* vi = (VIDEOINFOHEADER*)mt->pbFormat;
        swprintf_s(format, _countof(format), L"VideoInfo(%dx%d)",
                   vi->bmiHeader.biWidth, vi->bmiHeader.biHeight);
    } else {
        swprintf_s(format, _countof(format), L"[%08x]", mt->formattype.Data1);
    }
    static WCHAR buf[256];
    swprintf_s(buf, _countof(buf),
               L"<major=%s, sub=%s, size=%lu, format=%s>",
               major, sub, mt->lSampleSize, format);
    return buf;
}

static HRESULT copyBuffer(IMediaSample* dst, IMediaSample* src)
{
    HRESULT hr;
    
    hr = src->IsDiscontinuity();
    hr = dst->SetDiscontinuity((hr == S_OK)? TRUE : FALSE);

    hr = src->IsPreroll();
    hr = dst->SetPreroll((hr == S_OK)? TRUE : FALSE);

    hr = src->IsSyncPoint();
    hr = dst->SetSyncPoint((hr == S_OK)? TRUE : FALSE);

    LONGLONG tStart, tEnd;
    hr = src->GetMediaTime(&tStart, &tEnd);
    if (SUCCEEDED(hr)) {
        hr = dst->SetMediaTime(&tStart, &tEnd);
    }

    REFERENCE_TIME pStart, pEnd;
    hr = src->GetTime(&pStart, &pEnd);
    if (SUCCEEDED(hr)) {
        hr = dst->SetTime(&pStart, &pEnd);
    }

    AM_MEDIA_TYPE* mt = NULL;
    hr = src->GetMediaType(&mt);
    if (mt != NULL) {
        hr = dst->SetMediaType(mt);
        eraseMediaType(mt);
        CoTaskMemFree(mt);
    }

    long size = src->GetActualDataLength();
    dst->SetActualDataLength(size);

    BYTE* pSrc = NULL;
    BYTE* pDst = NULL;
    hr = src->GetPointer(&pSrc);
    hr = dst->GetPointer(&pDst);
    if (pSrc != NULL && pDst != NULL) {
        CopyMemory(pDst, pSrc, size);
    }
    
    return S_OK;
}


// IEnumPins
class FiltaaEnumPins : public IEnumPins
{
private:
    int _refCount;
    IPin* _pins[2];
    int _npins;
    int _index;
    
    ~FiltaaEnumPins() {
        for (int i = 0; i < _npins; i++) {
            _pins[i]->Release();
        }
    }

public:
    FiltaaEnumPins(IPin* pIn, IPin* pOut, int index=0) {
        _refCount = 0;
        _pins[0] = pIn;
        _pins[1] = pOut;
        _npins = 2;
        _index = index;
        for (int i = 0; i < _npins; i++) {
            _pins[i]->AddRef();
        }
        AddRef();
    }

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID iid, void** ppvObject) {
        if (ppvObject == NULL) return E_POINTER;
        if (iid == IID_IUnknown) {
            *ppvObject = this;
        } else if (iid == IID_IEnumPins) {
            *ppvObject = (IEnumPins*)this;
        } else {
            *ppvObject = NULL;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() {
        _refCount++; return _refCount;
    }
    STDMETHODIMP_(ULONG) Release() {
        _refCount--;
        if (_refCount) return _refCount;
        delete this;
        return 0;
    }

    // IEnumPins
    STDMETHODIMP Next(ULONG n, IPin** ppPins, ULONG* pFetched) {
        if (ppPins == NULL) return E_POINTER;
        if (n == 0) return E_INVALIDARG;
        ULONG i = 0;
        while (i < n && _index < _npins) {
            IPin* pin = _pins[_index];
            //fwprintf(stderr, L"EnumPins.Next: %p (%d)\n", pin, _index);
            pin->AddRef();
            ppPins[i++] = pin;
            _index++;
        }
        if (pFetched != NULL) {
            *pFetched = i;
        }
        return (i < n)? S_FALSE : S_OK;
    }
    STDMETHODIMP Skip(ULONG n) {
        while (0 < n--) {
            if (_npins <= _index) return S_FALSE;
            _index++;
        }
        return S_OK;
    }
    STDMETHODIMP Reset() {
        _index = 0;
        return S_OK;
    }
    STDMETHODIMP Clone(IEnumPins** pEnum) {
        if (pEnum == NULL) return E_POINTER;
        *pEnum = new FiltaaEnumPins(_pins[0], _pins[1], _index);
        return S_OK;
    }
};


// FiltaaEnumMediaTypes
class FiltaaEnumMediaTypes : public IEnumMediaTypes
{
private:
    int _refCount;
    AM_MEDIA_TYPE _mts[1];
    int _nmts;
    int _index;

    ~FiltaaEnumMediaTypes() {
        eraseMediaType(&(_mts[0]));
    }

public:
    FiltaaEnumMediaTypes(const AM_MEDIA_TYPE* mt, int index=0) {
        _refCount = 0;
        copyMediaType(&(_mts[0]), mt);
        _nmts = 1;
        _index = index;
        AddRef();
    }
    
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID iid, void** ppvObject) {
        if (ppvObject == NULL) return E_POINTER;
        if (iid == IID_IUnknown) {
            *ppvObject = this;
        } else if (iid == IID_IEnumMediaTypes) {
            *ppvObject = (IEnumMediaTypes*)this;
        } else {
            *ppvObject = NULL;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() {
        _refCount++; return _refCount;
    }
    STDMETHODIMP_(ULONG) Release() {
        _refCount--;
        if (_refCount) return _refCount;
        delete this;
        return 0;
    }

    // IEnumMediaTypes
    STDMETHODIMP Next(ULONG n, AM_MEDIA_TYPE** ppMediaTypes, ULONG* pFetched) {
        if (ppMediaTypes == NULL) return E_POINTER;
        if (n == 0) return E_INVALIDARG;
        ULONG i = 0;
        while (i < n && _index < _nmts) {
            AM_MEDIA_TYPE* src = &(_mts[_index]);
            //fwprintf(stderr, L"EnumMediaTypes.Next: %s (%d)\n", mt2str(src), _index);
            AM_MEDIA_TYPE* dst = (AM_MEDIA_TYPE*)CoTaskMemAlloc(sizeof(AM_MEDIA_TYPE));
            if (dst == NULL) return E_OUTOFMEMORY;
            if (FAILED(copyMediaType(dst, src))) return E_OUTOFMEMORY;
            ppMediaTypes[i++] = dst;
            _index++;
        }
        if (pFetched != NULL) {
            *pFetched = i;
        }
        return (i < n)? S_FALSE : S_OK;
    }
    STDMETHODIMP Skip(ULONG n) {
        while (0 < n--) {
            if (_nmts <= _index) return S_FALSE;
            _index++;
        }
        return S_OK;
    }
    STDMETHODIMP Reset() {
        _index = 0;
        return S_OK;
    }
    STDMETHODIMP Clone(IEnumMediaTypes** pEnum) {
        if (pEnum == NULL) return E_POINTER;
        *pEnum = new FiltaaEnumMediaTypes(_mts, _index);
        return S_OK;
    }
};


//  FiltaaInputPin
//
class FiltaaInputPin : public IPin, public IMemInputPin
{
private:
    int _refCount;
    Filtaa* _filter;
    PIN_DIRECTION _direction;
    LPCWSTR _name;
    IPin* _connected;
    BOOL _flushing;
    
    ~FiltaaInputPin();
    
public:
    FiltaaInputPin(Filtaa* filter, LPCWSTR name, PIN_DIRECTION direction);

    LPCWSTR Name() { return _name; }
    IPin* Connected() { return _connected; }

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

    // IPin methods
    STDMETHODIMP BeginFlush() {
        //fwprintf(stderr, L"InputPin(%s).BeginFlush\n", _name);
        if (_direction != PINDIR_INPUT) return E_UNEXPECTED;
        _flushing = TRUE;
        return _filter->BeginFlush();
    }
    STDMETHODIMP EndFlush() {
        //fwprintf(stderr, L"InputPin(%s).EndFlush\n", _name);
        if (_direction != PINDIR_INPUT) return E_UNEXPECTED;
        _flushing = FALSE;
        return _filter->EndFlush();
    }
    STDMETHODIMP EndOfStream() {
        //fwprintf(stderr, L"InputPin(%s).EndOfStream\n", _name);
        if (_direction != PINDIR_INPUT) return E_UNEXPECTED;
        return _filter->EndOfStream();
    }
    STDMETHODIMP NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate) {
        //fwprintf(stderr, L"InputPin(%s).NewSegment\n", _name);
        return _filter->NewSegment(tStart, tStop, dRate);
    }
    
    STDMETHODIMP Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* pmt);
    STDMETHODIMP ConnectedTo(IPin** ppPin);
    STDMETHODIMP ConnectionMediaType(AM_MEDIA_TYPE* pmt);
    STDMETHODIMP Disconnect();
    STDMETHODIMP ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* pmt);
    STDMETHODIMP EnumMediaTypes(IEnumMediaTypes** ppEnum);
    STDMETHODIMP QueryId(LPWSTR* Id);
    STDMETHODIMP QueryAccept(const AM_MEDIA_TYPE* pmt);
    STDMETHODIMP QueryDirection(PIN_DIRECTION* pPinDir);
    STDMETHODIMP QueryPinInfo(PIN_INFO* pInfo);
    
    STDMETHODIMP QueryInternalConnections(IPin**, ULONG*)
        { return E_NOTIMPL; }

    // IMemInputPin methods
    STDMETHODIMP GetAllocatorRequirements(ALLOCATOR_PROPERTIES* )
        { return E_NOTIMPL; }
    STDMETHODIMP GetAllocator(IMemAllocator** ppAllocator) {
        //fwprintf(stderr, L"InputPin(%s).GetAllocator\n", _name);
        if (_direction != PINDIR_INPUT) return E_UNEXPECTED;
        return _filter->GetAllocator(ppAllocator);
    }
    STDMETHODIMP NotifyAllocator(IMemAllocator* pAllocator, BOOL bReadOnly) {
        //fwprintf(stderr, L"InputPin(%s).NotifyAllocator\n", _name);
        if (_direction != PINDIR_INPUT) return E_UNEXPECTED;
        return _filter->NotifyAllocator(pAllocator, bReadOnly);
    }        
    STDMETHODIMP Receive(IMediaSample* pSample) {
        if (_direction != PINDIR_INPUT) return E_UNEXPECTED;
        return _filter->Receive(pSample);
    }
    STDMETHODIMP ReceiveCanBlock()
        { return S_FALSE; }
    STDMETHODIMP ReceiveMultiple(IMediaSample** pSamples, long nSamples, long* nSamplesProcessed);
    
};

FiltaaInputPin::FiltaaInputPin(Filtaa* filter, LPCWSTR name, PIN_DIRECTION direction)
{
    fwprintf(stderr, L"InputPin(%p,%s): direction=%d\n", this, name, direction);
    _refCount = 0;
    _filter = filter;
    _name = name;
    _direction = direction;
    _connected = NULL;
    _flushing = FALSE;
    AddRef();
}

FiltaaInputPin::~FiltaaInputPin()
{
    fwprintf(stderr, L"~InputPin(%s)\n", _name);
    if (_connected != NULL) {
        _connected->Release();
        _connected = NULL;
    }
}

// IUnknown methods
STDMETHODIMP FiltaaInputPin::QueryInterface(REFIID iid, void** ppvObject)
{
    if (ppvObject == NULL) return E_POINTER;
    if (iid == IID_IUnknown) {
        *ppvObject = this;
    } else if (iid == IID_IPin) {
        *ppvObject = (IPin*)this;
    } else if (iid == IID_IMemInputPin) {
        *ppvObject = (IMemInputPin*)this;
    } else {
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

// IPin methods
STDMETHODIMP FiltaaInputPin::EnumMediaTypes(IEnumMediaTypes** ppEnum)
{
    if (ppEnum == NULL) return E_POINTER;

    const AM_MEDIA_TYPE* mt = _filter->GetMediaType();
    //fwprintf(stderr, L"InputPin(%s).EnumMediaTypes\n", _name);
    if (mt == NULL) return VFW_E_NOT_CONNECTED;
    *ppEnum = (IEnumMediaTypes*) new FiltaaEnumMediaTypes(mt);
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* mt)
{
    HRESULT hr;
    if (pReceivePin == NULL) return E_POINTER;
    if (_direction != PINDIR_OUTPUT) return E_UNEXPECTED;
    if (_connected != NULL) return VFW_E_ALREADY_CONNECTED;
    
    //fwprintf(stderr, L"InputPin(%s).Connect: pin=%p, mt=%s\n", _name, pReceivePin, mt2str(mt));
    hr = _filter->Connect(pReceivePin, mt);
    if (FAILED(hr)) return hr;
    
    // assert(_connected == NULL);
    _connected = pReceivePin;
    _connected->AddRef();
    return hr;
}

STDMETHODIMP FiltaaInputPin::ReceiveConnection(IPin* pConnector, const AM_MEDIA_TYPE* mt)
{
    HRESULT hr;
    if (pConnector == NULL || mt == NULL) return E_POINTER;
    if (_direction != PINDIR_INPUT) return E_UNEXPECTED;
    if (_connected != NULL) return VFW_E_ALREADY_CONNECTED;
    //fwprintf(stderr, L"InputPin(%s).ReceiveConnection: pin=%p, mt=%s\n", _name, pConnector, mt2str(mt));
    
    hr = _filter->ReceiveConnection(mt);
    if (FAILED(hr)) return hr;
    
    _connected = pConnector;
    _connected->AddRef();
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::ConnectedTo(IPin** ppPin)
{
    if (ppPin == NULL) return E_POINTER;
    *ppPin = _connected;
    if (_connected == NULL) return VFW_E_NOT_CONNECTED;
    (*ppPin)->AddRef();
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::ConnectionMediaType(AM_MEDIA_TYPE* mt)
{
    if (mt == NULL) return E_POINTER;
    if (_connected == NULL) return VFW_E_NOT_CONNECTED;
    return copyMediaType(mt, _filter->GetMediaType());
}

STDMETHODIMP FiltaaInputPin::Disconnect()
{
    fwprintf(stderr, L"InputPin(%s).Disconnect\n", _name);
    if (_connected == NULL) return S_FALSE;
    if (_direction == PINDIR_INPUT) {
        _filter->DisconnectInput();
    } else {
        _filter->DisconnectOutput();
    }
    _connected->Release();
    _connected = NULL;
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::QueryId(LPWSTR* Id)
{
    HRESULT hr;
    if (Id == NULL) return E_POINTER;
    LPWSTR dst = (LPWSTR)CoTaskMemAlloc(sizeof(WCHAR)*(lstrlen(_name)+1));
    if (dst == NULL) return E_OUTOFMEMORY;
    lstrcpy(dst, _name);
    *Id = dst;
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::QueryAccept(const AM_MEDIA_TYPE* mt)
{
    return (isMediaTypeAcceptable(mt))? S_OK : S_FALSE;
}

STDMETHODIMP FiltaaInputPin::QueryDirection(PIN_DIRECTION* pPinDir)
{
    if (pPinDir == NULL) return E_POINTER;
    *pPinDir = _direction;
    return S_OK;
}

STDMETHODIMP FiltaaInputPin::QueryPinInfo(PIN_INFO* pInfo)
{
    if (pInfo == NULL) return E_POINTER;
    ZeroMemory(pInfo, sizeof(*pInfo));
    pInfo->pFilter = (IBaseFilter*)_filter;
    if (pInfo->pFilter != NULL) {
        pInfo->pFilter->AddRef();
    }
    pInfo->dir = _direction;
    StringCchCopy(pInfo->achName, _countof(pInfo->achName), _name);
    return S_OK;
}
    
STDMETHODIMP FiltaaInputPin::ReceiveMultiple(
    IMediaSample** pSamples, long nSamples, long* nSamplesProcessed)
{
    HRESULT hr;
    if (pSamples == NULL) return E_POINTER;

    long n = 0;
    for (long i = 0; i < nSamples; i++) {
        hr = Receive(pSamples[i]);
        if (FAILED(hr)) return hr;
        n++;
    }
    if (nSamplesProcessed != NULL) {
        *nSamplesProcessed = n;
    }
    
    return hr;
}



//  Filtaa
//
Filtaa::Filtaa()
{
    _refCount = 0;
    _name = L"Filtaa";
    _state = State_Stopped;
    _clock = NULL;
    _graph = NULL;
    _pIn = new FiltaaInputPin(this, L"In", PINDIR_INPUT);
    _pOut = new FiltaaInputPin(this, L"Out", PINDIR_OUTPUT);
    ZeroMemory(&_mediatype, sizeof(_mediatype));
    _transport = NULL;
    _allocatorIn = NULL;
    _allocatorOut = NULL;
    AddRef();
}

Filtaa::~Filtaa()
{
    eraseMediaType(&_mediatype);
    if (_allocatorIn != NULL) {
        _allocatorIn->Release();
        _allocatorIn = NULL;
    }
    if (_allocatorOut != NULL) {
        _allocatorOut->Release();
        _allocatorOut = NULL;
    }
    if (_transport != NULL) {
        _transport->Release();
        _transport = NULL;
    }
    if (_clock != NULL) {
        _clock->Release();
        _clock = NULL;
    }
    if (_graph != NULL) {
        _graph->Release();
        _graph = NULL;
    }
    if (_pIn != NULL) {
        _pIn->Release();
        _pIn = NULL;
    }
    if (_pOut != NULL) {
        _pOut->Release();
        _pOut = NULL;
    }
}

// IUnknown methods
STDMETHODIMP Filtaa::QueryInterface(REFIID iid, void** ppvObject)
{
    if (ppvObject == NULL) return E_POINTER;
    if (iid == IID_IUnknown) {
        *ppvObject = this;
    } else if (iid == IID_IPersist) {
        *ppvObject = (IPersist*)this;
    } else if (iid == IID_IMediaFilter) {
        *ppvObject = (IMediaFilter*)this;
    } else if (iid == IID_IBaseFilter) {
        *ppvObject = (IBaseFilter*)this;
    } else {
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

// IBaseFilter methods
STDMETHODIMP Filtaa::JoinFilterGraph(IFilterGraph* pGraph, LPCWSTR pName)
{
    fwprintf(stderr, L"Filtaa.JoinFilterGraph: name=%s\n", pName);
    if (pGraph != NULL) {
        pGraph->AddRef();
    }
    if (_graph != NULL) {
        _graph->Release();
    }
    _graph = pGraph;
    return S_OK;
}

STDMETHODIMP Filtaa::EnumPins(IEnumPins** ppEnum)
{
    //fwprintf(stderr, L"Filtaa.EnumPins: %p\n", ppEnum);
    if (ppEnum == NULL) return E_POINTER;
    *ppEnum = (IEnumPins*) new FiltaaEnumPins((IPin*)_pIn, (IPin*)_pOut);
    return S_OK;
}

STDMETHODIMP Filtaa::FindPin(LPCWSTR Id, IPin** ppPin)
{
    //fwprintf(stderr, L"Filtaa.FindPin: Id=%s\n", Id);
    if (Id == NULL) return E_POINTER;
    if (ppPin == NULL) return E_POINTER;
    if (lstrcmp(Id, _pIn->Name()) == 0) {
        *ppPin = (IPin*)_pIn;
        (*ppPin)->AddRef();
    } else if (lstrcmp(Id, _pOut->Name()) == 0) {
        *ppPin = (IPin*)_pOut;
        (*ppPin)->AddRef();
    } else {
        *ppPin = NULL;
        return VFW_E_NOT_FOUND;
    }
    return S_OK;
}

STDMETHODIMP Filtaa::QueryFilterInfo(FILTER_INFO* pInfo)
{
    //fwprintf(stderr, L"Filtaa.QueryFilterInfo\n");
    if (pInfo == NULL) return E_POINTER;
    ZeroMemory(pInfo, sizeof(*pInfo));
    pInfo->pGraph = _graph;
    if (pInfo->pGraph != NULL) {
        pInfo->pGraph->AddRef();
    }
    StringCchCopy(pInfo->achName, _countof(pInfo->achName), _name);
    return S_OK;
}

STDMETHODIMP Filtaa::GetSyncSource(IReferenceClock** ppClock)
{
    //fwprintf(stderr, L"Filtaa.GetSyncSource\n");
    if (ppClock == NULL) return E_POINTER;
    if (_clock != NULL) {
        _clock->AddRef();
    }
    (*ppClock) = _clock;
    return S_OK;
}

STDMETHODIMP Filtaa::SetSyncSource(IReferenceClock* pClock)
{
    //fwprintf(stderr, L"Filtaa.SetSyncSource\n");
    if (pClock != NULL) {
        pClock->AddRef();
    }
    if (_clock != NULL) {
        _clock->Release();
    }
    _clock = pClock;
    return S_OK;
}

// others

const AM_MEDIA_TYPE* Filtaa::GetMediaType()
{
    if (_pIn->Connected() == NULL) {
        return NULL;
    } else {
        return &_mediatype;
    }
}

HRESULT Filtaa::Connect(IPin* pReceivePin, const AM_MEDIA_TYPE* mt)
{
    HRESULT hr;
    if (_state != State_Stopped) return VFW_E_NOT_STOPPED;
    if (_pIn->Connected() == NULL) return VFW_E_NO_ACCEPTABLE_TYPES;
    if (mt != NULL && !isMediaTypeEqual(&_mediatype, mt)) return VFW_E_TYPE_NOT_ACCEPTED;
    
    hr = pReceivePin->ReceiveConnection((IPin*)_pOut, &_mediatype);
    if (FAILED(hr)) return hr;

    hr = pReceivePin->QueryInterface(IID_IMemInputPin, (void**)&_transport);
    if (FAILED(hr)) return hr;

    if (_allocatorOut == NULL) {
        _allocatorOut = _allocatorIn;
        _allocatorOut->AddRef();
    }
    hr = _transport->NotifyAllocator(_allocatorOut, FALSE);
    if (FAILED(hr)) return hr;
    
    return S_OK;
}

HRESULT Filtaa::DisconnectInput()
{
    if (_allocatorIn != NULL) {
        _allocatorIn->Release();
        _allocatorIn = NULL;
    }
    if (_allocatorOut != NULL) {
        _allocatorOut->Release();
        _allocatorOut = NULL;
    }
    return S_OK;
}

HRESULT Filtaa::DisconnectOutput()
{
    if (_transport != NULL) {
        _transport->Release();
        _transport = NULL;
    }
    return S_OK;
}

HRESULT Filtaa::ReceiveConnection(const AM_MEDIA_TYPE* mt)
{
    if (_state != State_Stopped) return VFW_E_NOT_STOPPED;
    if (!isMediaTypeAcceptable(mt)) return VFW_E_TYPE_NOT_ACCEPTED;
    return copyMediaType(&_mediatype, mt);
}

HRESULT Filtaa::BeginFlush()
{
    IPin* pin = _pOut->Connected();
    if (pin != NULL) {
        pin->BeginFlush();
    }
    return S_OK;
}

HRESULT Filtaa::EndFlush()
{
    IPin* pin = _pOut->Connected();
    if (pin != NULL) {
        pin->EndFlush();
    }
    return S_OK;
}

HRESULT Filtaa::EndOfStream()
{
    IPin* pin = _pOut->Connected();
    if (pin != NULL) {
        pin->EndOfStream();
    }
    return S_OK;
}

HRESULT Filtaa::NewSegment(REFERENCE_TIME tStart, REFERENCE_TIME tStop, double dRate)
{
    IPin* pin = _pOut->Connected();
    if (pin != NULL) {
        pin->NewSegment(tStart, tStop, dRate);
    }
    return S_OK;
}


// IMemInputPin methods
HRESULT Filtaa::GetAllocator(IMemAllocator** ppAllocator)
{
    if (ppAllocator == NULL) return E_POINTER;
    fwprintf(stderr, L"Filtaa.GetAllocator\n");
    return CoCreateInstance(
        CLSID_MemoryAllocator, 0, CLSCTX_INPROC_SERVER,
        IID_IMemAllocator, (void**)ppAllocator);
}

HRESULT Filtaa::NotifyAllocator(IMemAllocator* pAllocator, BOOL bReadOnly)
{
    HRESULT hr;
    if (pAllocator == NULL) return E_POINTER;
    fwprintf(stderr, L"Filtaa.NotifyAllocator: readonly=%d\n", bReadOnly);

    pAllocator->AddRef();
    if (_allocatorIn != NULL) {
        _allocatorIn->Release();
    }
    _allocatorIn = pAllocator;

    if (_allocatorOut != NULL) {
        _allocatorOut = NULL;
    }
    if (bReadOnly) {
        // Have my own allocator.
        ALLOCATOR_PROPERTIES prop, actual;
        hr = _allocatorIn->GetProperties(&prop);
        if (FAILED(hr)) return hr;
        // Override pAllocator.
        hr = GetAllocator(&_allocatorOut);
        if (FAILED(hr)) return hr;
        hr = _allocatorOut->SetProperties(&prop, &actual);
        if (FAILED(hr)) return hr;
        hr = _allocatorOut->Commit();
        if (FAILED(hr)) return hr;
    } else {
        _allocatorOut = _allocatorIn;
        _allocatorOut->AddRef();
    }
    return S_OK;
}

HRESULT Filtaa::Receive(IMediaSample* pSample)
{
    HRESULT hr;
    if (pSample == NULL) return E_POINTER;
    
    fwprintf(stderr, L"Filtaa.Receive: %p\n", pSample);
    if (_allocatorIn != _allocatorOut) {
        IMediaSample* tmp = NULL;
        hr = _allocatorOut->GetBuffer(&tmp, NULL, NULL, 0);
        if (FAILED(hr)) return hr;
        hr = copyBuffer(tmp, pSample);
        if (FAILED(hr)) return hr;
        pSample->Release();
        pSample = tmp;
    }
    
    AM_MEDIA_TYPE* mt = NULL;
    hr = pSample->GetMediaType(&mt);
    if (mt == NULL || isMediaTypeEqual(&_mediatype, mt)) {
        Transform(pSample);
    }
    if (mt != NULL) {
        eraseMediaType(mt);
        CoTaskMemFree(mt);
    }
    if (_transport != NULL) {
        _transport->Receive(pSample);
    }
    pSample->Release();
    
    return S_OK;
}

HRESULT Filtaa::Transform(IMediaSample* pSample)
{
    HRESULT hr;
    BYTE* buf = NULL;
    hr = pSample->GetPointer(&buf);
    if (FAILED(hr)) return hr;

    VIDEOINFOHEADER* vi = (VIDEOINFOHEADER*)_mediatype.pbFormat;
    BYTE* line = buf;
    int width = vi->bmiHeader.biWidth;
    int height = vi->bmiHeader.biHeight;
    size_t linesize = align32(width * 3);
    for (int y = 0; y < height; y++) {
        BYTE* p = line;
        for (int x = 0; x < width; x++) {
            p[0] = 255;
            p += 3;
        }
        line += linesize;
    }
    
    return S_OK;
}
