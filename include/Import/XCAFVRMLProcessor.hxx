#pragma once

#include "XCAFDocProcessor.hxx"

class XCAFVRMLProcessor : public XCAFDocProcessor
{
    Q_OBJECT

public:
    aiScene* processFile(const std::string& path) override;

private:
    aiScene* processVRMLFile(const std::string& path);
    void readVRMLFile(const std::string& filename, Handle(TDocStd_Document)& doc);
};
