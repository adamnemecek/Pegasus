/****************************************************************************************/
/*                                                                                      */
/*                                       Pegasus                                        */
/*                                                                                      */
/****************************************************************************************/

//! \file   Application.cpp
//! \author David Worsham
//! \date   4th July 2013
//! \brief  Building block class for a Pegasus application.
//!         Manages access to the Pegasus runtime.

#include "Pegasus/Application/Application.h"
#include "Pegasus/Application/Shared/ApplicationConfig.h"
#include "Pegasus/Application/AppWindowManager.h"
#include "Pegasus/Render/GL/GLExtensions.h"
#include "Pegasus/Window/Window.h"
#include "Pegasus/Window/StartupWindow.h"

namespace Pegasus {
namespace App {

// Constants
const char* STARTUP_WND_TYPE = "INTERNAL__Startup";

//----------------------------------------------------------------------------------------

Application::Application(const ApplicationConfig& config)
    : mInitialized(false), mAppTime(0.0f)
{
    Memory::IAllocator* coreAlloc = Memory::GetCoreAllocator();
    Memory::IAllocator* windowAlloc = Memory::GetWindowAllocator();
    AppWindowManagerConfig windowManagerConfig;
    WindowRegistration reg;

    // Set up debugging facilities
#if PEGASUS_ENABLE_LOG
    Core::LogManager::CreateInstance(coreAlloc);
    Core::LogManager::GetInstance()->RegisterHandler(config.mLoghandler);
#endif
#if PEGASUS_ENABLE_ASSERT
    Core::AssertionManager::CreateInstance(coreAlloc);
    Core::AssertionManager::GetInstance()->RegisterHandler(config.mAssertHandler);
#endif

    // Set up window manager
    windowManagerConfig.mAllocator = windowAlloc;
    windowManagerConfig.mMaxWindowTypes = mConfig.mMaxWindowTypes;
    windowManagerConfig.mMaxNumWindows = mConfig.mMaxNumWindows;
    mWindowManager = PG_NEW(windowAlloc, "AppWindowManager", Pegasus::Memory::PG_MEM_PERM) AppWindowManager(windowManagerConfig);

    // Register startup window
    reg.mTypeTag = Pegasus::App::WINDOW_TYPE_INVALID;
    reg.mDescription = "INTERNAL Startup Window";
    reg.mCreateFunc = Wnd::StartupWindow::Create;
    mWindowManager->RegisterWindowClass(STARTUP_WND_TYPE, reg);

    // Cache config
    mConfig = config;
}

//----------------------------------------------------------------------------------------

Application::~Application()
{
    Memory::IAllocator* windowAlloc = Memory::GetWindowAllocator();

    // Sanity check
    PG_ASSERTSTR(!mInitialized, "Application still initialized in destructor!");

    // Free windows
    mWindowManager->UnregisterWindowClass(STARTUP_WND_TYPE);
    PG_DELETE(windowAlloc, mWindowManager);

    // Tear down debugging facilities
#if PEGASUS_ENABLE_ASSERT
    Core::AssertionManager::GetInstance()->UnregisterHandler();
    Core::AssertionManager::DestroyInstance();
#endif
#if PEGASUS_ENABLE_LOG
    Core::LogManager::GetInstance()->UnregisterHandler();
    Core::LogManager::DestroyInstance();
#endif
}

//----------------------------------------------------------------------------------------

void Application::Initialize()
{
    Memory::IAllocator* coreAlloc = Memory::GetCoreAllocator();
    Io::IOManagerConfig ioManagerConfig;
    Wnd::Window* startupWindow = nullptr;

    // Sanity check
    PG_ASSERTSTR(!mInitialized, "Application already initialized!");

    // Set up IO manager
    // This must be done here because of the GetAppName virtual
    ioManagerConfig.mBasePath = mConfig.mBasePath;
    ioManagerConfig.mAppName = GetAppName();
    mIoManager = PG_NEW(coreAlloc, "IOManager", Pegasus::Memory::PG_MEM_PERM) Io::IOManager(ioManagerConfig);

    // start up the app, which creates and destroys the dummy window
    StartupAppInternal();

    // Initted
    mInitialized = true;
}

//----------------------------------------------------------------------------------------

//! Shutdown this application.
void Application::Shutdown()
{
    Memory::IAllocator* coreAlloc = Memory::GetCoreAllocator();

    // Sanity check
    PG_ASSERTSTR(mInitialized, "Application not initialized yet!");

    // Shuts down GLextensions, etc
    ShutdownAppInternal();

    // Tear down IO manager
    PG_DELETE(coreAlloc, mIoManager);

    // No longer initted
    mInitialized = false;
}

//----------------------------------------------------------------------------------------

IWindowRegistry* Application::GetWindowRegistry()
{
    return mWindowManager;
}

//----------------------------------------------------------------------------------------

Wnd::Window* Application::AttachWindow(const AppWindowConfig& appWindowConfig)
{
    Memory::IAllocator* renderAlloc = Memory::GetRenderAllocator();
    Memory::IAllocator* windowAlloc = Memory::GetWindowAllocator();
    Wnd::Window* newWnd = nullptr;
    Wnd::WindowConfig config;

    // Create window
    config.mAllocator = windowAlloc;
    config.mRenderAllocator = renderAlloc;
    config.mModuleHandle = mConfig.mModuleHandle;
    config.mWindowContext = this;
    config.mIsChild = appWindowConfig.mIsChild;
    config.mParentWindowHandle = appWindowConfig.mParentWindowHandle;
    config.mWidth = appWindowConfig.mWidth;
    config.mHeight = appWindowConfig.mHeight;
    config.mCreateVisible = true;
    config.mUseBasicContext = false;
    newWnd = mWindowManager->CreateWindow(appWindowConfig.mWindowType, config);

    if (newWnd != nullptr)
    {
        PG_LOG('APPL', "Window created");
    }
    else
    {
        PG_FAILSTR("Unable to create window!");
    }

    return newWnd;
}

//----------------------------------------------------------------------------------------

void Application::DetachWindow(Wnd::Window* wnd)
{
    mWindowManager->DestroyWindow(wnd);

    PG_LOG('APPL', "Window destroyed");
}

//----------------------------------------------------------------------------------------

//! Starts up the application.
//! \note Creates the dummy startup window used to initialize the OGL extensions.
void Application::StartupAppInternal()
{
    Memory::IAllocator* renderAlloc = Memory::GetRenderAllocator();
    Memory::IAllocator* windowAlloc = Memory::GetWindowAllocator();
    Wnd::Window* newWnd = nullptr;
    Wnd::WindowConfig config;

    // Create window and immediately destroy it
    config.mAllocator = windowAlloc;
    config.mRenderAllocator = renderAlloc;
    config.mModuleHandle = mConfig.mModuleHandle;
    config.mWindowContext = this;
    config.mIsChild = false;
    config.mWidth = 128;
    config.mHeight = 128;
    config.mCreateVisible = false;
    config.mUseBasicContext = true;
    newWnd = mWindowManager->CreateWindow(STARTUP_WND_TYPE, config);
    PG_ASSERTSTR(newWnd != nullptr, "[FATAL] Failed to create startup window!");

    // Init openGL extensions now that we have a context
    Render::GLExtensions::CreateInstance(renderAlloc);

    // Write some temporary debugging information
    Render::GLExtensions* extensions = Render::GLExtensions::GetInstance();
    switch (extensions->GetMaximumProfile())
    {
    case Render::GLExtensions::PROFILE_GL_3_3:
        PG_LOG('OGL_', "OpenGL 3.3 is the maximum detected profile.");
        break;

    case Render::GLExtensions::PROFILE_GL_4_3:
        PG_LOG('OGL_', "OpenGL 4.3 is the maximum detected profile.");
        break;

    default:
        PG_LOG('OGL_', "Error when initializing GLextensions->");
        break;
    }
    if (extensions->IsGLExtensionSupported("GL_ARB_draw_indirect"))
    {
        PG_LOG('OGL_', "GL_ARB_draw_indirect detected.");
    }
    else
    {
        PG_LOG('OGL_', "GL_ARB_draw_indirect NOT detected.");
    }
    if (extensions->IsGLExtensionSupported("GL_ATI_fragment_shader"))
    {
        PG_LOG('OGL_', "GL_ATI_fragment_shader detected.");
    }
    else
    {
        PG_LOG('OGL_', "GL_ATI_fragment_shader NOT detected.");
    }
    if (extensions->IsWGLExtensionSupported("WGL_ARB_buffer_region"))
    {
        PG_LOG('OGL_', "WGL_ARB_buffer_region detected.");
    }
    else
    {
        PG_LOG('OGL_', "WGL_ARB_buffer_region NOT detected.");
    }
    if (extensions->IsWGLExtensionSupported("WGL_3DL_stereo_control"))
    {
        PG_LOG('OGL_', "WGL_3DL_stereo_control detected.");
    }
    else
    {
        PG_LOG('OGL_', "WGL_3DL_stereo_control NOT detected.");
    }

    // Destroy the window, it is no longer needed
    mWindowManager->DestroyWindow(newWnd);

    PG_LOG('APPL', "Startup successful");
}

//----------------------------------------------------------------------------------------

void Application::ShutdownAppInternal()
{
    // Destroy openGL extensions
    Render::GLExtensions::DestroyInstance();
}


}   // namespace App
}   // namespace Pegasus
