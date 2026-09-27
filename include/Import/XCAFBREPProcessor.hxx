#pragma once

#include "XCAFDocProcessor.hxx"

class XCAFBREPProcessor : public XCAFDocProcessor
{
    Q_OBJECT

public:
    aiScene* processFile(const std::string& path) override;

private:
    aiScene* processBREPFile(const std::string& path);
};