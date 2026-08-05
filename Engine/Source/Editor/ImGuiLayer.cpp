#include "Editor/ImGuiLayer.h"

#include "Graphics/DescriptorAllocator.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"

#include <filesystem>
#include <stdexcept>
#include <string>

// Declared by the backend but deliberately not in its header, so the app can
// choose whether to route messages to it.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg,
                                                             WPARAM wParam, LPARAM lParam);

namespace
{
    // The DX12 backend does not own a descriptor heap - it asks us for slots.
    // That is exactly what the 9.3 allocator exists for, so ImGui's font
    // atlas lands in the same heap as the scene textures with no special
    // casing anywhere.
    void SrvDescriptorAlloc(ImGui_ImplDX12_InitInfo* info,
                            D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
                            D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
    {
        auto* allocator = static_cast<DescriptorAllocator*>(info->UserData);
        const DescriptorHandle handle = allocator->Allocate();
        // The CPU handle is a staging address - where the backend writes its
        // view - and it stays valid forever because staging pages are never
        // moved.
        //
        // The "GPU handle" is deliberately NOT an address: under the resolve
        // contract it carries the slot INDEX, and SrvDescriptorResolve turns
        // it into an address at draw time. That is what lets this run from
        // inside RenderDrawData, mid-recording, without caring whether the
        // heap is about to be replaced.
        *outCpu = allocator->CpuHandle(handle);
        outGpu->ptr = handle.index;
    }

    // Called once per draw command, against whatever heap is current.
    D3D12_GPU_DESCRIPTOR_HANDLE SrvDescriptorResolve(ImGui_ImplDX12_InitInfo* info,
                                                     ImTextureID logicalId)
    {
        auto* allocator = static_cast<DescriptorAllocator*>(info->UserData);
        DescriptorHandle handle;
        handle.index = UINT(logicalId);
        return allocator->GpuHandle(handle);
    }

    // Resolved by CPU handle, not by the "GPU handle", because the latter is
    // a slot index under the resolve contract and the CPU address is the one
    // thing that is stable across every heap replacement.
    void SrvDescriptorFree(ImGui_ImplDX12_InitInfo* info,
                           D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                           D3D12_GPU_DESCRIPTOR_HANDLE)
    {
        auto* allocator = static_cast<DescriptorAllocator*>(info->UserData);
        allocator->FreeByCpuHandle(cpu);
    }
}

ImGuiLayer::ImGuiLayer(HWND hwnd, ID3D12Device* device,
                       ID3D12CommandQueue* commandQueue,
                       DescriptorAllocator& srvAllocator,
                       DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat,
                       int framesInFlight)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    // By default ImGui writes imgui.ini to the WORKING DIRECTORY, which for
    // a debugger launch is the project folder - it lands in the repo. Pin it
    // next to the exe, where the build output already lives and is ignored.
    static std::string iniPath;
    {
        char exePath[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string path(exePath);
        const size_t slash = path.find_last_of("\\/");
        iniPath = (slash == std::string::npos ? std::string() : path.substr(0, slash + 1))
                + "imgui.ini";
        io.IniFilename = iniPath.c_str();

        // Whether a layout was saved from a previous run, decided HERE because
        // this is the only place that knows where the file lives - and it has
        // to be asked before ImGui loads it, after which the two cases are
        // indistinguishable. The editor uses it to apply its default panel
        // arrangement without overwriting one the user has since arranged.
        m_hadSavedLayout = std::filesystem::exists(iniPath);
    }

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Docking from the start: Phase 10's editor needs it, and switching
    // later costs more than enabling it now.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(hwnd))
    {
        throw std::runtime_error("ImGui_ImplWin32_Init failed");
    }

    m_device         = device;
    m_commandQueue   = commandQueue;
    m_srvAllocator   = &srvAllocator;
    m_rtvFormat      = rtvFormat;
    m_dsvFormat      = dsvFormat;
    m_framesInFlight = framesInFlight;

    if (!InitBackend())
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("ImGui_ImplDX12_Init failed");
    }
}

bool ImGuiLayer::InitBackend()
{
    ImGui_ImplDX12_InitInfo info = {};
    info.Device            = m_device;
    info.CommandQueue      = m_commandQueue;
    // MUST match kFramesInFlight. The backend keeps one vertex/index buffer
    // per frame for exactly the same reason our FrameResources exist.
    info.NumFramesInFlight = m_framesInFlight;
    info.RTVFormat         = m_rtvFormat;
    info.DSVFormat         = m_dsvFormat;
    // Captured by the backend and bound by it in RenderDrawData. When the heap
    // is replaced, RebindDescriptorHeap updates this copy rather than the
    // backend being torn down.
    info.SrvDescriptorHeap = m_srvAllocator->Heap();
    info.SrvDescriptorAllocFn = SrvDescriptorAlloc;
    info.SrvDescriptorFreeFn  = SrvDescriptorFree;
    // Opts into logical texture ids. Without this the backend would treat
    // every ImTextureID as a raw GPU address, which stops being valid the
    // moment the heap grows - see the patch note in imgui_impl_dx12.h.
    info.SrvDescriptorResolveFn = SrvDescriptorResolve;
    info.UserData          = m_srvAllocator;

    return ImGui_ImplDX12_Init(&info);
}

void ImGuiLayer::RebindDescriptorHeap()
{
    ImGui_ImplDX12_RebindDescriptorHeap(m_srvAllocator->Heap());
}

ImGuiLayer::~ImGuiLayer()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiLayer::NewFrame()
{
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::Render(ID3D12GraphicsCommandList* commandList)
{
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}

bool ImGuiLayer::WantsMouse() const
{
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::WantsTextInput() const
{
    return ImGui::GetIO().WantTextInput;
}

bool ImGuiLayer::HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    // Guard against messages arriving before Init or after Shutdown.
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return false;
    }
    return ImGui_ImplWin32_WndProcHandler(hwnd, message, wParam, lParam) != 0;
}
