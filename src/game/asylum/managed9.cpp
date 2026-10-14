#include "managed9.h"
#include "device9_forward.h"
#include <atomic>
#include <array>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include <type_traits>
namespace bvr::aa::managed9 {
namespace {
class Device;
struct Resource {
 Device* device; DWORD usage; bool dirty{true};
 Resource(Device*,DWORD); virtual ~Resource();
 virtual IUnknown* object()=0; virtual IUnknown* gpu()=0;
 virtual HRESULT upload()=0; virtual void retain()=0; virtual void drop()=0;
 virtual DWORD lod() const {return 0;}
 virtual bool copy_bytes(UINT,UINT,void*) const {return false;}
 void changed();
};
class Surface;
std::unordered_map<IDirect3DDevice9*,Device*> devices;
std::unordered_map<IDirect3DSurface9*,Surface*> surfaces;
std::unordered_map<IDirect3DSurface9*,Surface*> nativeSurfaces;
class Device final : public DeviceForward {
 std::atomic<ULONG> refs{1}; unsigned children{}; bool clearing{};
public:
 ComPtr<IDirect3DDevice9Ex> ex;
 ComPtr<IDirect3D9> factory;
 ComPtr<IDirect3DStateBlock9> initialState;
 std::unordered_map<IUnknown*,Resource*> logical,gpuObjects;
 std::array<ComPtr<IUnknown>,21> textures{}; std::array<DWORD,21> samplerLod{},samplerMip{};
 std::array<ComPtr<IUnknown>,16> streams{}; ComPtr<IUnknown> indices,depth;
 bool dirtyBound{}, recording{}; std::vector<ComPtr<IUnknown>> recorded;
 std::array<DWORD,21> recordedLod{},recordedMip{};std::array<bool,21> lodRecorded{},mipRecorded{};
 explicit Device(IDirect3DDevice9Ex* d):ex(d) {real=d;devices[this]=this;d->CreateStateBlock(D3DSBT_ALL,&initialState);}
 ~Device(){devices.erase(this);}
 void internal_add(){++children;}
 void internal_drop(){--children;if(!children&&!refs&&!clearing)delete this;}
 Resource* find(IUnknown* r){auto i=logical.find(r);return i==logical.end()?nullptr:i->second;}
 template<class I> I* unwrap(I* r){if(auto* m=find(r))return static_cast<I*>(m->gpu());return r;}
 HRESULT sync(){
  if(!dirtyBound)return S_OK;
  auto update=[&](IUnknown* o){auto* r=find(o);return r&&r->dirty?r->upload():S_OK;};
  for(auto& t:textures){const auto h=update(t.Get());if(FAILED(h))return h;}for(auto& b:streams){const auto h=update(b.Get());if(FAILED(h))return h;}const auto h=update(indices.Get());if(FAILED(h))return h;
  for(unsigned i=0;i<textures.size();++i)if(auto* r=find(textures[i].Get())){const DWORD s=i<16?i:D3DDMAPSAMPLER+i-16;real->SetSamplerState(s,D3DSAMP_MAXMIPLEVEL,std::max(samplerLod[i],r->lod()));real->SetSamplerState(s,D3DSAMP_MIPFILTER,r->lod()&&samplerMip[i]==D3DTEXF_NONE?D3DTEXF_POINT:samplerMip[i]);}dirtyBound=false;return S_OK;
 }
 void refresh();
 void replace_auto_depth(){ComPtr<IDirect3DSurface9>s;D3DSURFACE_DESC d{};if(SUCCEEDED(real->GetDepthStencilSurface(&s))&&SUCCEEDED(s->GetDesc(&d))){ComPtr<IDirect3DSurface9> replacement;if(SUCCEEDED(CreateDepthStencilSurface(d.Width,d.Height,d.Format,d.MultiSampleType,d.MultiSampleQuality,FALSE,&replacement,nullptr)))SetDepthStencilSurface(replacement.Get());}}
 HRESULT WINAPI QueryInterface(REFIID i,void** o) override {if(!o)return E_POINTER;*o=nullptr;if(i!=__uuidof(IUnknown)&&i!=__uuidof(IDirect3DDevice9))return E_NOINTERFACE;*o=this;AddRef();return S_OK;}
 HRESULT WINAPI GetDirect3D(IDirect3D9** o) override {if(!o)return D3DERR_INVALIDCALL;if(!factory)return real->GetDirect3D(o);*o=factory.Get();(*o)->AddRef();return S_OK;}
 ULONG WINAPI AddRef() override {return ++refs;}
 ULONG WINAPI Release() override {const ULONG n=--refs;if(!n){clearing=true;for(auto& t:textures)t.Reset();for(auto& s:streams)s.Reset();indices.Reset();depth.Reset();recorded.clear();clearing=false;if(!children)delete this;}return n;}
 HRESULT WINAPI Reset(D3DPRESENT_PARAMETERS* p) override {
  // Ex Reset preserves allocations, including all CPU/GPU managed twins.
  for(auto& t:textures)t.Reset();for(auto& s:streams)s.Reset();indices.Reset();depth.Reset();recorded.clear();
  const auto h=ex->ResetEx(p,nullptr);if(SUCCEEDED(h)){if(initialState)initialState->Apply();D3DVIEWPORT9 vp{0,0,p->BackBufferWidth,p->BackBufferHeight,0,1};real->SetViewport(&vp);for(auto& [_,r]:logical)r->changed();samplerLod.fill(0);samplerMip.fill(D3DTEXF_NONE);replace_auto_depth();}return h;
 }
 HRESULT WINAPI EvictManagedResources() override {return S_OK;}
 HRESULT WINAPI CreateTexture(UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DTexture9**,HANDLE*) override;
 HRESULT WINAPI CreateCubeTexture(UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DCubeTexture9**,HANDLE*) override;
 HRESULT WINAPI CreateVolumeTexture(UINT,UINT,UINT,UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DVolumeTexture9**,HANDLE*) override;
 HRESULT WINAPI CreateVertexBuffer(UINT,DWORD,DWORD,D3DPOOL,IDirect3DVertexBuffer9**,HANDLE*) override;
 HRESULT WINAPI CreateIndexBuffer(UINT,DWORD,D3DFORMAT,D3DPOOL,IDirect3DIndexBuffer9**,HANDLE*) override;
 HRESULT WINAPI CreateDepthStencilSurface(UINT,UINT,D3DFORMAT,D3DMULTISAMPLE_TYPE,DWORD,BOOL,IDirect3DSurface9**,HANDLE*) override;
 HRESULT WINAPI SetDepthStencilSurface(IDirect3DSurface9* s) override {const auto h=real->SetDepthStencilSurface(native_surface(s));if(SUCCEEDED(h))depth=s;return h;}
 HRESULT WINAPI GetDepthStencilSurface(IDirect3DSurface9** o) override {if(!o)return D3DERR_INVALIDCALL;if(depth){*o=static_cast<IDirect3DSurface9*>(depth.Get());(*o)->AddRef();return S_OK;}return real->GetDepthStencilSurface(o);}
 HRESULT WINAPI SetRenderTarget(DWORD i,IDirect3DSurface9* s) override {return real->SetRenderTarget(i,native_surface(s));}
 HRESULT WINAPI StretchRect(IDirect3DSurface9* a,const RECT* ar,IDirect3DSurface9* b,const RECT* br,D3DTEXTUREFILTERTYPE f) override {return real->StretchRect(native_surface(a),ar,native_surface(b),br,f);}
 HRESULT WINAPI ColorFill(IDirect3DSurface9* s,const RECT* r,D3DCOLOR c) override {return real->ColorFill(native_surface(s),r,c);}
 HRESULT WINAPI GetRenderTargetData(IDirect3DSurface9* a,IDirect3DSurface9* b) override {return real->GetRenderTargetData(native_surface(a),native_surface(b));}
 HRESULT WINAPI UpdateSurface(IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const POINT*) override;
 HRESULT WINAPI UpdateTexture(IDirect3DBaseTexture9* a,IDirect3DBaseTexture9* b) override {if(find(b))return D3DERR_INVALIDCALL;auto* m=find(a);if(m){const auto h=m->upload();if(FAILED(h))return h;}return real->UpdateTexture(unwrap(a),unwrap(b));}
 HRESULT WINAPI SetTexture(DWORD s,IDirect3DBaseTexture9* t) override;
 HRESULT WINAPI GetTexture(DWORD s,IDirect3DBaseTexture9** o) override;
 HRESULT WINAPI SetSamplerState(DWORD s,D3DSAMPLERSTATETYPE t,DWORD v) override;
 HRESULT WINAPI GetSamplerState(DWORD s,D3DSAMPLERSTATETYPE t,DWORD* v) override;
 HRESULT WINAPI SetStreamSource(UINT s,IDirect3DVertexBuffer9* b,UINT o,UINT stride) override {if(s>=16)return D3DERR_INVALIDCALL;auto* m=find(b);if(m){const auto h=m->upload();if(FAILED(h))return h;}const auto h=real->SetStreamSource(s,unwrap(b),o,stride);if(SUCCEEDED(h)){if(recording){if(b)recorded.emplace_back(b);}else streams[s]=b;}return h;}
 HRESULT WINAPI GetStreamSource(UINT s,IDirect3DVertexBuffer9** b,UINT* o,UINT* stride) override {const auto h=real->GetStreamSource(s,b,o,stride);if(SUCCEEDED(h)&&b&&*b){auto i=gpuObjects.find(*b);if(i!=gpuObjects.end()){(*b)->Release();*b=static_cast<IDirect3DVertexBuffer9*>(i->second->object());(*b)->AddRef();}}return h;}
 HRESULT WINAPI SetIndices(IDirect3DIndexBuffer9* b) override {auto* m=find(b);if(m){const auto h=m->upload();if(FAILED(h))return h;}const auto h=real->SetIndices(unwrap(b));if(SUCCEEDED(h)){if(recording){if(b)recorded.emplace_back(b);}else indices=b;}return h;}
 HRESULT WINAPI GetIndices(IDirect3DIndexBuffer9** b) override {const auto h=real->GetIndices(b);if(SUCCEEDED(h)&&b&&*b){auto i=gpuObjects.find(*b);if(i!=gpuObjects.end()){(*b)->Release();*b=static_cast<IDirect3DIndexBuffer9*>(i->second->object());(*b)->AddRef();}}return h;}
 HRESULT WINAPI DrawPrimitive(D3DPRIMITIVETYPE t,UINT s,UINT c) override {const auto h=sync();return FAILED(h)?h:real->DrawPrimitive(t,s,c);}
 HRESULT WINAPI DrawIndexedPrimitive(D3DPRIMITIVETYPE t,INT b,UINT m,UINT v,UINT s,UINT c) override {const auto h=sync();return FAILED(h)?h:real->DrawIndexedPrimitive(t,b,m,v,s,c);}
 HRESULT WINAPI DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT c,const void* v,UINT stride) override {const auto h=sync();if(FAILED(h))return h;const auto result=real->DrawPrimitiveUP(t,c,v,stride);if(SUCCEEDED(result))streams[0].Reset();return result;}
 HRESULT WINAPI DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE t,UINT m,UINT n,UINT c,const void* i,D3DFORMAT f,const void* v,UINT stride) override {const auto h=sync();if(FAILED(h))return h;const auto result=real->DrawIndexedPrimitiveUP(t,m,n,c,i,f,v,stride);if(SUCCEEDED(result)){streams[0].Reset();indices.Reset();}return result;}
 HRESULT WINAPI CreateStateBlock(D3DSTATEBLOCKTYPE,IDirect3DStateBlock9**) override;
 HRESULT WINAPI BeginStateBlock() override {const auto h=real->BeginStateBlock();if(SUCCEEDED(h)){recording=true;recorded.clear();lodRecorded.fill(false);mipRecorded.fill(false);}return h;}
 HRESULT WINAPI EndStateBlock(IDirect3DStateBlock9**) override;
};
Resource::Resource(Device* d,DWORD u):device(d),usage(u){device->internal_add();}
Resource::~Resource(){device->internal_drop();}
void Resource::changed(){dirty=true;device->dirtyBound=true;}
template<class F,class I> class Managed : public F,public Resource {
 std::atomic<ULONG> refs{1};
public:
 Managed(Device* d,I* r,DWORD u):Resource(d,u){this->real=r;}
 ~Managed() {device->logical.erase(object());device->gpuObjects.erase(gpu());}
 void registered(){device->logical[object()]=this;device->gpuObjects[gpu()]=this;}
 IUnknown* object() override {return static_cast<I*>(this);}
 IUnknown* gpu() override {return this->real.Get();}
 void retain() override {AddRef();} void drop() override {Release();}
 HRESULT WINAPI QueryInterface(REFIID i,void** o) override {
  if(!o)return E_POINTER;*o=nullptr;
  bool yes=i==__uuidof(IUnknown)||i==__uuidof(I);
  if constexpr(std::is_base_of_v<IDirect3DResource9,I>)yes=yes||i==__uuidof(IDirect3DResource9);
  if constexpr(std::is_base_of_v<IDirect3DBaseTexture9,I>)yes=yes||i==__uuidof(IDirect3DBaseTexture9);
  if(!yes)return E_NOINTERFACE;*o=static_cast<I*>(this);AddRef();return S_OK;
 }
 ULONG WINAPI AddRef() override {return ++refs;}
 ULONG WINAPI Release() override {const auto n=--refs;if(!n)delete this;return n;}
 HRESULT WINAPI GetDevice(IDirect3DDevice9** o) override {if(!o)return D3DERR_INVALIDCALL;*o=device;device->AddRef();return S_OK;}
 void WINAPI PreLoad() override {upload();}
};
template<class F,class I> class Texture : public Managed<F,I> {
 DWORD level{};
public:
 ComPtr<I> shadow;
 Texture(Device* d,I* r,I* s,DWORD u):Managed<F,I>(d,r,u),shadow(s){this->registered();}
 DWORD lod() const override {return level;}
 DWORD WINAPI GetLOD() override {return level;}
 DWORD WINAPI SetLOD(DWORD v) override {const DWORD old=level;level=std::min(v,this->real->GetLevelCount()-1);this->device->dirtyBound=true;return old;}
 HRESULT upload() override {
  if(!this->dirty)return S_OK;
  if constexpr(std::is_same_v<I,IDirect3DTexture9>)shadow->AddDirtyRect(nullptr);
  else if constexpr(std::is_same_v<I,IDirect3DCubeTexture9>)for(unsigned f=0;f<6;++f)shadow->AddDirtyRect(D3DCUBEMAP_FACES(f),nullptr);
  else shadow->AddDirtyBox(nullptr);
  const auto h=this->device->real->UpdateTexture(shadow.Get(),this->real.Get());
  if(SUCCEEDED(h)){if(this->usage&D3DUSAGE_AUTOGENMIPMAP)this->real->GenerateMipSubLevels();this->dirty=false;}return h;
 }
 DWORD WINAPI GetLevelCount() override {return this->usage&D3DUSAGE_AUTOGENMIPMAP?1:this->real->GetLevelCount();}
 void WINAPI GenerateMipSubLevels() override {this->changed();upload();this->real->GenerateMipSubLevels();}
};
class Tex2 final : public Texture<TextureForward,IDirect3DTexture9> {
 std::vector<DWORD> locks;
public:
 Tex2(Device*d,IDirect3DTexture9*r,IDirect3DTexture9*s,DWORD u):Texture(d,r,s,u),locks(s->GetLevelCount()){}
 HRESULT WINAPI GetLevelDesc(UINT l,D3DSURFACE_DESC* o) override {const auto h=shadow->GetLevelDesc(l,o);if(SUCCEEDED(h)){o->Pool=D3DPOOL_MANAGED;o->Usage=usage;}return h;}
 HRESULT WINAPI LockRect(UINT l,D3DLOCKED_RECT* o,const RECT* r,DWORD f) override {const auto h=shadow->LockRect(l,o,r,f);if(SUCCEEDED(h)&&l<locks.size())locks[l]=f;return h;}
 HRESULT WINAPI UnlockRect(UINT l) override {const auto h=shadow->UnlockRect(l);if(SUCCEEDED(h)&&l<locks.size()&&!(locks[l]&D3DLOCK_READONLY)){shadow->AddDirtyRect(nullptr);changed();}return h;}
 HRESULT WINAPI AddDirtyRect(const RECT* r) override {const auto h=shadow->AddDirtyRect(r);if(SUCCEEDED(h))changed();return h;}
 HRESULT WINAPI GetSurfaceLevel(UINT,IDirect3DSurface9**) override;
};
class Cube final : public Texture<CubeTextureForward,IDirect3DCubeTexture9> {
 std::vector<DWORD> locks;
public:
 Cube(Device*d,IDirect3DCubeTexture9*r,IDirect3DCubeTexture9*s,DWORD u):Texture(d,r,s,u),locks(6*s->GetLevelCount()){}
 HRESULT WINAPI GetLevelDesc(UINT l,D3DSURFACE_DESC* o) override {const auto h=shadow->GetLevelDesc(l,o);if(SUCCEEDED(h)){o->Pool=D3DPOOL_MANAGED;o->Usage=usage;}return h;}
 HRESULT WINAPI LockRect(D3DCUBEMAP_FACES f,UINT l,D3DLOCKED_RECT* o,const RECT* r,DWORD flags) override {const auto h=shadow->LockRect(f,l,o,r,flags);if(SUCCEEDED(h))locks[unsigned(f)*shadow->GetLevelCount()+l]=flags;return h;}
 HRESULT WINAPI UnlockRect(D3DCUBEMAP_FACES f,UINT l) override {const auto h=shadow->UnlockRect(f,l);if(SUCCEEDED(h)&&!(locks[unsigned(f)*shadow->GetLevelCount()+l]&D3DLOCK_READONLY)){shadow->AddDirtyRect(f,nullptr);changed();}return h;}
 HRESULT WINAPI AddDirtyRect(D3DCUBEMAP_FACES f,const RECT* r) override {const auto h=shadow->AddDirtyRect(f,r);if(SUCCEEDED(h))changed();return h;}
 HRESULT WINAPI GetCubeMapSurface(D3DCUBEMAP_FACES,UINT,IDirect3DSurface9**) override;
};
class VolumeTex final : public Texture<VolumeTextureForward,IDirect3DVolumeTexture9> {
 std::vector<DWORD> locks;
public:
 VolumeTex(Device*d,IDirect3DVolumeTexture9*r,IDirect3DVolumeTexture9*s,DWORD u):Texture(d,r,s,u),locks(s->GetLevelCount()){}
 HRESULT WINAPI GetLevelDesc(UINT l,D3DVOLUME_DESC* o) override {const auto h=shadow->GetLevelDesc(l,o);if(SUCCEEDED(h)){o->Pool=D3DPOOL_MANAGED;o->Usage=usage;}return h;}
 HRESULT WINAPI LockBox(UINT l,D3DLOCKED_BOX* o,const D3DBOX* b,DWORD f) override {const auto h=shadow->LockBox(l,o,b,f);if(SUCCEEDED(h))locks[l]=f;return h;}
 HRESULT WINAPI UnlockBox(UINT l) override {const auto h=shadow->UnlockBox(l);if(SUCCEEDED(h)&&!(locks[l]&D3DLOCK_READONLY)){shadow->AddDirtyBox(nullptr);changed();}return h;}
 HRESULT WINAPI AddDirtyBox(const D3DBOX* b) override {const auto h=shadow->AddDirtyBox(b);if(SUCCEEDED(h))changed();return h;}
 HRESULT WINAPI GetVolumeLevel(UINT,IDirect3DVolume9**) override;
};
// Child surfaces lock the same CPU shadow as the parent. The parent stays alive
// while a child is held; GetContainer preserves the logical texture identity.
class Surface final : public SurfaceForward {
 std::atomic<ULONG> refs{1}; Resource* parent{}; Device* owner{}; DWORD locked{};
 ComPtr<IDirect3DSurface9> gpuSurface; ComPtr<IDirect3DTexture9> depthImage;
 D3DSURFACE_DESC desc{};
public:
 Surface(Resource* p,IDirect3DSurface9* cpu,IDirect3DSurface9* g):parent(p),gpuSurface(g){real=cpu;parent->retain();cpu->GetDesc(&desc);desc.Pool=D3DPOOL_MANAGED;desc.Usage=p->usage;surfaces[this]=this;nativeSurfaces[g]=this;}
 Surface(Device* d,IDirect3DSurface9* g,IDirect3DTexture9* t,D3DFORMAT format):owner(d),gpuSurface(g),depthImage(t){real=g;d->internal_add();g->GetDesc(&desc);desc.Format=format;surfaces[this]=this;nativeSurfaces[g]=this;}
 ~Surface(){surfaces.erase(this);nativeSurfaces.erase(gpuSurface.Get());if(parent)parent->drop();if(owner)owner->internal_drop();}
 IDirect3DSurface9* native(){return gpuSurface.Get();} IDirect3DTexture9* depth_tex(){return depthImage.Get();}
 HRESULT WINAPI QueryInterface(REFIID i,void** o) override {if(!o)return E_POINTER;*o=nullptr;if(i!=__uuidof(IUnknown)&&i!=__uuidof(IDirect3DResource9)&&i!=__uuidof(IDirect3DSurface9))return E_NOINTERFACE;*o=this;AddRef();return S_OK;}
 ULONG WINAPI AddRef() override {return ++refs;} ULONG WINAPI Release() override {const auto n=--refs;if(!n)delete this;return n;}
 HRESULT WINAPI GetDevice(IDirect3DDevice9** o) override {if(!o)return D3DERR_INVALIDCALL;*o=owner?owner:parent->device;(*o)->AddRef();return S_OK;}
 HRESULT WINAPI GetContainer(REFIID i,void** o) override {if(parent)return parent->object()->QueryInterface(i,o);return owner->QueryInterface(i,o);}
 HRESULT WINAPI GetDesc(D3DSURFACE_DESC* o) override {if(!o)return D3DERR_INVALIDCALL;*o=desc;return S_OK;}
 HRESULT WINAPI LockRect(D3DLOCKED_RECT* o,const RECT* r,DWORD f) override {const auto h=real->LockRect(o,r,f);if(SUCCEEDED(h))locked=f;return h;}
 HRESULT WINAPI UnlockRect() override {const auto h=real->UnlockRect();if(SUCCEEDED(h)&&parent&&!(locked&D3DLOCK_READONLY))parent->changed();return h;}
};
class Volume final : public VolumeForward {
 std::atomic<ULONG> refs{1}; VolumeTex* parent; DWORD flags{};
public:
 Volume(VolumeTex* p,IDirect3DVolume9* v):parent(p){real=v;p->AddRef();}
 ~Volume(){parent->Release();}
 HRESULT WINAPI QueryInterface(REFIID i,void** o) override {if(!o)return E_POINTER;*o=nullptr;if(i!=__uuidof(IUnknown)&&i!=__uuidof(IDirect3DVolume9))return E_NOINTERFACE;*o=this;AddRef();return S_OK;}
 ULONG WINAPI AddRef() override {return ++refs;} ULONG WINAPI Release() override {const auto n=--refs;if(!n)delete this;return n;}
 HRESULT WINAPI GetDevice(IDirect3DDevice9** o) override {return parent->GetDevice(o);}
 HRESULT WINAPI GetContainer(REFIID i,void** o) override {return parent->QueryInterface(i,o);}
 HRESULT WINAPI GetDesc(D3DVOLUME_DESC* o) override {const auto h=real->GetDesc(o);if(SUCCEEDED(h)){o->Pool=D3DPOOL_MANAGED;o->Usage=parent->usage;}return h;}
 HRESULT WINAPI LockBox(D3DLOCKED_BOX* o,const D3DBOX* b,DWORD f) override {const auto h=real->LockBox(o,b,f);if(SUCCEEDED(h))flags=f;return h;}
 HRESULT WINAPI UnlockBox() override {const auto h=real->UnlockBox();if(SUCCEEDED(h)&&!(flags&D3DLOCK_READONLY)){parent->shadow->AddDirtyBox(nullptr);parent->changed();}return h;}
};
HRESULT Tex2::GetSurfaceLevel(UINT l,IDirect3DSurface9** o){if(!o)return D3DERR_INVALIDCALL;*o=nullptr;ComPtr<IDirect3DSurface9> c,g;auto h=shadow->GetSurfaceLevel(l,&c);if(FAILED(h))return h;h=real->GetSurfaceLevel(l,&g);if(FAILED(h))return h;auto i=nativeSurfaces.find(g.Get());if(i!=nativeSurfaces.end()){*o=i->second;(*o)->AddRef();}else *o=new Surface(this,c.Get(),g.Get());return S_OK;}
HRESULT Cube::GetCubeMapSurface(D3DCUBEMAP_FACES f,UINT l,IDirect3DSurface9** o){if(!o)return D3DERR_INVALIDCALL;*o=nullptr;ComPtr<IDirect3DSurface9> c,g;auto h=shadow->GetCubeMapSurface(f,l,&c);if(FAILED(h))return h;h=real->GetCubeMapSurface(f,l,&g);if(FAILED(h))return h;auto i=nativeSurfaces.find(g.Get());if(i!=nativeSurfaces.end()){*o=i->second;(*o)->AddRef();}else *o=new Surface(this,c.Get(),g.Get());return S_OK;}
HRESULT VolumeTex::GetVolumeLevel(UINT l,IDirect3DVolume9** o){if(!o)return D3DERR_INVALIDCALL;*o=nullptr;ComPtr<IDirect3DVolume9> v;const auto h=shadow->GetVolumeLevel(l,&v);if(SUCCEEDED(h))*o=new Volume(this,v.Get());return h;}
template<class F,class I> class Buffer final : public Managed<F,I> {
 std::vector<unsigned char> bytes; UINT offset{},size{}; DWORD flags{}; bool locked{};
public:
 Buffer(Device*d,I*r,DWORD u,UINT length):Managed<F,I>(d,r,u),bytes(length){this->registered();}
 bool copy_bytes(UINT at,UINT n,void* out) const override {if(!out||locked||at>bytes.size()||n>bytes.size()-at)return false;memcpy(out,bytes.data()+at,n);return true;}
 HRESULT WINAPI Lock(UINT o,UINT s,void** p,DWORD f) override {if(!p||locked||o>bytes.size())return D3DERR_INVALIDCALL;s=s?s:UINT(bytes.size()-o);if(s>bytes.size()-o||f&(D3DLOCK_DISCARD|D3DLOCK_NOOVERWRITE))return D3DERR_INVALIDCALL;offset=o;size=s;flags=f;locked=true;*p=bytes.data()+o;return S_OK;}
 HRESULT WINAPI Unlock() override {if(!locked)return D3DERR_INVALIDCALL;locked=false;if(!(flags&D3DLOCK_READONLY))this->changed();return S_OK;}
 HRESULT upload() override {if(!this->dirty)return S_OK;if(locked)return D3DERR_INVALIDCALL;void* p{};auto h=this->real->Lock(0,UINT(bytes.size()),&p,0);if(FAILED(h))return h;memcpy(p,bytes.data(),bytes.size());h=this->real->Unlock();if(SUCCEEDED(h))this->dirty=false;return h;}
 HRESULT WINAPI GetDesc(std::conditional_t<std::is_same_v<I,IDirect3DVertexBuffer9>,D3DVERTEXBUFFER_DESC,D3DINDEXBUFFER_DESC>* o) override {const auto h=this->real->GetDesc(o);if(SUCCEEDED(h)){o->Pool=D3DPOOL_MANAGED;o->Usage=this->usage;}return h;}
};
class State final : public StateBlockForward {
 std::atomic<ULONG> refs{1}; Device* d; std::vector<ComPtr<IUnknown>> keep;
 std::array<DWORD,21> lod{},mip{};
 std::array<bool,21> lodMask{},mipMask{};
public:
 State(Device* device,IDirect3DStateBlock9* r,D3DSTATEBLOCKTYPE type,bool recorded=false):d(device){real=r;d->internal_add();remember();for(unsigned i=0;i<21;++i)lodMask[i]=mipMask[i]=type==D3DSBT_ALL||(type==D3DSBT_PIXELSTATE?i<16:i>=16);if(recorded){lodMask=d->lodRecorded;mipMask=d->mipRecorded;for(unsigned i=0;i<21;++i){if(lodMask[i])lod[i]=d->recordedLod[i];if(mipMask[i])mip[i]=d->recordedMip[i];}}}
 ~State(){keep.clear();d->internal_drop();}
 void remember(){lod=d->samplerLod;mip=d->samplerMip;for(auto& t:d->textures)if(t)keep.push_back(t);for(auto&s:d->streams)if(s)keep.push_back(s);if(d->indices)keep.push_back(d->indices);if(d->depth)keep.push_back(d->depth);for(auto&r:d->recorded)keep.push_back(r);}
 HRESULT WINAPI QueryInterface(REFIID i,void** o) override {if(!o)return E_POINTER;*o=nullptr;if(i!=__uuidof(IUnknown)&&i!=__uuidof(IDirect3DStateBlock9))return E_NOINTERFACE;*o=this;AddRef();return S_OK;}
 ULONG WINAPI AddRef() override {return ++refs;} ULONG WINAPI Release() override {const auto n=--refs;if(!n)delete this;return n;}
 HRESULT WINAPI GetDevice(IDirect3DDevice9** o) override {if(!o)return D3DERR_INVALIDCALL;*o=d;d->AddRef();return S_OK;}
 HRESULT WINAPI Capture() override {const auto h=real->Capture();if(SUCCEEDED(h)){keep.clear();remember();}return h;}
 HRESULT WINAPI Apply() override {const auto h=real->Apply();if(SUCCEEDED(h)){d->refresh();for(unsigned i=0;i<21;++i){if(lodMask[i])d->samplerLod[i]=lod[i];if(mipMask[i])d->samplerMip[i]=mip[i];}}return h;}
};
unsigned sampler(DWORD s){return s<16?s:s>=D3DDMAPSAMPLER&&s<=D3DVERTEXTEXTURESAMPLER3?16+s-D3DDMAPSAMPLER:21;}
DWORD stage(unsigned s){return s<16?s:D3DDMAPSAMPLER+s-16;}
void Device::refresh(){for(unsigned s=0;s<textures.size();++s){ComPtr<IDirect3DBaseTexture9> t;if(SUCCEEDED(real->GetTexture(stage(s),&t))){auto i=gpuObjects.find(t.Get());textures[s]=i==gpuObjects.end()?static_cast<IUnknown*>(t.Get()):i->second->object();}}for(unsigned s=0;s<16;++s){ComPtr<IDirect3DVertexBuffer9>b;UINT o{},stride{};if(SUCCEEDED(GetStreamSource(s,&b,&o,&stride)))streams[s]=b;}ComPtr<IDirect3DIndexBuffer9>b;if(SUCCEEDED(GetIndices(&b)))indices=b;dirtyBound=true;}
HRESULT Device::SetTexture(DWORD s,IDirect3DBaseTexture9* t){const unsigned slot=sampler(s);if(slot>=textures.size())return D3DERR_INVALIDCALL;auto* m=find(t);if(m){const auto h=m->upload();if(FAILED(h))return h;}const auto h=real->SetTexture(s,unwrap(t));if(SUCCEEDED(h)){if(recording){if(t)recorded.emplace_back(t);}else textures[slot]=t;real->SetSamplerState(s,D3DSAMP_MAXMIPLEVEL,std::max(samplerLod[slot],m?m->lod():0));real->SetSamplerState(s,D3DSAMP_MIPFILTER,m&&m->lod()&&samplerMip[slot]==D3DTEXF_NONE?D3DTEXF_POINT:samplerMip[slot]);}return h;}
HRESULT Device::GetTexture(DWORD s,IDirect3DBaseTexture9** o){if(!o)return D3DERR_INVALIDCALL;const auto h=real->GetTexture(s,o);if(SUCCEEDED(h)&&*o){auto i=gpuObjects.find(*o);if(i!=gpuObjects.end()){(*o)->Release();*o=static_cast<IDirect3DBaseTexture9*>(i->second->object());(*o)->AddRef();}}return h;}
HRESULT Device::SetSamplerState(DWORD s,D3DSAMPLERSTATETYPE t,DWORD v){const unsigned i=sampler(s);if((t!=D3DSAMP_MAXMIPLEVEL&&t!=D3DSAMP_MIPFILTER)||i>=textures.size())return real->SetSamplerState(s,t,v);auto* m=find(textures[i].Get());const DWORD effective=t==D3DSAMP_MAXMIPLEVEL?std::max(v,m?m->lod():0):m&&m->lod()&&v==D3DTEXF_NONE?D3DTEXF_POINT:v;const auto h=real->SetSamplerState(s,t,effective);if(SUCCEEDED(h)){if(recording){(t==D3DSAMP_MAXMIPLEVEL?recordedLod:recordedMip)[i]=v;(t==D3DSAMP_MAXMIPLEVEL?lodRecorded:mipRecorded)[i]=true;}else (t==D3DSAMP_MAXMIPLEVEL?samplerLod:samplerMip)[i]=v;}return h;}
HRESULT Device::GetSamplerState(DWORD s,D3DSAMPLERSTATETYPE t,DWORD* v){const unsigned i=sampler(s);if((t==D3DSAMP_MAXMIPLEVEL||t==D3DSAMP_MIPFILTER)&&i<textures.size()&&v){*v=(t==D3DSAMP_MAXMIPLEVEL?samplerLod:samplerMip)[i];return S_OK;}return real->GetSamplerState(s,t,v);}
bool valid_managed(DWORD u,HANDLE* h){return !(u&(D3DUSAGE_DYNAMIC|D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL))&&!h;}
HRESULT Device::CreateTexture(UINT w,UINT h,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DTexture9** o,HANDLE* shared){if(p!=D3DPOOL_MANAGED)return real->CreateTexture(w,h,l,u,f,p,o,shared);if(!o||!valid_managed(u,shared))return D3DERR_INVALIDCALL;*o=nullptr;ComPtr<IDirect3DTexture9> g,c;auto hr=real->CreateTexture(w,h,l,u,f,D3DPOOL_DEFAULT,&g,nullptr);if(FAILED(hr))return hr;hr=real->CreateTexture(w,h,u&D3DUSAGE_AUTOGENMIPMAP?1:g->GetLevelCount(),0,f,D3DPOOL_SYSTEMMEM,&c,nullptr);if(SUCCEEDED(hr))*o=new Tex2(this,g.Get(),c.Get(),u);return hr;}
HRESULT Device::CreateCubeTexture(UINT e,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DCubeTexture9** o,HANDLE* shared){if(p!=D3DPOOL_MANAGED)return real->CreateCubeTexture(e,l,u,f,p,o,shared);if(!o||!valid_managed(u,shared))return D3DERR_INVALIDCALL;*o=nullptr;ComPtr<IDirect3DCubeTexture9> g,c;auto hr=real->CreateCubeTexture(e,l,u,f,D3DPOOL_DEFAULT,&g,nullptr);if(FAILED(hr))return hr;hr=real->CreateCubeTexture(e,u&D3DUSAGE_AUTOGENMIPMAP?1:g->GetLevelCount(),0,f,D3DPOOL_SYSTEMMEM,&c,nullptr);if(SUCCEEDED(hr))*o=new Cube(this,g.Get(),c.Get(),u);return hr;}
HRESULT Device::CreateVolumeTexture(UINT w,UINT h,UINT depth,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DVolumeTexture9** o,HANDLE* shared){if(p!=D3DPOOL_MANAGED)return real->CreateVolumeTexture(w,h,depth,l,u,f,p,o,shared);if(!o||!valid_managed(u,shared))return D3DERR_INVALIDCALL;*o=nullptr;ComPtr<IDirect3DVolumeTexture9> g,c;auto hr=real->CreateVolumeTexture(w,h,depth,l,u,f,D3DPOOL_DEFAULT,&g,nullptr);if(FAILED(hr))return hr;hr=real->CreateVolumeTexture(w,h,depth,g->GetLevelCount(),0,f,D3DPOOL_SYSTEMMEM,&c,nullptr);if(SUCCEEDED(hr))*o=new VolumeTex(this,g.Get(),c.Get(),u);return hr;}
HRESULT Device::CreateVertexBuffer(UINT n,DWORD u,DWORD f,D3DPOOL p,IDirect3DVertexBuffer9** o,HANDLE* shared){if(p!=D3DPOOL_MANAGED)return real->CreateVertexBuffer(n,u,f,p,o,shared);if(!o||!valid_managed(u,shared))return D3DERR_INVALIDCALL;*o=nullptr;ComPtr<IDirect3DVertexBuffer9> g;const auto h=real->CreateVertexBuffer(n,u,f,D3DPOOL_DEFAULT,&g,nullptr);if(SUCCEEDED(h))*o=new Buffer<VertexBufferForward,IDirect3DVertexBuffer9>(this,g.Get(),u,n);return h;}
HRESULT Device::CreateIndexBuffer(UINT n,DWORD u,D3DFORMAT f,D3DPOOL p,IDirect3DIndexBuffer9** o,HANDLE* shared){if(p!=D3DPOOL_MANAGED)return real->CreateIndexBuffer(n,u,f,p,o,shared);if(!o||!valid_managed(u,shared))return D3DERR_INVALIDCALL;*o=nullptr;ComPtr<IDirect3DIndexBuffer9> g;const auto h=real->CreateIndexBuffer(n,u,f,D3DPOOL_DEFAULT,&g,nullptr);if(SUCCEEDED(h))*o=new Buffer<IndexBufferForward,IDirect3DIndexBuffer9>(this,g.Get(),u,n);return h;}
HRESULT Device::CreateDepthStencilSurface(UINT w,UINT h,D3DFORMAT f,D3DMULTISAMPLE_TYPE m,DWORD q,BOOL discard,IDirect3DSurface9** o,HANDLE* shared){
 if(o&&!shared&&m==D3DMULTISAMPLE_NONE&&(f==D3DFMT_D24S8||f==D3DFMT_D24X8)){
  ComPtr<IDirect3DTexture9> t;ComPtr<IDirect3DSurface9>s;const auto hr=real->CreateTexture(w,h,1,D3DUSAGE_DEPTHSTENCIL,D3DFORMAT(MAKEFOURCC('I','N','T','Z')),D3DPOOL_DEFAULT,&t,nullptr);
  if(SUCCEEDED(hr)&&SUCCEEDED(t->GetSurfaceLevel(0,&s))){*o=new Surface(this,s.Get(),t.Get(),f);return S_OK;}
 }
 return real->CreateDepthStencilSurface(w,h,f,m,q,discard,o,shared);
}
UINT row_bytes(D3DFORMAT f,UINT w){
 if(f==D3DFMT_DXT1)return ((w+3)/4)*8;
 if(f==D3DFMT_DXT2||f==D3DFMT_DXT3||f==D3DFMT_DXT4||f==D3DFMT_DXT5)return ((w+3)/4)*16;
 switch(f){
 case D3DFMT_A8:case D3DFMT_L8:case D3DFMT_P8:return w;
 case D3DFMT_R5G6B5:case D3DFMT_A1R5G5B5:case D3DFMT_X1R5G5B5:case D3DFMT_A4R4G4B4:case D3DFMT_A8L8:case D3DFMT_L16:case D3DFMT_R16F:case D3DFMT_V8U8:return w*2;
 case D3DFMT_A8R8G8B8:case D3DFMT_X8R8G8B8:case D3DFMT_A8B8G8R8:case D3DFMT_X8B8G8R8:case D3DFMT_G16R16:case D3DFMT_R32F:case D3DFMT_G16R16F:case D3DFMT_A2R10G10B10:case D3DFMT_A2B10G10R10:case D3DFMT_Q8W8V8U8:case D3DFMT_V16U16:return w*4;
 case D3DFMT_A16B16G16R16:case D3DFMT_A16B16G16R16F:case D3DFMT_G32R32F:return w*8;
 case D3DFMT_A32B32G32R32F:return w*16;
 default:return 0;}
}
HRESULT Device::UpdateSurface(IDirect3DSurface9* a,const RECT* ar,IDirect3DSurface9* b,const POINT* point){
 auto i=surfaces.find(b);if(i!=surfaces.end()){
  D3DSURFACE_DESC d{};b->GetDesc(&d);if(d.Pool==D3DPOOL_MANAGED){
   if(!a)return D3DERR_INVALIDCALL;D3DSURFACE_DESC s{};a->GetDesc(&s);if(s.Format!=d.Format)return D3DERR_INVALIDCALL;
   const RECT r=ar?*ar:RECT{0,0,LONG(s.Width),LONG(s.Height)};const POINT at=point?*point:POINT{};const RECT to{at.x,at.y,at.x+r.right-r.left,at.y+r.bottom-r.top};
   if(r.left<0||r.top<0||r.right<=r.left||r.bottom<=r.top||r.right>LONG(s.Width)||r.bottom>LONG(s.Height)||at.x<0||at.y<0||to.right>LONG(d.Width)||to.bottom>LONG(d.Height))return D3DERR_INVALIDCALL;
   const UINT bytes=row_bytes(s.Format,r.right-r.left);if(!bytes)return D3DERR_INVALIDCALL;const bool bc=s.Format>=D3DFMT_DXT1&&s.Format<=D3DFMT_DXT5;const UINT rows=bc?(r.bottom-r.top+3)/4:r.bottom-r.top;
   D3DLOCKED_RECT src{},dst{};auto h=a->LockRect(&src,&r,D3DLOCK_READONLY);if(FAILED(h))return h;h=b->LockRect(&dst,&to,0);
   if(SUCCEEDED(h)){for(UINT y=0;y<rows;++y)memcpy(static_cast<BYTE*>(dst.pBits)+y*dst.Pitch,static_cast<BYTE*>(src.pBits)+y*src.Pitch,bytes);b->UnlockRect();}a->UnlockRect();return h;
  }
 }
 return real->UpdateSurface(native_surface(a),ar,native_surface(b),point);
}

HRESULT Device::CreateStateBlock(D3DSTATEBLOCKTYPE t,IDirect3DStateBlock9** o){if(!o)return D3DERR_INVALIDCALL;*o=nullptr;ComPtr<IDirect3DStateBlock9>b;const auto h=real->CreateStateBlock(t,&b);if(SUCCEEDED(h))*o=new State(this,b.Get(),t);return h;}
HRESULT Device::EndStateBlock(IDirect3DStateBlock9** o){if(!o)return D3DERR_INVALIDCALL;*o=nullptr;ComPtr<IDirect3DStateBlock9>b;const auto h=real->EndStateBlock(&b);if(SUCCEEDED(h)){recording=false;*o=new State(this,b.Get(),D3DSBT_ALL,true);recorded.clear();}return h;}
}
bool copy_buffer(IDirect3DDevice9*d,IUnknown*object,UINT offset,UINT n,void*out){const auto i=devices.find(d);if(i==devices.end()||!object||!out||n>256*1024)return false;auto*r=i->second->find(object);return r&&r->copy_bytes(offset,n,out);}
IDirect3DDevice9* wrap(IDirect3DDevice9Ex* d,IDirect3D9* factory){if(!d)return nullptr;auto* result=new Device(d);result->factory=factory;result->replace_auto_depth();return result;}
IDirect3DDevice9Ex* native_device(IDirect3DDevice9* d){const auto i=devices.find(d);return i==devices.end()?nullptr:i->second->ex.Get();}
IDirect3DTexture9* depth_texture(IDirect3DSurface9* s){const auto i=surfaces.find(s);return i==surfaces.end()?nullptr:i->second->depth_tex();}
IDirect3DSurface9* native_surface(IDirect3DSurface9* s){const auto i=surfaces.find(s);return i==surfaces.end()?s:i->second->native();}
}
