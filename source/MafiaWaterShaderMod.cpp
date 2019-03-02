#define WIN32_LEAN_AND_MEAN
#define _USE_MATH_DEFINES
#include <windows.h>
#include <cmath>
#include <d3d8.h>
#include <d3dx8.h>
#include <d3dx8tex.h>
#include <d3dvtbl.h>
#include <mmsystem.h>
#pragma comment(lib, "d3dx8.lib")
#pragma comment(lib, "d3d8.lib")
#pragma comment(lib, "legacy_stdio_definitions.lib")
#pragma comment(lib, "winmm.lib")
#include <injector\injector.hpp>
#include <injector\hooking.hpp>
#include <injector\calling.hpp>
#include <injector\assembly.hpp>
#include <injector\utility.hpp>
#include <time.h>
#include "cb.h"
#include "resources.h"
#include <array>

//#define USE_D3D_HOOK

#ifdef USE_D3D_HOOK
typedef HRESULT(STDMETHODCALLTYPE* CreateDevice_t)(IDirect3D8*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice8 **);
typedef HRESULT(STDMETHODCALLTYPE* TestCooperativeLevel_t)(LPDIRECT3DDEVICE8);
typedef HRESULT(STDMETHODCALLTYPE* Reset_t)(LPDIRECT3DDEVICE8, D3DPRESENT_PARAMETERS*);
typedef HRESULT(STDMETHODCALLTYPE* BeginScene_t)(LPDIRECT3DDEVICE8);
typedef HRESULT(STDMETHODCALLTYPE* SetTransform_t)(LPDIRECT3DDEVICE8, D3DTRANSFORMSTATETYPE, CONST D3DMATRIX*);
typedef HRESULT(STDMETHODCALLTYPE* SetRenderState_t)(LPDIRECT3DDEVICE8, D3DRENDERSTATETYPE, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* SetTexture_t)(LPDIRECT3DDEVICE8, DWORD, IDirect3DBaseTexture8*);
typedef HRESULT(STDMETHODCALLTYPE* DrawIndexedPrimitive_t)(LPDIRECT3DDEVICE8, D3DPRIMITIVETYPE, UINT, UINT, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* SetStreamSource_t)(LPDIRECT3DDEVICE8, UINT, IDirect3DVertexBuffer8*, UINT);
typedef HRESULT(STDMETHODCALLTYPE* SetIndices_t)(LPDIRECT3DDEVICE8, IDirect3DIndexBuffer8*, UINT);

TestCooperativeLevel_t RealD3D8TestCooperativeLevel = NULL;
Reset_t RealD3D8Reset = NULL;
BeginScene_t RealD3D8BeginScene = NULL;
SetTransform_t RealD3D8SetTransform = NULL;
DrawIndexedPrimitive_t RealD3D8DrawIndexedPrimitive = NULL;
SetRenderState_t RealD3D8SetRenderState = NULL;
SetTexture_t RealD3D8SetTexture = NULL;
SetStreamSource_t RealD3D8SetStreamSource = NULL;
SetIndices_t RealD3D8SetIndices = NULL;
CreateDevice_t RealD3D8CreateDevice = NULL;
#endif

std::string format(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::vector<char> v(1024);
    while (true)
    {
        va_list args2;
        va_copy(args2, args);
        int res = vsnprintf(v.data(), v.size(), fmt, args2);
        if ((res >= 0) && (res < static_cast<int>(v.size())))
        {
            va_end(args);
            va_end(args2);
            return std::string(v.data());
        }
        size_t size;
        if (res < 0)
            size = v.size() * 2;
        else
            size = static_cast<size_t>(res) + 1;
        v.clear();
        v.resize(size);
        va_end(args2);
    }
}

template<typename T>
std::array<uint8_t, sizeof(T)> to_bytes(const T& object)
{
    std::array<uint8_t, sizeof(T)> bytes;
    const uint8_t* begin = reinterpret_cast<const uint8_t*>(std::addressof(object));
    const uint8_t* end = begin + sizeof(T);
    std::copy(begin, end, std::begin(bytes));
    return bytes;
}

template <size_t n>
std::string pattern_str(const std::array<uint8_t, n> bytes)
{
    std::string result;
    for (size_t i = 0; i < n; i++)
    {
        result += format("%02X ", bytes[i]);
    }
    return result;
}

template <typename T>
std::string pattern_str(T t)
{
    return std::string((std::is_same<T, char>::value ? format("%c ", t) : format("%02X ", t)));
}

template <typename T, typename... Rest>
std::string pattern_str(T t, Rest... rest)
{
    return std::string((std::is_same<T, char>::value ? format("%c ", t) : format("%02X ", t)) + pattern_str(rest...));
}

std::wstring ls3df;
UINT_PTR* pVTable;

using namespace std;

ID3DXBuffer* shaderV;
ID3DXBuffer* shaderP;
ID3DXBuffer* shdBuff;
DWORD VS;
DWORD PS;
DWORD PSx8;
DWORD PSxS;
DWORD PSx2;
std::vector<IDirect3DTexture8*> refrTex;
IDirect3DTexture8* reflTex;
IDirect3DTexture8* fresnelTex;

int32_t sWidth, sHeight;

bool saveDecalMode = false;
bool afterWater = false;

IDirect3DSurface8* reflDSS;
IDirect3DSurface8* reflSurf;
IDirect3DSurface8* beforeReflSurf;
IDirect3DSurface8* beforeReflDSS;
IDirect3DTexture8* lastTex;

D3DXMATRIX worldMtx;
D3DXMATRIX viewMtx;
D3DXMATRIX projMtx;

DWORD wasFog = TRUE;
DWORD fogColor;

float tiling[4];
float zeroFive[4];
float fogColorS[4];
float posCam[4];

UINT actStream;
IDirect3DVertexBuffer8* actVB;
UINT actStride;
IDirect3DIndexBuffer8* actIB;
UINT actBaseVertIndex;

UINT mrStream;
IDirect3DVertexBuffer8* mrVB;
UINT mrStride;
IDirect3DIndexBuffer8* mrIB;
UINT mrBaseVertIndex;

D3DXVECTOR3 waterColor;

UINT mrMinIndex;
UINT mrNumVerts;
UINT mrStartIndex;
UINT mrPrimCount;

float lastDecalY = -1000;

enum ShaderType
{
    T_PS,
    T_VS
};

template <class T>
void LoadShader(LPDIRECT3DDEVICE8 d3d8device, T ID, DWORD* ptr, ShaderType t)
{
    HMODULE hm = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&lastDecalY, &hm);
    auto rsc = FindResource(hm, MAKEINTRESOURCE(ID), RT_RCDATA);
    if (t == ShaderType::T_PS)
    {
        d3d8device->CreatePixelShader((DWORD*)LockResource(LoadResource(hm, rsc)), ptr);
    }
    else if (t == ShaderType::T_VS)
    {
        // Create the shader declaration.
        static constexpr DWORD dwDecl[] =
        {
            D3DVSD_STREAM(0),
            D3DVSD_REG(D3DVSDE_POSITION,  D3DVSDT_FLOAT3),

            D3DVSD_REG(D3DVSDE_DIFFUSE,  D3DVSDT_D3DCOLOR),
            D3DVSD_END()
        };

        d3d8device->CreateVertexShader(dwDecl, (DWORD*)LockResource(LoadResource(hm, rsc)), ptr, 0);
    }
}

template <class T>
std::tuple<LPVOID, UINT> LoadTexture(T ID)
{
    HMODULE hm = NULL;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&lastDecalY, &hm);
    auto rsc = FindResource(hm, MAKEINTRESOURCE(ID), RT_RCDATA);
    return std::make_tuple(LockResource(LoadResource(hm, rsc)), SizeofResource(hm, rsc));
}

DWORD lastFVF = 0;
DWORD zEnable;

size_t numWaterFrames;
int32_t waterFrame = 0;

bool shaderx2 = false;
bool shaderSunset = false;

bool shipMode = false;
bool noFogMode = false;

bool reflMode = false;
bool decalMode = false;
bool firstPass = false;

bool skyboxMode = false;
bool glowMode = false;
bool freshScene = false;

struct decalCall
{
    IDirect3DVertexBuffer8* VB;
    IDirect3DIndexBuffer8* IB;

    UINT stride;
    UINT baseVertIndex;

    UINT minIndex, NumVertices, startIndex, primCount;

    D3DXMATRIX world, view, proj;
};

decalCall calls[50];
int32_t callID = 0;

inline void CreateDeviceCB(LPDIRECT3DDEVICE8 d3d8device)
{
    d3d8device->GetDepthStencilSurface(&beforeReflDSS);
    d3d8device->GetRenderTarget(&beforeReflSurf);

    D3DSURFACE_DESC desc;
    beforeReflDSS->GetDesc(&desc);
    sWidth = desc.Width;
    sHeight = desc.Height;

    LoadShader(d3d8device, IDR_TESTPS, &PS, T_PS);
    LoadShader(d3d8device, IDR_TESTPS_NIGHT, &PSx8, T_PS);
    LoadShader(d3d8device, IDR_TESTPS_SUNSET, &PSx2, T_PS);
    LoadShader(d3d8device, IDR_TESTPS_X2, &PSxS, T_PS);
    LoadShader(d3d8device, IDR_TESTVS, &VS, T_VS);

    static constexpr DWORD WaterTexArray[] = {
        IDR_WATER_NORMAL_0, IDR_WATER_NORMAL_1, IDR_WATER_NORMAL_2, IDR_WATER_NORMAL_3, IDR_WATER_NORMAL_4, IDR_WATER_NORMAL_5, IDR_WATER_NORMAL_6,
        IDR_WATER_NORMAL_7, IDR_WATER_NORMAL_8, IDR_WATER_NORMAL_9, IDR_WATER_NORMAL_10, IDR_WATER_NORMAL_11, IDR_WATER_NORMAL_12, IDR_WATER_NORMAL_13,
        IDR_WATER_NORMAL_14, IDR_WATER_NORMAL_15, IDR_WATER_NORMAL_16, IDR_WATER_NORMAL_17, IDR_WATER_NORMAL_18, IDR_WATER_NORMAL_19, IDR_WATER_NORMAL_20,
        IDR_WATER_NORMAL_21, IDR_WATER_NORMAL_22, IDR_WATER_NORMAL_23, IDR_WATER_NORMAL_24, IDR_WATER_NORMAL_25, IDR_WATER_NORMAL_26, IDR_WATER_NORMAL_27,
        IDR_WATER_NORMAL_28
    };

    numWaterFrames = std::size(WaterTexArray);

    for each (auto id in WaterTexArray)
    {
        auto[SrcData, SrcDataSize] = LoadTexture(id);
        IDirect3DTexture8* t = nullptr;
        if (D3DXCreateTextureFromFileInMemory(d3d8device, SrcData, SrcDataSize, &t) == D3D_OK)
        {
            refrTex.push_back(t);
        }
    }

    auto hr = d3d8device->CreateTexture(sWidth, sHeight, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &reflTex);
    assert(hr == D3D_OK);

    reflTex->GetSurfaceLevel(0, &reflSurf);

    hr = d3d8device->CreateDepthStencilSurface(sWidth, sHeight, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &reflDSS);
    assert(hr == D3D_OK);

    d3d8device->SetRenderTarget(reflSurf, reflDSS);
    d3d8device->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xFFFFFFFF, 1, 0);
    d3d8device->SetRenderTarget(beforeReflSurf, beforeReflDSS);
}

inline void TestCooperativeLevelCB(LPDIRECT3DDEVICE8 d3d8device)
{
    //destroyData
    beforeReflSurf->Release();
    beforeReflDSS->Release();
    reflSurf->Release();
    reflTex->Release();
    reflDSS->Release();

    callID = 0;

    d3d8device->DeletePixelShader(PS);
    d3d8device->DeletePixelShader(PSx8);
    d3d8device->DeletePixelShader(PSx2);
    d3d8device->DeletePixelShader(PSxS);
    d3d8device->DeleteVertexShader(VS);
}

inline void ResetCB(LPDIRECT3DDEVICE8 d3d8device)
{
    //recreate
    d3d8device->GetDepthStencilSurface(&beforeReflDSS);
    d3d8device->GetRenderTarget(&beforeReflSurf);

    auto hr = d3d8device->CreateTexture(sWidth, sHeight, 1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &reflTex);
    assert(hr == D3D_OK);

    reflTex->GetSurfaceLevel(0, &reflSurf);

    hr = d3d8device->CreateDepthStencilSurface(sWidth, sHeight, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &reflDSS);
    assert(hr == D3D_OK);

    LoadShader(d3d8device, IDR_TESTPS, &PS, T_PS);
    LoadShader(d3d8device, IDR_TESTPS_NIGHT, &PSx8, T_PS);
    LoadShader(d3d8device, IDR_TESTPS_SUNSET, &PSx2, T_PS);
    LoadShader(d3d8device, IDR_TESTPS_X2, &PSxS, T_PS);
    LoadShader(d3d8device, IDR_TESTVS, &VS, T_VS);
}

inline void BeginSceneCB(LPDIRECT3DDEVICE8 d3d8device)
{
    afterWater = false;
    freshScene = true;
    firstPass = true;

    auto delay = 1000 / 30;
    waterFrame = (timeGetTime() / delay) % numWaterFrames;
}

inline void SetTransformCB(LPDIRECT3DDEVICE8 d3d8device, D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix)
{
    if (State == D3DTS_WORLD)
    {
        worldMtx = *pMatrix;

        if ((((float*)worldMtx)[0]) < -1400)
        {
            glowMode = true;
            reflMode = true;
        }
        else if (abs(((float*)worldMtx)[0]) > 400)
        {
            reflMode = true;
        }
        else if (abs(((float*)worldMtx)[0]) > 100)
        {
            if (!skyboxMode)
                reflMode = false;

            saveDecalMode = true;
        }
        else
        {
            if (!skyboxMode)
                reflMode = false;

            saveDecalMode = false;
        }
    }
    if (State == D3DTS_VIEW)
        viewMtx = *pMatrix;

    if (State == D3DTS_PROJECTION)
        projMtx = *pMatrix;
}

inline void SetRenderStateCB(LPDIRECT3DDEVICE8 d3d8device, D3DRENDERSTATETYPE State, DWORD Value)
{
    if (State == D3DRS_FOGENABLE)
    {
        wasFog = Value;
        if (Value == FALSE)
        {
            skyboxMode = true;
            reflMode = true;
        }
        else
        {
            reflMode = false;
            skyboxMode = false;
        }
    }

    if (State == D3DRS_FOGCOLOR)
    {
        fogColor = Value;
    }

    if (State == D3DRS_ZENABLE)
    {
        zEnable = Value;
    }

    if (State == D3DRS_ALPHABLENDENABLE)
    {
        if ((Value == TRUE) && (zEnable))
        {
            DWORD pStateBlock = NULL;
            d3d8device->CreateStateBlock(D3DSBT_ALL, &pStateBlock);

            d3d8device->GetVertexShader(&lastFVF);
#ifdef USE_D3D_HOOK
            RealD3D8SetRenderState(d3d8device, D3DRS_FOGENABLE, FALSE);
            RealD3D8SetRenderState(d3d8device, D3DRS_SOFTWAREVERTEXPROCESSING, FALSE);
#else
            d3d8device->SetRenderState(D3DRS_FOGENABLE, FALSE);
            d3d8device->SetRenderState(D3DRS_SOFTWAREVERTEXPROCESSING, FALSE);
#endif
            //cout << "drawing "<<callID<<" chunks of water\n";

            for (auto i = 0; i < callID; i++)
            {
                d3d8device->SetStreamSource(0, calls[i].VB, calls[i].stride);
                d3d8device->SetIndices(calls[i].IB, calls[i].baseVertIndex);

                d3d8device->SetTexture(0, reflTex);
                d3d8device->SetTexture(1, refrTex[waterFrame]);

                d3d8device->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
                d3d8device->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);

                d3d8device->SetVertexShader(VS);

                if (shaderSunset)
                {
                    d3d8device->SetPixelShader(PSxS);
                }
                else if (waterColor.x < 0.001)
                {
                    d3d8device->SetPixelShader(PSx8);
                }
                else if (shaderx2)
                {
                    d3d8device->SetPixelShader(PSx2);
                }
                else
                {
                    d3d8device->SetPixelShader(PS);
                }

                if (shipMode)
                {
                    float s = 0.05f;
                    tiling[0] = s;
                    tiling[1] = s;
                    tiling[2] = s;
                    tiling[3] = s;
                    d3d8device->SetVertexShaderConstant(11, (void*)&tiling, 1);//tiling for parnik mission
                }

                D3DXMATRIX matTrans = calls[i].world*(viewMtx*calls[i].proj);
                D3DXMatrixTranspose(&matTrans, &matTrans);
                d3d8device->SetVertexShaderConstant(0, (void*)&matTrans, 4);
                D3DXMatrixTranspose(&matTrans, &calls[i].world);
                d3d8device->SetVertexShaderConstant(4, (void*)&matTrans, 4);

                float s = 0.5f;
                zeroFive[0] = s;
                zeroFive[1] = s;
                zeroFive[2] = s;
                zeroFive[3] = s;
                d3d8device->SetVertexShaderConstant(10, (void*)&zeroFive, 1);

                if (!shipMode)
                {
                    s = 0.1f;
                    tiling[0] = s;
                    tiling[1] = s;
                    tiling[2] = s;
                    tiling[3] = s;
                    d3d8device->SetVertexShaderConstant(11, (void*)&tiling, 1);//tiling
                }

                D3DXMATRIX camWorld;
                D3DXMatrixInverse(&camWorld, NULL, &viewMtx);
                posCam[0] = camWorld._41;
                posCam[1] = camWorld._42;
                posCam[2] = camWorld._43;
                posCam[3] = 1.0f;
                d3d8device->SetVertexShaderConstant(12, (void*)&posCam, 1);
                d3d8device->SetPixelShaderConstant(2, (void*)&waterColor, 1);

                fogColorS[0] = ((char*)&fogColor)[2] / 255.0f;
                fogColorS[1] = ((char*)&fogColor)[1] / 255.0f;
                fogColorS[2] = ((char*)&fogColor)[0] / 255.0f;
                fogColorS[3] = 1;
                d3d8device->SetPixelShaderConstant(5, (void*)&fogColorS, 1);

                //cout << "waterColor: " << waterColor.x << ", " << waterColor.y << ", " << waterColor.z << endl;
#ifdef USE_D3D_HOOK
                RealD3D8DrawIndexedPrimitive(d3d8device, D3DPT_TRIANGLELIST, calls[i].minIndex, calls[i].NumVertices, calls[i].startIndex, calls[i].primCount);
#else
                d3d8device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, calls[i].minIndex, calls[i].NumVertices, calls[i].startIndex, calls[i].primCount);
#endif
            }
            d3d8device->SetVertexShader(lastFVF);
            d3d8device->SetPixelShader(0);
            d3d8device->SetTextureStageState(0, D3DTSS_ADDRESSU, D3DTADDRESS_WRAP);
            d3d8device->SetTextureStageState(0, D3DTSS_ADDRESSV, D3DTADDRESS_WRAP);
#ifdef USE_D3D_HOOK
            RealD3D8SetRenderState(d3d8device, D3DRS_FOGENABLE, wasFog);
#else
            d3d8device->SetRenderState(D3DRS_FOGENABLE, wasFog);
#endif
            lastDecalY = calls[callID - 1].world._42;
            decalMode = false;
            afterWater = true;
            callID = 0;

            d3d8device->ApplyStateBlock(pStateBlock);
            d3d8device->DeleteStateBlock(pStateBlock);
        }
    }
}

inline void SetTextureCB(LPDIRECT3DDEVICE8 d3d8device, DWORD Stage, IDirect3DBaseTexture8* pTexture)
{
    lastTex = (IDirect3DTexture8*)pTexture;
}

inline HRESULT WINAPI DrawIndexedPrimitiveCB(LPDIRECT3DDEVICE8 d3d8device, D3DPRIMITIVETYPE pt, UINT minIndex, UINT NumVertices, UINT startIndex, UINT primCount)
{
    if ((skyboxMode) && (primCount > 12))
    {
        if (!glowMode)
        {
            skyboxMode = false;
            reflMode = false;
        }
    }

    if (saveDecalMode)
    {
        calls[callID].VB = actVB;
        calls[callID].stride = actStride;
        calls[callID].IB = actIB;
        calls[callID].baseVertIndex = actBaseVertIndex;
        calls[callID].minIndex = minIndex;
        calls[callID].NumVertices = NumVertices;
        calls[callID].startIndex = startIndex;
        calls[callID].primCount = primCount;
        calls[callID].world = worldMtx;
        //calls[callID].world._42 = -5;
        //calls[callID].view = viewMtx;
        calls[callID].proj = projMtx;
        callID++;
        return D3D_OK;
    }

    if (!reflMode)
    {
#ifdef USE_D3D_HOOK
        return RealD3D8DrawIndexedPrimitive(d3d8device, pt, minIndex, NumVertices, startIndex, primCount);
#else
        return d3d8device->DrawIndexedPrimitive(pt, minIndex, NumVertices, startIndex, primCount);
#endif
    }

    //enterReflMode
    d3d8device->SetRenderTarget(reflSurf, reflDSS);
    if (firstPass)
    {
        d3d8device->Clear(0, NULL, D3DCLEAR_ZBUFFER, 0xFFFFFFFF, 1, 0);
        firstPass = false;
    }

    if (glowMode)
    {
#ifdef USE_D3D_HOOK
        RealD3D8SetRenderState(d3d8device, D3DRS_ALPHABLENDENABLE, TRUE);
        RealD3D8SetRenderState(d3d8device, D3DRS_SRCBLEND, D3DBLEND_ONE);
        RealD3D8SetRenderState(d3d8device, D3DRS_DESTBLEND, D3DBLEND_ONE);
#else
        d3d8device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
        d3d8device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
        d3d8device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);
#endif
    }

    float pln[4] = { 0.0f, -1.0f, 0.0f, 9.5f };
    if ((!skyboxMode) || (glowMode))
    {
        if (!shipMode)
        {
            d3d8device->SetClipPlane(0, pln);
#ifdef USE_D3D_HOOK
            RealD3D8SetRenderState(d3d8device, D3DRS_CLIPPLANEENABLE, D3DCLIPPLANE0);
#else
            d3d8device->SetRenderState(D3DRS_CLIPPLANEENABLE, D3DCLIPPLANE0);
#endif
        }
    }

    if (waterColor.x < 0.001f)
    {
#ifdef USE_D3D_HOOK
        RealD3D8SetRenderState(d3d8device, D3DRS_FOGENABLE, FALSE);
#else
        d3d8device->SetRenderState(D3DRS_FOGENABLE, FALSE);
#endif

    }

    if (shaderSunset)
    {
        uint8_t fcol[4] = { 27, 33, 35, 255 };
#ifdef USE_D3D_HOOK
        RealD3D8SetRenderState(d3d8device, D3DRS_FOGCOLOR, *(DWORD*)&fcol);
#else
        d3d8device->SetRenderState(D3DRS_FOGCOLOR, *(DWORD*)&fcol);
#endif
    }
    if (noFogMode)
    {
        uint8_t fcol[4] = { 72, 68, 52, 255 };
#ifdef USE_D3D_HOOK
        RealD3D8SetRenderState(d3d8device, D3DRS_FOGCOLOR, *(DWORD*)&fcol);
#else
        d3d8device->SetRenderState(D3DRS_FOGCOLOR, *(DWORD*)&fcol);
#endif
    }

#ifdef USE_D3D_HOOK
    HRESULT hh = RealD3D8DrawIndexedPrimitive(d3d8device, pt, minIndex, NumVertices, startIndex, primCount);
#else
    HRESULT hh = d3d8device->DrawIndexedPrimitive(pt, minIndex, NumVertices, startIndex, primCount);
#endif

    if (glowMode)
    {
#ifdef USE_D3D_HOOK
        RealD3D8SetRenderState(d3d8device, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        RealD3D8SetRenderState(d3d8device, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        RealD3D8SetRenderState(d3d8device, D3DRS_ALPHABLENDENABLE, FALSE);
#else
        d3d8device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        d3d8device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        d3d8device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
#endif
    }
    if ((!skyboxMode) || (glowMode))
    {
#ifdef USE_D3D_HOOK
        RealD3D8SetRenderState(d3d8device, D3DRS_CLIPPLANEENABLE, 0);
#else
        d3d8device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
#endif
    }

    if (waterColor.x < 0.001f)
    {
#ifdef USE_D3D_HOOK
        RealD3D8SetRenderState(d3d8device, D3DRS_FOGENABLE, wasFog);
#else
        d3d8device->SetRenderState(D3DRS_FOGENABLE, wasFog);
#endif
    }

    if (shaderSunset || noFogMode)
    {
#ifdef USE_D3D_HOOK
        RealD3D8SetRenderState(d3d8device, D3DRS_FOGCOLOR, fogColor);
#else
        d3d8device->SetRenderState(D3DRS_FOGCOLOR, fogColor);
#endif
    }

    d3d8device->SetRenderTarget(beforeReflSurf, beforeReflDSS); //exitReflMode()

    // duplicate to backbuffer
    if (skyboxMode && (!glowMode))
    {
        //cout << "waterColor: "<<worldMtx._41<<", "<<worldMtx._42<<", "<<worldMtx._43<<endl;
        if (zEnable == FALSE)
        {
            IDirect3DSurface8* lsurf;
            lastTex->GetSurfaceLevel(0, &lsurf);
            D3DSURFACE_DESC desc;
            lsurf->GetDesc(&desc);
            lsurf->Release();

            if (desc.Width >= 256)
            {
                //cout << "probably found skybox: " << desc.Width << endl;
                waterColor = D3DXVECTOR3(
                    D3DXVec3Length((D3DXVECTOR3*)&worldMtx._11) - 1,
                    D3DXVec3Length((D3DXVECTOR3*)&worldMtx._21) - 1,
                    D3DXVec3Length((D3DXVECTOR3*)&worldMtx._31) - 1);
                shaderx2 = false;
                shaderSunset = false;
                shipMode = false;
                noFogMode = false;
                if (waterColor.x > 0.5f)
                {
                    shaderx2 = true;
                    waterColor.x -= 0.5f;
                }
                else if ((waterColor.x < 0.001f) && (waterColor.y < 0.001f) && (waterColor.z < 0.001f))
                {
                    shaderSunset = true;
                    waterColor = D3DXVECTOR3(0.125f, 0.118f, 0.098f);
                }
                else if (waterColor.z < 0.001f)
                {
                    shipMode = true;
                }
                else if (waterColor.y < 0.001f)
                {
                    noFogMode = true;
                }

#ifdef USE_D3D_HOOK
                hh = RealD3D8DrawIndexedPrimitive(d3d8device, pt, minIndex, NumVertices, startIndex, primCount);
#else
                hh = d3d8device->DrawIndexedPrimitive(pt, minIndex, NumVertices, startIndex, primCount);
#endif
            }
            else
            {
#ifdef USE_D3D_HOOK
                hh = RealD3D8DrawIndexedPrimitive(d3d8device, pt, minIndex, NumVertices, startIndex, primCount);
#else
                hh = d3d8device->DrawIndexedPrimitive(pt, minIndex, NumVertices, startIndex, primCount);
#endif
            }
        }
        else
        {
#ifdef USE_D3D_HOOK
            hh = RealD3D8DrawIndexedPrimitive(d3d8device, pt, minIndex, NumVertices, startIndex, primCount);
#else
            hh = d3d8device->DrawIndexedPrimitive(pt, minIndex, NumVertices, startIndex, primCount);
#endif
        }
    }
    glowMode = false;

    return hh;
}

inline void SetStreamSourceCB(LPDIRECT3DDEVICE8 d3d8device, UINT StreamNumber, IDirect3DVertexBuffer8* pStreamData, UINT Stride)
{
    actStream = StreamNumber;
    actVB = pStreamData;
    actStride = Stride;
}

inline void SetIndicesCB(LPDIRECT3DDEVICE8 d3d8device, IDirect3DIndexBuffer8* pIndexData, UINT BaseVertexIndex)
{
    actIB = pIndexData;
    actBaseVertIndex = BaseVertexIndex;
}

HRESULT WINAPI TestCooperativeLevel(LPDIRECT3DDEVICE8 d3d8device)
{
    TestCooperativeLevelCB(d3d8device);

#ifdef USE_D3D_HOOK
    return RealD3D8TestCooperativeLevel(d3d8device);
#else
    return d3d8device->TestCooperativeLevel();
#endif
}

HRESULT WINAPI Reset(LPDIRECT3DDEVICE8 d3d8device, D3DPRESENT_PARAMETERS* pPresentationParameters)
{
#ifdef USE_D3D_HOOK
    HRESULT h = RealD3D8Reset(d3d8device, pPresentationParameters);
#else
    HRESULT h = d3d8device->Reset(pPresentationParameters);
#endif

    if (h == D3D_OK)
    {
        ResetCB(d3d8device);
    }
    return h;
}

HRESULT WINAPI BeginScene(LPDIRECT3DDEVICE8 d3d8device)
{
    BeginSceneCB(d3d8device);

#ifdef USE_D3D_HOOK
    return RealD3D8BeginScene(d3d8device);
#else
    return d3d8device->BeginScene();
#endif
}

HRESULT WINAPI SetTransform(LPDIRECT3DDEVICE8 d3d8device, D3DTRANSFORMSTATETYPE State, CONST D3DMATRIX* pMatrix)
{
    SetTransformCB(d3d8device, State, pMatrix);

#ifdef USE_D3D_HOOK
    return RealD3D8SetTransform(d3d8device, State, pMatrix);
#else
    return d3d8device->SetTransform(State, pMatrix);
#endif
}

HRESULT WINAPI SetRenderState(LPDIRECT3DDEVICE8 d3d8device, D3DRENDERSTATETYPE State, DWORD Value)
{
    SetRenderStateCB(d3d8device, State, Value);

#ifdef USE_D3D_HOOK
    return RealD3D8SetRenderState(d3d8device, State, Value);
#else
    return d3d8device->SetRenderState(State, Value);
#endif

}

HRESULT WINAPI SetTexture(LPDIRECT3DDEVICE8 d3d8device, DWORD Stage, IDirect3DBaseTexture8* pTexture)
{
    SetTextureCB(d3d8device, Stage, pTexture);

#ifdef USE_D3D_HOOK
    return RealD3D8SetTexture(d3d8device, Stage, pTexture);
#else
    return d3d8device->SetTexture(Stage, pTexture);
#endif
}

HRESULT WINAPI DrawIndexedPrimitive(LPDIRECT3DDEVICE8 d3d8device, D3DPRIMITIVETYPE pt, UINT minIndex, UINT NumVertices, UINT startIndex, UINT primCount)
{
    return DrawIndexedPrimitiveCB(d3d8device, pt, minIndex, NumVertices, startIndex, primCount);
}

HRESULT WINAPI SetStreamSource(LPDIRECT3DDEVICE8 d3d8device, UINT StreamNumber, IDirect3DVertexBuffer8* pStreamData, UINT Stride)
{
    SetStreamSourceCB(d3d8device, StreamNumber, pStreamData, Stride);

#ifdef USE_D3D_HOOK
    return RealD3D8SetStreamSource(d3d8device, StreamNumber, pStreamData, Stride);
#else
    return d3d8device->SetStreamSource(StreamNumber, pStreamData, Stride);
#endif
}

HRESULT WINAPI SetIndices(LPDIRECT3DDEVICE8 d3d8device, IDirect3DIndexBuffer8* pIndexData, UINT BaseVertexIndex)
{
    SetIndicesCB(d3d8device, pIndexData, BaseVertexIndex);

#ifdef USE_D3D_HOOK
    return RealD3D8SetIndices(d3d8device, pIndexData, BaseVertexIndex);
#else
    return d3d8device->SetIndices(pIndexData, BaseVertexIndex);
#endif

}

#ifdef USE_D3D_HOOK
HRESULT WINAPI CreateDevice(IDirect3D8* d3d8i, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice8** ppReturnedDeviceInterface)
{
    HRESULT r = RealD3D8CreateDevice(d3d8i, Adapter, DeviceType, hFocusWindow, D3DCREATE_MIXED_VERTEXPROCESSING, pPresentationParameters, ppReturnedDeviceInterface);

    pVTable = (UINT_PTR*)(*((UINT_PTR*)*ppReturnedDeviceInterface));

    if (!RealD3D8TestCooperativeLevel)
        RealD3D8TestCooperativeLevel = (TestCooperativeLevel_t)pVTable[IDirect3DDevice8VTBL::TestCooperativeLevel];

    if (!RealD3D8Reset)
        RealD3D8Reset = (Reset_t)pVTable[IDirect3DDevice8VTBL::Reset];

    if (!RealD3D8BeginScene)
        RealD3D8BeginScene = (BeginScene_t)pVTable[IDirect3DDevice8VTBL::BeginScene];

    if (!RealD3D8SetTransform)
        RealD3D8SetTransform = (SetTransform_t)pVTable[IDirect3DDevice8VTBL::SetTransform];

    if (!RealD3D8SetRenderState)
        RealD3D8SetRenderState = (SetRenderState_t)pVTable[IDirect3DDevice8VTBL::SetRenderState];

    if (!RealD3D8SetTexture)
        RealD3D8SetTexture = (SetTexture_t)pVTable[IDirect3DDevice8VTBL::SetTexture];

    if (!RealD3D8DrawIndexedPrimitive)
        RealD3D8DrawIndexedPrimitive = (DrawIndexedPrimitive_t)pVTable[IDirect3DDevice8VTBL::DrawIndexedPrimitive];

    if (!RealD3D8SetStreamSource)
        RealD3D8SetStreamSource = (SetStreamSource_t)pVTable[IDirect3DDevice8VTBL::SetStreamSource];

    if (!RealD3D8SetIndices)
        RealD3D8SetIndices = (SetIndices_t)pVTable[IDirect3DDevice8VTBL::SetIndices];

    std::thread([]()
        {
            while (true)
            {
                using namespace std::chrono_literals;
                std::this_thread::sleep_for(2s);
                if (pVTable)
                {
                    injector::WriteMemory(&pVTable[IDirect3DDevice8VTBL::TestCooperativeLevel], &TestCooperativeLevel, true);
                    injector::WriteMemory(&pVTable[IDirect3DDevice8VTBL::Reset], &Reset, true);
                    injector::WriteMemory(&pVTable[IDirect3DDevice8VTBL::BeginScene], &BeginScene, true);
                    injector::WriteMemory(&pVTable[IDirect3DDevice8VTBL::SetTransform], &SetTransform, true);
                    injector::WriteMemory(&pVTable[IDirect3DDevice8VTBL::SetRenderState], &SetRenderState, true);
                    injector::WriteMemory(&pVTable[IDirect3DDevice8VTBL::SetTexture], &SetTexture, true);
                    injector::WriteMemory(&pVTable[IDirect3DDevice8VTBL::DrawIndexedPrimitive], &DrawIndexedPrimitive, true);
                    injector::WriteMemory(&pVTable[IDirect3DDevice8VTBL::SetStreamSource], &SetStreamSource, true);
                    injector::WriteMemory(&pVTable[IDirect3DDevice8VTBL::SetIndices], &SetIndices, true);
                }
            }
        }).detach();

        CreateDeviceCB(*ppReturnedDeviceInterface);

        return r;
}
#endif 

void Init()
{
#ifdef USE_D3D_HOOK
    auto pattern = hook::pattern(GetModuleHandle(ls3df.c_str()), "E8 ? ? ? ? 85 C0 A3 ? ? ? ? 75 1F 68 ? ? ? ? E8 ? ? ? ? 83 C4 04");
    static injector::hook_back<IDirect3D8*(WINAPI*)(UINT)> Direct3DCreate8;
    auto Direct3DCreate8Hook = [](UINT SDKVersion) -> IDirect3D8*
    {
        auto pID3D8 = Direct3DCreate8.fun(SDKVersion);
        auto pVTable = (UINT_PTR*)(*((UINT_PTR*)pID3D8));
        if (!RealD3D8CreateDevice)
            RealD3D8CreateDevice = (CreateDevice_t)pVTable[IDirect3D8VTBL::CreateDevice];
        injector::WriteMemory(&pVTable[IDirect3D8VTBL::CreateDevice], &CreateDevice, true);
        return pID3D8;
    }; Direct3DCreate8.fun = injector::MakeCALL(pattern.get_first(0), static_cast<IDirect3D8*(WINAPI*)(UINT)>(Direct3DCreate8Hook), true).get();
#else

    auto pattern = hook::pattern(GetModuleHandle(ls3df.c_str()), "68 ? ? ? ? 68 ? ? ? ? 1B D2 83 E2 60 83 C2 20 83 CA 04");
    static auto d3ddevaddr = *pattern.get_first<void*>(1);

    //CreateDevice
    pattern = hook::pattern(GetModuleHandle(ls3df.c_str()), "A1 ? ? ? ? 8B 08 6A 02 6A 11 6A 00");
    struct CreateDeviceHook
    {
        void operator()(injector::reg_pack& regs)
        {
            regs.eax = *(uint32_t*)d3ddevaddr;
            CreateDeviceCB((LPDIRECT3DDEVICE8)regs.eax);
        }
    }; injector::MakeInline<CreateDeviceHook>(pattern.get_first(0));

    //DrawIndexedPrimitive
    pattern = hook::pattern(GetModuleHandle(ls3df.c_str()), "FF 91 1C 01 00 00");
    for (size_t i = 0; i < pattern.size(); i++)
    {
        auto rp = hook::pattern((uintptr_t)pattern.get(i).get<uintptr_t>(-64), (uintptr_t)pattern.get(i).get<uintptr_t>(0), pattern_str(to_bytes(d3ddevaddr)));
        if (!rp.empty())
        {
            injector::MakeNOP(pattern.get(i).get<void>(0), 6, true);
            injector::MakeCALL(pattern.get(i).get<void>(0), DrawIndexedPrimitive, true);
        }
    }

    //SetIndices
    pattern = hook::pattern(GetModuleHandle(ls3df.c_str()), "FF 91 54 01 00 00");
    for (size_t i = 0; i < pattern.size(); i++)
    {
        auto rp = hook::pattern((uintptr_t)pattern.get(i).get<uintptr_t>(-64), (uintptr_t)pattern.get(i).get<uintptr_t>(0), pattern_str(to_bytes(d3ddevaddr)));
        if (!rp.empty())
        {
            injector::MakeNOP(pattern.get(i).get<void>(0), 6, true);
            injector::MakeCALL(pattern.get(i).get<void>(0), SetIndices, true);
        }
    }

    //SetStreamSource
    pattern = hook::pattern(GetModuleHandle(ls3df.c_str()), "FF 91 4C 01 00 00");
    for (size_t i = 0; i < pattern.size(); i++)
    {
        auto rp = hook::pattern((uintptr_t)pattern.get(i).get<uintptr_t>(-64), (uintptr_t)pattern.get(i).get<uintptr_t>(0), pattern_str(to_bytes(d3ddevaddr)));
        if (!rp.empty())
        {
            injector::MakeNOP(pattern.get(i).get<void>(0), 6, true);
            injector::MakeCALL(pattern.get(i).get<void>(0), SetStreamSource, true);
        }
    }

    //SetTexture
    pattern = hook::pattern(GetModuleHandle(ls3df.c_str()), "FF 91 F4 00 00 00");
    for (size_t i = 0; i < pattern.size(); i++)
    {
        auto rp = hook::pattern((uintptr_t)pattern.get(i).get<uintptr_t>(-64), (uintptr_t)pattern.get(i).get<uintptr_t>(0), pattern_str(to_bytes(d3ddevaddr)));
        if (!rp.empty())
        {
            injector::MakeNOP(pattern.get(i).get<void>(0), 6, true);
            injector::MakeCALL(pattern.get(i).get<void>(0), SetTexture, true);
        }
    }

    //SetRenderState
    pattern = hook::pattern(GetModuleHandle(ls3df.c_str()), "FF ? C8 00 00 00");
    for (size_t i = 0; i < pattern.size(); i++)
    {
        auto rp = hook::pattern((uintptr_t)pattern.get(i).get<uintptr_t>(-64), (uintptr_t)pattern.get(i).get<uintptr_t>(0), pattern_str(to_bytes(d3ddevaddr)));
        if (!rp.empty())
        {
            auto b = *pattern.get(i).get<uint8_t>(1);
            if (b < 0xA0) // call
            {
                injector::MakeNOP(pattern.get(i).get<void>(0), 6, true);
                injector::MakeCALL(pattern.get(i).get<void>(0), SetRenderState, true);
            }
            else //jmp
            {
                injector::MakeNOP(pattern.get(i).get<void>(0), 6, true);
                injector::MakeJMP(pattern.get(i).get<void>(0), SetRenderState, true);
            }
        }
    }

    //SetTransform
    pattern = hook::pattern(GetModuleHandle(ls3df.c_str()), "FF ? 94 00 00 00");
    for (size_t i = 0; i < pattern.size(); i++)
    {
        auto rp = hook::pattern((uintptr_t)pattern.get(i).get<uintptr_t>(-64), (uintptr_t)pattern.get(i).get<uintptr_t>(0), pattern_str(to_bytes(d3ddevaddr)));
        if (!rp.empty())
        {
            auto b = *pattern.get(i).get<uint8_t>(1);
            if (b < 0xA0) // call
            {
                injector::MakeNOP(pattern.get(i).get<void>(0), 6, true);
                injector::MakeCALL(pattern.get(i).get<void>(0), SetTransform, true);
            }
            else //jmp
            {
                injector::MakeNOP(pattern.get(i).get<void>(0), 6, true);
                injector::MakeJMP(pattern.get(i).get<void>(0), SetTransform, true);
            }
        }
    }

    //BeginScene
    pattern = hook::pattern(GetModuleHandle(ls3df.c_str()), "FF 92 88 00 00 00");
    for (size_t i = 0; i < pattern.size(); i++)
    {
        injector::MakeNOP(pattern.get(i).get<void>(0), 6, true);
        injector::MakeCALL(pattern.get(i).get<void>(0), BeginScene, true);
    }

    //TestCooperativeLevel
    pattern = hook::pattern(GetModuleHandle(ls3df.c_str()), "A1 ? ? ? ? 8B 08 68 ? ? ? ? 50 FF ? 38");
    struct ResetBeforeHook
    {
        void operator()(injector::reg_pack& regs)
        {
            regs.eax = *(uint32_t*)d3ddevaddr;
            TestCooperativeLevelCB((LPDIRECT3DDEVICE8)regs.eax);
        }
    }; injector::MakeInline<ResetBeforeHook>(pattern.get_first(0));

    //Reset
    pattern = hook::pattern(GetModuleHandle(ls3df.c_str()), "A1 ? ? ? ? 85 C0 74 ? 33 F6");
    struct ResetAfterHook
    {
        void operator()(injector::reg_pack& regs)
        {
            regs.eax = *(uint32_t*)d3ddevaddr;
            ResetCB((LPDIRECT3DDEVICE8)regs.eax);
        }
    }; injector::MakeInline<ResetAfterHook>(pattern.get_first(0));
#endif
}

extern "C" __declspec(dllexport) void InitializeASI()
{
    std::call_once(CallbackHandler::flag, []()
        {
            ModuleList dlls;
            dlls.Enumerate(ModuleList::SearchLocation::LocalOnly);
            for (auto& e : dlls.m_moduleList)
            {
                if (std::get<std::wstring>(e).substr(0, 2) == L"LS") //LS3DF.dll
                {
                    ls3df = std::get<std::wstring>(e) + L".dll";
                    CallbackHandler::RegisterCallback(ls3df, Init);
                    break;
                }
            }
        });
}

BOOL APIENTRY DllMain(HMODULE /*hModule*/, DWORD reason, LPVOID /*lpReserved*/)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        if (!IsUALPresent()) { InitializeASI(); }
    }
    return TRUE;
}