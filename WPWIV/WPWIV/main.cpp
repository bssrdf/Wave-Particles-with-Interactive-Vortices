#include <windows.h>
#include <wincodec.h>
#include <dinput.h>
#include "Scene.h"
#include "Renderer.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"

HWND hwnd = NULL;// Handle to the window
LPCTSTR WindowName = L"WPWIV";// name of the window (not the title)
LPCTSTR WindowTitle = L"WPWIV_1.0";// title of the window
int Width = 1024;// width and height of the window
int Height = 768;
IDirectInputDevice8* DIKeyboard;
IDirectInputDevice8* DIMouse;
DIMOUSESTATE mouseLastState;
BYTE keyboardLastState[256];
bool mouseAcquired = false;
LPDIRECTINPUT8 DirectInput;
bool FullScreen = false; // is window full screen?
bool Running = true; // we will exit the program when this becomes false
IDXGIFactory4* dxgiFactory;
ID3D12Device* device; // direct3d device
IDXGISwapChain3* swapChain; // swapchain used to switch between render targets
ID3D12CommandQueue* commandQueue; // container for command lists
ID3D12CommandAllocator* commandAllocator[FrameBufferCount]; // we want enough allocators for each buffer * number of threads (we only have one thread)
ID3D12GraphicsCommandList* commandList; // a command list we can record commands into, then execute them to render the frame
ID3D12Fence* fence[FrameBufferCount];    
HANDLE fenceEvent; // a handle to an event when our fence is unlocked by the gpu
UINT64 fenceValue[FrameBufferCount]; // this value is incremented each frame. each fence will have its own value
int frameIndex; // current rtv we are on

//Camera mCamera(XMFLOAT3(0.0f, 2.0f, -4.0f), XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), (float)Width, (float)Height, 45.0f, 0.1f, 1000.0f);
//Mesh mCube1(Mesh::MeshType::Cube, XMFLOAT3(-2, 0, 0), XMFLOAT3(1, 1, 1), XMFLOAT3(0, 0, 0));
//Mesh mCube2(Mesh::MeshType::Cube, XMFLOAT3(2, 0, 0), XMFLOAT3(1, 1, 1), XMFLOAT3(0, 0, 0));
//Mesh mPlane1(Mesh::MeshType::Plane, XMFLOAT3(-2, -1, 0), XMFLOAT3(1, 1, 1), XMFLOAT3(0, 0, 0));
//Mesh mPlane2(Mesh::MeshType::Plane, XMFLOAT3(2, -1, 0), XMFLOAT3(1, 1, 1), XMFLOAT3(0, 0, 0));
OrbitCamera mCamera(4.f, 0.f, 0.f, XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f), (float)Width, (float)Height, 45.0f, 0.1f, 1000.0f);
Mesh mPlane(Mesh::MeshType::Plane, XMFLOAT3(0, 0, 0), XMFLOAT3(1, 1, 1), XMFLOAT3(0, 0, 0));
Renderer mRenderer;
Shader mVertexShader(Shader::ShaderType::VertexShader, L"VertexShader.hlsl");
Shader mHullShader(Shader::ShaderType::HullShader, L"HullShader.hlsl");
Shader mDomainShader(Shader::ShaderType::DomainShader, L"DomainShader.hlsl");
Shader mPixelShader(Shader::ShaderType::PixelShader, L"PixelShader.hlsl");
Texture mTextureHeightMap(L"wave.jpg");
Texture mTextureAlbedo(L"checkerboard.jpg");
Scene mScene;

//imgui stuff
ID3D12DescriptorHeap* g_pd3dSrvDescHeap = NULL;
bool show_demo_window = true;
bool show_another_window = false;
ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

void InitConsole()
{
	AllocConsole();
	AttachConsole(GetCurrentProcessId());
	FILE* stream;
	freopen_s(&stream, "CON", "w", stdout);
}

bool InitDirectInput(HINSTANCE hInstance)
{
	HRESULT hr;

	hr = DirectInput8Create(hInstance, DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&DirectInput, NULL);
	if (!CheckError(hr, nullptr)) return false;

	hr = DirectInput->CreateDevice(GUID_SysKeyboard, &DIKeyboard, NULL);
	if (!CheckError(hr, nullptr)) return false;

	hr = DirectInput->CreateDevice(GUID_SysMouse, &DIMouse, NULL);
	if (!CheckError(hr, nullptr)) return false;

	hr = DIKeyboard->SetDataFormat(&c_dfDIKeyboard);
	if (!CheckError(hr, nullptr)) return false;

	hr = DIKeyboard->SetCooperativeLevel(hwnd, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	if (!CheckError(hr, nullptr)) return false;

	hr = DIMouse->SetDataFormat(&c_dfDIMouse);
	if (!CheckError(hr, nullptr)) return false;

	hr = DIMouse->SetCooperativeLevel(hwnd, DISCL_EXCLUSIVE | DISCL_NOWINKEY | DISCL_FOREGROUND);
	if (!CheckError(hr, nullptr)) return false;

	return true;
}

void DetectInput()
{
	BYTE keyboardCurrState[256];

	DIKeyboard->Acquire();
	
	DIKeyboard->GetDeviceState(sizeof(keyboardCurrState), (LPVOID)&keyboardCurrState);

	//keyboard control
	if (KEYDOWN(keyboardCurrState, DIK_ESCAPE))
	{
		PostMessage(hwnd, WM_DESTROY, 0, 0);
	}

	//mouse control
	if (KEYDOWN(keyboardCurrState, DIK_C))//control camera
	{
		if (!mouseAcquired)
		{
			DIMouse->Acquire();
			mouseAcquired = true;
		}

	}
	else
	{
		DIMouse->Unacquire();
		mouseAcquired = false;
	}

	if (mouseAcquired)
	{
		DIMOUSESTATE mouseCurrState;
		DIMouse->GetDeviceState(sizeof(DIMOUSESTATE), &mouseCurrState);
		if (mouseCurrState.lX != 0)
		{
			mCamera.SetHorizontalAngle(mCamera.GetHorizontalAngle() + mouseCurrState.lX * 0.1);
		}
		if (mouseCurrState.lY != 0)
		{
			float tempVerticalAngle = mCamera.GetVerticalAngle() + mouseCurrState.lY * 0.1;
			if (tempVerticalAngle > 90 - EPSILON) tempVerticalAngle = 89 - EPSILON;
			if (tempVerticalAngle < -90 + EPSILON) tempVerticalAngle = -89 + EPSILON;
			mCamera.SetVerticalAngle(tempVerticalAngle);
		}
		if (mouseCurrState.lZ != 0)
		{
			float tempDistance = mCamera.GetDistance() - mouseCurrState.lZ * 0.01;
			if (tempDistance < 0 + EPSILON) tempDistance = 0.1 + EPSILON;
			mCamera.SetDistance(tempDistance);
		}
		mouseLastState = mouseCurrState;
		mCamera.UpdateUniform();
		mCamera.UpdateUniformBuffer();
	}

	memcpy(keyboardLastState, keyboardCurrState, 256 * sizeof(BYTE));

	return;
}

bool InitDevice()
{
	HRESULT hr;

	// -- Create the Device -- //

	hr = CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory));
	if (FAILED(hr))
	{
		return false;
	}

	IDXGIAdapter1* adapter; // adapters are the graphics card (this includes the embedded graphics on the motherboard)

	int adapterIndex = 0; // we'll start looking for directx 12  compatible graphics devices starting at index 0

	bool adapterFound = false; // set this to true when a good one was found

							   // find first hardware gpu that supports d3d 12
	while (dxgiFactory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			// we dont want a software device
			continue;
		}

		// we want a device that is compatible with direct3d 12 (feature level 11 or higher)
		hr = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr);
		if (SUCCEEDED(hr))
		{
			adapterFound = true;
			break;
		}

		adapterIndex++;
	}

	if (!adapterFound)
	{
		Running = false;
		return false;
	}

	// Create the device
	hr = D3D12CreateDevice(
		adapter,
		D3D_FEATURE_LEVEL_11_0,
		IID_PPV_ARGS(&device)
	);
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}

	return true;
}

bool InitCommandQueue()
{
	HRESULT hr;

	// -- Create a direct command queue -- //

	D3D12_COMMAND_QUEUE_DESC cqDesc = {};
	cqDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; // direct means the gpu can directly execute this command queue

	hr = device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(&commandQueue)); // create the command queue
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}

	return true;
}

bool InitSwapChain()
{
	HRESULT hr;
	// -- Create the Swap Chain (double/tripple buffering) -- //

	DXGI_MODE_DESC backBufferDesc = {}; // this is to describe our display mode
	backBufferDesc.Width = Width; // buffer width
	backBufferDesc.Height = Height; // buffer height
	backBufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // format of the buffer (rgba 32 bits, 8 bits for each chanel)

														// describe our multi-sampling. We are not multi-sampling, so we set the count to 1 (we need at least one sample of course)

	DXGI_SAMPLE_DESC sampleDesc = {};//this way the first member variable will be initialized
	sampleDesc.Count = MultiSampleCount; // multisample count (no multisampling, so we just put 1, since we still need 1 sample)

						  // Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
	swapChainDesc.BufferCount = FrameBufferCount; // number of buffers we have
	swapChainDesc.BufferDesc = backBufferDesc; // our back buffer description
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // this says the pipeline will render to this swap chain
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // dxgi will discard the buffer (data) after we call present
	swapChainDesc.OutputWindow = hwnd; // handle to our window
	swapChainDesc.SampleDesc = sampleDesc; // our multi-sampling description
	swapChainDesc.Windowed = !FullScreen; // set to true, then if in fullscreen must call SetFullScreenState with true for full screen to get uncapped fps

	IDXGISwapChain* tempSwapChain;

	hr = dxgiFactory->CreateSwapChain(
		commandQueue, // the queue will be flushed once the swap chain is created
		&swapChainDesc, // give it the swap chain description we created above
		&tempSwapChain // store the created swap chain in a temp IDXGISwapChain interface
	);
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}

	swapChain = static_cast<IDXGISwapChain3*>(tempSwapChain);

	frameIndex = swapChain->GetCurrentBackBufferIndex();

	return true;
}

bool InitCommandList()
{

	HRESULT hr;

	// -- Create the Command Allocators -- //

	for (int i = 0; i < FrameBufferCount; i++)
	{
		hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator[i]));
		if (FAILED(hr))
		{
			Running = false;
			return false;
		}
	}

	// -- Create a Command List -- //

	// create the command list with the first allocator
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator[frameIndex], NULL, IID_PPV_ARGS(&commandList));
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}

	return true;
}

bool InitFence()
{

	HRESULT hr;

	// -- Create a Fence & Fence Event -- //

	// create the fences
	for (int i = 0; i < FrameBufferCount; i++)
	{
		hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence[i]));
		if (FAILED(hr))
		{
			Running = false;
			return false;
		}
		fenceValue[i] = 0; // set the initial fence value to 0
	}

	// create a handle to a fence event
	fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (fenceEvent == nullptr)
	{
		Running = false;
		return false;
	}

	return true;
}

bool FlushCommand()
{
	HRESULT hr;
	// Now we execute the command list to upload the initial assets (triangle data)
	commandList->Close();
	ID3D12CommandList* ppCommandLists[] = { commandList };
	commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// increment the fence value now, otherwise the buffer might not be uploaded by the time we start drawing
	fenceValue[frameIndex]++;
	hr = commandQueue->Signal(fence[frameIndex], fenceValue[frameIndex]);
	if (FAILED(hr))
	{
		Running = false;
		return false;
	}

	return true;
}

bool CreateScene()
{
	mScene.pCamera = &mCamera;
	//mScene.pMeshVec.push_back(&mCube1);
	//mScene.pMeshVec.push_back(&mCube2);
	//mScene.pMeshVec.push_back(&mPlane1);
	//mScene.pMeshVec.push_back(&mPlane2);
	mScene.pMeshVec.push_back(&mPlane);
	mScene.pShaderVec.push_back(&mVertexShader);
	mScene.pShaderVec.push_back(&mHullShader);
	mScene.pShaderVec.push_back(&mDomainShader);
	mScene.pShaderVec.push_back(&mPixelShader);
	mScene.pTextureVec.push_back(&mTextureHeightMap);
	mScene.pTextureVec.push_back(&mTextureAlbedo);

	// CPU side, read data from disk
	int shaderCount = mScene.pShaderVec.size();
	for (int i = 0; i < shaderCount; i++)
	{
		if (!mScene.pShaderVec[i]->CreateShader())
		{
			printf("CreateShader %d failed\n", i);
			return false;
		}
	}

	int textureCount = mScene.pTextureVec.size();
	for (int i = 0; i < textureCount; i++)
	{
		if (!mScene.pTextureVec[i]->LoadTextureBuffer())
		{
			printf("LoadTexture failed\n");
			return false;
		}
	}

	return true;
}

bool InitScene()
{
	if (!mScene.InitScene(device))
		return false;

	if (!mScene.pCamera->InitCamera(device))
	{
		printf("InitCamera failed\n");
		return false;
	}

	int meshCount = mScene.pMeshVec.size();
	for (int i = 0; i < meshCount; i++)
	{
		if (!mScene.pMeshVec[i]->InitMesh(device))
		{
			printf("InitMesh failed\n");
			return false;
		}
	}

	int textureCount = mScene.pTextureVec.size();
	for (int i = 0; i < textureCount; i++)
	{
		if (!mScene.pTextureVec[i]->InitTexture(device))
		{
			printf("InitTexture failed\n");
			return false;
		}
	}

	return true;
}

bool InitImgui()
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.NumDescriptors = 1;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	if (device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_pd3dSrvDescHeap)) != S_OK)
		return false;

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	//io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls

	// Setup Platform/Renderer bindings
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(device, FrameBufferCount,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		g_pd3dSrvDescHeap->GetCPUDescriptorHandleForHeapStart(),
		g_pd3dSrvDescHeap->GetGPUDescriptorHandleForHeapStart());

	// Setup Style
	// ImGui::StyleColorsDark();
	ImGui::StyleColorsClassic();

	// Load Fonts
	// - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them. 
	// - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple. 
	// - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
	// - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
	// - Read 'misc/fonts/README.txt' for more instructions and details.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	//io.Fonts->AddFontDefault();
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
	//ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
	//IM_ASSERT(font != NULL);
	return true;
}

bool InitD3D()
{
	// Create system level dx12 context

	if (!InitDevice())
	{
		printf("InitDevice failed\n");
		return false;
	}

	if (!InitCommandQueue())
	{
		printf("InitCommandQueue failed\n");
		return false;
	}

	if (!InitSwapChain())
	{
		printf("InitSwapChain failed\n");
		return false;
	}

	if (!InitCommandList())
	{
		printf("InitCommandList failed\n");
		return false;
	}

	if (!InitFence())
	{
		printf("InitFence failed\n");
		return false;
	}

	// GPU side, upload data to GPU
	if (!InitScene())
	{
		printf("InitScene failed\n");
		return false;
	}

	// GPU side, create GPU pipeline

	if (!mRenderer.CreateRenderer(device, swapChain, Width, Height))
	{
		printf("InitRenderTargetBuffer failed\n");
		return false;
	}

	if (!mRenderer.CreateGraphicsPipeline(device, &mVertexShader, &mHullShader, &mDomainShader, nullptr, &mPixelShader, mScene.pTextureVec))
	{
		printf("CreateGraphicsPipeline failed\n");
		return false;
	}

	if (!FlushCommand())
	{
		printf("ExecuteCreateCommand failed\n");
		return false;
	}

	// Create imgui context
	if (!InitImgui())
	{
		printf("ExecuteCreateCommand failed\n");
		return false;
	}

	return true;
}

void Update()
{

	//mCube1.UpdateUniform();
	//mCube1.UpdateUniformBuffer();

	//XMFLOAT3 temp = mCube2.GetPosition();
	//temp.y = temp.y > 2.f ? 0.f : temp.y + 0.0001f;
	//mCube2.SetPosition(temp);
	//mCube2.UpdateUniform();
	//mCube2.UpdateUniformBuffer();

	//XMFLOAT3 tempR = mPlane2.GetRotation();
	//tempR.y += 0.0001f;
	//mPlane2.SetRotation(tempR);
	//mPlane2.UpdateUniform();
	//mPlane2.UpdateUniformBuffer();
}

void WaitForPreviousFrame()
{
	HRESULT hr;

	// swap the current rtv buffer index so we draw on the correct buffer
	frameIndex = swapChain->GetCurrentBackBufferIndex();

	// if the current fence value is still less than "fenceValue", then we know the GPU has not finished executing
	// the command queue since it has not reached the "commandQueue->Signal(fence, fenceValue)" command
	if (fence[frameIndex]->GetCompletedValue() < fenceValue[frameIndex])
	{
		// we have the fence create an event which is signaled once the fence's current value is "fenceValue"
		hr = fence[frameIndex]->SetEventOnCompletion(fenceValue[frameIndex], fenceEvent);
		if (FAILED(hr))
		{
			Running = false;
		}

		// We will wait until the fence has triggered the event that it's current value has reached "fenceValue". once it's value
		// has reached "fenceValue", we know the command queue has finished executing
		WaitForSingleObject(fenceEvent, INFINITE);
	}

	// increment fenceValue for next frame
	fenceValue[frameIndex]++;
}

void UpdatePipeline()
{
	HRESULT hr;

	// We have to wait for the gpu to finish with the command allocator before we reset it
	WaitForPreviousFrame();

	// we can only reset an allocator once the gpu is done with it
	// resetting an allocator frees the memory that the command list was stored in
	hr = commandAllocator[frameIndex]->Reset();
	if (FAILED(hr))
	{
		Running = false;
	}

	// reset the command list. by resetting the command list we are putting it into
	// a recording state so we can start recording commands into the command allocator.
	// the command allocator that we reference here may have multiple command lists
	// associated with it, but only one can be recording at any time. Make sure
	// that any other command lists associated to this command allocator are in
	// the closed state (not recording).
	// Here you will pass an initial pipeline state object as the second parameter,
	// but in this tutorial we are only clearing the rtv, and do not actually need
	// anything but an initial default pipeline, which is what we get by setting
	// the second parameter to NULL
	hr = commandList->Reset(commandAllocator[frameIndex], mRenderer.GetGraphicsPSO());
	if (FAILED(hr))
	{
		Running = false;
	}

	// RECORD GRAPHICS PIPELINE BEGIN //
	mRenderer.RecordBegin(frameIndex, commandList);

	///////// MY PIPELINE /////////
	//vvvvvvvvvvvvvvvvvvvvvvvvvvv//
	mRenderer.RecordGraphicsPipelinePatch(frameIndex, commandList, &mScene); ;// mRenderer.RecordGraphicsPipeline(frameIndex, commandList, &mScene);
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^//
	///////// MY PIPELINE /////////

	///////// IMGUI PIPELINE /////////
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvv//
	commandList->SetDescriptorHeaps(1, &g_pd3dSrvDescHeap);
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
	///////// IMGUI PIPELINE /////////
	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^//

	mRenderer.RecordEnd(frameIndex, commandList);
	// RECORD GRAPHICS PIPELINE END //

	hr = commandList->Close();
	if (FAILED(hr))
	{
		Running = false;
	}
}

void Render()
{
	HRESULT hr;

	UpdatePipeline(); // update the pipeline by sending commands to the commandqueue

	// create an array of command lists (only one command list here)
	ID3D12CommandList* ppCommandLists[] = { commandList };

	// execute the array of command lists
	commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// this command goes in at the end of our command queue. we will know when our command queue 
	// has finished because the fence value will be set to "fenceValue" from the GPU since the command
	// queue is being executed on the GPU
	hr = commandQueue->Signal(fence[frameIndex], fenceValue[frameIndex]);
	if (FAILED(hr))
	{
		Running = false;
	}

	// present the current backbuffer
	hr = swapChain->Present(0, 0);
	if (FAILED(hr))
	{
		Running = false;
	}
}

void Gui()
{
	////////////////////////////////////
	////////// IMGUI EXAMPLES///////////
	//vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv//

	// Start the Dear ImGui frame
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	static float f = 0.5f;
	static int u = 32;
	bool needToUpdateSceneUniform = false;

	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(200, 120));

	ImGui::Begin("Control Panel ");                          // Create a window called "Hello, world!" and append into it.

	ImGui::Text("Wave Particles Scale ");               // Display some text (you can use a format strings too)

	ImGui::SliderFloat("float ", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f  
	ImGui::SliderInt("uint ", &u, 0, 128);

	if (f != mScene.GetWaveParticleScale())
	{
		mScene.SetWaveParticleScale(f);
		needToUpdateSceneUniform = true;
	}

	if (u != mScene.GetWaveParticleScale())
	{
		mScene.SetTessellationFactor(u);
		needToUpdateSceneUniform = true;
	}

	if (needToUpdateSceneUniform)
	{
		mScene.UpdateUniformBuffer();
	}

	ImGui::Text("%.3f ms/frame (%.1f FPS) ", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	ImGui::End();

	//^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^//
	////////// IMGUI EXAMPLES///////////
	////////////////////////////////////
}

void Cleanup()
{
	//imgui stuff
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	//direct input stuff
	DIKeyboard->Unacquire();
	DIMouse->Unacquire();
	DirectInput->Release();

	// wait for the gpu to finish all frames
	for (int i = 0; i < FrameBufferCount; ++i)
	{
		frameIndex = i;
		WaitForPreviousFrame();
	}

	// get swapchain out of full screen before exiting
	BOOL fs = false;
	swapChain->GetFullscreenState(&fs, NULL);
	if (fs == TRUE)
		swapChain->SetFullscreenState(false, NULL);

	SAFE_RELEASE(device);
	SAFE_RELEASE(swapChain);
	SAFE_RELEASE(commandQueue);
	SAFE_RELEASE(commandList);

	for (int i = 0; i < FrameBufferCount; ++i)
	{
		SAFE_RELEASE(commandAllocator[i]);
		SAFE_RELEASE(fence[i]);
	};
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hwnd,	UINT msg, WPARAM wParam, LPARAM lParam)
{
	//vv imgui vv//
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
		return true;
	//^^ imgui ^^//

	switch (msg)
	{
	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE) {
			if (MessageBox(0, L"Are you sure you want to exit?",
				L"Really?", MB_YESNO | MB_ICONQUESTION) == IDYES)
			{
				Running = false;
				DestroyWindow(hwnd);
			}
		}
		return 0;

	case WM_DESTROY: // x button on top right corner of window was pressed
		Running = false;
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProc(hwnd,
		msg,
		wParam,
		lParam);
}

// create and show the window
bool InitializeWindow(HINSTANCE hInstance, int ShowWnd,	bool fullscreen)
{
	if (fullscreen)
	{
		HMONITOR hmon = MonitorFromWindow(hwnd,
			MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = { sizeof(mi) };
		GetMonitorInfo(hmon, &mi);

		Width = mi.rcMonitor.right - mi.rcMonitor.left;
		Height = mi.rcMonitor.bottom - mi.rcMonitor.top;
	}

	WNDCLASSEX wc;

	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WndProc;
	wc.cbClsExtra = NULL;
	wc.cbWndExtra = NULL;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 2);
	wc.lpszMenuName = NULL;
	wc.lpszClassName = WindowName;
	wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

	if (!RegisterClassEx(&wc))
	{
		MessageBox(NULL, L"Error registering class",
			L"Error", MB_OK | MB_ICONERROR);
		return false;
	}

	hwnd = CreateWindowEx(NULL,
		WindowName,
		WindowTitle,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		Width, Height,
		NULL,
		NULL,
		hInstance,
		NULL);

	if (!hwnd)
	{
		MessageBox(NULL, L"Error creating window",
			L"Error", MB_OK | MB_ICONERROR);
		return false;
	}

	if (fullscreen)
	{
		SetWindowLong(hwnd, GWL_STYLE, 0);
	}

	ShowWindow(hwnd, ShowWnd);
	UpdateWindow(hwnd);

	return true;
}

void mainloop() 
{
	MSG msg;
	ZeroMemory(&msg, sizeof(MSG));

	while (Running)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
				break;

			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		else {
			// run gui code
			Gui();
			// run game code
			DetectInput();
			Update(); // update the game logic
			Render(); // execute the command queue (rendering the scene is the result of the gpu executing the command lists)
		}
	}
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	InitConsole();

	// initialize data
	if (!CreateScene())
	{
		MessageBox(0, L"Failed to initialize data",	L"Error", MB_OK);
		return 1;
	}

	// create the window
	if (!InitializeWindow(hInstance, nShowCmd, FullScreen))
	{
		MessageBox(0, L"Window Initialization - Failed", L"Error", MB_OK);
		return 1;
	}

	//Initialize input device
	if (!InitDirectInput(hInstance))
	{
		MessageBox(0, L"Failed to initialize input", L"Error", MB_OK);
		return 1;
	}

	// initialize direct3d
	if (!InitD3D())
	{
		MessageBox(0, L"Failed to initialize direct3d 12", L"Error", MB_OK);
		Cleanup();
		return 1;
	}

	// start the main loop
	mainloop();

	// we want to wait for the gpu to finish executing the command list before we start releasing everything
	WaitForPreviousFrame();

	// close the fence event
	CloseHandle(fenceEvent);

	// clean up everything
	Cleanup();

	return 0;
}

bool CheckError(HRESULT hr, ID3D10Blob* error_message)
{
	if (FAILED(hr))
	{
		printf("FAILED:0x%x\n", hr);
		if (error_message != nullptr)
		{
			printf("return value: %d, error message: %s\n", hr, (char*)error_message->GetBufferPointer());
		}
		return false;
	}
	return true;
}