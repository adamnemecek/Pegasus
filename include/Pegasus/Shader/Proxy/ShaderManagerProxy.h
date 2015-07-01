/****************************************************************************************/
/*                                                                                      */
/*                                       Pegasus                                        */
/*                                                                                      */
/****************************************************************************************/

//! \file   ShaderManagerProxy.h
//! \author Kleber Garcia 
//! \date   16 March 2014
//! \brief  Proxy interface, used by the editor and launcher to interact with the shader mgr

#ifndef PEGASUS_SHADERMANAGERPROXY_H
#define PEGASUS_SHADERMANAGERPROXY_H

#if PEGASUS_ENABLE_PROXIES

#include "Pegasus/Shader/Shared/IShaderManagerProxy.h"
#include "Pegasus/Core/Shared/ISourceCodeProxy.h"
#include "Pegasus/Shader/Shared/IShaderProxy.h"
#include "Pegasus/Shader/ShaderTracker.h"
#include "Pegasus/Shader/Proxy/ProgramProxy.h"
#include "Pegasus/Utils/Vector.h"
#include "Pegasus/Shader/ShaderSource.h"
#include "Pegasus/Shader/ProgramLinkage.h"

namespace Pegasus {
    namespace Core {
        namespace CompilerEvents {
            class ICompilerEventListener;
        }
    }

    namespace AssetLib {
        class IAssetProxy;
    }
}

namespace Pegasus
{
namespace Shader
{

class IProgramProxy;
class ShaderManager;


//! Implementation of IShaderManagerProxy and wrapper proxy to expose to the editor
class ShaderManagerProxy : public IShaderManagerProxy
{
public:
    ShaderManagerProxy(ShaderManager * object);
    virtual ~ShaderManagerProxy(){}

    //! \return gets the number of programs in the application
    virtual int GetProgramCount() const;
    
    //! \param i index of the program requested
    //! \return gets a reference to a program
    virtual IProgramProxy* GetProgram(int i);

    //! \return total number of shaders in the application
    virtual int GetShaderCount() const;

    //! \param id  of the shader requested
    //! \return gets a reference to a shader
    virtual IShaderProxy* GetShader(int id); 

    //! Sets the event listener to be used for the shader
    //! \param event listener reference
    virtual void RegisterEventListener(Pegasus::Core::CompilerEvents::ICompilerEventListener * eventListener);

    //! number of shaders is the number of sources
    virtual int GetSourceCount() const { return GetShaderCount(); }

    //! returns the respective shader
    virtual Pegasus::Core::ISourceCodeProxy* GetSource(int id) { return GetShader(id); }

    //! Loads a shader from an asset
    //! \param the asset to use to load this proxy
    //! \return the shader proxy to load
    virtual IShaderProxy* OpenShader(AssetLib::IAssetProxy* asset);

    //! Closes a shader.
    //! \param the shader proxy to close
    virtual void CloseShader(IShaderProxy* shader);

    //! Loads a program from an asset proxy
    //! \param the program proxy to use to load this program
    //! \return the program proxy to use
    virtual IProgramProxy* OpenProgram(AssetLib::IAssetProxy* asset);

    //! creates a blank new program using a respective path as its file to be saved as
    //! \param the path of the program to use.
    //! \param the actual program name to use.
    virtual IProgramProxy* CreateNewProgram(const char* programName);

    //! Closes a program.
    //! \param the program proxy to close
    virtual void CloseProgram(IProgramProxy* program);

    //! returns true if the asset passed is an actual shader
    //! \param asset the asset to test if its a shader
    //! \return  true if this asset is a shader, false otherwise
    virtual bool IsShader(const AssetLib::IAssetProxy* asset) const; 

    //! returns true if this asset is a program.
    //! \param asset the asset to test if its a program
    //! \return true if this is a program, false otherwise
    virtual bool IsProgram(const AssetLib::IAssetProxy* asset) const;

    //! Returns the asset of this shader. If it does not have it then returns null
    //! \return asset proxy attached to this shader. If it does not exist then returns null
    virtual AssetLib::IAssetProxy* GetShaderAsset(IShaderProxy* shader);

    //! Returns the asset of this program. If it does not have it then returns null
    //! \return asset proxy attached to this program. If it does not exist then returns null
    virtual AssetLib::IAssetProxy* GetProgramAsset(IProgramProxy* program);

    //! Flushes a shader to its containing asset.
    virtual void FlushShaderToAsset(IShaderProxy* shader);

    //! Flushes a program to its containing asset.
    virtual void FlushProgramToAsset(IProgramProxy* program);

    //! Binds a program node to an node.
    //! \note  This means the runtime program passed now has a new owner.
    //! unbinds anything if program has anything else binded. If asset is null, then we just unbind program
    //! from anything in the hdd
    //! \param program the program to bind to an asset
    //! \param the asset to bind to.
    virtual void BindProgramToAsset(IProgramProxy* program, AssetLib::IAssetProxy* asset);

private:
    //! keeps program alive into an internal pointer (opened)
    void InternalOpenProgram(ProgramLinkageRef program);

    //! reference to internal program
    ShaderManager * mObject;
    Utils::Vector<ShaderSourceRef>    mOpenedShaders;
    Utils::Vector<ProgramLinkageRef> mOpenedPrograms;
};
}
}

#endif // Proxy
#endif // Include guard
