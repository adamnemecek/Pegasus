/****************************************************************************************/
/*                                                                                      */
/*                                    Pegasus Editor                                    */
/*                                                                                      */
/****************************************************************************************/

//! \file	Application.h
//! \author	Kevin Boulanger
//! \date	02nd July 2013
//! \brief	Worker thread to contain an application to run

#ifndef EDITOR_APPLICATION_H
#define EDITOR_APPLICATION_H

#include <QThread>

#include "Pegasus/Core/Log.h"
#include "Pegasus/Core/Assertion.h"
#include "Pegasus/Window/WindowDefs.h"

class ApplicationInterface;
class ViewportWidget;

namespace Pegasus {
    namespace Application {
        class IApplicationProxy;
    }
    namespace Window {
        class IWindowProxy;
    }
}


//! Application worker thread
class Application : public QThread
{
    Q_OBJECT

public:

    //! Constructor, does not initialize the application, use \a Open() for that
    Application(QObject *parent = 0);

    //! Destructor, closes the running application
    virtual ~Application();


    //! Set the file name of the application to load
    //! \param fileName Path and name of the library file to open
    //! \todo Add error management
    void SetFile(const QString & fileName);

    //! Set the viewport parameters for the window associated with the application
    //! \param windowHandle Viewport window handle that will become the parent of the rendering window
    //! \param width Initial width of the viewport in pixels (> 0)
    //! \param height Initial height of the viewport in pixels (> 0)
    //! \todo Handle multiple viewports
    void SetViewportParameters(/**index,*/ Pegasus::Window::WindowHandle windowHandle,
                               int width, int height);


    //! Error codes for the application thread
    typedef enum Error
    {
        ERROR_INVALID_FILE_NAME = 0,    //!< Invalid file name (not properly formatted)
        ERROR_INVALID_APPLICATION,      //!< The library of the application does not have the correct entry points
        ERROR_INVALID_INTERFACE,        //!< The library interface does not have the right type
        ERROR_INVALID_VIEWPORT          //!< Invalid viewport given to the application
    };


    //! Running function of the thread, initializes the engine and the application,
    //! then runs the main loop
    //! \warning Do not rename to Run(), as this function is an overload of QThread
    void run();


    //! Function called by the log handler, emitting the \a LogSentFromApplication signal
    //! \warning To be called in the application thread, not the editor thread
    //! \param logChannel Pegasus log channel
    //! \param msgStr Content of the log message
    void EmitLogFromApplication(Pegasus::Core::LogChannel logChannel, const QString & msgStr);

    //! Function called by the assertion handler, emitting the \a AssertionSentFromApplication signal
    //! \warning To be called in the application thread, not the editor thread
    //! \param fileStr String with the filename where the assertion test failed
    //! \param line Line number where the assertion test failed
    //! \param msgStr Optional string of a message making the assertion test easier to understand.
    //!               Empty string if no message is defined.
    //! \return Pegasus::Core::AssertionManager::ASSERTION_xxx constant,
    //!         chosen from the button the user clicked on
    Pegasus::Core::AssertionManager::ReturnCode EmitAssertionFromApplication(const QString & testStr,
                                                                             const QString & fileStr,
                                                                             int line,
                                                                             const QString & msgStr);

    //------------------------------------------------------------------------------------
    
signals:

	//! Signal emitted when an error occurred when loading the application
    //! \param error Error code
	void LoadingError(Application::Error error);

    //! Signal emitted when the application has successfully loaded (not running)
    void LoadingSucceeded();

    //! Signal emitted when the viewport is resized
    //! \param width New width of the viewport, in pixels
    //! \param height New height of the viewport, in pixels
    void ViewportResized(int width, int height);

    //! Signal emitted from the application thread when the application sends a log message
    //! \param logChannel Pegasus log channel
    //! \param msgStr Content of the log message
    void LogSentFromApplication(Pegasus::Core::LogChannel logChannel, const QString & msgStr);

    //! Signal emitted from the application thread when the application sends an assertion error
    //! \param testStr String representing the assertion test itself
    //! \param fileStr String with the filename where the assertion test failed
    //! \param line Line number where the assertion test failed
    //! \param msgStr Optional string of a message making the assertion test easier to understand.
    //!               Empty string if no message is defined.
    void AssertionSentFromApplication(const QString & testStr, const QString & fileStr, int line, const QString & msgStr);

    //------------------------------------------------------------------------------------

private slots:
        
    //! Called when the viewport is resized (editor thread)
    //! \param width New width of the viewport, in pixels
    //! \param height New height of the viewport, in pixels
    void ResizeViewport(int width, int height);

    //! Called when a log message is received from the application thread
    //! through a queue connection
    //! \warning This is the editor thread function, which does the final post
    //!          into the console dock window
    //! \param logChannel Pegasus log channel
    //! \param msgStr Content of the log message
    void LogReceivedFromApplication(Pegasus::Core::LogChannel logChannel, const QString & msgStr);

    //! Called when an assertion error is received from the application thread
    //! through a blocking queue connection
    //! \warning This is the editor thread function, which shows the assertion dialog box
    //! \param testStr String representing the assertion test itself
    //! \param fileStr String with the filename where the assertion test failed
    //! \param line Line number where the assertion test failed
    //! \param msgStr Optional string of a message making the assertion test easier to understand.
    //!               Empty string if no message is defined.
    void AssertionReceivedFromApplication(const QString & testStr, const QString & fileStr, int line, const QString & msgStr);

    //------------------------------------------------------------------------------------

private:

    //! Handler for log messages coming from the application itself
    //! \warning This is a static function, so it cannot emit any signal.
    //!          We have to call a member function, \a EmitLogFromApplication,
    //!          running in the application thread
    //! \param logChannel Log channel that receives the message
    //! \param msgStr String of the message to log
    static void LogHandler(Pegasus::Core::LogChannel logChannel, const char * msgStr);

    //! Handler for assertion errors coming from the application itself
    //! \warning This is a static function, so it cannot emit any signal.
    //!          We have to call a member function, \a EmitAssertionFromApplication,
    //!          running in the application thread
    //! \param testStr String representing the assertion test itself
    //! \param fileStr String with the filename where the assertion test failed
    //! \param line Line number where the assertion test failed
    //! \param msgStr Optional string of a message making the assertion test easier to understand.
    //!               nullptr if no message is defined.
    //! \return Pegasus::Core::AssertionManager::ASSERTION_xxx constant,
    //!         chosen from the button the user clicked on
    static Pegasus::Core::AssertionManager::ReturnCode AssertionHandler(const char * testStr,
                                                                        const char * fileStr,
                                                                        int line,
                                                                        const char * msgStr);

    //------------------------------------------------------------------------------------

private:

    //! Application interface object, instantiated in the application thread
    ApplicationInterface * mApplicationInterface;

    //! File name of the application to load
    QString mFileName;

    //! Application proxy object created when running the application DLL
    Pegasus::Application::IApplicationProxy * mApplication;

    //! Window used to the render the viewport of the application
    //! \todo Handle multiple windows
    Pegasus::Window::IWindowProxy * mAppWindow;

    //! Viewport window handle associated with the application
    //! (will become the parent of the rendering window)
    Pegasus::Window::WindowHandle mViewportWindowHandle;

    //! Initial width of the viewport in pixels (> 0)
    int mViewportInitialWidth;

    //! Initial height of the viewport in pixels (> 0)
    int mViewportInitialHeight;

    //! Return code of the assertion handler. AssertionManager::ASSERTION_INVALID by default.
    //! The value is not the default only when leaving an assertion dialog box.
    //! This is used to freeze the application thread until a return code is present
    AssertionManager::ReturnCode mAssertionReturnCode;
};


#endif  // EDITOR_APPLICATION_H