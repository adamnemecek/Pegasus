/****************************************************************************************/
/*                                                                                      */
/*                                       Pegasus                                        */
/*                                                                                      */
/****************************************************************************************/

//! \file   ProgramData.h
//! \author Kleber Garcia
//! \date   1st December 2013
//! \brief  Program Data definition 

#ifndef PEGASUS_PROGRAM_DATA_H
#define PEGASUS_PROGRAM_DATA_H

#include "Pegasus/Graph/NodeData.h"
#include "Pegasus/Render/GL/GLEWStaticInclude.h"

namespace Pegasus
{
namespace Shader
{

class ProgramData : public Pegasus::Graph::NodeData
{
   friend class ProgramLinkage;

public:
    //! Default Constructor
    //! \param allocator the allocator used for internal allocations
    explicit ProgramData(Alloc::IAllocator* allocator);

    //! Destructor
    virtual ~ProgramData(); 

    //! Returns the GL handle of a shader stage
    //! \return GLuint the GLuint used in glAttachShader opengl calls
    GLuint GetGlHandle() const { return mHandle; }

    //! Convinience function that activates the current shader program
    void Use() const;
  
private:
    void SetGlHandle(GLuint handle){ mHandle = handle;}
    GLuint mHandle;
};


typedef Pegasus::Core::Ref<ProgramData> ProgramDataRef;
typedef Pegasus::Core::Ref<ProgramData> & ProgramDataIn;
typedef Pegasus::Core::Ref<ProgramData> & ProgramDataInOut;
typedef Pegasus::Core::Ref<ProgramData> ProgramDataReturn;

}
}

#endif