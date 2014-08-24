#include <Windows.h>
#include <tchar.h>
#include <Kinect.h>
#include <d3d11.h>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <filesystem>
#include <exception>

#pragma comment( lib, "kinect20.lib" )
#pragma comment( lib, "d3d11.lib" )

namespace
{
	const TCHAR* g_appName = _T( "Kinect Depth" );
	const int g_windowWidth = 640;
	const int g_windowHeight = 530;
}

//! Custom deleter of std::unique_ptr for COM instance.
struct Deleter
{
	void operator()( IUnknown* com ) {
		if( com ) com->Release();
	}
};

//! Assertion failed if HRESULT failed.
void Assert( HRESULT hr )
{
	if( FAILED( hr ) ) {
		std::stringstream ss;
		ss << "Error : " << std::hex << hr;
		throw std::runtime_error( ss.str() );
	}
}

//! Read all binary data from file.
std::string fileGetContents( const char* path )
{
	// Get directory name of exe file.
	namespace sys = std::tr2::sys;
	auto exeDir = sys::path( __argv[ 0 ] ).parent_path();

	std::stringstream newPathSS;
	newPathSS << exeDir << sys::slash< sys::path >::value << path;
	auto newPath = newPathSS.str();

	// Read and copy all.
	std::ifstream ifs( newPath, std::ios::binary );
	std::string str(
		(std::istreambuf_iterator< char >( ifs )),
		std::istreambuf_iterator< char >()
		);

	// Error occurs if file not found or empty file.
	if( str.size() == 0 )
	{
		std::stringstream ss;
		ss << "File not found : " << path;
		throw std::runtime_error( ss.str() );
	}
	return str;
}

struct Kinect
{
	enum
	{
		MAX_DEPTH_FRAME_WIDTH = 512,
		MAX_DEPTH_FRAME_HEIGHT = 424,
		MAX_DEPTH_FRAME_BYTE_PER_PIXEL = 2
	};

	void init()
	{
		HRESULT hr;

		IKinectSensor* sensor;
		hr = GetDefaultKinectSensor( &sensor );
		Assert( hr );
		sensor_.reset( sensor );

		hr = sensor_->Open();
		Assert( hr );

		// Sensor -> Depth Source
		IDepthFrameSource* depthSource;
		hr = sensor_->get_DepthFrameSource( &depthSource );
		Assert( hr );
		depthSource_.reset( depthSource );

		// Depth Source -> Depth Reader
		IDepthFrameReader* depthReader;
		hr = depthSource_->OpenReader( &depthReader );
		Assert( hr );
		depthReader_.reset( depthReader );
	}

	void release()
	{
		sensor_->Close();
	}

	std::unique_ptr< IKinectSensor, Deleter > sensor_;
	std::unique_ptr< IDepthFrameSource, Deleter > depthSource_;
	std::unique_ptr< IDepthFrameReader, Deleter > depthReader_;

	//std::array< unsigned char, (MAX_DEPTH_FRAME_WIDTH * MAX_DEPTH_FRAME_HEIGHT * MAX_DEPTH_FRAME_BYTE_PER_PIXEL) > depthFrame_;
};

struct D3D
{
	void init( HWND hWnd )
	{
		HRESULT hr;

		DXGI_SWAP_CHAIN_DESC scDesc;
		memset( &scDesc, 0, sizeof scDesc );
		scDesc.BufferDesc.Width = g_windowWidth;
		scDesc.BufferDesc.Height = g_windowHeight;
		scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		scDesc.BufferDesc.RefreshRate.Denominator = 1;
		scDesc.BufferDesc.RefreshRate.Numerator = 60;
		scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		scDesc.BufferCount = 1;
		scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		scDesc.SampleDesc.Count = 1;
		scDesc.Windowed = TRUE;
		scDesc.OutputWindow = hWnd;

		D3D_FEATURE_LEVEL features[] = { D3D_FEATURE_LEVEL_11_0 };

		UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
		flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

		IDXGISwapChain* swapChain;
		ID3D11Device* device;
		ID3D11DeviceContext* context;
		hr = D3D11CreateDeviceAndSwapChain(
			nullptr, D3D_DRIVER_TYPE_HARDWARE, NULL, flags, features, ARRAYSIZE( features ),
			D3D11_SDK_VERSION, &scDesc, &swapChain, &device, &featureLevel_, &context );
		Assert( hr );
		swapChain_.reset( swapChain );
		device_.reset( device );
		context_.reset( context );

		ID3D11Texture2D* backBuffer;
		ID3D11RenderTargetView* backBufferRTV;
		hr = swapChain->GetBuffer( 0, __uuidof( ID3D11Texture2D ), reinterpret_cast< void** >( &backBuffer ) );
		Assert( hr );
		hr = device_->CreateRenderTargetView( backBuffer, nullptr, &backBufferRTV );
		Assert( hr );
		backBufferRTV_.reset( backBufferRTV );
		backBuffer->Release();

		// Common state

		ID3D11RasterizerState* rs;
		D3D11_RASTERIZER_DESC rsDesc;
		memset( &rsDesc, 0, sizeof rsDesc );
		rsDesc.CullMode = D3D11_CULL_BACK;
		rsDesc.FillMode = D3D11_FILL_SOLID;
		rsDesc.DepthClipEnable = TRUE;
		hr = device_->CreateRasterizerState( &rsDesc, &rs );
		Assert( hr );
		rasterState_.reset( rs );

		ID3D11SamplerState* ss;
		D3D11_SAMPLER_DESC sampleDesc;
		memset( &sampleDesc, 0, sizeof sampleDesc );
		sampleDesc.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
		sampleDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampleDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampleDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sampleDesc.MinLOD = -FLT_MAX;
		sampleDesc.MaxLOD = +FLT_MAX;
		sampleDesc.MaxAnisotropy = 1;
		hr = device_->CreateSamplerState( &sampleDesc, &ss );
		samplerState_.reset( ss );

		// Depth texture

		ID3D11Texture2D* tex;
		D3D11_TEXTURE2D_DESC texDesc;
		texDesc = CD3D11_TEXTURE2D_DESC(
			DXGI_FORMAT_R16_UNORM, Kinect::MAX_DEPTH_FRAME_WIDTH, Kinect::MAX_DEPTH_FRAME_HEIGHT, 1, 1,
			D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DYNAMIC, D3D11_CPU_ACCESS_WRITE );
		hr = device_->CreateTexture2D( &texDesc, nullptr, &tex );
		Assert( hr );
		depthFrame_.reset( tex );

		ID3D11ShaderResourceView* srv;
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		memset( &srvDesc, 0, sizeof srvDesc );
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		hr = device_->CreateShaderResourceView( depthFrame_.get(), &srvDesc, &srv );
		Assert( hr );
		depthFrameSRV_.reset( srv );

		std::string binData;
		ID3D11VertexShader* vs;
		binData = fileGetContents( "def.vs.cso" );
		hr = device_->CreateVertexShader(
			reinterpret_cast< const void* >( binData.c_str() ), binData.size(), nullptr, &vs );
		Assert( hr );
		fullscreenVS_.reset( vs );

		ID3D11PixelShader* ps;
		binData = fileGetContents( "def.ps.cso" );
		hr = device_->CreatePixelShader(
			reinterpret_cast< const void* >( binData.c_str() ), binData.size(), nullptr, &ps );
		Assert( hr );
		texPS_.reset( ps );
	}

	void release()
	{
	}

	D3D_FEATURE_LEVEL featureLevel_;
	std::unique_ptr< IDXGISwapChain, Deleter > swapChain_;
	std::unique_ptr< ID3D11Device, Deleter > device_;
	std::unique_ptr< ID3D11DeviceContext, Deleter > context_;
	std::unique_ptr< ID3D11RenderTargetView, Deleter > backBufferRTV_;
	std::unique_ptr< ID3D11RasterizerState, Deleter > rasterState_;
	std::unique_ptr< ID3D11SamplerState, Deleter > samplerState_;

	std::unique_ptr< ID3D11Texture2D, Deleter > depthFrame_;
	std::unique_ptr< ID3D11ShaderResourceView, Deleter > depthFrameSRV_;
	std::unique_ptr< ID3D11VertexShader, Deleter > fullscreenVS_;
	std::unique_ptr< ID3D11PixelShader, Deleter > texPS_;
};

namespace
{
	HWND g_hWnd = NULL;
	Kinect g_kinect;
	D3D g_d3d;
}

void Step()
{
	HRESULT hr;

	IDepthFrame* frame;
	hr = g_kinect.depthReader_->AcquireLatestFrame( &frame );
	if( hr == E_PENDING )
	{
		return;
	}
	Assert( hr );

	UINT frameSize;
	UINT16* framePtr;
	hr = frame->AccessUnderlyingBuffer( &frameSize, &framePtr );
	Assert( hr );

	// Copy pixels to Direct3D texture.
	D3D11_MAPPED_SUBRESOURCE map;
	D3D11_TEXTURE2D_DESC texDesc;
	hr = g_d3d.context_->Map( g_d3d.depthFrame_.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map );
	Assert( hr );
	g_d3d.depthFrame_->GetDesc( &texDesc );
	for( unsigned int y = 0; y < texDesc.Height; ++y )
	{
		const int copySize = Kinect::MAX_DEPTH_FRAME_WIDTH * Kinect::MAX_DEPTH_FRAME_BYTE_PER_PIXEL;
		auto* destStart = reinterpret_cast< unsigned char* >( map.pData ) + map.RowPitch * y;
		const auto* srcStart = reinterpret_cast< unsigned char* >( framePtr ) + y * copySize;
		const auto* srcEnd = srcStart + copySize;
		const auto dest = stdext::make_checked_array_iterator( destStart, copySize );
		std::copy( srcStart, srcEnd, dest );
	}
	g_d3d.context_->Unmap( g_d3d.depthFrame_.get(), 0 );

	frame->Release();
}

void Draw()
{
	ID3D11DeviceContext* context = g_d3d.context_.get();
	
	// Clear
	float clearColor[] = { 0.3f, 0.3f, 0.3f, 1.0f };
	context->ClearRenderTargetView( g_d3d.backBufferRTV_.get(), clearColor );

	auto* rtv = g_d3d.backBufferRTV_.get();
	context->OMSetRenderTargets( 1, &rtv, nullptr );
	
	// Draw color frame
	auto* srv = g_d3d.depthFrameSRV_.get();
	auto* rs = g_d3d.samplerState_.get();
	D3D11_VIEWPORT viewport = { 0, 0, g_windowWidth, g_windowHeight, 0, 1 };
	context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
	context->VSSetShader( g_d3d.fullscreenVS_.get(), nullptr, 0 );
	context->RSSetState( g_d3d.rasterState_.get() );
	context->PSSetShader( g_d3d.texPS_.get(), nullptr, 0 );
	context->PSSetShaderResources( 0, 1, &srv );
	context->PSSetSamplers( 0, 1, &rs );
	context->RSSetViewports( 1, &viewport );
	context->Draw( 4, 0 );

	g_d3d.swapChain_->Present( 1, 0 );
}

int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow )
{
	nCmdShow; lpCmdLine; hPrevInstance;

	WNDCLASS wcls;
	memset( &wcls, 0, sizeof wcls );
	wcls.style = CS_HREDRAW | CS_VREDRAW;
	wcls.lpfnWndProc = []( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam ) -> LRESULT {
		switch( message ) {
		case WM_KEYDOWN:
			if( wParam == VK_ESCAPE ) {
				PostMessage( hWnd, WM_DESTROY, 0, 0 );
				return 0;
			}
			break;
		case WM_DESTROY:
			PostQuitMessage( 0 );
			break;
		}
		return DefWindowProc( hWnd, message, wParam, lParam );
	};
	wcls.hInstance = hInstance;
	wcls.lpszClassName = g_appName;
	RegisterClass( &wcls );

	RECT rect = { 0, 0, g_windowWidth, g_windowHeight };
	AdjustWindowRect( &rect, WS_OVERLAPPEDWINDOW, FALSE );

	const int windowWidth  = ( rect.right  - rect.left );
	const int windowHeight = ( rect.bottom - rect.top );
	g_hWnd = CreateWindow( g_appName, g_appName, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, windowWidth, windowHeight, NULL, NULL, hInstance, NULL );

	ShowWindow( g_hWnd, SW_SHOW );

	try {
		g_kinect.init();
		g_d3d.init( g_hWnd );

		MSG msg;
		memset( &msg, 0, sizeof msg );
		while( msg.message != WM_QUIT ) {
			BOOL r = PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE );
			if( r == 0 ) {
				Step();
				Draw();
			}
			else {
				DispatchMessage( &msg );
			}
		}

		g_d3d.release();
		g_kinect.release();
	}
	catch( std::exception &e ) {
		MessageBoxA( g_hWnd, e.what(), nullptr, MB_ICONSTOP );
	}

	return 0;
}
