#include "Graphics/ImGuiLayer.h"

#include "Graphics/DescriptorAllocator.h"

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"

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
        *outCpu = handle.cpu;
        *outGpu = handle.gpu;
    }

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

    ImGui_ImplDX12_InitInfo info = {};
    info.Device            = device;
    info.CommandQueue      = commandQueue;
    // MUST match kFramesInFlight. The backend keeps one vertex/index buffer
    // per frame for exactly the same reason our FrameResources exist.
    info.NumFramesInFlight = framesInFlight;
    info.RTVFormat         = rtvFormat;
    info.DSVFormat         = dsvFormat;
    info.SrvDescriptorHeap = srvAllocator.Heap();
    info.SrvDescriptorAllocFn = SrvDescriptorAlloc;
    info.SrvDescriptorFreeFn  = SrvDescriptorFree;
    info.UserData          = &srvAllocator;

    if (!ImGui_ImplDX12_Init(&info))
    {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("ImGui_ImplDX12_Init failed");
    }
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

bool ImGuiLayer::WantsKeyboard() const
{
    return ImGui::GetIO().WantCaptureKeyboard;
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
