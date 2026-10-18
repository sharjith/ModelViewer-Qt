#pragma once

#include "XCAFDocProcessor.hxx"

class XCAFSTEPProcessor : public XCAFDocProcessor
{
    Q_OBJECT

public:
    aiScene* processFile(const std::string& path) override;

private:
    aiScene* processSTEPFile(const std::string& path);
    void readSTEPFile(const std::string& filename, Handle(TDocStd_Document)& doc);
};