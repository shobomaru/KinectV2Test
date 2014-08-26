#include <Windows.h>
#include <tchar.h>
#include <Kinect.h>
#include <d3d11.h>
#include <DirectXMath.h>
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
	const TCHAR* g_appName = _T( "Kinect Body" );
	const int g_windowWidth = 1280;
	const int g_windowHeight = 720;
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


namespace human
{
	// Boneの接続
	const int JOINT_ORDER[ JointType_Count ] = {
		JointType_SpineBase,		// 脊椎Base
		JointType_SpineMid,			// 脊椎中間
		JointType_SpineShoulder,	// 脊椎肩
		JointType_Neck,				// 首
		JointType_Head,				// 頭
		JointType_ShoulderLeft,		// 左肩
		JointType_ElbowLeft,		// 左肘
		JointType_WristLeft,		// 左手首
		JointType_HandLeft,			// 左手
		JointType_ThumbLeft,		// 左親指
		JointType_HandTipLeft,		// 左手先
		JointType_ShoulderRight,	// （右も同様）
		JointType_ElbowRight,
		JointType_WristRight,
		JointType_HandRight,
		JointType_ThumbRight,
		JointType_HandTipRight,
		JointType_HipLeft,			// 左尻
		JointType_KneeLeft,			// 左膝
		JointType_AnkleLeft,		// 左足首
		JointType_FootLeft,			// 左足元
		JointType_HipRight,			// （右も同様）
		JointType_KneeRight,
		JointType_AnkleRight,
		JointType_FootRight
	};

	// Boneの長さ[cm]
	const float BONE_LENGTH[ 20 ] = {
		0.0f,  // SpineBase
		5.1f,  // 尻 -> 背
		28.3f, // 背 -> 首
		21.5f, // 首 -> 頭
		19.8f, // 首 -> 左肩
		24.3f, // 左肩 -> 左肘
		26.5f, // 左肘 -> 左手首
		8.2f,  // 左手首 -> 左手
		19.8f, // （体は左右対称なので同じ値とする）
		24.3f,
		26.5f,
		8.2f,
		10.0f, // 尻 -> 左股
		35.8f, // 左股 -> 左膝
		35.2f, // 左膝 -> 左足首
		11.5f, // 左足首 -> 左足
		10.0f,
		35.8f,
		35.2f,
		11.5f
	};

	// Boneのルートから地面までの距離[cm]
	const float BONE_ROOT_DISTANCE = 108.4f;

} // namespace human


struct Kinect
{
	enum
	{
		MAX_BODY_INDEX_FRAME_WIDTH = 512,
		MAX_BODY_INDEX_FRAME_HEIGHT = 424,
		MAX_BODY_INDEX_FRAME_BYTE_PER_PIXEL = 1
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

		// Sensor -> Body Source
		IBodyFrameSource* bodySource;
		hr = sensor_->get_BodyFrameSource( &bodySource );
		Assert( hr );
		bodySource_.reset( bodySource );

		// Body Source -> Body Reader
		IBodyFrameReader* bodyReader;
		hr = bodySource_->OpenReader( &bodyReader );
		Assert( hr );
		bodyReader_.reset( bodyReader );

		// Coordinate mapper
		ICoordinateMapper* mapper;
		hr = sensor_->get_CoordinateMapper( &mapper );
		Assert( hr );
		coordMapper_.reset( mapper );
	}

	void release()
	{
		sensor_->Close();
	}

	std::unique_ptr< IKinectSensor, Deleter > sensor_;
	std::unique_ptr< IBodyFrameSource, Deleter > bodySource_;
	std::unique_ptr< IBodyFrameReader, Deleter > bodyReader_;

	std::unique_ptr< ICoordinateMapper, Deleter > coordMapper_;
};

struct D3D
{
	struct MeshFormat
	{
		float x;
		float y;
		float z;
		unsigned int color;
	};

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

		// Body

		std::string binData;
		std::string vsBinData;
		ID3D11VertexShader* vs;
		binData = fileGetContents( "def.vs.cso" );
		hr = device_->CreateVertexShader(
			reinterpret_cast< const void* >( binData.c_str() ), binData.size(), nullptr, &vs );
		Assert( hr );
		modelVS_.reset( vs );
		vsBinData = binData;

		ID3D11PixelShader* ps;
		binData = fileGetContents( "def.ps.cso" );
		hr = device_->CreatePixelShader(
			reinterpret_cast< const void* >( binData.c_str() ), binData.size(), nullptr, &ps );
		Assert( hr );
		modelPS_.reset( ps );

		D3D11_BUFFER_DESC bufDesc = CD3D11_BUFFER_DESC( 64, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DEFAULT, 0 );
		ID3D11Buffer* buf;
		hr = device_->CreateBuffer( &bufDesc, nullptr, &buf );
		Assert( hr );
		modelCB_.reset( buf );

		// 四角錐ポリゴン
		static const MeshFormat pyramidVertex[] = {
			{ -1.0f,  0.0f, -1.0f, 0xFF33AAAA },
			{  1.0f,  0.0f, -1.0f, 0xFF33AAAA },
			{ -1.0f,  0.0f,  1.0f, 0xFF33AAAA },
		
			{ -1.0f,  0.0f,  1.0f, 0xFF33AAAA },
			{  1.0f,  0.0f, -1.0f, 0xFF33AAAA },
			{  1.0f,  0.0f,  1.0f, 0xFF33AAAA },

			{  1.0f,  0.0f, -1.0f, 0xFFEE33BB },
			{ -1.0f,  0.0f, -1.0f, 0xFFEE33BB },
			{  0.0f,  1.0f,  0.0f, 0xFFEE33BB },
		
			{ -1.0f,  0.0f, -1.0f, 0xFFCC33BB },
			{ -1.0f,  0.0f,  1.0f, 0xFFCC33BB },
			{  0.0f,  1.0f,  0.0f, 0xFFCC33BB },
		
			{ -1.0f,  0.0f,  1.0f, 0xFFAA33BB },
			{  1.0f,  0.0f,  1.0f, 0xFFAA33BB },
			{  0.0f,  1.0f,  0.0f, 0xFFAA33BB },
		
			{  1.0f,  0.0f,  1.0f, 0xFF8833BB },
			{  1.0f,  0.0f, -1.0f, 0xFF8833BB },
			{  0.0f,  1.0f,  0.0f, 0xFF8833BB },
		};

		bufDesc = CD3D11_BUFFER_DESC( sizeof pyramidVertex, D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_IMMUTABLE, 0 );
		D3D11_SUBRESOURCE_DATA subresData;
		subresData.pSysMem = pyramidVertex;
		subresData.SysMemPitch = subresData.SysMemSlicePitch = 0;
		hr = device_->CreateBuffer( &bufDesc, &subresData, &buf );
		Assert( hr );
		modelVB_.reset( buf );

		D3D11_INPUT_ELEMENT_DESC ieDesc[] = {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "COLOR", 0, DXGI_FORMAT_B8G8R8A8_UNORM, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
		};
		ID3D11InputLayout* il;
		hr = device_->CreateInputLayout( ieDesc, ARRAYSIZE( ieDesc ), vsBinData.data(), vsBinData.size(), &il );
		Assert( hr );
		modelIL_.reset( il );
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

	std::unique_ptr< ID3D11VertexShader, Deleter > modelVS_;
	std::unique_ptr< ID3D11PixelShader, Deleter > modelPS_;
	std::unique_ptr< ID3D11Buffer, Deleter > modelCB_;
	std::unique_ptr< ID3D11Buffer, Deleter > modelVB_;
	std::unique_ptr< ID3D11InputLayout, Deleter > modelIL_;

	std::array< float, 4 * JointType_Count > jointRot_;
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

	IBodyFrame* frame;
	hr = g_kinect.bodyReader_->AcquireLatestFrame( &frame );
	if( hr == E_PENDING )
	{
		return;
	}
	Assert( hr );
	
	IBody* bodies[ BODY_COUNT ] = {};
	hr = frame->GetAndRefreshBodyData( ARRAYSIZE( bodies ), bodies );
	Assert( hr );

	// test
	g_d3d.jointRot_[ 0 ] = 0;
	g_d3d.jointRot_[ 1 ] = 0;
	g_d3d.jointRot_[ 2 ] = 0;
	for( int bi = 0; bi < ARRAYSIZE( bodies ); ++bi )
	{
		BOOLEAN isTracked;
		hr = bodies[ bi ]->get_IsTracked( &isTracked );
		Assert( hr );
		if( isTracked )
		{
			JointOrientation jointOrients[ JointType_Count ];
			hr = bodies[ bi ]->GetJointOrientations( ARRAYSIZE( jointOrients ), jointOrients );
			Assert( hr );
			g_d3d.jointRot_[ 0 ] = jointOrients[ JointType_SpineBase ].Orientation.x;
			g_d3d.jointRot_[ 1 ] = jointOrients[ JointType_SpineBase ].Orientation.y;
			g_d3d.jointRot_[ 2 ] = jointOrients[ JointType_SpineBase ].Orientation.z;
			break;
		}
	}

	//for( auto& body : bodies ) {
	//	body->Release();
	//}
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
	
	// Draw body

	auto cbModelWVP = DirectX::XMMatrixIdentity();
	auto matWorld = DirectX::XMMatrixIdentity();
	matWorld = DirectX::XMMatrixRotationRollPitchYaw( g_d3d.jointRot_[ 0 ], g_d3d.jointRot_[ 1 ], g_d3d.jointRot_[ 2 ] );
	auto matView =  DirectX::XMMatrixLookAtLH(
		DirectX::XMVectorSet( 0, 0, -3, 0 ), DirectX::XMVectorSet( 0, 0, 5, 0 ), DirectX::XMVectorSet( 0, 1, 0, 0 )
		);
	auto matProj = DirectX::XMMatrixPerspectiveFovLH(
		DirectX::XMConvertToRadians( 50 ), (float)g_windowWidth / (float)g_windowHeight, 0.01f, 1000.0f
		);
	cbModelWVP = matWorld * matView * matProj;
	cbModelWVP = DirectX::XMMatrixTranspose( cbModelWVP );
	g_d3d.context_->UpdateSubresource( g_d3d.modelCB_.get(), 0, nullptr, &cbModelWVP, 0, 0 );

	auto* vb = g_d3d.modelVB_.get();
	unsigned int stride = sizeof( D3D::MeshFormat );
	unsigned int offset = 0;
	auto* cb = g_d3d.modelCB_.get();
	D3D11_VIEWPORT viewport = { 0, 0, g_windowWidth, g_windowHeight, 0, 1 };
	context->IASetPrimitiveTopology( D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	context->IASetInputLayout( g_d3d.modelIL_.get() );
	context->IASetVertexBuffers( 0, 1, &vb, &stride, &offset );
	context->VSSetShader( g_d3d.modelVS_.get(), nullptr, 0 );
	context->VSSetConstantBuffers( 0, 1, &cb );
	context->RSSetState( g_d3d.rasterState_.get() );
	context->PSSetShader( g_d3d.modelPS_.get(), nullptr, 0 );
	context->RSSetViewports( 1, &viewport );
	context->Draw( 18, 0 );

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
