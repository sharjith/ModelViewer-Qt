#pragma once 

#include <string>
#include <assimp/scene.h>

class IXCAFDocProcessor 
{
public:
    virtual ~IXCAFDocProcessor() = default;
    virtual aiScene* processFile(const std::string& path) = 0;
};
